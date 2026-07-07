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
