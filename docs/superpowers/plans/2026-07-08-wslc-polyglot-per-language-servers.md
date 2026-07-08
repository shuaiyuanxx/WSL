# WSLC Polyglot Hello — Per-Language TCP Servers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the `[tcp]` aggregator with a per-language TCP server model: each of 18 languages runs its own TCP server on its own port (7001–7018), waits, and the Windows app connects to each sequentially, does a `send`→`hello`→`ack` handshake, then the server exits. `[cli]` (27 languages) is unchanged.

**Architecture:** `run-all.sh` launches all 18 `servers/srv.<ext>` in the background (each binds its port and blocks on accept), runs the 27 `[cli]` lines, then `wait`s for the servers. The csproj declares 18 `PortMappings`. `Program.cs` maps the ports and runs a sequential handshake loop over 7001..7018, printing `[tcp] <hello>` per language. All 18 server sources + the multi-port mapping were verified end-to-end with real `wslc` builds during design.

**Tech Stack:** existing polyglot image (debian + 27 toolchains) + `lua-socket`; C# / .NET 8 `Microsoft.WSL.Containers` 2.9.3; `wslc` 2.9.3.

## Global Constraints

- **Location:** all changes under `test/demo/wslc-polyglot-hello/`.
- **This REPLACES the aggregator design:** DELETE `polyglot/tcp_server.py` and the whole `polyglot/senders/` dir; ADD `polyglot/servers/`. Remove the aggregator launch + per-language `tcp` sends from `run-all.sh`; remove the aggregator PortMapping (9099) + the single TcpClient reader from `Program.cs`.
- **Topology:** container = TCP server, Windows = client (container→host inbound is blocked; host→container via PortMappings is supported — verified).
- **18 languages, fixed ports:** python=7001, javascript=7002, ruby=7003, php=7004, perl=7005, lua=7006, r=7007, go=7008, rust=7009, java=7010, kotlin=7011, dart=7012, julia=7013, typescript=7014, c=7015, c++=7016, ocaml=7017, swift=7018.
- **Handshake (exact):** Windows connects → sends `send\n` → server replies `hello world from [<lang>]\n` → Windows sends `ack\n` → server exits. Windows prints `[tcp] hello world from [<lang>]`.
- **Windows connects SEQUENTIALLY** in port order 7001..7018 (one handshake fully completes before the next).
- **All 18 servers start in the background at container start** and wait; each exits after its ack. `run-all.sh` `wait`s for them.
- **Message content:** each server sends exactly `hello world from [<lang>]` — same as that language's `[cli]` line (note `[c++]`).
- **Compiled servers** (built at image-build time into `servers/bin/`): c, c++, rust, go, ocaml, kotlin (jar), java (class), typescript (tsc→js), dart (exe). **Interpreted** (run from source): python, node(js), ruby, php, perl, lua, r, julia, swift (interpreted — no swiftc).
- **`lua-socket` apt package required** (srv.lua uses `require('socket')`).
- **Hard-fail:** `run-all.sh` keeps `set -euo pipefail`; a server exiting non-zero aborts non-zero.
- **All 18 server snippets in this plan were verified** by a real `wslc` build + an in-container 18/18 handshake probe — use them verbatim.
- **Timing:** 18 port mappings each take ~30–55 s to activate; the WSLC proxy may accept+immediately-close before the in-container server is reachable. The Windows side must retry each port until the FULL handshake completes (not just connect). A full run may take several minutes.
- **`[cli]` channel, TIOBE order, 27 languages, fresh-store, EnableAutoRemove, teardown — unchanged.**
- **Honesty rule:** `wslc` 2.9.3 installed; build (1–2) and run (3–5) are verifiable here. Do not claim a run passed without observing 27 `[cli]` + 18 `[tcp]` + exit 0. NOTE the wslc stale-build-context bug: `dotnet build`'s WslcImage target may fail with `"/servers": not found`; workaround: `touch polyglot.tar` to skip the image rebuild (C# still compiles), OR build the image from a COPIED context dir then `wslc image save` to the app output dir.

---

### Task 1: Remove the aggregator + senders

**Files:**
- Delete: `test/demo/wslc-polyglot-hello/polyglot/tcp_server.py`
- Delete: `test/demo/wslc-polyglot-hello/polyglot/senders/` (all 18 files)

**Interfaces:**
- Consumes: nothing.
- Produces: a clean slate — the aggregator model is gone. Task 3 (Dockerfile/run-all.sh) and Task 5 (Program.cs) will remove the remaining references.

- [ ] **Step 1: Delete the aggregator and senders dir**

```bash
git rm test/demo/wslc-polyglot-hello/polyglot/tcp_server.py
git rm -r test/demo/wslc-polyglot-hello/polyglot/senders
```

- [ ] **Step 2: Verify they're gone**

Run: `ls test/demo/wslc-polyglot-hello/polyglot/ 2>&1; test ! -e test/demo/wslc-polyglot-hello/polyglot/senders && echo "senders removed"`
Expected: `tcp_server.py` and `senders/` no longer listed; "senders removed".

- [ ] **Step 3: Commit**

```bash
git commit -m "demo(wslc-polyglot-hello): remove TCP aggregator + one-shot senders (superseded)"
```

---

### Task 2: The 18 language server programs

**Files (all under `test/demo/wslc-polyglot-hello/polyglot/servers/`):**
- Create: `srv.py srv.js srv.rb srv.php srv.pl srv.lua srv.R srv.go srv.rs Srv.java Srv.kt srv.dart srv.jl srv.ts srv.c srv.cpp srv.ml srv.swift`

**Interfaces:**
- Consumes: nothing.
- Produces: 18 servers, each takes a port as `argv[1]`, binds `0.0.0.0:<port>`, accepts one connection, reads a request, sends `hello world from [<lang>]\n`, reads the ack, exits. Referenced by `run-all.sh` (Task 3) and compiled by the Dockerfile (Task 3). All content below was verified by an 18/18 in-container handshake probe — use verbatim. LF line endings.

- [ ] **Step 1: Interpreted-language servers**

`srv.py`:
```python
import socket,sys
p=int(sys.argv[1]); s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
s.bind(('0.0.0.0',p)); s.listen(1)
c,_=s.accept(); c.recv(64)
c.sendall(b'hello world from [python]\n')
c.recv(64); c.close()
```

`srv.js`:
```javascript
const net=require('net'); const port=+process.argv[2];
const srv=net.createServer(c=>{ c.once('data',()=>{ c.write('hello world from [javascript]\n'); c.once('data',()=>{c.end(); srv.close();}); }); });
srv.listen(port,'0.0.0.0');
```

`srv.rb`:
```ruby
require 'socket'; port=ARGV[0].to_i
s=TCPServer.new('0.0.0.0',port); c=s.accept; c.recv(64)
c.write("hello world from [ruby]\n"); c.recv(64); c.close
```

`srv.php`:
```php
<?php $port=(int)$argv[1];
$s=stream_socket_server("tcp://0.0.0.0:$port"); $c=stream_socket_accept($s,-1);
fread($c,64); fwrite($c,"hello world from [php]\n"); fread($c,64); fclose($c);
```

`srv.pl`:
```perl
use IO::Socket::INET; my $port=$ARGV[0];
my $s=IO::Socket::INET->new(LocalAddr=>'0.0.0.0',LocalPort=>$port,Proto=>'tcp',Listen=>1,ReuseAddr=>1);
my $c=$s->accept(); my $b; $c->recv($b,64); print $c "hello world from [perl]\n"; $c->recv($b,64); close($c);
```

`srv.lua` (needs `lua-socket`, added in Task 3):
```lua
local socket=require('socket'); local port=tonumber(arg[1])
local s=assert(socket.bind('0.0.0.0',port)); local c=s:accept(); c:receive(1)
c:send('hello world from [lua]\n'); c:receive(1); c:close()
```

`srv.R`:
```r
port <- as.integer(commandArgs(TRUE)[1])
srv <- serverSocket(port); con <- socketAccept(srv, blocking=TRUE, open="r+b")
readLines(con, n=1, warn=FALSE); writeLines("hello world from [r]", con); readLines(con, n=1, warn=FALSE); close(con); close(srv)
```

`srv.jl`:
```julia
using Sockets; port=parse(Int,ARGS[1]); srv=listen(IPv4(0),port); c=accept(srv)
readavailable(c); write(c,"hello world from [julia]\n"); readavailable(c); close(c); close(srv)
```

`srv.swift` (run via `swift` interpret):
```swift
import Glibc
let port = UInt16(CommandLine.arguments[1])!
let fd = socket(AF_INET, Int32(SOCK_STREAM.rawValue), 0)
var one: Int32 = 1
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, socklen_t(MemoryLayout<Int32>.size))
var a = sockaddr_in()
a.sin_family = sa_family_t(AF_INET)
a.sin_addr.s_addr = INADDR_ANY
a.sin_port = port.bigEndian
withUnsafePointer(to: &a) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { _ = bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size)) } }
listen(fd, 1)
let c = accept(fd, nil, nil)
var b = [UInt8](repeating: 0, count: 64)
_ = read(c, &b, 64)
let m = "hello world from [swift]\n"
_ = m.withCString { send(c, $0, strlen($0), 0) }
_ = read(c, &b, 64)
close(c)
```

- [ ] **Step 2: Compiled-language servers**

`srv.go`:
```go
package main
import("net";"os";"strconv")
func main(){ p,_:=strconv.Atoi(os.Args[1]); l,_:=net.Listen("tcp","0.0.0.0:"+strconv.Itoa(p)); c,_:=l.Accept(); b:=make([]byte,64); c.Read(b); c.Write([]byte("hello world from [go]\n")); c.Read(b); c.Close() }
```

`srv.rs`:
```rust
use std::net::TcpListener; use std::io::{Read,Write}; use std::env;
fn main(){ let p:u16=env::args().nth(1).unwrap().parse().unwrap();
  let l=TcpListener::bind(("0.0.0.0",p)).unwrap(); let (mut c,_)=l.accept().unwrap();
  let mut b=[0u8;64]; let _=c.read(&mut b); c.write_all(b"hello world from [rust]\n").unwrap(); let _=c.read(&mut b); }
```

`Srv.java`:
```java
import java.net.*; import java.io.*;
public class Srv { public static void main(String[] a) throws Exception {
  int p=Integer.parseInt(a[0]); try(ServerSocket ss=new ServerSocket(p)){ Socket c=ss.accept();
    c.getInputStream().read(new byte[64]); c.getOutputStream().write("hello world from [java]\n".getBytes()); c.getOutputStream().flush();
    c.getInputStream().read(new byte[64]); c.close(); } } }
```

`Srv.kt`:
```kotlin
import java.net.ServerSocket
fun main(a:Array<String>){ val p=a[0].toInt(); ServerSocket(p).use{ ss-> val c=ss.accept()
  c.getInputStream().read(ByteArray(64)); c.getOutputStream().write("hello world from [kotlin]\n".toByteArray()); c.getOutputStream().flush()
  c.getInputStream().read(ByteArray(64)); c.close() } }
```

`srv.dart`:
```dart
import 'dart:io';
void main(List<String> a) async { final p=int.parse(a[0]); final ss=await ServerSocket.bind('0.0.0.0',p);
  await for (final c in ss){ c.listen((d){ c.write('hello world from [dart]\n'); }); await Future.delayed(Duration(milliseconds:800)); c.destroy(); break; } await ss.close(); }
```

`srv.ts` (the `declare` lines are required — the image's TypeScript has no `@types/node`):
```typescript
declare const require:any; declare const process:any;
const net=require('net'); const port=+process.argv[2];
const srv=net.createServer((c:any)=>{ c.once('data',()=>{ c.write('hello world from [typescript]\n'); c.once('data',()=>{c.end(); srv.close();}); }); });
srv.listen(port,'0.0.0.0');
```

`srv.c`:
```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
int main(int argc,char**argv){ int p=atoi(argv[1]); int fd=socket(AF_INET,SOCK_STREAM,0);
  int one=1; setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
  struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(p);
  bind(fd,(struct sockaddr*)&a,sizeof(a)); listen(fd,1); int c=accept(fd,0,0);
  char b[64]; read(c,b,64); const char*m="hello world from [c]\n"; write(c,m,strlen(m)); read(c,b,64); close(c); return 0; }
```

`srv.cpp`:
```cpp
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
int main(int argc,char**argv){ int p=atoi(argv[1]); int fd=socket(AF_INET,SOCK_STREAM,0);
  int one=1; setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
  sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(p);
  bind(fd,(sockaddr*)&a,sizeof(a)); listen(fd,1); int c=accept(fd,0,0);
  char b[64]; read(c,b,64); const char*m="hello world from [c++]\n"; write(c,m,strlen(m)); read(c,b,64); close(c); return 0; }
```

`srv.ml`:
```ocaml
let () =
  let port = int_of_string Sys.argv.(1) in
  let s = Unix.socket Unix.PF_INET Unix.SOCK_STREAM 0 in
  Unix.setsockopt s Unix.SO_REUSEADDR true;
  Unix.bind s (Unix.ADDR_INET (Unix.inet_addr_any, port));
  Unix.listen s 1;
  let (c,_) = Unix.accept s in
  let b = Bytes.create 64 in
  ignore (Unix.read c b 0 64);
  let m = "hello world from [ocaml]\n" in
  ignore (Unix.write_substring c m 0 (String.length m));
  ignore (Unix.read c b 0 64);
  Unix.close c
```

- [ ] **Step 3: Verify all 18 exist with LF endings**

Run: `ls test/demo/wslc-polyglot-hello/polyglot/servers/ | wc -l`
Expected: `18`.
Run: `for f in test/demo/wslc-polyglot-hello/polyglot/servers/*; do grep -lq $'\r' "$f" && echo "CRLF: $f"; done; echo "(none above = LF)"`
Expected: `(none above = LF)`.

- [ ] **Step 4: Commit**

```bash
git add test/demo/wslc-polyglot-hello/polyglot/servers/
git commit -m "demo(wslc-polyglot-hello): 18 per-language TCP server programs (verified)"
```

---

### Task 3: Dockerfile + run-all.sh (build servers, launch + wait)

**Files:**
- Modify: `test/demo/wslc-polyglot-hello/polyglot/Dockerfile`
- Modify: `test/demo/wslc-polyglot-hello/polyglot/run-all.sh`

**Interfaces:**
- Consumes: the 18 servers (Task 2).
- Produces: an image where the 9 compiled servers are prebuilt into `/app/servers/bin/`, `lua-socket` is installed, and `run-all.sh` launches all 18 servers in the background (each on its port), runs the 27 `[cli]` lines, and `wait`s for the servers (hard-fail on non-zero). This is what `dotnet build` saves as `polyglot.tar`.

- [ ] **Step 1: Update the Dockerfile — remove aggregator/senders COPY+build, add servers**

In `test/demo/wslc-polyglot-hello/polyglot/Dockerfile`:
1. Ensure `lua-socket` is in the apt install line (it was added for the senders; keep it).
2. **Remove** the `COPY tcp_server.py ...`, `COPY senders/ ...`, and the sender-compile `RUN` block (they reference deleted files).
3. **Add** (before the final `CMD`): copy `servers/` and compile the 9 compiled servers.

Replace the sender COPY/compile block with:

```dockerfile
COPY servers/ /app/servers/

# Compile the compiled-language servers into /app/servers/bin (interpreted
# servers run from source at container start). Validated by the 18/18 probe.
RUN mkdir -p /app/servers/bin \
 && gcc  -o /app/servers/bin/srv_c   /app/servers/srv.c \
 && g++  -o /app/servers/bin/srv_cpp /app/servers/srv.cpp \
 && rustc -o /app/servers/bin/srv_rust /app/servers/srv.rs \
 && (cd /app/servers && go build -o /app/servers/bin/srv_go srv.go) \
 && ocamlopt unix.cmxa /app/servers/srv.ml -o /app/servers/bin/srv_ocaml \
 && kotlinc /app/servers/Srv.kt -include-runtime -d /app/servers/bin/srv_kt.jar \
 && (cd /app/servers && javac Srv.java -d /app/servers/bin) \
 && tsc --outDir /app/servers/bin /app/servers/srv.ts \
 && (cd /app/servers && dart compile exe srv.dart -o /app/servers/bin/srv_dart)
```

> `ocamlopt unix.cmxa` (no ocamlfind). `tsc --outDir /app/servers/bin srv.ts` emits `srv.js` in the bin dir. Swift has no swiftc → interpreted at runtime. All validated by the probe build.

- [ ] **Step 2: Replace `run-all.sh` — launch 18 servers, run [cli], wait for servers**

Replace `test/demo/wslc-polyglot-hello/polyglot/run-all.sh` with:

```bash
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
```

> `date +%s` is used for the wall-clock timeout — this runs INSIDE the Linux container (bash), not in the plan's authoring shell, so it is available and fine. The `PIDS` array preserves launch order; the wait loop enforces the overall 300s bound.

- [ ] **Step 3: Build the image with `wslc`**

Run: `"/c/Program Files/WSL/wslc.exe" image build -t wslc-demo/polyglot:latest -f test/demo/wslc-polyglot-hello/polyglot/Dockerfile test/demo/wslc-polyglot-hello/polyglot`
Expected: build succeeds; the 9 compiled servers compile; tags the image. (If it fails with `"/servers": not found` — the wslc stale-context bug — build from a copied context dir instead: `cp -r polyglot /tmp/pg && wslc image build -t wslc-demo/polyglot:latest -f /tmp/pg/Dockerfile /tmp/pg`.)

- [ ] **Step 4: Standalone smoke — 27 [cli] lines + servers wait (no Windows client)**

Standalone there is no Windows client, so the 18 servers never get their ack and run-all's 300s timeout will eventually fire (hard-fail rc=1). That's EXPECTED here — we only check the `[cli]` output appears:
Run: `timeout 90 "/c/Program Files/WSL/wslc.exe" container run --rm wslc-demo/polyglot:latest 2>/dev/null | grep -c "^\[cli\]"`
Expected: `27`. (The container will still be running its server-wait when the 90s `timeout` kills it — fine. The real [tcp] handshake is verified in Task 5 with the actual app.)

- [ ] **Step 5: Commit**

```bash
git add test/demo/wslc-polyglot-hello/polyglot/Dockerfile test/demo/wslc-polyglot-hello/polyglot/run-all.sh
git commit -m "demo(wslc-polyglot-hello): build 18 servers + launch/wait in run-all.sh (hard-fail)"
```

---

### Task 4: C# project — 18 PortMappings

**Files:**
- Modify: `test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj` (no change — WslcImage item stays) — actually NONE; ports are set in Program.cs's ContainerSettings, not the csproj.

**This task is folded into Task 5** (the 18 PortMappings live in Program.cs's `ContainerSettings`, alongside the handshake loop). No standalone Task 4 — proceed to Task 5.

---

### Task 5: Windows app — 18 PortMappings + sequential handshake loop

**Files:**
- Modify: `test/demo/wslc-polyglot-hello/app/Program.cs`

**Interfaces:**
- Consumes: the 18 container servers via the mapped `localhost:7001..7018`.
- Produces: the finished app. Replaces the old single-port (9099) mapping + aggregator TcpClient reader with 18 PortMappings + a sequential per-port handshake loop printing `[tcp] <hello>`.

- [ ] **Step 1: Replace the ContainerSettings networking (remove 9099, add 7001..7018)**

In `Program.cs`, the current `ContainerSettings` has (from the aggregator design):
```csharp
        NetworkingMode = ContainerNetworkingMode.Bridged,
        PortMappings = new List<ContainerPortMapping>
        {
            new(9099, 9099, PortProtocol.TCP),
        },
```
Replace the `PortMappings` list with the 18 language ports:
```csharp
        NetworkingMode = ContainerNetworkingMode.Bridged,
        PortMappings = new List<ContainerPortMapping>
        {
            new(7001, 7001, PortProtocol.TCP), new(7002, 7002, PortProtocol.TCP),
            new(7003, 7003, PortProtocol.TCP), new(7004, 7004, PortProtocol.TCP),
            new(7005, 7005, PortProtocol.TCP), new(7006, 7006, PortProtocol.TCP),
            new(7007, 7007, PortProtocol.TCP), new(7008, 7008, PortProtocol.TCP),
            new(7009, 7009, PortProtocol.TCP), new(7010, 7010, PortProtocol.TCP),
            new(7011, 7011, PortProtocol.TCP), new(7012, 7012, PortProtocol.TCP),
            new(7013, 7013, PortProtocol.TCP), new(7014, 7014, PortProtocol.TCP),
            new(7015, 7015, PortProtocol.TCP), new(7016, 7016, PortProtocol.TCP),
            new(7017, 7017, PortProtocol.TCP), new(7018, 7018, PortProtocol.TCP),
        },
```

- [ ] **Step 2: Replace the aggregator TcpClient reader with a sequential handshake loop**

Remove the old background TcpClient reader block (the one that connected to 9099 and retried past empty closes). Replace it with a sequential loop over the 18 `(lang, port)` pairs, run as a background task so the container keeps streaming `[cli]` meanwhile. For each port: retry-connect + full handshake (`send` → read hello → `ack`) until it succeeds or a per-port deadline elapses; print `[tcp] <hello>`.

Insert this after `container.Start()` (replacing the removed reader):
```csharp
    // Per-language TCP handshake: connect SEQUENTIALLY to each mapped server
    // port, send "send", read the hello, send "ack" (server then exits), print
    // [tcp]. The Bridged mapping can take ~30-55s per port to become reachable,
    // and the WSLC proxy may accept+immediately-close before the in-container
    // server is listening — so retry each port until a FULL handshake completes.
    using var tcpCts = new CancellationTokenSource();
    (string lang, int port)[] servers =
    {
        ("python",7001),("javascript",7002),("ruby",7003),("php",7004),
        ("perl",7005),("lua",7006),("r",7007),("go",7008),("rust",7009),
        ("java",7010),("kotlin",7011),("dart",7012),("julia",7013),
        ("typescript",7014),("c",7015),("c++",7016),("ocaml",7017),("swift",7018),
    };
    var tcpWork = Task.Run(async () =>
    {
        foreach (var (lang, port) in servers)
        {
            var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(120);
            string? hello = null;
            while (hello is null && DateTime.UtcNow < deadline && !tcpCts.IsCancellationRequested)
            {
                try
                {
                    using var client = new System.Net.Sockets.TcpClient();
                    await client.ConnectAsync("127.0.0.1", port, tcpCts.Token);
                    var ns = client.GetStream();
                    await ns.WriteAsync(Encoding.UTF8.GetBytes("send\n"), tcpCts.Token);
                    using var reader = new StreamReader(ns);
                    var line = await reader.ReadLineAsync(tcpCts.Token);
                    if (!string.IsNullOrEmpty(line))
                    {
                        await ns.WriteAsync(Encoding.UTF8.GetBytes("ack\n"), tcpCts.Token);
                        hello = line;                    // full handshake done
                    }
                }
                catch (OperationCanceledException) { return; }
                catch { try { await Task.Delay(1000, tcpCts.Token); } catch (OperationCanceledException) { return; } }
            }
            if (hello is not null) Console.WriteLine($"[tcp] {hello}");
            else Console.Error.WriteLine($"[tcp] FAILED to handshake [{lang}] on port {port}");
        }
    });

    exitCode = await done.Task;
    Console.WriteLine($"[done] run-all.sh exited code={exitCode}");
    await Task.WhenAny(tcpWork, Task.Delay(5000));
    tcpCts.Cancel();
```

> `Encoding` is already `using System.Text;`. `System.Net.Sockets.TcpClient` fully-qualified (no new using needed). If `ReadLineAsync(CancellationToken)` is unavailable, use `ReadLineAsync()` and rely on cancel + close.

- [ ] **Step 3: Build to verify it compiles (criterion 1)**

Run: `dotnet build test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug`
Expected: `Build succeeded`, 0 errors. (If the WslcImage target fails with `"/servers": not found`, `touch` an existing `polyglot.tar` in the output dir to skip the image rebuild, or rebuild the image from a copied context; the C# still compiles.)

- [ ] **Step 4: Commit**

```bash
git add test/demo/wslc-polyglot-hello/app/Program.cs
git commit -m "demo(wslc-polyglot-hello): 18 PortMappings + sequential per-language TCP handshake"
```

---

### Task 6: End-to-end run + README

**Files:**
- Modify: `test/demo/wslc-polyglot-hello/README.md`

**Interfaces:**
- Consumes: the finished app + image.
- Produces: the verified end-to-end demo + updated docs. Terminal task.

- [ ] **Step 1: Full build (refresh polyglot.tar)**

Run: `dotnet build test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug`
Expected: `Build succeeded`, `polyglot.tar` produced. (Use the copied-context or `touch` workaround if the WslcImage target hits the stale-context bug; then `wslc image save -o <outdir>/polyglot.tar wslc-demo/polyglot:latest`.)

- [ ] **Step 2: Run end-to-end — 27 [cli] + 18 [tcp] + exit 0 (may take several minutes)**

Run: `timeout 400 dotnet run --project test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug > /tmp/pg-srv.log 2>&1; echo "exit=$?"`
Then:
- `grep -c "^\[cli\]" /tmp/pg-srv.log` → expect `27`
- `grep -c "^\[tcp\]" /tmp/pg-srv.log` → expect `18`
- `grep "^\[tcp\]" /tmp/pg-srv.log | grep -oE "\[[a-z+]+\]$" | sort -u | tr '\n' ' '` → expect the 18 language tags (incl `[c++]`, `[javascript]`, `[typescript]`)
- `grep "\[done\] run-all.sh exited code=0" /tmp/pg-srv.log` → present, exit 0

If any `[tcp]` line is missing or `[tcp] FAILED to handshake` appears, that language's server failed — investigate that server + its port. Do not proceed until 27 `[cli]` + 18 `[tcp]` + exit 0. (The run is slow because 18 mappings activate at ~30–55s each and Windows connects sequentially — budget several minutes.)

- [ ] **Step 3: Update the README**

Rewrite the TCP section for the per-language-server model:
1. Intro/`[tcp]` description: each of the 18 languages runs its OWN TCP server on its OWN port; Windows connects sequentially and does a `send`→`hello`→`ack` handshake; the server then exits.
2. Add the port table (7001–7018 ↔ language).
3. Document the handshake protocol.
4. Keep the topology rationale (container = server because container→host inbound is blocked).
5. Timing caveat: 18 mappings × ~30–55s activation + sequential connect ⇒ a full run takes SEVERAL MINUTES.
6. Keep the wslc stale-build-context workaround note.
7. Remove any remaining mention of the old aggregator / `tcp_server.py` / `senders/`.
(Write concrete prose — no placeholders.)

- [ ] **Step 4: Final build sanity + commit**

Run: `dotnet build test/demo/wslc-polyglot-hello/app/WslcPolyglotHello.csproj -c Debug`
Expected: `Build succeeded`, 0 errors.

```bash
git add test/demo/wslc-polyglot-hello/README.md
git commit -m "demo(wslc-polyglot-hello): document per-language TCP servers + handshake"
```

---

## Manual verification (runnable here — `wslc` 2.9.3 installed; both prerequisites verified)

1. **Compiles + builds tar (criteria 1-2):** `dotnet build ...` → `Build succeeded` + `polyglot.tar` (may need the stale-context workaround).
2. **27 [cli] + 18 [tcp] + exit 0 (criterion 3):** `dotnet run ...` → 27 `[cli]` lines and 18 `[tcp]` lines (each via a real per-language-server handshake over its own mapped port), exit 0. Slow (~several minutes).
3. **Hard-fail (criterion 4):** if a server is broken, run-all.sh's wait detects a non-zero/timed-out server and exits non-zero.
4. **Teardown + clean re-run (criterion 5):** run twice back-to-back; the second run starts without a name/port conflict.
