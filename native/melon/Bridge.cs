using System.ComponentModel;
using System.Runtime.InteropServices;
using MelonLoader;

[assembly: MelonInfo(typeof(EpochPact.MelonBridge.EpochPactBridge), "EpochPact Native Bridge", "0.1.2-dev.6", "Falor")]
[assembly: MelonGame("Eleventh Hour Games", "Last Epoch")]

namespace EpochPact.MelonBridge;

// Optional bootstrap only. Gameplay, offline gating and IPC remain in the same
// native core used by the standalone loader. No game assemblies / Harmony patches.
public sealed class EpochPactBridge : MelonMod
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryExW(string filename, IntPtr file, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    private delegate void StartCore();
    private static IntPtr core;

    public override void OnEarlyInitializeMelon()
    {
        try
        {
            string root = Path.GetDirectoryName(Environment.ProcessPath ?? "") ?? "";
            string cache = Path.Combine(root, "MelonLoader", "Il2CppAssemblies", "UnityEngine.CoreModule.dll");
            if (File.Exists(Path.Combine(root, "EpochPact", "disabled"))) return;
            using var game = File.OpenRead(Path.Combine(root, "GameAssembly.dll"));
            using var hash = System.Security.Cryptography.SHA256.Create();
            if (Convert.ToHexString(hash.ComputeHash(game)).ToLowerInvariant() !=
                "d5f9fb458f0e9878e697a92b590a586a52fd77e645f8bf87bb4f2b74c6c56567") return;
            int repaired = InteropCacheRepair.Repair(cache, Path.Combine(root, "EpochPact", "backups", "melon-interop"));
            if (repaired != 0) LoggerInstance.Msg($"Repaired {repaired} duplicate generated Unity delegate-cache types; original cache backed up.");
        }
        catch (Exception error) { LoggerInstance.Error("MelonLoader generated-cache repair did not run: " + error.Message); }
    }

    public override void OnLateInitializeMelon()
    {
        if (!OperatingSystem.IsWindows() || IntPtr.Size != 8 || core != IntPtr.Zero) return;
        try
        {
            string exe = Environment.ProcessPath ?? throw new InvalidOperationException("Game executable unavailable.");
            if (!Path.GetFileName(exe).Equals("Last Epoch.exe", StringComparison.OrdinalIgnoreCase)) return;
            string folder = Path.Combine(Path.GetDirectoryName(exe)!, "EpochPact");
            if (File.Exists(Path.Combine(folder, "disabled"))) return;
            core = LoadLibraryExW(Path.Combine(folder, "EpochPact.Core.dll"), IntPtr.Zero, 0x100 | 0x800);
            if (core == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            IntPtr start = GetProcAddress(core, "EpochPact_Start");
            if (start == IntPtr.Zero) throw new InvalidOperationException("EpochPact_Start export unavailable.");
            Marshal.GetDelegateForFunctionPointer<StartCore>(start)();
            LoggerInstance.Msg("EpochPact native core started. Existing MelonLoader mods were preserved.");
        }
        catch (Exception error) { LoggerInstance.Error("EpochPact did not start: " + error.Message); }
        // Never unload: native hooks must remain valid until IL2CPP shuts down.
    }
}
