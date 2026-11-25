"""Packs the .jamo libraries in this folder into src/libraries.cpp, so jamo.exe carries them, and into compiler/libraries_data.jamo
for the compiler written in Jamo. Run it after changing a library."""
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "src" / "libraries.cpp"
JAMO_OUT = HERE.parent / "compiler" / "libraries_data.jamo"
END = ")JAMOLIBRARY"


def library_texts():
    return [(library.name, library.read_text(encoding="utf-8").replace("\r\n", "\n")) for library in sorted(HERE.glob("*.jamo"))]


def make_source():
    parts = []
    for name, text in library_texts():
        assert END not in text, f"{name} can't hold {END}"
        parts.append(f'    if (name == "{name}") return R"JAMOLIBRARY({text}{END}";\n')
    return ("// Made by libraries/embed.py from the .jamo files in libraries. Change those, not this.\n"
            '#include "libraries.hpp"\n\n'
            "const char* builtin_library(const std::string& name) {\n" + "".join(parts) + "    return nullptr;\n}\n")


def jamo_text(text):
    out = []
    for letter in text:
        if letter == "\\":
            out.append("\\\\")
        elif letter == '"':
            out.append('\\"')
        elif letter == "\n":
            out.append("\\n")
        elif letter == "\r":
            out.append("\\r")
        elif letter == "\t":
            out.append("\\t")
        else:
            assert ord(letter) >= 32, "libraries can't hold other control characters"
            out.append(letter)
    return "".join(out)


def make_jamo_source():
    lines = ["# Made by libraries/embed.py from the .jamo files in libraries. Change those, not this.", "",
             "# The source of a built-in library, or nothing when there's none by that name", "func builtin_library(name: string): string", "    match name"]
    for name, text in library_texts():
        lines.append(f'        case "{name}"')
        lines.append(f'            return "{jamo_text(text)}"')
    lines += ["    end", '    return ""', "end", ""]
    return "\n".join(lines)


if __name__ == "__main__":
    OUT.write_text(make_source(), encoding="utf-8", newline="\n")
    JAMO_OUT.write_text(make_jamo_source(), encoding="utf-8", newline="\n")
    print(f"wrote {OUT} and {JAMO_OUT}")
