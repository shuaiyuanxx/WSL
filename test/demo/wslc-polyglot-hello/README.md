# WSLC Polyglot Hello Demo

A Windows app that uses the **WSL Containers SDK** (`Microsoft.WSL.Containers`)
to run **one Debian container** that prints "hello world" in **~20 programming
languages**, in **TIOBE popularity rank order**. For each language the container
emits two channel-tagged lines to stdout:

```
[cli] hello world from [python]
[http] hello world from [python]
```

The Windows app streams the container's stdout to the console. There is **no
real HTTP server** — `[cli]` / `[http]` are just labels the container prints.

The ~20 languages are built into one image via the SDK's `WslcImage` build
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
`wslc image build` on `polyglot/Dockerfile` (installing ~20 toolchains and
**compiling** the compiled languages) then `wslc image save` to `polyglot.tar`
in the output dir. **The first build is slow** (~20 toolchains; image ~1–1.5 GB).

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
| 10 | 16 | Ada | compiled at build |
| 11 | 17 | Assembly | assembled+linked at build |
| 12 | 19 | Fortran | compiled at build |
| 13 | 20 | Ruby | interpreted |
| 14 | 22 | Perl | interpreted |
| 15 | 23 | COBOL | compiled at build |
| 16 | 24 | Prolog | SWI-Prolog |
| 17 | 34 | Lua | interpreted |
| 18 | 36 | OCaml | compiled at build |
| 19 | 46 | Haskell | compiled at build |
| 20 | — | Bash | interpreted |

**Skipped** (can't run as a Linux process): Visual Basic, VBScript, SQL/PL-SQL/
Transact-SQL, Scratch, GML, VHDL, Ladder Logic, LabVIEW, X++, ABAP, SAS, CFML,
Objective-C (Apple-bound), MATLAB, Delphi, Caml/ML (superseded by OCaml), plus
heavy toolchains omitted to keep the image small (Swift, Julia, Kotlin, Scala,
Dart, D, Lisp).

## Troubleshooting

- **`WSLC0001` (`wslc --version` failed during build):** `wslc` not on MSBuild's
  PATH. Ensure `C:\Program Files\WSL` is on PATH, or the csproj's `<WslcCliPath>`.
- **Restore 401 / package not found:** fix the `wslc-local` source in `nuget.config`.
- **`error CS1705`:** bump `<WindowsSdkPackageVersion>`.
- **Platform error about x64/arm64:** build x64 (the csproj pins it).
- **A language failed → the run aborts (fail-fast):** read the last
  `run-all: FAILED at [<lang>]` line on stderr and the exit code.
