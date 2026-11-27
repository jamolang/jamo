// Tests the checker by itself. Built and run by tests/run_tests.py.
#include <cstdio>
#include <string>

#include "../../src/checker.hpp"
#include "../../src/parser.hpp"

static int failures = 0;

static void check(bool passed, const std::string& name) {
    if (!passed) {
        std::printf("FAIL %s\n", name.c_str());
        failures++;
    }
}

static std::string errors_of(const std::string& source) {
    Errors errors("t", source);
    const auto tokens = read_tokens(source, errors);
    Program program = parse_program(tokens, errors);
    if (!errors.any()) check_program(program, errors);
    return errors.text();
}

static void good(const std::string& source) {
    const std::string text = errors_of(source);
    check(text.empty(), "should pass:\n" + source + "\ngot: " + text);
}

static void bad(const std::string& body, const std::string& expected) {
    const std::string text = errors_of(body);
    check(!text.empty() && text.find(expected) != std::string::npos, "wrong message for:\n" + body + "\nwanted: " + expected + "\ngot: " + text);
}

static std::string in_main(const std::string& line) {
    return "func main()\n" + line + "\nend\n";
}

int main() {
    // The spec's own example
    good("func add(a: int, b: int): int\n    return a + b\nend\n\nfunc main()\n    var total = add(2, 3)\n    if total > 4\n        print(total)\n    else\n        print(0)\n    end\nend\n");
    // Rules that should pass
    good(in_main("var x: int = 1\nvar y: bool = true\nx = 2\ny = false"));
    good("func main()\nlater()\nend\nfunc later()\nend");
    good("func f(n: int): int\nif n < 2\nreturn n\nelse\nreturn f(n - 1)\nend\nend\nfunc main()\nprint(f(3))\nend");
    good("func f(): int\nreturn 1\nend\nfunc main()\nf()\nend");
    good(in_main("var x = 1\nif true\nvar x = true\nprint(x)\nend\nprint(x + 1)"));
    good(in_main("var x = 1\nif true\nvar x = x + 1\nprint(x)\nend"));
    good(in_main("print(true == false)\nprint(1 < 2 == true)\nprint(1 != 2)"));
    good(in_main("print(-9223372036854775808)\nprint(9223372036854775807)"));
    good(in_main("var d = 0\nprint(1 / d)"));
    good(in_main("return"));
    good("func f(): int\nwhile true\nend\nreturn 1\nend\nfunc main()\nend");
    good("func f(): int\nreturn 1\nprint(2)\nend\nfunc main()\nend");
    good("func f(a: int, a2: bool)\na = 2\nend\nfunc main()\nend");

    // Unknown names, with suggestions
    bad(in_main("var score = 1\nvar total = scor + 1"), "nothing has this name: scor");
    bad(in_main("var score = 1\nvar total = scor + 1"), "Did you mean score?");
    bad(in_main("x = 1"), "nothing has this name: x");
    bad(in_main("nope()"), "no function called nope");
    bad("func helper()\nend\nfunc main()\nhelpr()\nend", "Did you mean helper?");
    bad(in_main("var x = x"), "nothing has this name: x");
    bad(in_main("if true\nvar inner = 1\nend\nprint(inner)"), "nothing has this name: inner");

    // Wrong types
    bad(in_main("var x: int = true"), "declared as an int");
    bad(in_main("var x: bool = 1"), "declared as a bool");
    bad(in_main("var x = 1\nx = true"), "holds an int");
    bad(in_main("print(1 + true)"), "needs a number");
    bad(in_main("print(true < false)"), "needs a number");
    bad(in_main("print(-true)"), "minus sign");
    bad(in_main("if 1\nend"), "must be a bool");
    bad(in_main("while 1\nend"), "must be a bool");
    bad(in_main("print(1 == true)"), "same type");
    bad(in_main("print(1 < 2 < 3)"), "in a chain");
    bad("func f(a: int)\nend\nfunc main()\nf(true)\nend", "must be an int");
    bad("func f(): bool\nreturn 1\nend\nfunc main()\nend", "returns a bool");

    // Functions with no value
    bad("func f()\nend\nfunc main()\nvar x = f()\nend", "can't go in a variable");
    bad("func f()\nend\nfunc main()\nprint(f())\nend", "nothing to print");
    bad("func f()\nend\nfunc main()\nprint(f() + 1)\nend", "can't be used here");
    bad("func f()\nreturn 1\nend\nfunc main()\nend", "doesn't return a value");

    // Wrong number of inputs
    bad("func f(a: int)\nend\nfunc main()\nf()\nend", "takes 1 input");
    bad("func f()\nend\nfunc main()\nf(1)\nend", "takes 0 inputs");

    // Duplicate names
    bad("func f()\nend\nfunc f()\nend\nfunc main()\nend", "already a function");
    bad("func f(a: int, a: int)\nend\nfunc main()\nend", "already an input");
    bad("func f(a: int)\nvar a = 1\nend\nfunc main()\nend", "already a variable");
    bad(in_main("var x = 1\nvar x = 2"), "already a variable");

    // Mixed names
    bad("func f<T>(x: T): T\nreturn x\nend\nfunc main()\nprint(f)\nend", "is generic, so it can't be used as a value");
    bad(in_main("var x = 1\nx()"), "is an int, not a function");

    // Missing return
    bad("func f(): int\nend\nfunc main()\nend", "every path");
    bad("func f(): int\nif true\nreturn 1\nend\nend\nfunc main()\nend", "every path");
    bad("func f(): int\nwhile true\nreturn 1\nend\nend\nfunc main()\nend", "every path");
    bad("func f(): int\nif true\nreturn 1\nelse\nprint(1)\nend\nend\nfunc main()\nend", "every path");
    bad("func f(): int\nreturn\nend\nfunc main()\nend", "must return an int");

    // main
    bad("func helper()\nend", "no main function");
    bad("", "no main function");
    bad("func main(a: int)\nend", "can't have any inputs");
    bad("func main(): int\nreturn 1\nend", "can't return");

    // Numbers
    bad(in_main("print(9223372036854775808)"), "too large");
    bad(in_main("print(9223372036854775809)"), "too large");
    bad(in_main("print(-9223372036854775809)"), "too large");
    bad(in_main("print(1 / 0)"), "divides by zero");
    bad(in_main("print(1 / -0)"), "divides by zero");
    bad(in_main("print(1 / (0))"), "divides by zero");

    // One mistake shouldn't cause several
    {
        const std::string text = errors_of(in_main("var a = nope\nvar b = a + 1\nprint(b)"));
        check(text.find("line") == text.rfind("line"), "one unknown name gives one error: " + text);
    }
    // Several separate mistakes are all reported
    {
        const std::string text = errors_of(in_main("print(aaa)\nprint(bbb)"));
        check(text.find("aaa") != std::string::npos && text.find("bbb") != std::string::npos, "two separate errors");
    }

    std::printf("checker: %s\n", failures == 0 ? "ok" : "FAILED");
    return failures == 0 ? 0 : 1;
}
