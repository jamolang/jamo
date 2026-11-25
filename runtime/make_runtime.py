"""One-off helper: checks hand-written runtime instructions with GNU as, then writes them as readable C++ in src/runtime.cpp."""
import os
import tempfile
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent
ROOT = HERE.resolve().parent
BIN = Path(os.environ["LOCALAPPDATA"]) / "Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
BRANCHES = {"jmp": "jump", "je": "jump_if_equal", "jne": "jump_if_not_equal", "jns": "jump_if_not_negative", "jb": "jump_if_below", "jae": "jump_if_at_least", "ja": "jump_if_above", "jbe": "jump_if_at_most", "jp": "jump_if_unordered", "jl": "jump_if_less", "jge": "jump_if_greater_equal", "js": "jump_if_negative", "jg": "jump_if_greater", "jle": "jump_if_less_equal", "call": "call"}

routines = []
for line in (HERE / "runtime.dsl").read_text().splitlines():
    if not line.strip():
        continue
    if line.startswith("@ "):
        words = line[2:].split(" ", 1)
        routines.append({"name": words[0], "about": words[1] if len(words) > 1 else "", "lines": []})
    else:
        routines[-1]["lines"].append(line.strip())

names = {r["name"] for r in routines}
plain = [text for r in routines for text in r["lines"] if not text.startswith("!") and not text.endswith(":") and text.split()[0] not in BRANCHES]
assert not [text for text in plain if text.startswith("j") or text.startswith("call")], [text for text in plain if text.startswith("j") or text.startswith("call")]
source = Path(tempfile.gettempdir()) / "jamo_runtime_check.s"
source.write_text(".intel_syntax noprefix\n" + "".join(text + "\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n" for text in plain))
obj = Path(tempfile.gettempdir()) / "jamo_runtime_check.o"
subprocess.run([str(BIN / "as.exe"), "--64", "-o", str(obj), str(source)], check=True)
dump = subprocess.run([str(BIN / "objdump.exe"), "-d", "-M", "intel", "--insn-width=16", str(obj)], capture_output=True, text=True, check=True).stdout
chunks, current = [], []
nops = 0
for row in dump.splitlines():
    match = re.match(r"\s*[0-9a-f]+:\s+((?:[0-9a-f]{2} )+)\s*(.*)", row)
    if not match:
        continue
    data = match.group(1).split()
    if match.group(2).startswith("nop") and len(data) == 1:
        nops += 1
        if nops == 7:
            chunks.append(current)
            current, nops = [], 0
        continue
    assert nops == 0, row
    current += data
assert len(chunks) >= len(plain) and not any(chunks[len(plain):]), (len(chunks), len(plain))

out = []
got = iter(chunks)
for routine in routines:
    local = [text[:-1] for text in routine["lines"] if text.endswith(":")]
    out.append("")
    if routine["about"]:
        out.append(f"// {routine['about']}")
    out.append(f"void write_{routine['name']}(Assembler& out, const Runtime& runtime, const RuntimeText& text) {{")
    uses_text = any("text." in t for t in routine["lines"])
    uses_runtime = True
    if not uses_text:
        out.append("    (void)text;")
    for name in local:
        out.append(f"    const Label {name} = out.new_label();")
    out.append(f"    out.place(runtime.{routine['name']});")
    for text in routine["lines"]:
        if text.startswith("!"):
            out.append("    " + text[2:])
        elif text.endswith(":"):
            out.append(f"    out.place({text[:-1]});")
        elif text.split()[0] in BRANCHES:
            op, target = text.split()
            label = target if target in local else f"runtime.{target}"
            assert target in local or target in names, text
            out.append(f"    out.{BRANCHES[op]}({label});")
        else:
            data = ", ".join("0x" + b.upper() for b in next(got))
            out.append(f"    out.bytes({{{data}}});  // {text}")
    out.append("}")

fields = ", ".join(r["name"] for r in routines)
calls = "\n".join(f"    write_{r['name']}(out, runtime, text);" for r in routines)
cpp = f"""#include "runtime.hpp"

// Each runtime routine is real machine code, written out byte by byte, with its instruction beside it.
// Jamo code keeps nothing in registers across these calls, so routines can use any of them.

namespace {{
{chr(10).join(out)}

}}  // namespace

Runtime make_runtime_labels(Assembler& out) {{
    Runtime runtime;
    for (Label* label : {{{", ".join("&runtime." + r["name"] for r in routines)}}}) *label = out.new_label();
    return runtime;
}}

void write_runtime(Assembler& out, const Runtime& runtime, const RuntimeText& text) {{
{calls}
}}
"""
(ROOT / "src" / "runtime.cpp").write_text(cpp, encoding="utf-8", newline="\r\n")
header = (ROOT / "src" / "runtime.hpp")
words = header.read_text(encoding="utf-8")
words = re.sub(r"struct Runtime \{.*?\};", "struct Runtime {\n    Label " + fields + ";\n};", words, count=1, flags=re.S)
header.write_text(words, encoding="utf-8", newline="\r\n")


# --- The same routines as Jamo, for the compiler written in Jamo (compiler/runtime.jamo) ---
def data_name(name):
    return "DATA_" + re.sub(r"(?<!^)(?=[A-Z])", "_", name).upper()


FILE_NAME_OF = """    for i in 1..len(text.file_names)
        var next = out.new_label()
        out.mov_rcx_number(i)
        out.cmp_rax_rcx()
        out.jump_if_not_equal(next)
        out.lea_rax_string(text.file_names[i])
        out.hex("c3")
        out.place(next)
    end
    out.lea_rax_string(text.file_names[0])"""
INLINE = [
    (r"out\.call_windows\(DataRef::(\w+)\);", lambda m: f"out.call_windows({data_name(m.group(1))})"),
    (r"out\.load_rcx_data\(DataRef::(\w+)\);", lambda m: f"out.load_rcx_data({data_name(m.group(1))})"),
    (r"out\.load_rax_data\(DataRef::(\w+)\);", lambda m: f"out.load_rax_data({data_name(m.group(1))})"),
    (r"out\.lea_data\((R\w+), DataRef::(\w+)\);", lambda m: f"out.lea_data(REG_{m.group(1)}, {data_name(m.group(2))}, 0)"),
    (r"out\.lea_string\((R\w+), text\.(\w+)\);", lambda m: f"out.lea_string(REG_{m.group(1)}, text.{m.group(2)})"),
    (r"out\.lea_rax_string\(text\.(\w+)\);", lambda m: f"out.lea_rax_string(text.{m.group(1)})"),
    (r"out\.(\w+)\(\);", lambda m: f"out.{m.group(1)}()"),
]


def jamo_inline(text):
    for pattern, make in INLINE:
        match = re.fullmatch(pattern, text)
        if match:
            return make(match)
    raise SystemExit(f"runtime.jamo can't write this yet: {text}")


def constant(name):
    return "R_" + name.upper()


jamo = ["# Made by runtime/make_runtime.py from runtime.dsl. Change that, not this.", "#",
        "# Each runtime routine is real machine code, written out as hex, with its instruction beside it.", "",
        'use "assembler.jamo"', ""]
for i, routine in enumerate(routines):
    jamo.append(f"const {constant(routine['name'])} = {i}")
jamo.append(f"const RUNTIME_ROUTINES = {len(routines)}")
got = iter(chunks)
for routine in routines:
    local = [text[:-1] for text in routine["lines"] if text.endswith(":")]
    jamo.append("")
    if routine["about"]:
        jamo.append(f"# {routine['about']}")
    jamo.append(f"func write_rt_{routine['name']}(out: Assembler, rt: [int], text: RuntimeText)")
    for name in local:
        jamo.append(f"    var at_{name} = out.new_label()")
    jamo.append(f"    out.place(rt[{constant(routine['name'])}])")
    lines = routine["lines"]
    i = 0
    while i < len(lines):
        text = lines[i]
        if text.startswith("! for (size_t i = 1; i < text.file_names.size(); i++) {"):
            jamo.append(FILE_NAME_OF)
            while not lines[i].startswith("! out.lea_rax_string(text.file_names[0]);"):
                i += 1
        elif text.startswith("!"):
            jamo.append("    " + jamo_inline(text[2:]))
        elif text.endswith(":"):
            jamo.append(f"    out.place(at_{text[:-1]})")
        elif text.split()[0] in BRANCHES:
            op, target = text.split()
            label = f"at_{target}" if target in local else f"rt[{constant(target)}]"
            jamo.append(f"    out.{BRANCHES[op]}({label})")
        else:
            jamo.append(f'    out.hex("{"".join(next(got))}")  # {text}')
        i += 1
    jamo.append("end")
jamo += ["", "# Every routine's label, made first, in order", "func make_runtime_labels(out: Assembler): [int]", "    var rt: [int] = []",
         "    for i in 0..RUNTIME_ROUTINES", "        rt.add(out.new_label())", "    end", "    return rt", "end", "",
         "func write_runtime(out: Assembler, rt: [int], text: RuntimeText)"]
jamo += [f"    write_rt_{r['name']}(out, rt, text)" for r in routines]
jamo += ["end", ""]
(ROOT / "compiler" / "runtime.jamo").write_text("\n".join(jamo), encoding="utf-8", newline="\n")
print(f"{len(plain)} instructions, {sum(len(c) for c in chunks)} bytes")
sys.exit(0)
