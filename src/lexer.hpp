// Lexer: splits Jamo source text into tokens, the words and symbols of the language.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "errors.hpp"

enum class TokenKind {
    End,
    Newline,
    Name,
    Number,
    Float,
    String,
    FStringStart, FStringText, FStringEnd,
    // Keywords
    Func, Extern, Use, Struct, Enum, Const, Match, Case, EndWord, Var, If, Elif, Else, While, For, In, Break, Continue, And, Or, Not, Return, True, False, Print, Int, FloatWord, Bool, StringWord, Len,
    // Symbols
    LeftParen, RightParen, LeftBracket, RightBracket, LeftBrace, RightBrace, Colon, Comma, Assign, Dot, DotDot,
    PlusAssign, MinusAssign, StarAssign, SlashAssign,
    Plus, Minus, Star, Slash, Percent,
    Equal, NotEqual, Less, Greater, LessEqual, GreaterEqual,
    // Keywords added after the rest, so the numbers above stay the same
    Try, Catch,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    std::string value;    // String, FStringText: the text it stands for, with escapes worked out
    double real = 0;      // Float
    uint64_t number = 0;  // Up to 2^63, since -9223372036854775808 starts out as a positive number
    Spot spot;
};

std::vector<Token> read_tokens(const std::string& source, Errors& errors);
