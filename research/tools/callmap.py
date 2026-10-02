# Static call map: for each named method, the E8/E9 rel32 targets inside its body that land
# exactly on another method's start (names from our dump's methods.tsv).
import sys, csv, bisect, pefile
R, names = sys.argv[1], sys.argv[2:]
pe = pefile.PE(r"C:\Program Files (x86)\Steam\steamapps\common\Last Epoch\GameAssembly.dll", fast_load=True)
by_rva, starts = {}, set()
rows = []
with open(R + "/methods.tsv", encoding="utf-8", errors="replace") as f:
    next(f)
    for line in f:
        p = line.rstrip("\n").split("\t")
        if len(p) < 8 or not p[7].startswith("0x"): continue
        rva = int(p[7], 16)
        label = f"{p[2]}.{p[3]}({p[5]})"
        by_rva.setdefault(rva, label)
        starts.add(rva)
        rows.append((p[2], p[3], p[5], rva))
ordered = sorted(starts)
def body(rva):
    i = bisect.bisect_right(ordered, rva)
    end = ordered[i] if i < len(ordered) else rva + 0x400
    end = min(end, rva + 0x2000)
    return pe.get_data(rva, end - rva), end
for want in names:
    cls, meth = want.rsplit(".", 1)
    for c, m, params, rva in rows:
        if c == cls and m == meth:
            data, end = body(rva)
            print(f"== {c}.{m}({params}) RVA 0x{rva:X} size {end - rva} first bytes {data[:24].hex(' ')}")
            seen = []
            for i in range(len(data) - 5):
                if data[i] in (0xE8, 0xE9):
                    tgt = rva + i + 5 + int.from_bytes(data[i+1:i+5], "little", signed=True)
                    if tgt in by_rva and tgt != rva:
                        seen.append(f"   +0x{i:X} {'call' if data[i]==0xE8 else 'jmp '} {by_rva[tgt]}")
            print("\n".join(seen) if seen else "   (no direct calls to named methods)")
