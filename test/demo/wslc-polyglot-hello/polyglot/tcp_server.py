#!/usr/bin/env python3
"""In-container TCP aggregator for the polyglot demo.

Language senders connect to 127.0.0.1:9098 and send one line each. The Windows
app connects (via a mapped port) to 0.0.0.0:9099. Lines are buffered in a queue
until Windows connects, so a language that sends before Windows attaches is not
lost. Drain-mode accept loop (never a fixed-count blocking accept) so a slow
sender (e.g. Julia's JIT startup) can't stall delivery.
"""
import socket
import threading
import queue

UP_PORT = 9098     # language senders -> aggregator
DOWN_PORT = 9099   # aggregator -> Windows downstream

q: "queue.Queue[bytes]" = queue.Queue()


def _accept_senders(up: socket.socket) -> None:
    while True:
        conn, _ = up.accept()
        try:
            data = conn.recv(4096)
            if data:
                q.put(data)
        finally:
            conn.close()


def main() -> None:
    up = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    up.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    up.bind(("127.0.0.1", UP_PORT))
    up.listen(16)
    threading.Thread(target=_accept_senders, args=(up,), daemon=True).start()

    down = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    down.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    down.bind(("0.0.0.0", DOWN_PORT))
    down.listen(1)
    print("tcp_server: listening (9098 senders / 9099 windows)", flush=True)

    # The Windows client connects via a mapped port that can take ~55s to become
    # stably reachable, and early flaky connects may drop immediately. Keep
    # accepting downstream connections (each with a bounded wait) until we have
    # forwarded at least one queued item, or until an overall deadline so
    # run-all.sh's `wait` on us can't hang the container forever.
    import time
    deadline = time.monotonic() + 150
    forwarded = 0
    while forwarded == 0 and time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        down.settimeout(max(1.0, remaining))
        try:
            win, _ = down.accept()
        except socket.timeout:
            break
        print("tcp_server: windows connected", flush=True)
        try:
            # forward queued + subsequent lines; stop after a 15s idle gap
            while True:
                try:
                    item = q.get(timeout=15)
                except queue.Empty:
                    break
                win.sendall(item)
                forwarded += 1
        except OSError:
            # connection dropped (flaky early mapping) — loop and re-accept
            pass
        finally:
            win.close()
    if forwarded == 0:
        print("tcp_server: windows never connected; exiting", flush=True)


if __name__ == "__main__":
    main()
