#!/usr/bin/env bash
# Runs one hello-world per language in TIOBE rank order for the [cli] channel.
# Also launches 18 per-language TCP servers (one per port); each waits for the
# Windows client, does a send->hello->ack handshake, then exits. Fail-fast: any
# [cli] error, or any server exiting non-zero, aborts the run non-zero.
set -euo pipefail

HELLO=/app/hello
BIN=/app/bin
SBIN=/app/servers/bin
SRV=/app/servers

# ---- Launch the 18 per-language TCP servers (each binds its port, waits) -----
# PIDS preserves launch order for the wait loop at the end.
PIDS=()
python3   "$SRV/srv.py"  7001 & PIDS+=($!)
node      "$SRV/srv.js"  7002 & PIDS+=($!)
ruby      "$SRV/srv.rb"  7003 & PIDS+=($!)
php       "$SRV/srv.php" 7004 & PIDS+=($!)
perl      "$SRV/srv.pl"  7005 & PIDS+=($!)
lua5.4    "$SRV/srv.lua" 7006 & PIDS+=($!)
Rscript   "$SRV/srv.R"   7007 & PIDS+=($!)
"$SBIN/srv_go"           7008 & PIDS+=($!)
"$SBIN/srv_rust"         7009 & PIDS+=($!)
java -cp "$SBIN" Srv     7010 & PIDS+=($!)
java -jar "$SBIN/srv_kt.jar" 7011 & PIDS+=($!)
"$SBIN/srv_dart"         7012 & PIDS+=($!)
julia     "$SRV/srv.jl"  7013 & PIDS+=($!)
node      "$SBIN/srv.js" 7014 & PIDS+=($!)   # typescript (tsc-compiled)
"$SBIN/srv_c"            7015 & PIDS+=($!)
"$SBIN/srv_cpp"          7016 & PIDS+=($!)
"$SBIN/srv_ocaml"        7017 & PIDS+=($!)
swift     "$SRV/srv.swift" 7018 & PIDS+=($!)

# cli <lang> <command...> : run the program, capture stdout, assert non-empty,
# print the [cli] line. (Unchanged mechanism.)
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

# ---- Run each language's [cli] line in TIOBE order (27 languages) ------------
cli python     python3 "$HELLO/hello.py"
cli c          "$BIN/hello_c"
cli c++        "$BIN/hello_cpp"
cli java       java "$HELLO/hello.java"
cli javascript node "$HELLO/hello.js"
cli r          Rscript "$HELLO/hello.R"
cli rust       "$BIN/hello_rust"
cli go         "$BIN/hello_go"
cli php        php "$HELLO/hello.php"
cli swift      swift "$HELLO/hello.swift"
cli ada        "$BIN/hello_ada"
cli assembly   "$BIN/hello_asm"
cli fortran    "$BIN/hello_fortran"
cli ruby       ruby "$HELLO/hello.rb"
cli perl       perl "$HELLO/hello.pl"
cli cobol      "$BIN/hello_cobol"
cli prolog     swipl -q "$HELLO/hello_prolog.pl"
cli julia      julia "$HELLO/hello.jl"
cli kotlin     java -jar "$BIN/hello_kt.jar"
cli dart       dart "$HELLO/hello.dart"
cli lisp       sbcl --script "$HELLO/hello.lisp"
cli lua        lua5.4 "$HELLO/hello.lua"
cli ocaml      "$BIN/hello_ocaml"
cli haskell    "$BIN/hello_haskell"
cli typescript node "$BIN/hello_ts.js"
cli zig        "$BIN/hello_zig"
cli bash       bash "$HELLO/hello.sh"

echo "run-all: all [cli] languages OK" >&2

# ---- Wait for the 18 servers to finish their handshakes and exit -------------
# Each server exits after the Windows client sends its ack. A server exiting
# non-zero (crash, bind failure) is a hard-fail. An overall timeout guards
# against a server that never gets a client so the container can't hang forever.
rc=0
end=$(( $(date +%s) + 300 ))
for pid in "${PIDS[@]}"; do
    while kill -0 "$pid" 2>/dev/null && [ "$(date +%s)" -lt "$end" ]; do sleep 1; done
    if kill -0 "$pid" 2>/dev/null; then
        echo "run-all: FAILED: a server (pid $pid) did not finish within timeout" >&2
        kill "$pid" 2>/dev/null || true
        rc=1
    else
        wait "$pid" || { echo "run-all: FAILED: server pid $pid exited non-zero" >&2; rc=1; }
    fi
done
echo "run-all: all servers done (rc=$rc)" >&2
exit $rc
