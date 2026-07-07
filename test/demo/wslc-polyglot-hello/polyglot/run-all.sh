#!/usr/bin/env bash
# Runs one hello-world per language in TIOBE rank order. For each, captures the
# program's single stdout line and echoes it twice, tagged [cli] and [http].
# Fail-fast: any language that errors or prints nothing aborts the whole run.
set -euo pipefail

HELLO=/app/hello
BIN=/app/bin

# emit <lang> <command...> : run the command, capture stdout, assert non-empty,
# print the two tagged lines. Any failure (non-zero exit, empty output) aborts.
emit() {
    local lang="$1"; shift
    local out
    out="$("$@")"                      # runs the program; set -e aborts on non-zero
    # strip leading/trailing whitespace (Fortran's `print *` adds a leading space)
    out="$(printf '%s' "$out" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
    if [ -z "$out" ]; then
        echo "run-all: FAILED at [$lang]: no output" >&2
        exit 1
    fi
    echo "[cli] $out"
    echo "[http] $out"
}

# TIOBE rank order (see plan Global Constraints)
emit python    python3 "$HELLO/hello.py"
emit c         "$BIN/hello_c"
emit c++       "$BIN/hello_cpp"
emit java      java "$HELLO/hello.java"
emit javascript node "$HELLO/hello.js"
emit r         Rscript "$HELLO/hello.R"
emit rust      "$BIN/hello_rust"
emit go        "$BIN/hello_go"
emit php       php "$HELLO/hello.php"
emit swift     swift "$HELLO/hello.swift"
emit ada       "$BIN/hello_ada"
emit assembly  "$BIN/hello_asm"
emit fortran   "$BIN/hello_fortran"
emit ruby      ruby "$HELLO/hello.rb"
emit perl      perl "$HELLO/hello.pl"
emit cobol     "$BIN/hello_cobol"
emit prolog    swipl -q "$HELLO/hello_prolog.pl"
emit julia     julia "$HELLO/hello.jl"
emit kotlin    java -jar "$BIN/hello_kt.jar"
emit dart      dart "$HELLO/hello.dart"
emit lisp      sbcl --script "$HELLO/hello.lisp"
emit lua       lua5.4 "$HELLO/hello.lua"
emit ocaml     "$BIN/hello_ocaml"
emit haskell   "$BIN/hello_haskell"
emit typescript node "$BIN/hello_ts.js"
emit zig       "$BIN/hello_zig"
emit bash      bash "$HELLO/hello.sh"

echo "run-all: all languages OK" >&2
