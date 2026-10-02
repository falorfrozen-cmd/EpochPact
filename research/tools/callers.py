# Static callers: every E8 rel32 in GameAssembly's code whose target is one of the named
# methods, mapped to the method that contains the call (names from methods.tsv).
import sys, bisect, pefile
R, wants = sys.argv[1], sys.argv[2:]
pe = pefile.PE(r"C:\Program Files (x86)\Steam\steamapps\common\Last Epoch\GameAssembly.dll", fast_load=True)
starts, names, targets = [], {}, {}
with open(R + "/methods.tsv", encoding="utf-8", errors="replace") as f:
    next(f)
    for line in f:
        p = line.rstrip("\n").split("\t")
        if len(p) < 8 or not p[7].startswith("0x"): continue
        rva = int(p[7], 16)
        label = f"{(p[1] + '.') if p[1] else ''}{p[2]}.{p[3]}({p[5]})"
        names.setdefault(rva, label)
        for w in wants:
            if f"{p[2]}.{p[3]}" == w: targets.setdefault(rva, label)
starts = sorted(names)
hits = {t: [] for t in targets}
for sec in [s for s in pe.sections if s.Characteristics & 0x20000000]:
    data = sec.get_data()
    base = sec.VirtualAddress
    for i in range(len(data) - 5):
        if data[i] != 0xE8: continue
        tgt = base + i + 5 + int.from_bytes(data[i+1:i+5], "little", signed=True)
        if tgt in hits:
            rva = base + i
            j = bisect.bisect_right(starts, rva) - 1
            hits[tgt].append(names[starts[j]] if j >= 0 else hex(rva))
for t, callers in hits.items():
    print(f"== {targets[t]} (RVA 0x{t:X}): {len(callers)} call sites")
    for c in sorted(set(callers)): print("   " + c)
