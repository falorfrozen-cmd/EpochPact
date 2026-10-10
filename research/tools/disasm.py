# Disassemble a method from GameAssembly.dll by RVA, using methods.tsv for names.
# Usage: py -3 research/tools/disasm.py <rva-hex> <bytes> [<rva-hex> <bytes> ...]
import sys
import pefile
import capstone

GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Last Epoch\GameAssembly.dll"
pe = pefile.PE(GAME, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

sections = []
for s in pe.sections:
    sections.append((s.VirtualAddress, min(s.Misc_VirtualSize, s.SizeOfRawData), s.get_data()))


def read(rva, n):
    for va, size, data in sections:
        if va <= rva < va + size:
            off = rva - va
            return data[off:off + n]
    return b""


args = sys.argv[1:]
for i in range(0, len(args), 2):
    rva = int(args[i], 16)
    n = int(args[i + 1])
    print(f"== RVA 0x{rva:X} ({n} bytes)")
    for insn in md.disasm(read(rva, n), image_base + rva):
        print(f"  {insn.address - image_base:08x}: {insn.mnemonic:<10} {insn.op_str}")
