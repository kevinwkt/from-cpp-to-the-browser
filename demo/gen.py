#!/usr/bin/env python3
"""gen.py — compile each specimen in specimens/ through the whole pipeline and
write pipeline.json for the explorer in index.html.

For every specimen:   C++ source  ->  LLVM IR at -O0  ->  LLVM IR at -O2
                      ->  .wasm (clang -O2 + wasm-ld)  ->  WASM text (wasm2wat)
                      ->  the bytes, split into sections
Plus the size and instruction count of every fractal-*.wasm build.
Nothing here is hand-written: it is all real compiler output, lightly trimmed
(comments, metadata and attribute annotations removed) so it fits on a screen.
"""
import glob, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLANG = os.environ.get("CLANG", "clang")
CXX = CLANG[: -len("clang")] + "clang++" if CLANG.endswith("clang") else "clang++"
COMMON = ["--target=wasm32", "-std=c++17", "-nostdlib", "-fno-exceptions", "-fno-rtti"]

SECTION_NAMES = {0: "custom", 1: "type", 2: "import", 3: "function", 4: "table", 5: "memory",
                 6: "global", 7: "export", 8: "start", 9: "elem", 10: "code", 11: "data",
                 12: "datacount", 13: "tag"}


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout


def trim_ir(text):
    out = []
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith((";", "!", "attributes", "target ", "source_filename", "declare", "$")):
            continue
        line = re.sub(r", ![\w.]+ !\d+", "", line)           # !tbaa !5, !llvm.loop !7 …
        line = re.sub(r"\s#\d+(?=\s|\{|$)", "", line)          # attribute groups
        line = re.sub(r" dereferenceable\(\d+\)", "", line)
        line = re.sub(r"(?<=ptr) align \d+", "", line)         # parameter alignment hints
        line = re.sub(r"\btail call\b", "call", line)
        for tok in (" noundef", "hidden ", " local_unnamed_addr", "dso_local ", " nocapture", " readonly",
                    " nonnull", "zeroext ", "signext ", " linkonce_odr", " unnamed_addr", ", comdat",
                    " comdat", "mustprogress "):
            line = line.replace(tok, "")
        out.append(line)
    return "\n".join(out)


def count_ir_instructions(ir):
    n = 0
    for line in ir.splitlines():
        if line.startswith("  ") and not line.strip().endswith(":"):
            n += 1
    return n


def trim_wat(text):
    """Drop custom sections and give exported functions their export names
    ($add instead of (;0;)), since wasm-ld emits no name section without DWARF."""
    names = dict((int(i), n) for n, i in re.findall(r'\(export "(\w+)" \(func (\d+)\)\)', text))
    out = []
    for l in text.splitlines():
        if l.strip().startswith("(@custom"):
            continue
        m = re.match(r"(\s*\(func) \(;(\d+);\)(.*)", l)
        if m and int(m.group(2)) in names:
            l = f"{m.group(1)} ${names[int(m.group(2))]}{m.group(3)}"
        out.append(l)
    return "\n".join(out)


def count_wat_instructions(wat):
    n, in_func = 0, False
    for line in wat.splitlines():
        s = line.strip()
        if s.startswith("(func"):
            in_func = True
            continue
        if in_func:
            if s.startswith("(") and not s.startswith("(local"):
                in_func = False
                continue
            if s and not s.startswith("(local") and not s.startswith(")"):
                n += 1
    return n


def leb128(buf, i):
    result, shift = 0, 0
    while True:
        b = buf[i]; i += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, i
        shift += 7


def sections(buf):
    assert buf[:4] == b"\0asm", "not a wasm binary"
    out = [{"name": "magic + version", "id": None, "offset": 0, "length": 8, "hex": buf[:8].hex()}]
    i = 8
    while i < len(buf):
        sid = buf[i]
        size, j = leb128(buf, i + 1)
        name = SECTION_NAMES.get(sid, f"id {sid}")
        if sid == 0:
            nlen, k = leb128(buf, j)
            name = "custom: " + buf[k:k + nlen].decode("utf8", "replace")
        end = j + size
        out.append({"name": name, "id": sid, "offset": i, "length": end - i, "hex": buf[i:end].hex()})
        i = end
    return out


def specimen(path):
    name = os.path.basename(path)[:-4]
    src = open(path).read()
    ir0 = trim_ir(run([CXX, *COMMON, "-O0", "-S", "-emit-llvm", "-o", "-", path]))
    ir2 = trim_ir(run([CXX, *COMMON, "-O2", "-S", "-emit-llvm", "-o", "-", path]))
    wasm = os.path.join(HERE, f"specimens/{name}.wasm")
    exports = re.findall(r'extern "C"\s+[\w:<>*& ]+?\b(\w+)\s*\(', src)
    run([CXX, *COMMON, "-O2", "-g", "-Wl,--no-entry", "-Wl,--strip-debug",
         *[f"-Wl,--export={e}" for e in exports], "-o", wasm, path])
    wat = trim_wat(run(["wasm2wat", wasm]))
    buf = open(wasm, "rb").read()
    title = re.search(r"^//\s*TITLE:\s*(.+)$", src, re.M)
    blurb = re.search(r"^//\s*BLURB:\s*(.+)$", src, re.M)
    return {
        "id": name,
        "title": title.group(1).strip() if title else name,
        "blurb": blurb.group(1).strip() if blurb else "",
        "source": "\n".join(l for l in src.splitlines() if not l.startswith(("// TITLE:", "// BLURB:"))).strip(),
        "ir_O0": ir0, "ir_O2": ir2, "wat": wat,
        "counts": {"ir_O0": count_ir_instructions(ir0), "ir_O2": count_ir_instructions(ir2),
                   "wat": count_wat_instructions(wat), "bytes": len(buf)},
        "sections": sections(buf),
    }


def builds():
    out = []
    for name, flags in [("O0", "-O0"), ("O1", "-O1"), ("O2", "-O2"), ("O3", "-O3"),
                        ("O3-simd", "-O3 -msimd128")]:
        f = os.path.join(HERE, f"fractal-{name}.wasm")
        if not os.path.exists(f):
            continue
        wat = run(["wasm2wat", f])
        out.append({"id": name, "flags": flags, "file": f"fractal-{name}.wasm",
                    "bytes": os.path.getsize(f), "instructions": count_wat_instructions(wat)})
    return out


def main():
    data = {
        "compiler": run([CXX, "--version"]).splitlines()[0],
        "specimens": [specimen(p) for p in sorted(glob.glob(os.path.join(HERE, "specimens", "*.cpp")))],
        "builds": builds(),
    }
    with open(os.path.join(HERE, "pipeline.json"), "w") as f:
        json.dump(data, f, indent=1)
    for s in data["specimens"]:
        c = s["counts"]
        print(f"  {s['id']:<14} IR -O0: {c['ir_O0']:>3}  IR -O2: {c['ir_O2']:>3}  wasm instrs: {c['wat']:>3}  bytes: {c['bytes']}")
    for b in data["builds"]:
        print(f"  {b['file']:<22} {b['bytes']:>5} bytes  {b['instructions']:>4} instructions")


if __name__ == "__main__":
    main()
