# WSLC Polyglot Hello Demo — Design

**Date:** 2026-07-07
**Status:** Approved (design), pending spec review

## Goal

A WSLC demo where a Windows C# console app runs **one polyglot Linux container**
that executes a "hello world" program in ~20 different programming languages, in
**TIOBE popularity rank order**. For each language the container emits two
channel-tagged lines to stdout:

```
[cli] hello world from [<lang>]
[http] hello world from [<lang>]
```

The Windows app streams the container's stdout to the console verbatim. There is
**no HTTP server and no port mapping** — `[cli]` and `[http]` are labels the
container prints; the app just receives and prints every line.

The demo reuses the SDK-native build/load/run lifecycle proven in the
`wslc-multi-service` demo (`<WslcImage>` MSBuild target → `wslc image build` →
`LoadImageAsync` → `CreateContainer` → stream stdout → teardown).

## Non-goals

- Not a TAEF test. Hand-run demo, outside `wsltests.dll`.
- No real HTTP server, no networking, no port mappings. `[http]`/`[cli]` are
  literal string tags emitted by the container, nothing more.
- No attempt to include all 50 TIOBE languages. ~15 of them cannot run as a
  Linux process (hardware-description: VHDL, Ladder Logic; visual: Scratch, GML;
  Windows/proprietary-runtime: Visual Basic, VBScript, X++, ABAP, SAS, CFML,
  MATLAB, Delphi; database-engine: SQL/PL-SQL/T-SQL; Apple-bound: Objective-C;
  superseded/ambiguous: Caml, ML). These are **skipped, not stubbed**.
- No heavy toolchains (Swift, Julia, Kotlin, Scala, Dart, D, Lisp) — excluded to
  keep the image ~1–1.5 GB and the build fast.
- No debugger integration. This demo is about polyglot execution + streaming.

## Decisions (locked with user)

| Choice | Decision |
|---|---|
| What runs | ~20 hello-world programs, one per language, in TIOBE rank order |
| Packaging | ONE polyglot Docker image with all ~20 toolchains |
| Where HTTP "server" lives | Nowhere — `[cli]`/`[http]` are string tags the container prints |
| Windows app job | Receive container stdout and print every line to the console |
| Output format (per language) | `[cli] hello world from [<lang>]` and `[http] hello world from [<lang>]` |
| Execution order | TIOBE July-2026 rank order (Python #1 first) |
| Error handling | **Fail-fast** — any language error aborts the sequence with non-zero exit |
| Location | new folder `test/demo/wslc-polyglot-hello/` |
| SDK package | `Microsoft.WSL.Containers` 2.9.3; `wslc` 2.9.3 CLI on PATH (`C:\Program Files\WSL`) |

## The language set (locked, in execution order)

19 TIOBE-ranked languages + Bash (slotted last). Compiled languages are compiled
at **image-build time** in the Dockerfile so runtime is pure execution.

| Order | Rank | Language | Source file | Runtime step | Toolchain (apt unless noted) |
|---|---|---|---|---|---|
| 1 | 1 | Python | `hello.py` | interpret | `python3` |
| 2 | 2 | C | `hello.c` | run prebuilt | `gcc` (compiled at build) |
| 3 | 3 | C++ | `hello.cpp` | run prebuilt | `g++` (compiled at build) |
| 4 | 4 | Java | `hello.java` | run (`java` single-file) | `default-jdk` |
| 5 | 6 | JavaScript | `hello.js` | interpret | `nodejs` |
| 6 | 9 | R | `hello.R` | interpret | `r-base` (`Rscript`) |
| 7 | 10 | Rust | `hello.rs` | run prebuilt | `rustc` (compiled at build) |
| 8 | 13 | Go | `hello.go` | run prebuilt | `golang` (compiled at build) |
| 9 | 14 | PHP | `hello.php` | interpret | `php-cli` |
| 10 | 16 | Ada | `hello.adb` | run prebuilt | `gnat` (compiled at build) |
| 11 | 17 | Assembly | `hello.s` | run prebuilt | `binutils`/`gcc` (assembled+linked at build) |
| 12 | 19 | Fortran | `hello.f90` | run prebuilt | `gfortran` (compiled at build) |
| 13 | 20 | Ruby | `hello.rb` | interpret | `ruby` |
| 14 | 22 | Perl | `hello.pl` | interpret | `perl` |
| 15 | 23 | COBOL | `hello.cob` | run prebuilt | `gnucobol` (`cobc`, compiled at build) |
| 16 | 24 | Prolog | `hello_prolog.pl` | interpret | `swi-prolog` (`swipl`) |
| 17 | 34 | Lua | `hello.lua` | interpret | `lua5.4` |
| 18 | 36 | OCaml | `hello.ml` | run prebuilt | `ocaml` (compiled at build) |
| 19 | 46 | Haskell | `hello.hs` | run prebuilt | `ghc` (compiled at build) |
| 20 | — | Bash | `hello.sh` | interpret | (in base image) |

**File-name note:** Perl and Prolog both use `.pl`. Prolog's source is named
`hello_prolog.pl` to avoid the clash. All sources live in `polyglot/hello/`.

## Layout

```
test/demo/wslc-polyglot-hello/
├─ app/
│  ├─ WslcPolyglotHello.csproj   # net8.0-windows10.0.19041.0, x64, win-x64,
│  │                             # WindowsSdkPackageVersion 10.0.26100.80,
│  │                             # WslcCliPath pin, Microsoft.WSL.Containers 2.9.3,
│  │                             # 1 <WslcImage Include="polyglot">
│  └─ Program.cs                 # load polyglot.tar -> run -> stream stdout -> teardown
├─ polyglot/
│  ├─ Dockerfile                 # debian:stable-slim + toolchains; compiles the compiled langs
│  ├─ run-all.sh                 # runs each hello-world in order; prints [cli]+[http]; fail-fast
│  └─ hello/                     # the 20 source files (see table)
├─ nuget.config                  # wslc-local -> C:\Users\shuaiyuan\Downloads + nuget.org
└─ README.md
```

## Data flow

**Build time (`dotnet build`).** The csproj declares one `<WslcImage Include="polyglot">`
(Dockerfile = `polyglot/Dockerfile`, Context = `polyglot/`). The SDK targets run
`wslc image build -t wslc-demo/polyglot:latest -f polyglot/Dockerfile polyglot`
then `wslc image save -o $(OutDir)polyglot.tar ...`. The Dockerfile installs all
toolchains and **compiles** the compiled languages' sources into binaries baked
into the image. Result: `polyglot.tar` in the output dir.

**Run time (`Program.cs`).**
```
1. prereq: WslcService.GetMissingComponents(); polyglot.tar exists next to exe
2. clean the session data dir (fresh store — see wslc-multi-service fix)
3. session = new Session("WslcPolyglotHello", dataDir){CpuCount=2, MemorySizeInMB=2048}; Start
4. await session.LoadImageAsync(polyglot.tar)
5. c = session.CreateContainer(ContainerSettings("wslc-demo/polyglot:latest"){
        Name="wslc-polyglot-hello", InitProcess=<empty CommandLine, OutputMode=Event>,
        EnableAutoRemove=true })
6. c.InitProcess.OutputReceived += bytes -> Console.Write(UTF8)       // prints every line
   c.InitProcess.ErrorReceived  += bytes -> Console.Error.Write(UTF8)
   c.InitProcess.Exited += code -> done.TrySetResult(code)           // finite sequence
7. c.Start()
8. int code = await done.Task    // completes when run-all.sh finishes (or Ctrl+C fallback)
9. finally: best-effort Stop(SIGTERM,10s) + Delete(Force) + session.Terminate()
10. return code (non-zero if run-all.sh failed fast on a language)
```

The container's `run-all.sh`, for each language in order:
```
echo "[cli] hello world from [<lang>]"
echo "[http] hello world from [<lang>]"
```
where the middle of each pair actually invokes the language (interpret or run the
prebuilt binary) and asserts it prints the expected `hello world from [<lang>]`.
See "run-all.sh contract" below.

```mermaid
flowchart LR
    subgraph Build["dotnet build (WslcImage target)"]
        B["wslc image build+save\n(polyglot Dockerfile, compiles compiled langs)"]
    end
    subgraph Out["app output dir"]
        T["polyglot.tar"]
    end
    subgraph App["Windows C# app"]
        P[Program.cs]
    end
    subgraph WSLC["WSLC Session"]
        L[LoadImageAsync]
        C["polyglot container\nrun-all.sh (20 langs, in order)"]
    end
    B -->|tar| T
    T -.reads.-> P
    P --> L --> C
    C -->|stdout: [cli]/[http] lines| P
    P -->|Console.Write| Console
```

## run-all.sh contract

- `#!/usr/bin/env bash`, `set -euo pipefail` (**fail-fast**: any non-zero step or
  unset var aborts the whole script with non-zero exit → container init exits
  non-zero → app returns non-zero).
- Languages are processed in the exact order of the table above.
- For each language it runs the program and prints the two tagged lines. The
  program's own output is `hello world from [<lang>]`; the script prefixes the
  channel tag. Concretely, each language's hello-world **prints its own
  `hello world from [<lang>]`**, and run-all.sh captures that once and emits it
  twice with `[cli] ` / `[http] ` prefixes — guaranteeing the two lines are
  identical and that the language actually ran (if the program errors or prints
  nothing, `set -e` / an explicit check aborts).
- No per-language error recovery (fail-fast, per decision). On failure the script
  prints a final `run-all: FAILED at [<lang>]` to stderr and exits non-zero.
- Compiled languages invoke their **prebuilt** binary (built in the Dockerfile),
  e.g. `/app/bin/hello_c`. Interpreted languages invoke the interpreter on the
  source, e.g. `python3 /app/hello/hello.py`.

## Error handling

- **Prereq (app):** missing WSL components → `wsl --install` guidance, exit 1.
  Missing `polyglot.tar` → clear message ("did `dotnet build` run the WslcImage
  target?"), exit 1.
- **Build (Dockerfile):** if any toolchain fails to install or any compiled
  language fails to compile, `docker/wslc build` fails → the demo won't ship a
  broken image. This is the primary guard for criterion 2.
- **Run (container):** fail-fast — the first language that errors aborts
  `run-all.sh`; the app surfaces the non-zero exit and cleans up.
- **Teardown (`finally`):** best-effort Stop/Delete per the container + session
  Terminate, plus `EnableAutoRemove` and the fresh-session-store step, so
  re-runs never collide (carried over from `wslc-multi-service`).

## Acceptance criteria (the bar)

1. The Windows app **compiles** (`dotnet build`).
2. The build **produces `polyglot.tar`** — i.e. all ~20 toolchains install and
   all compiled languages compile in the image.
3. Running it **streams `[cli]` and `[http] hello world from [<lang>]` for all 20
   languages, in TIOBE order, to the console, with no errors** and a zero exit
   code (fail-fast means a clean run proves every language worked).
4. On exit it **auto-destroys** the container + session; **re-running does not
   error**.

## Testing / verification

Hand-run demo:
```
dotnet build test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug   # criteria 1-2
dotnet run   --project test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug  # criteria 3-4
```
`wslc` 2.9.3 is installed, so build (1–2) and run (3–4) are verifiable in this
environment. The **first build is slow** (installs ~20 toolchains; image
~1–1.5 GB) and the compiled-language compile steps run then. A green
`dotnet run` printing 40 lines (20 languages × 2 tags) in order is the success
signal.

## README contents (outline)

- What it demonstrates (one polyglot container, 20 languages in TIOBE order,
  `[cli]`/`[http]` tagged lines streamed to the console; no real HTTP).
- Prereqs: WSL container runtime + `wslc` on PATH; .NET 8 SDK; the nupkg in the
  `nuget.config` source. Note the large first build.
- How the image is built (the `<WslcImage>` item + Dockerfile compiling the
  compiled languages); how to run; expected 40-line ordered output.
- The language table (which languages, which ranks, which are skipped and why).
- Troubleshooting: `WSLC0001` (`wslc` not on PATH), restore 401 (local nuget
  source), `CS1705` (`WindowsSdkPackageVersion`), x64 requirement, and "a
  language failed → the run aborts fail-fast; read the last `run-all:` line".
