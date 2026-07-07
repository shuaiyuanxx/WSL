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
