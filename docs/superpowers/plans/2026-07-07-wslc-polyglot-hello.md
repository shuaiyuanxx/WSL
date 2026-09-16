# WSLC Polyglot Hello Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Windows C# .NET 8 console app that uses the `Microsoft.WSL.Containers` SDK to run one polyglot Debian container which executes a "hello world" in ~20 languages in TIOBE rank order, each emitting `[cli]`/`[http]`-tagged lines to stdout, which the app streams to the console.

**Architecture:** The csproj declares one `<WslcImage>` item; `dotnet build` runs `wslc image build` on a Debian Dockerfile that installs ~20 toolchains and compiles the compiled languages into `/app/bin/*` at build time, then `wslc image save` produces `polyglot.tar`. At runtime `Program.cs` loads the tar, creates+starts one container whose entrypoint `run-all.sh` runs each hello-world in order (fail-fast), and streams the container's stdout to the console until the finite sequence exits. Reuses the `wslc-multi-service` lifecycle (fresh session store, `EnableAutoRemove`, best-effort teardown).

**Tech Stack:** C# / .NET 8 (`net8.0-windows10.0.19041.0`, x64), `Microsoft.WSL.Containers` 2.9.3 (local nuget), `wslc` 2.9.3 CLI, `debian:stable-slim` + ~20 language toolchains (bash entrypoint).

## Global Constraints

- **Location:** all files under `test/demo/wslc-polyglot-hello/`.
- **TFM / platform:** `net8.0-windows10.0.19041.0`, `<WindowsSdkPackageVersion>10.0.26100.80</WindowsSdkPackageVersion>` (avoids CS1705), pinned **x64** (`<Platforms>`/`<Platform>`/`<RuntimeIdentifier>win-x64`). The nuget's targets error if the platform isn't x64/arm64.
- **Package:** `Microsoft.WSL.Containers` `2.9.3`, restored from a **local folder** source (`C:\Users\shuaiyuan\Downloads`) in the demo's own `nuget.config`. Not on public NuGet.
- **`wslc` CLI:** the SDK's `WslcImage` build targets invoke bare `wslc`; it lives at `C:\Program Files\WSL\wslc.exe`. Add `<WslcCliPath>C:\Program Files\WSL\wslc.exe</WslcCliPath>` to the csproj (MSBuild's PATH did not resolve bare `wslc` in the sibling demo → `WSLC0001`).
- **This is a demo, not a TAEF test.** Not added to any `CMakeLists.txt` / `wsltests.dll`. Runs via `dotnet` only.
- **Image tag:** `wslc-demo/polyglot:latest`. **Container name:** `wslc-polyglot-hello`. **Tar name:** `polyglot.tar` in `$(OutDir)` (== `AppContext.BaseDirectory` at runtime).
- **Output format (exact):** for each language, two lines to stdout — `[cli] hello world from [<lang>]` then `[http] hello world from [<lang>]`. Nothing else per language on stdout.
- **Language order (exact, 20):** python, c, c++, java, javascript, r, rust, go, php, ada, assembly, fortran, ruby, perl, cobol, prolog, lua, ocaml, haskell, bash. `<lang>` tags use those exact lowercase strings (note `c++`).
- **Fail-fast:** `run-all.sh` uses `set -euo pipefail`; the first language that errors aborts the run with non-zero exit; the app returns that non-zero code.
- **Compiled at build time** (invoked as prebuilt binaries at runtime): c, c++, rust, go, ada, assembly, fortran, cobol, ocaml, haskell. **Interpreted at runtime:** python, java (single-file `java hello.java`), javascript, r, php, ruby, perl, prolog, lua, bash.
- **Verified toolchains (Debian stable, confirmed by probe builds):** apt packages `python3 default-jdk nodejs r-base rustc golang php-cli ruby perl swi-prolog lua5.4 gcc g++ gfortran gnat gnucobol ocaml-nox ghc binutils libc6-dev ca-certificates`. All compile/run commands below were validated in a real `wslc image build`.
- **Honesty rule:** `wslc` 2.9.3 is installed, so `dotnet build` (criteria 1–2) and `dotnet run` (criteria 3–4) are verifiable here. The **first build is slow** (~20 toolchains, image ~1–1.5 GB). Do not claim a run passed unless the 40 ordered lines were actually observed with exit 0.

---

### Task 1: Scaffold folder, nuget.config, README skeleton

**Files:**
- Create: `test/demo/wslc-polyglot-hello/nuget.config`
- Create: `test/demo/wslc-polyglot-hello/README.md`

**Interfaces:**
- Consumes: nothing.
- Produces: the demo root + the local nuget source Task 4's build relies on.

- [ ] **Step 1: Create `test/demo/wslc-polyglot-hello/nuget.config`**

```xml
<?xml version="1.0" encoding="utf-8"?>
<!--
  NuGet configuration for the WSLC polyglot-hello demo.

  Microsoft.WSL.Containers is not on public NuGet; it is restored from a local
  folder holding the .nupkg. The package version must match the installed WSL
  runtime family (check `wslc version`), or the first SDK call throws
  AccessViolationException.
-->
<configuration>
  <packageSources>
    <clear />
    <add key="wslc-local" value="C:\Users\shuaiyuan\Downloads" />
    <add key="nuget.org" value="https://api.nuget.org/v3/index.json" />
  </packageSources>
</configuration>
```

- [ ] **Step 2: Create `test/demo/wslc-polyglot-hello/README.md`**

````markdown
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
````

- [ ] **Step 3: Verify the files exist**

Run: `ls test/demo/wslc-polyglot-hello/nuget.config test/demo/wslc-polyglot-hello/README.md`
Expected: both paths listed.

- [ ] **Step 4: Commit**

```bash
git add test/demo/wslc-polyglot-hello/nuget.config test/demo/wslc-polyglot-hello/README.md
git commit -m "demo(wslc-polyglot-hello): scaffold folder, local nuget source, README skeleton"
```

---

### Task 2: The 20 hello-world source files

**Files (all under `test/demo/wslc-polyglot-hello/polyglot/hello/`):**
- Create: `hello.py hello.c hello.cpp hello.java hello.js hello.R hello.rs hello.go hello.php hello.adb hello.s hello.f90 hello.rb hello.pl hello.cob hello_prolog.pl hello.lua hello.ml hello.hs hello.sh`

**Interfaces:**
- Consumes: nothing.
- Produces: 20 source files, each of which prints exactly `hello world from [<lang>]` (with the exact tag strings from Global Constraints). Task 3's `run-all.sh` and Task 3's Dockerfile reference these by name.

Each file prints the literal `hello world from [<lang>]` and nothing else. Create all 20:

- [ ] **Step 1: Interpreted-language sources**

`hello.py`:
```python
print("hello world from [python]")
```

`hello.java` (single-file source, class name need not match file for `java hello.java`):
```java
public class Hello {
    public static void main(String[] args) {
        System.out.println("hello world from [java]");
    }
}
```

`hello.js`:
```javascript
console.log("hello world from [javascript]");
```

`hello.R`:
```r
cat("hello world from [r]\n")
```

`hello.php`:
```php
<?php
echo "hello world from [php]\n";
```

`hello.rb`:
```ruby
puts "hello world from [ruby]"
```

`hello.pl` (Perl):
```perl
print "hello world from [perl]\n";
```

`hello_prolog.pl` (SWI-Prolog; `:- initialization(main, main).` runs then halts):
```prolog
:- initialization(main, main).
main :- write('hello world from [prolog]'), nl.
```

`hello.lua`:
```lua
print("hello world from [lua]")
```

`hello.sh`:
```bash
#!/usr/bin/env bash
echo "hello world from [bash]"
```

- [ ] **Step 2: Compiled-language sources (validated by probe build)**

`hello.c`:
```c
#include <stdio.h>
int main(void) { printf("hello world from [c]\n"); return 0; }
```

`hello.cpp`:
```cpp
#include <iostream>
int main() { std::cout << "hello world from [c++]\n"; return 0; }
```

`hello.rs`:
```rust
fn main() { println!("hello world from [rust]"); }
```

`hello.go`:
```go
package main

import "fmt"

func main() { fmt.Println("hello world from [go]") }
```

`hello.adb`:
```ada
with Ada.Text_IO;
procedure Hello is
begin
   Ada.Text_IO.Put_Line("hello world from [ada]");
end Hello;
```

`hello.s` (x86-64 GNU as; PIE-safe `lea msg(%rip)`, direct syscalls):
```gas
.section .rodata
msg: .ascii "hello world from [assembly]\n"
len = . - msg
.section .text
.globl _start
_start:
    mov $1, %rax
    mov $1, %rdi
    lea msg(%rip), %rsi
    mov $len, %rdx
    syscall
    mov $60, %rax
    xor %rdi, %rdi
    syscall
```

`hello.f90`:
```fortran
program hello
    print *, "hello world from [fortran]"
end program hello
```
> Note: Fortran `print *` prepends a leading space. run-all.sh trims it (Task 3).

`hello.cob` (fixed-form; leading spaces matter):
```cobol
       IDENTIFICATION DIVISION.
       PROGRAM-ID. HELLO.
       PROCEDURE DIVISION.
           DISPLAY "hello world from [cobol]".
           STOP RUN.
```

`hello.ml`:
```ocaml
let () = print_endline "hello world from [ocaml]"
```

`hello.hs`:
```haskell
main :: IO ()
main = putStrLn "hello world from [haskell]"
```

- [ ] **Step 3: Verify all 20 files exist**

Run: `ls test/demo/wslc-polyglot-hello/polyglot/hello/ | wc -l`
Expected: `20`.

- [ ] **Step 4: Commit**

```bash
git add test/demo/wslc-polyglot-hello/polyglot/hello/
git commit -m "demo(wslc-polyglot-hello): 20 hello-world source files"
```

---

### Task 3: Dockerfile + run-all.sh (the polyglot image)

**Files:**
- Create: `test/demo/wslc-polyglot-hello/polyglot/run-all.sh`
- Create: `test/demo/wslc-polyglot-hello/polyglot/Dockerfile`

**Interfaces:**
- Consumes: the 20 sources in `hello/` (Task 2).
- Produces: an image context that `wslc image build` turns into `wslc-demo/polyglot:latest`, whose `CMD` runs `run-all.sh`, which prints the 40 tagged lines (2 per language) in order to stdout and exits 0 on success / non-zero fail-fast.

- [ ] **Step 1: Create `polyglot/run-all.sh`**

`run-all.sh` runs each language, captures its single stdout line, verifies it is
non-empty, and emits it twice with `[cli] `/`[http] ` prefixes. Compiled langs
call their prebuilt binary in `/app/bin`; interpreted langs call the interpreter.
Fortran's leading space is stripped. `set -euo pipefail` gives fail-fast.

```bash
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
emit ada       "$BIN/hello_ada"
emit assembly  "$BIN/hello_asm"
emit fortran   "$BIN/hello_fortran"
emit ruby      ruby "$HELLO/hello.rb"
emit perl      perl "$HELLO/hello.pl"
emit cobol     "$BIN/hello_cobol"
emit prolog    swipl -q "$HELLO/hello_prolog.pl"
emit lua       lua5.4 "$HELLO/hello.lua"
emit ocaml     "$BIN/hello_ocaml"
emit haskell   "$BIN/hello_haskell"
emit bash      bash "$HELLO/hello.sh"

echo "run-all: all languages OK" >&2
```

- [ ] **Step 2: Create `polyglot/Dockerfile`**

Installs all toolchains in one layer, copies sources, compiles the compiled
languages into `/app/bin`, and sets `run-all.sh` as CMD. All compile commands
were validated in a probe `wslc image build`.

```dockerfile
FROM debian:stable-slim

WORKDIR /app

# One layer for all ~20 toolchains (validated on debian:stable-slim).
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 default-jdk nodejs r-base rustc golang php-cli ruby perl \
        swi-prolog lua5.4 \
        gcc g++ gfortran gnat gnucobol ocaml-nox ghc binutils libc6-dev \
        ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY hello/ /app/hello/
COPY run-all.sh /app/run-all.sh
RUN chmod +x /app/run-all.sh

# Compile the compiled languages into /app/bin (runtime just executes these).
RUN mkdir -p /app/bin \
 && gcc      -O2 -o /app/bin/hello_c        /app/hello/hello.c \
 && g++      -O2 -o /app/bin/hello_cpp      /app/hello/hello.cpp \
 && rustc    -O  -o /app/bin/hello_rust     /app/hello/hello.rs \
 && (cd /app/hello && go build -o /app/bin/hello_go hello.go) \
 && gnatmake -o /app/bin/hello_ada /app/hello/hello.adb -D /tmp \
 && as -o /tmp/hello.o /app/hello/hello.s && ld -o /app/bin/hello_asm /tmp/hello.o \
 && gfortran -O2 -o /app/bin/hello_fortran  /app/hello/hello.f90 \
 && cobc -x -o /app/bin/hello_cobol /app/hello/hello.cob \
 && ocamlopt -o /app/bin/hello_ocaml /app/hello/hello.ml \
 && ghc -o /app/bin/hello_haskell /app/hello/hello.hs -outputdir /tmp/ghc

CMD ["/app/run-all.sh"]
```

> `gnatmake ... -D /tmp` and `ghc ... -outputdir /tmp/ghc` keep intermediate
> artifacts out of `/app` so the source dir stays clean. `go build` runs in the
> source dir because Go wants the package there. All validated by probe build.

- [ ] **Step 3: Build the image with `wslc` to verify it compiles + runs**

This is the real verification of the whole container (criteria 2 + 3 at the
image level). It is **slow** the first time (installs ~20 toolchains).

Run: `"/c/Program Files/WSL/wslc.exe" image build -t wslc-demo/polyglot:latest -f test/demo/wslc-polyglot-hello/polyglot/Dockerfile test/demo/wslc-polyglot-hello/polyglot`
Expected: build succeeds and tags `wslc-demo/polyglot:latest`.

- [ ] **Step 4: Run the built image to confirm the 40 ordered lines**

Run: `"/c/Program Files/WSL/wslc.exe" container run --rm wslc-demo/polyglot:latest`
Expected (stdout, in this exact order, 40 lines):
```
[cli] hello world from [python]
[http] hello world from [python]
[cli] hello world from [c]
[http] hello world from [c]
... (c++, java, javascript, r, rust, go, php, ada, assembly, fortran, ruby, perl, cobol, prolog, lua, ocaml) ...
[cli] hello world from [haskell]
[http] hello world from [haskell]
[cli] hello world from [bash]
[http] hello world from [bash]
```
(`run-all: all languages OK` appears on stderr.) If any language is missing or
out of order, fix its source (Task 2) or its `emit` line and rebuild.

- [ ] **Step 5: Commit**

```bash
git add test/demo/wslc-polyglot-hello/polyglot/run-all.sh test/demo/wslc-polyglot-hello/polyglot/Dockerfile
git commit -m "demo(wslc-polyglot-hello): polyglot Dockerfile + run-all.sh (20 langs, fail-fast)"
```

---

### Task 4: C# project — SDK reference + WslcImage build item

**Files:**
- Create: `test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj`
- Create: `test/demo/wslc-polyglot-hello/app/Program.cs`

**Interfaces:**
- Consumes: the `wslc-local` source (Task 1); the `polyglot/` image context (Tasks 2–3).
- Produces: a project that compiles (criterion 1) and, at `dotnet build`, builds+saves `polyglot.tar` into `$(OutDir)` (criterion 2). Task 5 replaces `Program.cs` with the load/run logic. The tar filename is `polyglot.tar`, loaded by Task 5 from `AppContext.BaseDirectory`.

- [ ] **Step 1: Create `app/WslcPolyglotHello.csproj`**

```xml
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0-windows10.0.19041.0</TargetFramework>
    <!-- Raise the Windows SDK.NET ref pack for the SDK's C# projection (CS1705). -->
    <WindowsSdkPackageVersion>10.0.26100.80</WindowsSdkPackageVersion>
    <!-- The SDK's WslcImage targets call bare `wslc`; MSBuild's PATH may not
         resolve it, so pin the full path (matches the wslc-multi-service demo). -->
    <WslcCliPath>C:\Program Files\WSL\wslc.exe</WslcCliPath>
    <Platforms>x64</Platforms>
    <Platform>x64</Platform>
    <RuntimeIdentifier>win-x64</RuntimeIdentifier>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <AssemblyName>WslcPolyglotHello</AssemblyName>
    <RootNamespace>WslcPolyglotHello</RootNamespace>
    <AllowUnsafeBlocks>false</AllowUnsafeBlocks>
  </PropertyGroup>

  <ItemGroup>
    <PackageReference Include="Microsoft.WSL.Containers" Version="2.9.3" />
  </ItemGroup>

  <!-- Built at `dotnet build` time: wslc image build -t <Image> -f <Dockerfile>
       <Context>, then wslc image save -o $(OutDir)polyglot.tar <Image>. -->
  <ItemGroup>
    <WslcImage Include="polyglot">
      <Image>wslc-demo/polyglot:latest</Image>
      <Dockerfile>$(MSBuildProjectDirectory)\..\polyglot\Dockerfile</Dockerfile>
      <Context>$(MSBuildProjectDirectory)\..\polyglot</Context>
    </WslcImage>
  </ItemGroup>

</Project>
```

- [ ] **Step 2: Create a minimal placeholder `app/Program.cs`**

```csharp
using Microsoft.WSL.Containers;

// Placeholder — load/run logic is added in the next task. Referencing the SDK
// type here proves the package reference resolves.
if (WslcService.GetMissingComponents().Count > 0)
{
    Console.Error.WriteLine("WSL components are missing. Run: wsl --install");
    return 1;
}
Console.WriteLine("WSLC SDK reference resolved.");
return 0;
```

- [ ] **Step 3: Build — verifies compile (criterion 1) + builds the tar (criterion 2)**

Run: `dotnet build test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug`
Expected: `Build succeeded`, 0 errors, plus `WSLC: Building image ...` / `WSLC: Saving image ...` messages. (Slow first build.) If restore 401s → fix `nuget.config`; if `CS1705` → bump `WindowsSdkPackageVersion`; if `WSLC0001` → confirm the `WslcCliPath`.

- [ ] **Step 4: Confirm the tar was produced**

Run: `ls test/demo/wslc-polyglot-hello/app/bin/x64/Debug/net8.0-windows10.0.19041.0/win-x64/polyglot.tar`
Expected: `polyglot.tar` listed. (If path differs: `find test/demo/wslc-polyglot-hello/app/bin -name '*.tar'`.)

- [ ] **Step 5: Commit**

```bash
git add test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj test/demo/wslc-polyglot-hello/app/Program.cs
git commit -m "demo(wslc-polyglot-hello): C# project + WslcImage build item (builds polyglot.tar)"
```

---

### Task 5: Orchestration — load, run, stream, teardown

**Files:**
- Modify: `test/demo/wslc-polyglot-hello/app/Program.cs` (replace placeholder body)

**Interfaces:**
- Consumes: `WslcService.GetMissingComponents()`, `WslcService.GetVersion()`, `Session`, `SessionSettings`, `Container`, `ContainerSettings`, `ProcessSettings`, `ProcessOutputMode`, `Signal`, `DeleteContainerOption`, `session.LoadImageAsync(path)`. Reads `polyglot.tar` from `AppContext.BaseDirectory`.
- Produces: the finished demo. Terminal deliverable.

**Note:** mirrors `wslc-multi-service/Program.cs` but with ONE image and waits for
the init process to **exit** (finite sequence) rather than Ctrl+C. Use
`List<Container>` — the SDK's `IContainer` is inaccessible (`Container` is the
public concrete type returned by `CreateContainer`).

- [ ] **Step 1: Replace `app/Program.cs`**

```csharp
using System.Text;
using Microsoft.WSL.Containers;

// ---- Config ---------------------------------------------------------------
const string ImageTag = "wslc-demo/polyglot:latest";
const string ContainerName = "wslc-polyglot-hello";

string exeDir = AppContext.BaseDirectory;
string tarPath = Path.Combine(exeDir, "polyglot.tar");   // emitted by the WslcImage target

// ---- Prerequisite checks --------------------------------------------------
if (WslcService.GetMissingComponents().Count > 0)
{
    Console.Error.WriteLine("WSL components are missing. Run: wsl --install");
    return 1;
}
if (!File.Exists(tarPath))
{
    Console.Error.WriteLine(
        $"image tar not found: {tarPath}. Did `dotnet build` run the WslcImage target?");
    return 1;
}

var version = WslcService.GetVersion();
Console.WriteLine($"WSL version: {version.Major}.{version.Minor}.{version.Revision}");

// ---- Session --------------------------------------------------------------
string dataDir = Path.Combine(Path.GetTempPath(), "WslcPolyglotHello", "session");

// Fresh session store each run: a hard-killed prior run leaves its container in
// this session's storage.vhdx, which then collides at CreateContainer. The SDK
// has no delete-by-name and the wslc CLI can't reach another session's store,
// so discard the stale store before starting.
if (Directory.Exists(dataDir))
{
    try { Directory.Delete(dataDir, recursive: true); }
    catch (Exception ex)
    {
        Console.Error.WriteLine(
            $"could not clear stale session store at {dataDir}: {ex.Message}. " +
            "If a container-name conflict follows, delete that folder manually.");
    }
}
Directory.CreateDirectory(dataDir);

var sessionSettings = new SessionSettings("WslcPolyglotHello", dataDir)
{
    CpuCount = 2,
    MemorySizeInMB = 2048,
};

using var session = new Session(sessionSettings);
session.ProcessCrashed += info =>
    Console.Error.WriteLine(
        $"[crash] {info.ProcessName} pid={info.Pid} signal={info.Signal} dump={info.DumpPath}");
session.Start();
Console.WriteLine("session started.");

var containers = new List<Container>();
int exitCode = 0;

try
{
    Console.WriteLine($"[load] LoadImageAsync <- {tarPath}");
    await session.LoadImageAsync(tarPath);

    var initProcess = new ProcessSettings
    {
        CommandLine = new List<string>(),      // empty -> image CMD (run-all.sh)
        OutputMode = ProcessOutputMode.Event,
    };

    var containerSettings = new ContainerSettings(ImageTag)
    {
        Name = ContainerName,
        InitProcess = initProcess,
        EnableAutoRemove = true,               // clean re-run even after a crash
    };

    var container = session.CreateContainer(containerSettings);
    containers.Add(container);

    // The sequence is finite: complete when run-all.sh exits.
    var done = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
    container.InitProcess.OutputReceived += data => Console.Write(Encoding.UTF8.GetString(data));
    container.InitProcess.ErrorReceived += data => Console.Error.Write(Encoding.UTF8.GetString(data));
    container.InitProcess.Exited += code => done.TrySetResult(code);

    // Ctrl+C as a fallback so the user can abort a stuck run into cleanup.
    Console.CancelKeyPress += (_, e) => { e.Cancel = true; done.TrySetResult(-1); };

    container.Start();
    Console.WriteLine("polyglot container started; streaming output...");

    exitCode = await done.Task;                // run-all.sh exit code (non-zero = fail-fast)
    Console.WriteLine($"[done] run-all.sh exited code={exitCode}");
}
catch (Exception ex)
{
    Console.Error.WriteLine($"[error] {ex.Message}");
    exitCode = 1;
}
finally
{
    Console.WriteLine("stopping and cleaning up...");
    foreach (var c in containers)
    {
        try { c.Stop(Signal.SIGTERM, TimeSpan.FromSeconds(10)); } catch { /* best effort */ }
        try { c.Delete(DeleteContainerOption.Force); } catch { /* best effort */ }
    }
    try { session.Terminate(); } catch { /* best effort */ }
}

return exitCode;
```

- [ ] **Step 2: Build to verify it compiles (criterion 1)**

Run: `dotnet build test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug`
Expected: `Build succeeded`, 0 errors. If `LoadImageAsync` doesn't resolve as `await session.LoadImageAsync(tarPath)`, or a member name (`Container`, `Signal.SIGTERM`, `DeleteContainerOption.Force`, `ProcessOutputMode.Event`) errors, fix to the IDE-suggested member (compiler is authoritative) and rebuild.

- [ ] **Step 3: Run — verify the 40 ordered lines stream to the console (criteria 3–4)**

Run (finite; exits on its own — no timeout needed, but cap it in case a language hangs): `timeout 120 dotnet run --project test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug`
Expected: prints `WSL version: 2.9.3`, `session started.`, then the 40 `[cli]`/`[http]` lines **in TIOBE order**, then `[done] run-all.sh exited code=0` and `stopping and cleaning up...`. Process exits 0.
Then run it again immediately and confirm no "name already in use" (criterion 4, clean re-run).

- [ ] **Step 4: Commit**

```bash
git add test/demo/wslc-polyglot-hello/app/Program.cs
git commit -m "demo(wslc-polyglot-hello): load + run polyglot container, stream output, teardown"
```

---

### Task 6: README runbook + language table + final verification

**Files:**
- Modify: `test/demo/wslc-polyglot-hello/README.md` (replace the placeholder comment)

**Interfaces:**
- Consumes: the finished app + image.
- Produces: the user-facing runbook. Terminal task.

- [ ] **Step 1: Replace the `<!-- Runbook ... -->` line in `README.md`**

````markdown
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
````

- [ ] **Step 2: Final build (criteria 1–2)**

Run: `dotnet build test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug`
Expected: `Build succeeded`, 0 errors, `polyglot.tar` present.

- [ ] **Step 3: Confirm the tar exists**

Run: `find test/demo/wslc-polyglot-hello/app/bin -name 'polyglot.tar'`
Expected: one path listed.

- [ ] **Step 4: Commit**

```bash
git add test/demo/wslc-polyglot-hello/README.md
git commit -m "demo(wslc-polyglot-hello): README runbook + language table"
```

---

## Manual verification (criteria 3–4 — needs the WSLC runtime service active)

`wslc` 2.9.3 is installed, so build (1–2) is verified in Tasks 3–6. Criteria 3–4
(load/run/stream/teardown) need the container **runtime service** active; if it
is (Task 3 Step 4's `wslc container run` proves it), verify via Task 5 Step 3.

1. **Ordered output (criterion 3):** `dotnet run ...`; confirm 40 lines, 2 per
   language, in TIOBE order, `[done] ... code=0`, exit 0.
2. **Teardown + clean re-run (criterion 4):** run twice back-to-back; the second
   run starts without a name conflict.
