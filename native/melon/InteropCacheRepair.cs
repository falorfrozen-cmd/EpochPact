using AsmResolver.DotNet;
using System.Security.Cryptography;

namespace EpochPact.MelonBridge;

// MelonLoader 0.7.3 can generate duplicate compiler delegate-cache types for
// this Unity build. Repair only its generated managed wrappers, not game DLLs,
// loader binaries or third-party mods. Native lookup strings remain unchanged.
public static class InteropCacheRepair
{
    private static byte[] Hash(Stream stream) { using var hash = SHA256.Create(); return hash.ComputeHash(stream); }
    private static string FileHash(string path) { using var stream=File.OpenRead(path);return Convert.ToHexString(Hash(stream)).ToLowerInvariant(); }
    private static void RequireUnlinked(string path)
    {
        for(string? part=Path.GetFullPath(path);part!=null;part=Path.GetDirectoryName(part))
            if((File.Exists(part)||Directory.Exists(part))&&(File.GetAttributes(part)&FileAttributes.ReparsePoint)!=0)
                throw new IOException("Linked interop/cache backup path was left untouched.");
    }
    public static int Repair(string path, string backupDirectory)
    {
        RequireUnlinked(path);RequireUnlinked(backupDirectory);
        if (!File.Exists(path)) return 0;
        var assembly = AssemblyDefinition.FromFile(path);
        var duplicates = assembly.Modules.SelectMany(module => module.GetAllTypes())
            .GroupBy(type => type.FullName, StringComparer.Ordinal).Where(group => group.Count() > 1).ToList();
        if (duplicates.Count == 0) return 0;
        // Do not guess how to repair real Unity classes or arbitrary invalid DLLs.
        if (duplicates.Any(group => group.Any(type => type.Name?.ToString() != "<>O")))
            throw new IOException("Unexpected duplicate interop type; no cache changes were made.");
        int changed = 0;
        foreach (var group in duplicates)
            foreach (var type in group.Skip(1)) type.Name = "<>O__EpochPactInterop_" + (++changed);
        string hash=FileHash(path);
        Directory.CreateDirectory(backupDirectory);
        string backup = Path.Combine(backupDirectory, Path.GetFileName(path) + "." + hash + ".bak");
        RequireUnlinked(backup);
        if (!File.Exists(backup)) File.Copy(path, backup);
        if(FileHash(backup)!=hash)throw new IOException("Interop backup verification failed; original cache kept.");
        string pending = path + ".epochpact-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            assembly.Write(pending);
            var check = AssemblyDefinition.FromFile(pending);
            if (check.Modules.SelectMany(module => module.GetAllTypes()).GroupBy(type => type.FullName).Any(group => group.Count() > 1))
                throw new IOException("Interop cache verification failed; original kept.");
            RequireUnlinked(path);RequireUnlinked(backupDirectory);
            if (FileHash(path) != hash)
                throw new IOException("Interop cache changed during repair; original kept.");
            File.Move(pending, path, true);
            return changed;
        }
        finally { if (File.Exists(pending)) File.Delete(pending); }
    }
}
