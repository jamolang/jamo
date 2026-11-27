// Tests the parser by itself. Built and run by tests/run_tests.py.
#include <cstdio>
#include <string>

#include "../../src/parser.hpp"

static int failures = 0;

static void check(bool passed, const std::string& name) {
    if (!passed) {
        std::printf("FAIL %s\n", name.c_str());
        failures++;
    }
}

static Program parse(const std::string& source, Errors& errors) {
    const auto tokens = read_tokens(source, errors);
    return parse_program(tokens, errors);
}

static void bad(const std::string& source, const std::string& expected_words) {
    Errors errors("t", source);
    parse(source, errors);
    check(errors.any(), "should fail: " + source);
    check(errors.text().find(expected_words) != std::string::npos, "wrong message for: " + source + "\ngot: " + errors.text());
}

int main() {
    {
        const std::string source =
            "func add(a: int, b: bool): int\n"
            "    return 1 + 2 * 3\n"
            "end\n\n"
            "func main()\n"
            "    var x: int = -(1 + 2)\n"
            "    x = 4\n"
            "    add(1, true)\n"
            "    if x < 2\n"
            "        print(x)\n"
            "    else\n"
            "        while false\n"
            "        end\n"
            "    end\n"
            "    return\n"
            "end\n";
        Errors errors("t", source);
        const Program program = parse(source, errors);
        check(!errors.any(), "valid program: " + errors.text());
        check(program.functions.size() == 2, "two functions");
        const Function& add = *program.functions[0];
        check(add.inputs.size() == 2 && add.inputs[1].type == Type::Bool && add.return_type == Type::Int, "function header");
        const Expr& sum = *add.body[0]->value;
        check(sum.kind == ExprKind::Binary && sum.op == BinaryOp::Add, "+ on the outside");
        check(sum.right->op == BinaryOp::Multiply, "* before +");
        const Function& main_function = *program.functions[1];
        check(main_function.return_type == Type::Nothing, "main returns nothing");
        check(main_function.body[0]->kind == StmtKind::Var && main_function.body[0]->has_type, "var with type");
        check(main_function.body[0]->value->kind == ExprKind::Negate, "negate");
        check(main_function.body[1]->kind == StmtKind::Assign, "assign");
        check(main_function.body[2]->kind == StmtKind::Call && main_function.body[2]->value->inputs.size() == 2, "call");
        check(main_function.body[3]->has_else && main_function.body[3]->other_body[0]->kind == StmtKind::While, "if, else and while");
        check(main_function.body[4]->kind == StmtKind::Return && !main_function.body[4]->value, "return with no value");
    }
    {
        const std::string source = "func main()\n    var a = 1 - 2 - 3\n    var b = 1 < 2 == true\nend";
        Errors errors("t", source);
        const Program program = parse(source, errors);
        check(!errors.any(), "no final newline is fine");
        check(program.functions[0]->body[0]->value->left->op == BinaryOp::Subtract, "- groups from the left");
        check(program.functions[0]->body[1]->value->op == BinaryOp::Equal, "== after <");
    }
    {
        const std::string source = "func main()\r\n\tprint(1)\r\nend\r\n";
        Errors errors("t", source);
        parse(source, errors);
        check(!errors.any(), "Windows line endings");
    }
    bad("print(1)", "top level");
    bad("func main()\nfunc inner() 5\nend", "needs => and what it gives back");
    bad("func main()\n", "never gets its end");
    bad("func main()\nif true\nend", "never gets its end");
    bad("func main()\nvar x\nend", "always needs a value");
    bad("func main()\nvar if = 1\nend", "reserved word");
    bad("func main()\nvar print = 1\nend", "reserved word");
    bad("func int()\nend", "reserved word");
    bad("func main(a)\nend", "need a type");
    bad("func main(a: 5)\nend", "needs a type");
    bad("func main()\nvar x = 1 var y = 2\nend", "own line");
    bad("func main()\nvar x = 1 +\nend", "stops before");
    bad("func main()\nvar x = (1\nend", "closing )");
    bad("func main()\nx\nend", "doesn't do anything");
    bad("func main()\n1 + 2\nend", "can't start");
    bad("func main()\nif true\nelse\nelse\nend\nend", "one else");
    bad("func main()\nwhile true\nelse\nend\nend", "can't have an else");
    bad("func main()\nprint(1, 2)\nend", "one value");
    bad("func main()\nprint()\nend", "one value");
    bad("func main()\nvar x = print(1)\nend", "only be its own statement");
    bad("end", "top level");
    bad("func main()\nend end", "own line");

    // Garbage and deep nesting must never crash it
    {
        std::string deep = "func main()\nvar x = ";
        for (int i = 0; i < 100000; i++) deep += "(";
        Errors errors("t", deep);
        parse(deep, errors);
        check(errors.any(), "deep parentheses");
    }
    {
        std::string deep = "func main()\nvar x = ";
        for (int i = 0; i < 100000; i++) deep += "-";
        Errors errors("t", deep);
        parse(deep, errors);
        check(errors.any(), "many minus signs");
    }
    {
        std::string deep = "func main()\nvar x = 1";
        for (int i = 0; i < 100000; i++) deep += "+1";
        deep += "\nend\n";
        Errors errors("t", deep);
        parse(deep, errors);
        check(errors.any(), "a very long sum");
    }
    {
        std::string deep = "func main()\n";
        for (int i = 0; i < 100000; i++) deep += "if true\n";
        Errors errors("t", deep);
        parse(deep, errors);
        check(errors.any(), "deep blocks");
    }
    {
        const char* words[] = {"func", "end", "var", "if", "else", "while", "return", "print", "(", ")", ":", ",", "=", "+", "-", "*", "/", "==", "<", "1", "x", "true", "int", "\n"};
        unsigned seed = 1;
        for (int round = 0; round < 3000; round++) {
            std::string source;
            for (int i = 0; i < 40; i++) {
                seed = seed * 1103515245u + 12345u;
                source += words[(seed >> 16) % 24];
                source += ' ';
            }
            Errors errors("t", source);
            parse(source, errors);
        }
        check(true, "random tokens didn't crash");
    }
    std::printf("parser: %s\n", failures == 0 ? "ok" : "FAILED");
    return failures == 0 ? 0 : 1;
}
