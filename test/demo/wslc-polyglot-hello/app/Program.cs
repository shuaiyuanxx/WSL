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

    // pid-1 stays alive (sleep) so the Windows app can CreateProcess into the
    // running container and orchestrate everything itself — no run-all.sh.
    var initProcess = new ProcessSettings { OutputMode = ProcessOutputMode.Event };
    initProcess.CommandLine.Add("sleep");
    initProcess.CommandLine.Add("infinity");

    var containerSettings = new ContainerSettings(ImageTag)
    {
        Name = ContainerName,
        InitProcess = initProcess,
        EnableAutoRemove = true,               // clean re-run even after a crash
        NetworkingMode = ContainerNetworkingMode.Bridged,   // required for PortMappings
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
    };

    var container = session.CreateContainer(containerSettings);
    containers.Add(container);

    // Ctrl+C as a fallback so the user can abort a stuck run into cleanup.
    Console.CancelKeyPress += (_, e) => { e.Cancel = true; Environment.Exit(130); };

    container.Start();
    Console.WriteLine("polyglot container started (pid-1 = sleep); orchestrating...");

    // ---- helpers: run a process inside the container via CreateProcess -------

    // Run one program to completion; capture stdout, assert non-empty, return it.
    // Throws on non-zero exit or empty output (fail-fast, same as the old script).
    async Task<string> ExecCaptureAsync(string label, params string[] argv)
    {
        var ps = new ProcessSettings { OutputMode = ProcessOutputMode.Event };
        foreach (var a in argv) ps.CommandLine.Add(a);

        var sb = new StringBuilder();
        var exited = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        using var proc = container.CreateProcess(ps);
        proc.OutputReceived += d => sb.Append(Encoding.UTF8.GetString(d));
        proc.ErrorReceived += d => Console.Error.Write(Encoding.UTF8.GetString(d));
        proc.Exited += code => exited.TrySetResult(code);
        proc.Start();
        if (proc.State == ProcessState.Exited) exited.TrySetResult(proc.ExitCode);

        int code = await exited.Task;
        await Task.Delay(50);                  // let any final OutputReceived chunk drain
        string outText = sb.ToString().Trim();
        if (code != 0) throw new Exception($"[{label}] exited {code}");
        if (outText.Length == 0) throw new Exception($"[{label}] produced no output");
        return outText;
    }

    // Launch a long-running server process in the background (do not await exit).
    // Returns the Process so we can wait on it after the handshakes.
    Process ExecBackground(params string[] argv)
    {
        var ps = new ProcessSettings { OutputMode = ProcessOutputMode.Event };
        foreach (var a in argv) ps.CommandLine.Add(a);
        var proc = container.CreateProcess(ps);
        proc.ErrorReceived += d => Console.Error.Write(Encoding.UTF8.GetString(d));
        proc.Start();
        return proc;
    }

    const string HELLO = "/app/hello";
    const string BIN = "/app/bin";
    const string SRV = "/app/servers";
    const string SBIN = "/app/servers/bin";

    // ---- Launch the 18 per-language TCP servers (each binds its port, waits) --
    var serverProcs = new List<Process>
    {
        ExecBackground("python3", $"{SRV}/srv.py", "7001"),
        ExecBackground("node", $"{SRV}/srv.js", "7002"),
        ExecBackground("ruby", $"{SRV}/srv.rb", "7003"),
        ExecBackground("php", $"{SRV}/srv.php", "7004"),
        ExecBackground("perl", $"{SRV}/srv.pl", "7005"),
        ExecBackground("lua5.4", $"{SRV}/srv.lua", "7006"),
        ExecBackground("Rscript", $"{SRV}/srv.R", "7007"),
        ExecBackground($"{SBIN}/srv_go", "7008"),
        ExecBackground($"{SBIN}/srv_rust", "7009"),
        ExecBackground("java", "-cp", SBIN, "Srv", "7010"),
        ExecBackground("java", "-jar", $"{SBIN}/srv_kt.jar", "7011"),
        ExecBackground($"{SBIN}/srv_dart", "7012"),
        ExecBackground("julia", $"{SRV}/srv.jl", "7013"),
        ExecBackground("node", $"{SBIN}/srv.js", "7014"),   // typescript (tsc-compiled)
        ExecBackground($"{SBIN}/srv_c", "7015"),
        ExecBackground($"{SBIN}/srv_cpp", "7016"),
        ExecBackground($"{SBIN}/srv_ocaml", "7017"),
        ExecBackground("swift", $"{SRV}/srv.swift", "7018"),
    };

    // ---- Run each language's [cli] line in TIOBE order (27 languages) --------
    (string lang, string[] argv)[] cliJobs =
    {
        ("python",     new[]{"python3", $"{HELLO}/hello.py"}),
        ("c",          new[]{$"{BIN}/hello_c"}),
        ("c++",        new[]{$"{BIN}/hello_cpp"}),
        ("java",       new[]{"java", $"{HELLO}/hello.java"}),
        ("javascript", new[]{"node", $"{HELLO}/hello.js"}),
        ("r",          new[]{"Rscript", $"{HELLO}/hello.R"}),
        ("rust",       new[]{$"{BIN}/hello_rust"}),
        ("go",         new[]{$"{BIN}/hello_go"}),
        ("php",        new[]{"php", $"{HELLO}/hello.php"}),
        ("swift",      new[]{"swift", $"{HELLO}/hello.swift"}),
        ("ada",        new[]{$"{BIN}/hello_ada"}),
        ("assembly",   new[]{$"{BIN}/hello_asm"}),
        ("fortran",    new[]{$"{BIN}/hello_fortran"}),
        ("ruby",       new[]{"ruby", $"{HELLO}/hello.rb"}),
        ("perl",       new[]{"perl", $"{HELLO}/hello.pl"}),
        ("cobol",      new[]{$"{BIN}/hello_cobol"}),
        ("prolog",     new[]{"swipl", "-q", $"{HELLO}/hello_prolog.pl"}),
        ("julia",      new[]{"julia", $"{HELLO}/hello.jl"}),
        ("kotlin",     new[]{"java", "-jar", $"{BIN}/hello_kt.jar"}),
        ("dart",       new[]{"dart", $"{HELLO}/hello.dart"}),
        ("lisp",       new[]{"sbcl", "--script", $"{HELLO}/hello.lisp"}),
        ("lua",        new[]{"lua5.4", $"{HELLO}/hello.lua"}),
        ("ocaml",      new[]{$"{BIN}/hello_ocaml"}),
        ("haskell",    new[]{$"{BIN}/hello_haskell"}),
        ("typescript", new[]{"node", $"{BIN}/hello_ts.js"}),
        ("zig",        new[]{$"{BIN}/hello_zig"}),
        ("bash",       new[]{"bash", $"{HELLO}/hello.sh"}),
    };
    foreach (var (lang, argv) in cliJobs)
    {
        string outText = await ExecCaptureAsync(lang, argv);
        Console.WriteLine($"[cli] {outText}");
    }
    Console.WriteLine("all [cli] languages OK");

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
            catch (OperationCanceledException) { break; }
            catch { try { await Task.Delay(1000, tcpCts.Token); } catch (OperationCanceledException) { break; } }
        }
        if (hello is not null) Console.WriteLine($"[tcp] {hello}");
        else { Console.Error.WriteLine($"[tcp] FAILED to handshake [{lang}] on port {port}"); exitCode = 1; }
    }

    // ---- Wait for the 18 servers to finish (each exits after its ack) --------
    // A server that never got a client would hang; bound the wait so the app
    // can't stall. Each server exits 0 after the handshake.
    foreach (var proc in serverProcs)
    {
        var exited = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        proc.Exited += code => exited.TrySetResult(code);
        // Exited may already have fired before we subscribed; State covers that.
        if (proc.State == ProcessState.Exited) exited.TrySetResult(proc.ExitCode);
        await Task.WhenAny(exited.Task, Task.Delay(10000));
        proc.Dispose();
    }
    Console.WriteLine("all servers done");
    tcpCts.Cancel();
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
