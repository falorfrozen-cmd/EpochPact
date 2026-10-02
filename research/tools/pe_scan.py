import pefile, sys, winreg, json
G = r"C:\Program Files (x86)\Steam\steamapps\common\Last Epoch"
known = set()
with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Control\Session Manager\KnownDLLs") as k:
    i = 0
    while True:
        try:
            _, v, _ = winreg.EnumValue(k, i); known.add(str(v).lower()); i += 1
        except OSError:
            break
out = {}
for name in ["Last Epoch.exe", "UnityPlayer.dll", "GameAssembly.dll"]:
    pe = pefile.PE(G + "\\" + name, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"], pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"], pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT"]])
    imps = [e.dll.decode().lower() for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])]
    delay = [e.dll.decode().lower() for e in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", [])]
    exps = [s.name.decode() for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name] if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") else []
    def tag(d):
        if d.startswith("api-ms-") or d.startswith("ext-ms-"): return d + " (api set)"
        return d + (" (KnownDLL)" if d in known else " (searched: app dir first)")
    print(f"== {name}: {len(imps)} imports, {len(delay)} delay-load, {len(exps)} exports")
    for d in imps: print("   import", tag(d))
    for d in delay: print("   delay ", tag(d))
    out[name] = {"imports": imps, "delay": delay, "exports": exps}
il = [e for e in out["GameAssembly.dll"]["exports"] if e.startswith("il2cpp_")]
print(f"== GameAssembly il2cpp_* exports: {len(il)}")
json.dump(out, open(sys.argv[1], "w"), indent=1)
