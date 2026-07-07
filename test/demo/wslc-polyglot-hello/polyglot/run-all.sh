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
AGG_PID=$!
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
cli typescript node "$BIN/hello_ts.js";           tcp typescript node "$SBIN/send.js"
cli zig        "$BIN/hello_zig"
cli bash       bash "$HELLO/hello.sh"

echo "run-all: all languages OK" >&2

# The 18 TCP sends are buffered in the aggregator until the Windows client
# connects (its mapped port can take ~15s to become active). Keep the container
# alive by waiting for the aggregator to finish forwarding — it exits after
# Windows connects + drains, or after its own 90s no-connect timeout. Without
# this wait, run-all.sh (the init process) would return and the container would
# stop, killing the aggregator before the [tcp] lines are delivered.
wait "$AGG_PID" 2>/dev/null || true
