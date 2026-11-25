// Tests the code generator by itself, by its exact bytes. Real runs are tested in run_tests.py.
#include <cstdio>
#include <string>

#include "../../src/checker.hpp"
#include "../../src/codegen.hpp"
#include "../../src/parser.hpp"

static int failures = 0;

static void check(bool passed, const std::string& name) {
    if (!passed) {
        std::printf("FAIL %s\n", name.c_str());
        failures++;
    }
}

static MachineCode compile(const std::string& source) {
    Errors errors("t", source);
    const auto tokens = read_tokens(source, errors);
    Program program = parse_program(tokens, errors);
    if (!errors.any()) check_program(program, errors);
    check(!errors.any(), "compiles: " + errors.text());
    return generate_code(program, "t.jamo");
}

static bool contains(const MachineCode& code, const std::vector<uint8_t>& wanted) {
    if (code.bytes.size() < wanted.size()) return false;
    for (size_t at = 0; at + wanted.size() <= code.bytes.size(); at++) {
        bool same = true;
        for (size_t i = 0; i < wanted.size() && same; i++) same = code.bytes[at + i] == wanted[i];
        if (same) return true;
    }
    return false;
}

int main() {
    {
        const MachineCode code = compile("func main()\nprint(7)\nend\n");
        check(contains(code, {0x31, 0xC9, 0xE9}), "startup sets exit code 0, then exits");
        check(contains(code, {0x48, 0xC7, 0xC0, 7, 0, 0, 0}), "mov rax, 7");
        for (const DataFixup& fixup : code.fixups) {
            check(fixup.offset + 4 <= code.bytes.size(), "fixup is inside the code");
        }
    }
    {
        const MachineCode code = compile("func main()\nprint(5000000000)\nend\n");
        check(contains(code, {0x48, 0xB8, 0x00, 0xF2, 0x05, 0x2A, 0x01, 0, 0, 0}), "large numbers use a 64-bit load");
    }
    {
        const MachineCode code = compile("func main()\nvar a = 1\nvar b = 2\nprint(a + b)\nend\n");
        check(contains(code, {0x48, 0x89, 0x85, 0xF8, 0xFF, 0xFF, 0xFF}), "first local at rbp-8");
        check(contains(code, {0x48, 0x89, 0x85, 0xF0, 0xFF, 0xFF, 0xFF}), "second local at rbp-16");
        check(contains(code, {0x48, 0x01, 0xC8}), "add");
    }
    {
        // f divides by an input, which can stop with an error, so it's really called rather than written in place
        const MachineCode code = compile("func f(a: int, b: int): int\nreturn a / b\nend\nfunc main()\nf(1, 2)\nend\n");
        check(contains(code, {0x48, 0x8B, 0x85, 24, 0, 0, 0}), "first input sits above the second");
        check(contains(code, {0x48, 0x83, 0xC4, 16}), "caller removes the inputs");  // add rsp, 16, in its short form
    }
    {
        const MachineCode code = compile("func main()\nvar d = 1\nprint(1 / d)\nend\n");
        check(contains(code, {0x48, 0x83, 0xF9, 0xFF}), "division checks for -1");
        check(contains(code, {0x48, 0x99, 0x48, 0xF7, 0xF9}), "cqo, idiv");
    }
    {
        // A big frame must touch each page on the way down
        std::string source = "func main()\n";
        for (int i = 0; i < 2000; i++) source += "var v" + std::to_string(i) + " = 0\n";
        source += "end\n";
        const MachineCode code = compile(source);
        check(contains(code, {0x48, 0x8B, 0x84, 0x24, 0x00, 0xF0, 0xFF, 0xFF}), "touches one page down");
        check(contains(code, {0x48, 0x8B, 0x84, 0x24, 0x00, 0xD0, 0xFF, 0xFF}), "touches three pages down");
    }
    {
        const std::string source = "func main()\nprint(1)\nend\n";
        check(compile(source).bytes == compile(source).bytes, "same source gives the same bytes");
    }
    std::printf("codegen: %s\n", failures == 0 ? "ok" : "FAILED");
    return failures == 0 ? 0 : 1;
}
