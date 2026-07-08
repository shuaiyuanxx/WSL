# WSLC Polyglot Hello Demo

A Windows app that uses the **WSL Containers SDK** (`Microsoft.WSL.Containers`)
to run **one Debian container** that prints "hello world" in **27 programming
languages**, in **TIOBE popularity rank order**. It demonstrates **two output
channels**:

- **`[cli]`** — every language prints its line to the container's **stdout**,
  which the SDK streams to the Windows console (all 27 languages).
- **`[tcp]`** — **18** of the languages *also* run their own **TCP server**; the
  Windows app connects to each and does a `send`→`hello`→`ack` handshake to get
  its line over a **real socket** (see "Real TCP channel" below).

So the console shows, per participating language:

```
[cli] hello world from [python]
[tcp] hello world from [python]
```

The `[cli]` line comes over stdout; the `[tcp]` line is a genuine socket message
that travelled container → Windows. The 9 languages where a raw socket is
disproportionate (assembly, cobol, fortran, ada, prolog, lisp, zig, haskell,
bash) emit `[cli]` only.

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

The app loads `polyglot.tar`, starts the container, streams its stdout (the
`[cli]` lines), and handshakes with each language's TCP server to get the
`[tcp]` lines. You get 27 `[cli]` lines (all languages) and 18 `[tcp]` lines (the
languages with a TCP server), for example:

```
[cli] hello world from [python]
[tcp] hello world from [python]
[cli] hello world from [c]
[tcp] hello world from [c]
[cli] hello world from [assembly]     # cli only — no [tcp] for skip-group
...
[cli] hello world from [bash]
[done] run-all.sh exited code=0
```

> **The `[tcp]` lines can take up to ~1 minute to appear.** The container's TCP
> port is exposed to Windows via the SDK's port mapping, which in this WSL
> version can take ~30–55 s to become stably reachable. The app keeps retrying
> and the container waits for delivery, so a full run may take a couple of
> minutes. `[cli]` lines appear immediately; `[tcp]` lines arrive once the
> mapping is up.

On exit the container + session are torn down; re-runs start clean.

## Real TCP channel

The `[tcp]` lines are **genuine TCP socket messages** exchanged between the
container and the Windows app — not a label. Each of the 18 participating
languages runs its **own** TCP server on its **own** port; the Windows app
connects to each in turn and performs a request/response/ack handshake.

- **Each language = its own TCP server.** `polyglot/servers/srv.<ext>` (one per
  language) binds `0.0.0.0:<port>`, waits (`accept`), and stays active until the
  Windows client talks to it. Ports are fixed 7001–7018 (see the port table
  below).
- **SDK maps 18 ports.** `Program.cs` sets `NetworkingMode = Bridged` and 18
  `PortMappings` (`7001→7001 … 7018→7018`), exposing each language server on
  Windows `localhost:<port>`.
- **Windows = client, sequential.** After the container starts, the app connects
  to each port **in order** and runs this handshake per language:

  ```
  Windows → server:  send
  server  → Windows: hello world from [<lang>]
  Windows → server:  ack
  server exits
  ```

  The app prints `[tcp] hello world from [<lang>]` after each successful
  handshake.

**Why the container is the server (not Windows):** in the WSLC network a
container **cannot** initiate a connection to a Windows host port (inbound is
blocked by the WSL Hyper-V firewall — verified: `Connection refused`). The
supported direction is **host → container** via `PortMappings`. So each language
listens and Windows connects. (A Windows-side `HttpListener`/server on a
non-localhost address also needs admin, another reason to put the servers in the
container.)

**Timing:** each of the 18 port mappings can take ~30–55 s to become reachable,
the WSLC port-proxy may accept+immediately-close a port before its server is
listening (so the app retries each port until the *full* handshake completes),
and Windows connects **sequentially** — so a full run can take **several
minutes**. `run-all.sh` launches all 18 servers in the background and `wait`s for
them (each exits after its ack), with a bounded overall timeout so nothing hangs
forever.

### Port assignment

| Port | Lang | Port | Lang | Port | Lang |
|---|---|---|---|---|---|
| 7001 | python | 7007 | r | 7013 | julia |
| 7002 | javascript | 7008 | go | 7014 | typescript |
| 7003 | ruby | 7009 | rust | 7015 | c |
| 7004 | php | 7010 | java | 7016 | c++ |
| 7005 | perl | 7011 | kotlin | 7017 | ocaml |
| 7006 | lua | 7012 | dart | 7018 | swift |


## Languages (TIOBE July-2026 rank order)

27 languages — every top-50 language that runs as a Linux process (minus Scala,
see below).

**TCP servers (18)** — emit `[cli]` AND run a per-language TCP server (port
7001–7018) that handshakes with the Windows app to deliver `[tcp]`:
python, javascript, ruby, php, perl, lua, r, go, rust, java, kotlin, dart,
julia, swift, typescript, c, c++, ocaml.
**`[cli]`-only (9)** — a TCP server is disproportionate for a demo: assembly,
cobol, fortran, ada, prolog, lisp, zig, haskell, bash.

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
  `run-all: FAILED at [<lang>]` line on stderr and the exit code. A TCP server
  that crashes or never gets its client aborts with
  `run-all: FAILED: server pid <n> ...`.
- **No `[tcp]` lines appear:** the SDK port mappings (`localhost:7001–7018`) may
  not have become reachable — each can take ~30–55 s in this WSL version, and the
  app retries each port for ~120 s. Confirm nothing else on Windows holds ports
  7001–7018. `[cli]` lines are unaffected.
- **Some `[tcp] FAILED to handshake` lines:** that language's server never became
  reachable within the per-port retry window (120 s). Check the container stderr
  for that server crashing, and that its port (see the table above) is free on
  Windows.
- **`dotnet build` fails at the `WslcImage` target with `"/senders": not found`
  (or similar):** the `wslc` BuildKit daemon can cache a stale/corrupt context
  snapshot for the `polyglot/` path that survives even `wsl --shutdown`. Two
  workarounds: (a) if a valid `polyglot.tar` already exists in the output dir,
  `touch` it so the incremental check skips the image rebuild and only the C#
  compiles; or (b) build the image from a *copied* context directory (a
  different path gets a fresh context key), then `wslc image save` it to the
  app output dir as `polyglot.tar`.
