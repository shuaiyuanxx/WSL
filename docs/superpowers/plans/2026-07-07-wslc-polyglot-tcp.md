# WSLC Polyglot Hello — Real TCP Channel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a real TCP transport to the `wslc-polyglot-hello` demo: 18 send-group languages open a socket to an in-container aggregator, which forwards each line over a mapped port to the Windows C# app; the app prints `[tcp] <line>`. The `[cli]` stdout channel and all 27 languages stay unchanged.

**Architecture:** In-container `tcp_server.py` aggregator listens on `127.0.0.1:9098` (language senders) and `0.0.0.0:9099` (Windows downstream), buffering lines until Windows connects. `run-all.sh` launches it, then for each send-group language runs a separate `send.<ext>` program after printing the `[cli]` line (hard-fail on send error). The SDK maps container 9099→Windows `localhost:9099` (`Bridged` + `PortMappings`). `Program.cs` adds a background `TcpClient` reader that prints `[tcp]` lines. All 18 sender snippets + the topology + timing were verified end-to-end with real `wslc` builds during design.

**Tech Stack:** existing polyglot image (debian:stable-slim + 27 toolchains) + `lua-socket` apt package; C# / .NET 8 `Microsoft.WSL.Containers` 2.9.3; `wslc` 2.9.3.

## Global Constraints

- **Location:** all changes under `test/demo/wslc-polyglot-hello/`.
- **Topology:** container = TCP server, Windows = client. Container→host inbound is blocked in WSLC; host→container via `PortMappings` is the supported direction (verified).
- **Ports:** container `9098` = language→aggregator; container `9099` = aggregator→Windows, mapped to Windows `localhost:9099`, `PortProtocol.TCP`.
- **Message content:** each `[tcp]` line is exactly `hello world from [<lang>]` — identical to that language's `[cli]` line.
- **Send-group (18) — print `[cli]` AND send TCP:** python, javascript, ruby, php, perl, lua, r, go, rust, java, kotlin, dart, julia, swift, typescript, c, c++, ocaml.
- **Skip-group (9) — `[cli]` only:** assembly, cobol, fortran, ada, prolog, lisp, zig, haskell, bash.
- **Hard-fail:** `run-all.sh` keeps `set -euo pipefail`; a send-group language failing to send aborts the run non-zero (`run-all: FAILED at [<lang>] (tcp)`).
- **`hello.*` sources are NOT modified.** TCP send lives in separate `send.<ext>` programs. This keeps the verified 27-language `[cli]` build intact.
- **Compiled senders** (built at image-build time into `/app/bin/`): c, c++, rust, go, ocaml, kotlin (jar), java (class), dart (exe), typescript (tsc→js). **Interpreted senders** (run directly): python, javascript(node), ruby, php, perl, lua, r, julia, swift (`swift` interprets — no `swiftc` in the image).
- **`lua-socket` apt package is required** (lua's sender uses `require('socket')`).
- **Windows TCP reader is best-effort;** the container-side hard-fail is the correctness gate. The reader must never block teardown.
- **All 18 sender snippets in this plan were verified** by real `wslc image build` + run during design — use them verbatim.
- **julia's sender is slow to start (JIT).** The aggregator MUST be drain-mode (a background accept loop), never a fixed-count blocking accept, so a slow sender can't stall it.
- **Honesty rule:** `wslc` 2.9.3 is installed and the full path is verified; `dotnet build` + `dotnet run` are runnable here. Do not claim a run passed without observing the `[cli]` and `[tcp]` lines.

---

### Task 1: The aggregator `tcp_server.py`

**Files:**
- Create: `test/demo/wslc-polyglot-hello/polyglot/tcp_server.py`

**Interfaces:**
- Consumes: nothing (pure Python stdlib).
- Produces: a server that (a) listens `127.0.0.1:9098` for language senders (reads one message each, enqueues), (b) listens `0.0.0.0:9099` for the Windows downstream, buffers until Windows connects, then forwards every queued/subsequent line. Runtime-launched by `run-all.sh` (Task 2). Windows `TcpClient` (Task 4) connects to the mapped 9099.

- [ ] **Step 1: Create `polyglot/tcp_server.py`**

```python
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

    win, _ = down.accept()
    print("tcp_server: windows connected", flush=True)
    try:
        while True:
            try:
                item = q.get(timeout=20)
            except queue.Empty:
                break
            win.sendall(item)
    finally:
        win.close()


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Syntax-check it compiles as Python**

Run: `python3 -c "import ast; ast.parse(open('test/demo/wslc-polyglot-hello/polyglot/tcp_server.py').read()); print('ok')"`
Expected: `ok`. (Python is on the host PATH.)

- [ ] **Step 3: Commit**

```bash
git add test/demo/wslc-polyglot-hello/polyglot/tcp_server.py
git commit -m "demo(wslc-polyglot-hello): in-container TCP aggregator (buffered, drain-mode)"
```

---

### Task 2: The 18 sender programs

**Files (all under `test/demo/wslc-polyglot-hello/polyglot/senders/`):**
- Create: `send.py send.js send.rb send.php send.pl send.lua send.R send.go send.rs Send.java Send.kt send.dart send.jl send.swift send.ts send.c send.cpp send.ml`

**Interfaces:**
- Consumes: the aggregator on `127.0.0.1:9098` (Task 1).
- Produces: 18 programs, each connects to `127.0.0.1:9098` and sends `hello world from [<lang>]\n`. Referenced by `run-all.sh` (Task 3) and compiled by the Dockerfile (Task 3). All content below was verified by real `wslc image build` + run — use verbatim.

- [ ] **Step 1: Interpreted-language senders**

`send.py`:
```python
import socket
socket.create_connection(('127.0.0.1',9098)).sendall(b'hello world from [python]\n')
```

`send.js`:
```javascript
const net=require('net');const s=net.connect(9098,'127.0.0.1',()=>{s.end('hello world from [javascript]\n');});
```

`send.rb`:
```ruby
require 'socket'; TCPSocket.open('127.0.0.1',9098){|s| s.write "hello world from [ruby]\n"}
```

`send.php`:
```php
<?php $s=fsockopen('127.0.0.1',9098); fwrite($s,"hello world from [php]\n"); fclose($s);
```

`send.pl`:
```perl
use IO::Socket::INET; my $s=IO::Socket::INET->new(PeerAddr=>'127.0.0.1',PeerPort=>9098,Proto=>'tcp'); print $s "hello world from [perl]\n"; close($s);
```

`send.lua` (needs the `lua-socket` apt package — added in Task 3's Dockerfile):
```lua
local socket=require('socket'); local c=socket.tcp(); c:connect('127.0.0.1',9098); c:send('hello world from [lua]\n'); c:close()
```

`send.R`:
```r
con<-socketConnection(host='127.0.0.1',port=9098,blocking=TRUE,open='w'); writeLines('hello world from [r]',con,sep='\n'); close(con)
```

`send.jl`:
```julia
using Sockets; s=connect("127.0.0.1",9098); write(s,"hello world from [julia]\n"); close(s)
```

`send.swift` (run via `swift` interpret; no `swiftc` in the image):
```swift
import Glibc
let fd = socket(AF_INET, Int32(SOCK_STREAM.rawValue), 0)
var addr = sockaddr_in()
addr.sin_family = sa_family_t(AF_INET)
addr.sin_port = in_port_t(UInt16(9098).bigEndian)
_ = "127.0.0.1".withCString { inet_pton(AF_INET, $0, &addr.sin_addr) }
let rc = withUnsafePointer(to: &addr) {
    $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
        connect(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.stride))
    }
}
if rc == 0 {
    let msg = "hello world from [swift]\n"
    _ = msg.withCString { send(fd, $0, strlen($0), 0) }
}
close(fd)
```

- [ ] **Step 2: Compiled-language senders**

`send.go`:
```go
package main
import ("net")
func main(){ c,_:=net.Dial("tcp","127.0.0.1:9098"); c.Write([]byte("hello world from [go]\n")); c.Close() }
```

`send.rs`:
```rust
use std::net::TcpStream; use std::io::Write;
fn main(){ let mut s=TcpStream::connect("127.0.0.1:9098").unwrap(); s.write_all(b"hello world from [rust]\n").unwrap(); }
```

`Send.java`:
```java
import java.net.Socket; import java.io.OutputStream;
public class Send { public static void main(String[] a) throws Exception {
  try(Socket s=new Socket("127.0.0.1",9098)){ s.getOutputStream().write("hello world from [java]\n".getBytes()); } } }
```

`Send.kt`:
```kotlin
import java.net.Socket
fun main(){ Socket("127.0.0.1",9098).use { it.getOutputStream().write("hello world from [kotlin]\n".toByteArray()) } }
```

`send.dart`:
```dart
import 'dart:io';
void main() async { final s=await Socket.connect('127.0.0.1',9098); s.write('hello world from [dart]\n'); await s.flush(); await s.close(); }
```

`send.ts` (the `declare const require` line is required — the image's TypeScript has no `@types/node`):
```typescript
declare const require: any;
const net = require('net');
const s = net.connect(9098, '127.0.0.1', () => { s.end('hello world from [typescript]\n'); });
```

`send.c`:
```c
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
int main(){ int fd=socket(AF_INET,SOCK_STREAM,0); struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_port=htons(9098); inet_pton(AF_INET,"127.0.0.1",&a.sin_addr); connect(fd,(struct sockaddr*)&a,sizeof(a)); const char*m="hello world from [c]\n"; write(fd,m,strlen(m)); close(fd); return 0; }
```

`send.cpp`:
```cpp
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
int main(){ int fd=socket(AF_INET,SOCK_STREAM,0); sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(9098); inet_pton(AF_INET,"127.0.0.1",&a.sin_addr); connect(fd,(sockaddr*)&a,sizeof(a)); const char*m="hello world from [c++]\n"; write(fd,m,strlen(m)); close(fd); return 0; }
```

`send.ml`:
```ocaml
let () =
  let s = Unix.socket Unix.PF_INET Unix.SOCK_STREAM 0 in
  Unix.connect s (Unix.ADDR_INET (Unix.inet_addr_of_string "127.0.0.1", 9098));
  let m = "hello world from [ocaml]\n" in
  ignore (Unix.write_substring s m 0 (String.length m));
  Unix.close s
```

- [ ] **Step 3: Verify all 18 sender files exist with LF endings**

Run: `ls test/demo/wslc-polyglot-hello/polyglot/senders/ | wc -l`
Expected: `18`.
Run: `for f in test/demo/wslc-polyglot-hello/polyglot/senders/*; do grep -lq $'\r' "$f" && echo "CRLF: $f"; done; echo "(none above = all LF)"`
Expected: `(none above = all LF)`.

- [ ] **Step 4: Commit**

```bash
git add test/demo/wslc-polyglot-hello/polyglot/senders/
git commit -m "demo(wslc-polyglot-hello): 18 per-language TCP sender programs (verified)"
```

---

### Task 3: Dockerfile + run-all.sh (build senders, wire sending)

**Files:**
- Modify: `test/demo/wslc-polyglot-hello/polyglot/Dockerfile`
- Modify: `test/demo/wslc-polyglot-hello/polyglot/run-all.sh`

**Interfaces:**
- Consumes: `tcp_server.py` (Task 1), the 18 senders (Task 2).
- Produces: an image where the 9 compiled senders are prebuilt into `/app/senders/bin/`, `lua-socket` is installed, and `run-all.sh` launches the aggregator + sends per send-group language (hard-fail). This is what `dotnet build` (Task 4/5) saves as `polyglot.tar` and what the app runs.

- [ ] **Step 1: Add `lua-socket` + the sender build to the Dockerfile**

In `test/demo/wslc-polyglot-hello/polyglot/Dockerfile`, add `lua-socket` to the main apt install line (the line that installs `sbcl node-typescript ...`):

```dockerfile
        sbcl node-typescript lua-socket \
```

Then, after the existing `COPY hello/ /app/hello/` and `COPY run-all.sh ...` lines, add copying + compiling the senders. Insert a new COPY and a new compile RUN **before** the final `CMD` line:

```dockerfile
COPY tcp_server.py /app/tcp_server.py
COPY senders/ /app/senders/

# Compile the compiled-language senders into /app/senders/bin (interpreted
# senders run from source at container start).
RUN mkdir -p /app/senders/bin \
 && gcc  -o /app/senders/bin/send_c   /app/senders/send.c \
 && g++  -o /app/senders/bin/send_cpp /app/senders/send.cpp \
 && rustc -o /app/senders/bin/send_rust /app/senders/send.rs \
 && (cd /app/senders && go build -o /app/senders/bin/send_go send.go) \
 && ocamlopt unix.cmxa /app/senders/send.ml -o /app/senders/bin/send_ocaml \
 && kotlinc /app/senders/Send.kt -include-runtime -d /app/senders/bin/send_kt.jar \
 && (cd /app/senders && javac Send.java -d /app/senders/bin) \
 && tsc /app/senders/send.ts \
 && (cd /app/senders && dart compile exe send.dart -o /app/senders/bin/send_dart)
```

> Notes (all validated by probe builds): `ocamlopt unix.cmxa ...` links the unix lib without `ocamlfind` (not in the image). `tsc /app/senders/send.ts` emits `/app/senders/send.js` in place (no `--outFile`). Swift has no `swiftc` in the image, so its sender is interpreted at runtime (not compiled here).

- [ ] **Step 2: Update `run-all.sh` to launch the aggregator and send per language**

Replace `test/demo/wslc-polyglot-hello/polyglot/run-all.sh` with:

```bash
#!/usr/bin/env bash
# Runs one hello-world per language in TIOBE rank order. For each, captures the
# program's single stdout line and echoes it as [cli]. Send-group languages also
# send the same line over TCP to the in-container aggregator (real socket ->
# mapped port -> Windows). Fail-fast: any error (a language, an empty line, or a
# send-group TCP send) aborts the whole run non-zero.
set -euo pipefail

HELLO=/app/hello
BIN=/app/bin
SBIN=/app/senders/bin
SND=/app/senders

# ---- Start the TCP aggregator and wait until it is listening -----------------
python3 /app/tcp_server.py &
for _ in $(seq 1 50); do
    if (exec 3<>/dev/tcp/127.0.0.1/9098) 2>/dev/null; then exec 3>&- 3<&-; break; fi
    sleep 0.2
done
if ! (exec 3<>/dev/tcp/127.0.0.1/9098) 2>/dev/null; then
    echo "run-all: FAILED: tcp aggregator did not come up on 9098" >&2
    exit 1
fi
exec 3>&- 3<&- 2>/dev/null || true

# cli <lang> <command...> : run the program, capture stdout, assert non-empty,
# print the [cli] line. (Same mechanism as before.)
cli() {
    local lang="$1"; shift
    local out
    out="$("$@")"
    out="$(printf '%s' "$out" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
    if [ -z "$out" ]; then
        echo "run-all: FAILED at [$lang]: no output" >&2
        exit 1
    fi
    echo "[cli] $out"
}

# tcp <lang> <send-command...> : run the send-group language's sender. On any
# non-zero exit, hard-fail. Julia's JIT start is slow -> allow up to 60s.
tcp() {
    local lang="$1"; shift
    if ! timeout 60 "$@"; then
        echo "run-all: FAILED at [$lang] (tcp)" >&2
        exit 1
    fi
}

# ---- Run each language in TIOBE order ---------------------------------------
# Send-group: cli + tcp. Skip-group: cli only.
cli python     python3 "$HELLO/hello.py";        tcp python     python3 "$SND/send.py"
cli c          "$BIN/hello_c";                   tcp c          "$SBIN/send_c"
cli c++        "$BIN/hello_cpp";                  tcp c++        "$SBIN/send_cpp"
cli java       java "$HELLO/hello.java";          tcp java       java -cp "$SBIN" Send
cli javascript node "$HELLO/hello.js";            tcp javascript node "$SND/send.js"
cli r          Rscript "$HELLO/hello.R";          tcp r          Rscript "$SND/send.R"
cli rust       "$BIN/hello_rust";                 tcp rust       "$SBIN/send_rust"
cli go         "$BIN/hello_go";                   tcp go         "$SBIN/send_go"
cli php        php "$HELLO/hello.php";            tcp php        php "$SND/send.php"
cli swift      swift "$HELLO/hello.swift";        tcp swift      swift "$SND/send.swift"
cli ada        "$BIN/hello_ada"
cli assembly   "$BIN/hello_asm"
cli fortran    "$BIN/hello_fortran"
cli ruby       ruby "$HELLO/hello.rb";            tcp ruby       ruby "$SND/send.rb"
cli perl       perl "$HELLO/hello.pl";            tcp perl       perl "$SND/send.pl"
cli cobol      "$BIN/hello_cobol"
cli prolog     swipl -q "$HELLO/hello_prolog.pl"
cli julia      julia "$HELLO/hello.jl";           tcp julia      julia "$SND/send.jl"
cli kotlin     java -jar "$BIN/hello_kt.jar";     tcp kotlin     java -jar "$SBIN/send_kt.jar"
cli dart       dart "$HELLO/hello.dart";          tcp dart       "$SBIN/send_dart"
cli lisp       sbcl --script "$HELLO/hello.lisp"
cli lua        lua5.4 "$HELLO/hello.lua";         tcp lua        lua5.4 "$SND/send.lua"
cli ocaml      "$BIN/hello_ocaml";                tcp ocaml      "$SBIN/send_ocaml"
cli haskell    "$BIN/hello_haskell"
cli typescript node "$BIN/hello_ts.js";           tcp typescript node "$SND/send.js"
cli zig        "$BIN/hello_zig"
cli bash       bash "$HELLO/hello.sh"

echo "run-all: all languages OK" >&2
```

> `tcp typescript node "$SND/send.js"` runs the `send.js` emitted by `tsc` at build time (Step 1). All send-command forms above match the probe-verified invocations.

- [ ] **Step 3: Build the image with `wslc` (verifies senders compile + install)**

Run: `"/c/Program Files/WSL/wslc.exe" image build -t wslc-demo/polyglot:latest -f test/demo/wslc-polyglot-hello/polyglot/Dockerfile test/demo/wslc-polyglot-hello/polyglot`
Expected: build succeeds (installs `lua-socket`, compiles the 9 compiled senders, tags the image). Slow (rebuilds toolchain layers if cache is cold).

- [ ] **Step 4: Run the image standalone — confirm all 27 `[cli]` lines still stream in order**

Run: `"/c/Program Files/WSL/wslc.exe" container run --rm wslc-demo/polyglot:latest 2>/dev/null | grep -c "^\[cli\]"`
Expected: `27`. (Standalone there is no Windows downstream; the aggregator buffers the 18 TCP sends in its queue and `run-all.sh` still completes because sends succeed at the socket level — the aggregator accepts them. If run-all hangs, the aggregator's sender-accept loop is not draining; revisit Task 1.)

- [ ] **Step 5: Commit**

```bash
git add test/demo/wslc-polyglot-hello/polyglot/Dockerfile test/demo/wslc-polyglot-hello/polyglot/run-all.sh
git commit -m "demo(wslc-polyglot-hello): build senders + wire TCP sending in run-all.sh (hard-fail)"
```

---

### Task 4: Windows app — PortMappings + TCP reader

**Files:**
- Modify: `test/demo/wslc-polyglot-hello/app/Program.cs`

**Interfaces:**
- Consumes: the container's aggregator on the mapped `localhost:9099`.
- Produces: the finished app. Adds `Bridged` + `PortMappings 9099->9099` to the container settings and a background `TcpClient` reader printing `[tcp] <line>`. The `[cli]` streaming, fresh-store, teardown all unchanged.

- [ ] **Step 1: Add networking to `ContainerSettings`**

In `Program.cs`, the `ContainerSettings` block currently is:

```csharp
    var containerSettings = new ContainerSettings(ImageTag)
    {
        Name = ContainerName,
        InitProcess = initProcess,
        EnableAutoRemove = true,
    };
```

Change it to add the two networking properties:

```csharp
    var containerSettings = new ContainerSettings(ImageTag)
    {
        Name = ContainerName,
        InitProcess = initProcess,
        EnableAutoRemove = true,
        NetworkingMode = ContainerNetworkingMode.Bridged,   // required for PortMappings
        PortMappings = new List<ContainerPortMapping>
        {
            new(9099, 9099, PortProtocol.TCP),              // container 9099 -> Windows localhost:9099
        },
    };
```

- [ ] **Step 2: Add a background TCP reader after `container.Start()`**

The current code around start is:

```csharp
    container.Start();
    Console.WriteLine("polyglot container started; streaming output...");

    exitCode = await done.Task;
    Console.WriteLine($"[done] run-all.sh exited code={exitCode}");
```

Insert a cancellable background TCP reader between `container.Start()` and the `await done.Task`. It polls-connect to `localhost:9099` (the container server may not be up yet) and prints each received line as `[tcp] ...`. Best-effort: never throws into the main flow.

```csharp
    container.Start();
    Console.WriteLine("polyglot container started; streaming output...");

    // Background: connect to the container's TCP aggregator (mapped to
    // localhost:9099) and print each received line. Best-effort — the
    // container-side hard-fail is the correctness gate; this only displays.
    using var tcpCts = new CancellationTokenSource();
    var tcpReader = Task.Run(async () =>
    {
        for (int attempt = 0; attempt < 30 && !tcpCts.IsCancellationRequested; attempt++)
        {
            try
            {
                using var client = new System.Net.Sockets.TcpClient();
                await client.ConnectAsync("127.0.0.1", 9099, tcpCts.Token);
                using var reader = new StreamReader(client.GetStream());
                string? line;
                while ((line = await reader.ReadLineAsync(tcpCts.Token)) is not null)
                {
                    Console.WriteLine($"[tcp] {line}");
                }
                return; // stream closed cleanly
            }
            catch (OperationCanceledException) { return; }
            catch { await Task.Delay(1000, tcpCts.Token).ContinueWith(_ => { }); }
        }
    });

    exitCode = await done.Task;
    Console.WriteLine($"[done] run-all.sh exited code={exitCode}");

    // give the reader a moment to drain any final [tcp] lines, then stop it
    await Task.WhenAny(tcpReader, Task.Delay(2000));
    tcpCts.Cancel();
```

> `using System.Net.Sockets;` — `Program.cs` already `using System.Text;`. Reference `TcpClient` as `System.Net.Sockets.TcpClient` (as written) to avoid any ambiguity with the SDK, or add the using at the top. Either is fine; the fully-qualified form needs no new using.

- [ ] **Step 3: Build to verify it compiles (criterion 1)**

Run: `dotnet build test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug`
Expected: `Build succeeded`, 0 errors. If `ContainerNetworkingMode` / `ContainerPortMapping` / `PortProtocol` don't resolve, they are in the `Microsoft.WSL.Containers` namespace already imported — confirm the names against IntelliSense (verified present in the SDK). If `ReadLineAsync(CancellationToken)` overload is unavailable, use `ReadLineAsync()` (no token) and rely on `tcpCts.Cancel()` + stream close.

- [ ] **Step 4: Commit**

```bash
git add test/demo/wslc-polyglot-hello/app/Program.cs
git commit -m "demo(wslc-polyglot-hello): map container port + read [tcp] lines on Windows"
```

---

### Task 5: End-to-end run + README

**Files:**
- Modify: `test/demo/wslc-polyglot-hello/README.md`

**Interfaces:**
- Consumes: the finished app + image.
- Produces: the verified end-to-end demo + updated docs. Terminal task.

- [ ] **Step 1: Full build (refresh `polyglot.tar` with the TCP image)**

Run: `dotnet build test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug`
Expected: `Build succeeded`, `WSLC: Building image` / `Saving image`, `polyglot.tar` produced.

- [ ] **Step 2: Run the app end-to-end — verify `[cli]` × 27 AND `[tcp]` × 18**

Run: `timeout 180 dotnet run --project test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug > /tmp/pg-tcp.log 2>&1; echo "exit=$?"`
Then:
- `grep -c "^\[cli\]" /tmp/pg-tcp.log` → expect `27`
- `grep -c "^\[tcp\]" /tmp/pg-tcp.log` → expect `18`
- `grep "^\[tcp\]" /tmp/pg-tcp.log | grep -oE "\[[a-z+]+\]$" | sort -u | tr '\n' ' '` → expect the 18 send-group tags
- `grep "\[done\] run-all.sh exited code=0" /tmp/pg-tcp.log` → present, exit 0

If a `[tcp]` line is missing for a language, that language's sender failed — investigate that sender (the run hard-fails, so `[done] ... code!=0` and a `run-all: FAILED at [<lang>] (tcp)` on stderr). Do not proceed until 27 `[cli]` + 18 `[tcp]` + exit 0.

- [ ] **Step 3: Update the README**

Replace the `## Languages` section's intro and add a TCP section. Specifically:
1. Update the top intro line (currently "prints ... in 27 programming languages") to note that 18 of them also send their message over a **real TCP socket** to the Windows app, and `[cli]`/`[tcp]` are two channels.
2. Replace the example-output block near the top so it shows both channels, e.g.:

```
[cli] hello world from [python]
[tcp] hello world from [python]
[cli] hello world from [c]
[tcp] hello world from [c]
...
```

3. Add a `## Real TCP channel` section explaining: container runs a TCP aggregator; SDK maps container 9099 → Windows `localhost:9099` (`Bridged` + `PortMappings`); Windows `TcpClient` reads and prints `[tcp]` lines; `[cli]` is still stdout. Note the verified WSLC networking constraint: container→host inbound is blocked, host→container via PortMappings is the supported direction, which is why the container is the server.
4. In the language table, add a "TCP?" column: the 18 send-group = yes, the 9 skip-group = no (`[cli]` only). List which are skipped and why (raw socket disproportionate for a demo: assembly, cobol, fortran, ada, prolog, lisp, zig, haskell, bash).
5. Troubleshooting: `[tcp]` lines missing → container aggregator / port mapping; a language failing → hard-fail, read `run-all: FAILED at [<lang>] (tcp)`.

(Write concrete prose for each of the above in the README — no placeholders.)

- [ ] **Step 4: Final build sanity + commit**

Run: `dotnet build test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug`
Expected: `Build succeeded`, 0 errors.

```bash
git add test/demo/wslc-polyglot-hello/README.md
git commit -m "demo(wslc-polyglot-hello): document real TCP channel + [cli]/[tcp] output"
```

---

## Manual verification (all runnable here — `wslc` 2.9.3 installed)

1. **Compiles + builds tar (criteria 1-2):** `dotnet build ...` → `Build succeeded` + `polyglot.tar`.
2. **`[cli]` × 27 + `[tcp]` × 18, exit 0 (criterion 3):** `dotnet run ...` → the two channels as in Task 5 Step 2. The `[tcp]` lines are real socket messages (container aggregator → mapped port → Windows TcpClient).
3. **Hard-fail (criterion 4):** if a send-group sender is broken, the run exits non-zero with `run-all: FAILED at [<lang>] (tcp)`. (Not triggered in a healthy run.)
4. **Teardown + clean re-run (criterion 5):** run twice back-to-back; the second run starts without a name conflict.
