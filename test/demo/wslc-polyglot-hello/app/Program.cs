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
            new(9099, 9099, PortProtocol.TCP),              // container 9099 -> Windows localhost:9099
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

    // Background: connect to the container's TCP aggregator (mapped to
    // localhost:9099) and print each received line. Best-effort — the
    // container-side hard-fail is the correctness gate; this only displays.
    // The Bridged port mapping can take ~55s to become stably reachable on
    // Windows, so retry-connect for a generous window (~150s) rather than a
    // small fixed attempt count, until cancelled.
    using var tcpCts = new CancellationTokenSource();
    var tcpReader = Task.Run(async () =>
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(150);
        int received = 0;
        while (DateTime.UtcNow < deadline && !tcpCts.IsCancellationRequested && received == 0)
        {
            try
            {
                using var client = new System.Net.Sockets.TcpClient();
                await client.ConnectAsync("127.0.0.1", 9099, tcpCts.Token);
                using var reader = new StreamReader(client.GetStream());
                string? line;
                while ((line = await reader.ReadLineAsync(tcpCts.Token)) is not null)
                {
                    Console.WriteLine($"[tcp] {line}");
                    received++;
                }
                // Stream closed. The WSLC port-proxy accepts (and immediately
                // closes) connections before the in-container aggregator is
                // actually listening on 9099. So an empty close is NOT "done" —
                // only stop once we've received at least one line. Otherwise
                // wait and reconnect.
                if (received > 0) return;
                try { await Task.Delay(1000, tcpCts.Token); } catch (OperationCanceledException) { return; }
            }
            catch (OperationCanceledException) { return; }
            catch
            {
                try { await Task.Delay(1000, tcpCts.Token); } catch (OperationCanceledException) { return; }
            }
        }
    });

    exitCode = await done.Task;                // run-all.sh exit code (non-zero = fail-fast)
    Console.WriteLine($"[done] run-all.sh exited code={exitCode}");

    // give the reader a moment to drain any final [tcp] lines, then stop it
    await Task.WhenAny(tcpReader, Task.Delay(5000));
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
