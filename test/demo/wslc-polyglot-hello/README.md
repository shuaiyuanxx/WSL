# WSLC Polyglot Hello Demo

A Windows app that uses the **WSL Containers SDK** (`Microsoft.WSL.Containers`)
to run **one Debian container** that prints "hello world" in **27 programming
languages**, in **TIOBE popularity rank order**. For each language the container
emits two channel-tagged lines to stdout:

```
[cli] hello world from [python]
[http] hello world from [python]
```

The Windows app streams the container's stdout to the console. There is **no
real HTTP server** — `[cli]` / `[http]` are just labels the container prints.

The 27 languages are built into one image via the SDK's `WslcImage` build
targets (`wslc image build` at `dotnet build` time). Compiled languages
(C, C++, Rust, Go, Ada, Assembly, Fortran, COBOL, OCaml, Haskell) are compiled
into the image; interpreted ones run at container start.

> **Preview SDK.** `Microsoft.WSL.Containers` is a preview API and may change.

## Prerequisites

- WSL container runtime deployed, and the **`wslc` CLI** on `PATH`
  (`C:\Program Files\WSL`) — the build invokes it. Version must match the SDK
  package (2.9.3), else a runtime SDK call throws `AccessViolationException`.
- **.NET 8 SDK**.
- The `Microsoft.WSL.Containers` `.nupkg` in the folder referenced by
  `nuget.config` (`wslc-local`).
- No Docker required — the image is built via `wslc image build`.

## Build it (builds the polyglot image)

```bash
dotnet build test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug
```

The project declares one `<WslcImage>` item; the SDK targets run
`wslc image build` on `polyglot/Dockerfile` (installing 27 toolchains and
**compiling** the compiled languages) then `wslc image save` to `polyglot.tar`
in the output dir. **The first build is slow** (27 toolchains, several fetched
from the network — Swift, Julia, Kotlin, Zig, Dart; the image and `polyglot.tar`
are roughly 7 GB).

## Run it

```bash
dotnet run --project test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug
```

The app loads `polyglot.tar`, starts the container, and streams its stdout. You
get two tagged lines per language, in TIOBE order:

```
[cli] hello world from [python]
[http] hello world from [python]
[cli] hello world from [c]
[http] hello world from [c]
... 40 lines total ...
[cli] hello world from [bash]
[http] hello world from [bash]
[done] run-all.sh exited code=0
```

`[cli]` / `[http]` are just labels the container prints — there is no real HTTP
server. On exit the container + session are torn down; re-runs start clean.

## Languages (TIOBE July-2026 rank order)

27 languages — every top-50 language that runs as a Linux process (minus Scala,
see below).

| # | Rank | Language | How it runs |
|---|---|---|---|
| 1 | 1 | Python | interpreted |
| 2 | 2 | C | compiled at build |
| 3 | 3 | C++ | compiled at build |
| 4 | 4 | Java | `java` single-file |
| 5 | 6 | JavaScript | Node |
| 6 | 9 | R | Rscript |
| 7 | 10 | Rust | compiled at build |
| 8 | 13 | Go | compiled at build |
| 9 | 14 | PHP | interpreted |
| 10 | 15 | Swift | interpreted (`swift`) |
| 11 | 16 | Ada | compiled at build |
| 12 | 17 | Assembly | assembled+linked at build |
| 13 | 19 | Fortran | compiled at build |
| 14 | 20 | Ruby | interpreted |
| 15 | 22 | Perl | interpreted |
| 16 | 23 | COBOL | compiled at build |
| 17 | 24 | Prolog | SWI-Prolog |
| 18 | 27 | Julia | interpreted |
| 19 | 28 | Kotlin | compiled at build (→ jar) |
| 20 | 32 | Dart | interpreted |
| 21 | 33 | Lisp | SBCL script |
| 22 | 34 | Lua | interpreted |
| 23 | 36 | OCaml | compiled at build |
| 24 | 46 | Haskell | compiled at build |
| 25 | 47 | TypeScript | `tsc` at build → Node |
| 26 | 48 | Zig | compiled at build |
| 27 | — | Bash | interpreted |

Compiled languages are built into the image at `dotnet build` time; interpreted
ones run at container start. Toolchains not in Debian apt are fetched during the
build: Swift (swift.org tarball), Julia (official tarball, 1.11+), Zig (official
tarball), Kotlin (JetBrains GitHub release), Dart (Google apt repo).

**Skipped:**
- **Can't run as a Linux process:** Visual Basic, Classic VB, VBScript, SQL/
  PL-SQL/Transact-SQL, Scratch, GML, VHDL, Ladder Logic, LabVIEW, X++, ABAP, SAS,
  CFML, Objective-C (Apple-bound), MATLAB, Delphi, Caml/ML (superseded by OCaml).
- **Scala** — runnable in Linux, but its toolchain download (Scala 3 GitHub
  release) failed with a repeatable TLS error from inside the build container in
  this environment. Excluded pending a reliable fetch path, not a language
  limitation. (D was also left out — lower-ranked, not yet added.)

## Troubleshooting

- **`WSLC0001` (`wslc --version` failed during build):** `wslc` not on MSBuild's
  PATH. Ensure `C:\Program Files\WSL` is on PATH, or the csproj's `<WslcCliPath>`.
- **Restore 401 / package not found:** fix the `wslc-local` source in `nuget.config`.
- **`error CS1705`:** bump `<WindowsSdkPackageVersion>`.
- **Platform error about x64/arm64:** build x64 (the csproj pins it).
- **A language failed → the run aborts (fail-fast):** read the last
  `run-all: FAILED at [<lang>]` line on stderr and the exit code.
