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

<!-- Runbook + language table + troubleshooting filled in by a later task -->
