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

    // The sequence is finite: complete when run-all.sh exits.
    var done = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
    container.InitProcess.OutputReceived += data => Console.Write(Encoding.UTF8.GetString(data));
    container.InitProcess.ErrorReceived += data => Console.Error.Write(Encoding.UTF8.GetString(data));
    container.InitProcess.Exited += code => done.TrySetResult(code);

    // Ctrl+C as a fallback so the user can abort a stuck run into cleanup.
    Console.CancelKeyPress += (_, e) => { e.Cancel = true; done.TrySetResult(-1); };

    container.Start();
    Console.WriteLine("polyglot container started; streaming output...");

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

    exitCode = await done.Task;                // run-all.sh exit code (non-zero = fail-fast)
    Console.WriteLine($"[done] run-all.sh exited code={exitCode}");

    // give the handshake loop a moment to finish any final [tcp] line, then stop it
    await Task.WhenAny(tcpWork, Task.Delay(5000));
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
