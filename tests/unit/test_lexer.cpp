// Tests the lexer by itself. Built and run by tests/run_tests.py.
#include <cstdio>
#include <string>

#include "../../src/lexer.hpp"

static int failures = 0;

static void check(bool passed, const char* name) {
    if (!passed) {
        std::printf("FAIL %s\n", name);
        failures++;
    }
}

static std::vector<Token> lex(const std::string& source, Errors& errors) {
    return read_tokens(source, errors);
}

int main() {
    {
        const std::string source = "func add(a: int): bool\n# note\n  return a <= 2 != true\nend";
        Errors errors("t", source);
        const auto tokens = lex(source, errors);
        check(!errors.any(), "clean file has no errors");
        check(tokens[0].kind == TokenKind::Func, "func keyword");
        check(tokens[1].kind == TokenKind::Name && tokens[1].text == "add", "name");
        check(tokens[5].kind == TokenKind::Int, "int keyword");
        check(tokens[7].kind == TokenKind::Colon, "colon");
        check(tokens[8].kind == TokenKind::Bool, "bool keyword");
        check(tokens[9].kind == TokenKind::Newline, "newline");
        check(tokens[10].kind == TokenKind::Newline, "comment skipped, keeping its newline");
        check(tokens[11].kind == TokenKind::Return && tokens[11].spot.line == 3 && tokens[11].spot.column == 2, "line and column");
        check(tokens[13].kind == TokenKind::LessEqual, "<=");
        check(tokens[14].kind == TokenKind::Number && tokens[14].number == 2, "number");
        check(tokens[15].kind == TokenKind::NotEqual, "!=");
        check(tokens[16].kind == TokenKind::True, "true");
        check(tokens.back().kind == TokenKind::End, "ends with End");
    }
    {
        const std::string source = "var x = 1\r\n\tvar y = 2\r\n";
        Errors errors("t", source);
        const auto tokens = lex(source, errors);
        check(!errors.any(), "Windows line endings and tabs");
        check(tokens[5].spot.line == 2 && tokens[5].spot.column == 1, "tab counts as one column");
    }
    {
        const std::string source = "\xEF\xBB\xBFvar";
        Errors errors("t", source);
        const auto tokens = lex(source, errors);
        check(!errors.any() && tokens[0].kind == TokenKind::Var && tokens[0].spot.column == 0, "Notepad's start bytes skipped");
    }
    {
        const std::string source = "9223372036854775808";
        Errors errors("t", source);
        const auto tokens = lex(source, errors);
        check(!errors.any() && tokens[0].number == 9223372036854775808ULL, "2^63 reads, for the minus sign");
    }
    {
        const char* bad[] = {"9223372036854775809", "99999999999999999999999", "@", "!", "12abc", "\"never closes", "\"bad \\q\"","\xC3\xA9", "$"};
        for (const char* source : bad) {
            Errors errors("t", source);
            lex(source, errors);
            check(errors.any(), source);
        }
    }
    {
        const std::string source = "x # caf\xC3\xA9 is fine here\n";
        Errors errors("t", source);
        lex(source, errors);
        check(!errors.any(), "non-English text in a comment");
    }
    {
        const std::string source = "var total = @\n";
        Errors errors("game.jamo", source);
        lex(source, errors);
        const std::string text = errors.text();
        check(text.find("game.jamo line 1:") == 0, "error starts with file and line");
        check(text.find("    var total = @\n") != std::string::npos, "error shows the line");
        check(text.find("\n                ^\n") != std::string::npos, "marker sits under the mistake");
    }
    {
        // Garbage must never crash it, even with every possible byte
        std::string garbage;
        for (int round = 0; round < 50; round++) {
            for (int code = 0; code < 256; code++) garbage.push_back(static_cast<char>(code));
        }
        Errors errors("t", garbage);
        lex(garbage, errors);
        check(errors.any(), "garbage gives errors");
        check(errors.messages.size() <= 20, "errors are capped");
    }
    std::printf("lexer: %s\n", failures == 0 ? "ok" : "FAILED");
    return failures == 0 ? 0 : 1;
}
