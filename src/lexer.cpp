#include "lexer.hpp"

#include <charconv>

namespace {

const uint64_t LARGEST_NUMBER = 9223372036854775808ULL;  // 2^63, allowed only after a minus sign

enum class TextEnd { Closed, Brace, Unclosed };

// These can't be inside brackets, so one starting a line means a closing bracket was forgotten
bool starts_a_statement(TokenKind kind) {
    switch (kind) {
        case TokenKind::Func: case TokenKind::Struct: case TokenKind::Enum: case TokenKind::Use: case TokenKind::EndWord:
        case TokenKind::Var: case TokenKind::If: case TokenKind::Elif: case TokenKind::Else: case TokenKind::While:
        case TokenKind::For: case TokenKind::Match: case TokenKind::Case: case TokenKind::Break: case TokenKind::Continue:
        case TokenKind::Return: case TokenKind::Print:
            return true;
        default:
            return false;
    }
}

// A line that ends with one of these carries on to the next line
bool leaves_line_open(TokenKind kind) {
    switch (kind) {
        case TokenKind::Comma: case TokenKind::Assign: case TokenKind::DotDot:
        case TokenKind::PlusAssign: case TokenKind::MinusAssign: case TokenKind::StarAssign: case TokenKind::SlashAssign:
        case TokenKind::Plus: case TokenKind::Minus: case TokenKind::Star: case TokenKind::Slash: case TokenKind::Percent:
        case TokenKind::Equal: case TokenKind::NotEqual: case TokenKind::Less: case TokenKind::Greater:
        case TokenKind::LessEqual: case TokenKind::GreaterEqual:
        case TokenKind::And: case TokenKind::Or: case TokenKind::Not:
            return true;
        default:
            return false;
    }
}

bool is_letter(char letter) {
    return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || letter == '_';
}

bool is_digit(char letter) {
    return letter >= '0' && letter <= '9';
}

TokenKind kind_of_word(const std::string& word) {
    if (word == "func") return TokenKind::Func;
    if (word == "use") return TokenKind::Use;
    if (word == "extern") return TokenKind::Extern;
    if (word == "struct") return TokenKind::Struct;
    if (word == "enum") return TokenKind::Enum;
    if (word == "const") return TokenKind::Const;
    if (word == "match") return TokenKind::Match;
    if (word == "case") return TokenKind::Case;
    if (word == "end") return TokenKind::EndWord;
    if (word == "var") return TokenKind::Var;
    if (word == "if") return TokenKind::If;
    if (word == "elif") return TokenKind::Elif;
    if (word == "else") return TokenKind::Else;
    if (word == "and") return TokenKind::And;
    if (word == "or") return TokenKind::Or;
    if (word == "not") return TokenKind::Not;
    if (word == "while") return TokenKind::While;
    if (word == "for") return TokenKind::For;
    if (word == "in") return TokenKind::In;
    if (word == "break") return TokenKind::Break;
    if (word == "continue") return TokenKind::Continue;
    if (word == "return") return TokenKind::Return;
    if (word == "true") return TokenKind::True;
    if (word == "false") return TokenKind::False;
    if (word == "print") return TokenKind::Print;
    if (word == "int") return TokenKind::Int;
    if (word == "float") return TokenKind::FloatWord;
    if (word == "bool") return TokenKind::Bool;
    if (word == "string") return TokenKind::StringWord;
    if (word == "len") return TokenKind::Len;
    return TokenKind::Name;
}

std::string describe_letter(char letter) {
    const unsigned char code = static_cast<unsigned char>(letter);
    if (code >= 128) return "This character isn't allowed here. Non-English text can only go in comments and quoted text.";
    if (code < 32 || code == 127) return "This file has a control character here, which can't be part of Jamo code.";
    if (letter == '.') return "A decimal number needs a digit before its dot, like 0.5.";
    return std::string("Jamo doesn't use the character ") + letter + " here.";
}

}  // namespace

std::vector<Token> read_tokens(const std::string& source, Errors& errors) {
    std::vector<Token> tokens;
    size_t at = 0;
    int line = 1;
    size_t line_start = 0;
    std::vector<int> fstring_braces;  // An f-string's { } can hold another f-string, or a struct's { }
    int open_brackets = 0;  // Lines inside brackets carry on

    // Notepad can start a file with these three invisible bytes
    if (source.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        at = 3;
        line_start = 3;
    }

    const auto add = [&](TokenKind kind, size_t start, size_t length) {
        Token token;
        token.kind = kind;
        token.text = source.substr(start, length);
        token.spot = {line, static_cast<int>(start - line_start), static_cast<int>(length)};
        tokens.push_back(token);
    };

    // Reads quoted text up to its closing quote, or in an f-string up to its next {
    const auto read_text = [&](bool is_fstring, std::string& value) {
        while (at < source.size() && source[at] != '\n') {
            const char inside = source[at];
            const char after = at + 1 < source.size() ? source[at + 1] : '\n';
            if (inside == '"') {
                at++;
                return TextEnd::Closed;
            }
            if (is_fstring && (inside == '{' || inside == '}') && after == inside) {
                value.push_back(inside);
                at += 2;
            } else if (is_fstring && inside == '{') {
                at++;
                return TextEnd::Brace;
            } else if (is_fstring && inside == '}') {
                errors.add({line, static_cast<int>(at - line_start), 1}, "This } has no { before it.", "To show a } in an f-string, write }}.");
                at++;
            } else if (inside != '\\') {
                value.push_back(inside);
                at++;
            } else if (after == '\n') {
                at++;
            } else if (after == 'n' || after == 't' || after == 'r' || after == '"' || after == '\\') {
                value.push_back(after == 'n' ? '\n' : after == 't' ? '\t' : after == 'r' ? '\r' : after);
                at += 2;
            } else {
                errors.add({line, static_cast<int>(at - line_start), 2}, "Jamo doesn't have this escape.", "The escapes are \\n \\t \\r \\\" and \\\\.");
                at += 2;
            }
        }
        return TextEnd::Unclosed;
    };

    // Reads the next stretch of an f-string's text, after its start or after a }
    const auto read_fstring_part = [&]() {
        const size_t start = at;
        std::string value;
        const TextEnd text_end = read_text(true, value);
        if (text_end == TextEnd::Unclosed) {
            errors.add({line, static_cast<int>(start - line_start), static_cast<int>(at - start)}, "This f-string never closes.", "Put a \" at its end. For a new line inside text, write \\n.");
            fstring_braces.clear();
            return;
        }
        if (!value.empty()) {
            add(TokenKind::FStringText, start, at - start - 1);
            tokens.back().value = std::move(value);
        }
        if (text_end == TextEnd::Brace) {
            add(TokenKind::LeftBrace, at - 1, 1);
        } else {
            add(TokenKind::FStringEnd, at - 1, 1);
            fstring_braces.pop_back();
        }
    };

    while (at < source.size() && !errors.is_full()) {
        const char letter = source[at];
        const char next = at + 1 < source.size() ? source[at + 1] : '\0';

        if (letter == ' ' || letter == '\t' || letter == '\r') {
            at++;
        } else if (letter == '\n') {
            if (!fstring_braces.empty()) {
                errors.add({line, static_cast<int>(at - line_start), 1}, "This f-string has a { that never gets its }.");
                fstring_braces.clear();
            }
            if (open_brackets == 0 && (tokens.empty() || !leaves_line_open(tokens.back().kind))) add(TokenKind::Newline, at, 1);
            at++;
            line++;
            line_start = at;
        } else if (letter == '#') {
            while (at < source.size() && source[at] != '\n') at++;
        } else if (is_letter(letter) && !(letter == 'f' && next == '"')) {
            const size_t start = at;
            while (at < source.size() && (is_letter(source[at]) || is_digit(source[at]))) at++;
            const TokenKind word = kind_of_word(source.substr(start, at - start));
            if (open_brackets > 0 && starts_a_statement(word) && !tokens.empty() && tokens.back().spot.line != line) {
                open_brackets = 0;
                add(TokenKind::Newline, start, 0);
            }
            add(word, start, at - start);
        } else if (is_digit(letter)) {
            const size_t start = at;
            uint64_t number = 0;
            bool too_large = false;
            while (at < source.size() && is_digit(source[at])) {
                const uint64_t digit = static_cast<uint64_t>(source[at] - '0');
                if (number > (LARGEST_NUMBER - digit) / 10) {
                    too_large = true;
                } else {
                    number = number * 10 + digit;
                }
                at++;
            }
            if (at + 1 < source.size() && source[at] == '.' && is_digit(source[at + 1])) {
                at++;
                while (at < source.size() && is_digit(source[at])) at++;
                const Spot spot = {line, static_cast<int>(start - line_start), static_cast<int>(at - start)};
                double real = 0;
                const auto result = std::from_chars(source.data() + start, source.data() + at, real);
                if (result.ec != std::errc{}) {
                    errors.add(spot, "This decimal number is too large or too small for a float.");
                } else {
                    add(TokenKind::Float, start, at - start);
                    tokens.back().real = real;
                }
                continue;
            }
            if (at + 1 >= source.size() ? at < source.size() && source[at] == '.' : source[at] == '.' && source[at + 1] != '.') {
                errors.add({line, static_cast<int>(start - line_start), static_cast<int>(at - start + 1)}, "A decimal number needs a digit after its dot.", "Like this: " + source.substr(start, at - start) + ".0");
                at++;
                continue;
            }
            const Spot spot = {line, static_cast<int>(start - line_start), static_cast<int>(at - start)};
            if (at < source.size() && is_letter(source[at])) {
                errors.add(spot, "A name can't start with a number.");
                while (at < source.size() && (is_letter(source[at]) || is_digit(source[at]))) at++;
            } else if (too_large) {
                errors.add(spot, "This number is too large for 64 bits.", "The largest int is 9223372036854775807.");
            } else {
                add(TokenKind::Number, start, at - start);
                tokens.back().number = number;
            }
        } else if (letter == '"') {
            const size_t start = at;
            std::string value;
            at++;
            if (read_text(false, value) == TextEnd::Closed) {
                add(TokenKind::String, start, at - start);
                tokens.back().value = std::move(value);
            } else {
                errors.add({line, static_cast<int>(start - line_start), static_cast<int>(at - start)}, "This text never closes.", "Put a \" at its end. For a new line inside text, write \\n.");
            }
        } else if (letter == 'f' && next == '"') {
            add(TokenKind::FStringStart, at, 2);
            at += 2;
            fstring_braces.push_back(0);
            read_fstring_part();
        } else if (letter == '}' && !fstring_braces.empty() && fstring_braces.back() == 0) {
            add(TokenKind::RightBrace, at, 1);
            at++;
            read_fstring_part();
        } else if (letter == '.' && next == '.') {
            add(TokenKind::DotDot, at, 2);
            at += 2;
        } else if (next == '=' && (letter == '+' || letter == '-' || letter == '*' || letter == '/')) {
            add(letter == '+' ? TokenKind::PlusAssign : letter == '-' ? TokenKind::MinusAssign : letter == '*' ? TokenKind::StarAssign : TokenKind::SlashAssign, at, 2);
            at += 2;
        } else if (letter == '=' && next == '=') {
            add(TokenKind::Equal, at, 2);
            at += 2;
        } else if (letter == '!' && next == '=') {
            add(TokenKind::NotEqual, at, 2);
            at += 2;
        } else if (letter == '<' && next == '=') {
            add(TokenKind::LessEqual, at, 2);
            at += 2;
        } else if (letter == '>' && next == '=') {
            add(TokenKind::GreaterEqual, at, 2);
            at += 2;
        } else {
            TokenKind kind = TokenKind::End;
            switch (letter) {
                case '(': kind = TokenKind::LeftParen; break;
                case ')': kind = TokenKind::RightParen; break;
                case '[': kind = TokenKind::LeftBracket; break;
                case '{': kind = TokenKind::LeftBrace; break;
                case '}': kind = TokenKind::RightBrace; break;
                case ']': kind = TokenKind::RightBracket; break;
                case '.': kind = is_digit(next) ? TokenKind::End : TokenKind::Dot; break;
                case ':': kind = TokenKind::Colon; break;
                case ',': kind = TokenKind::Comma; break;
                case '=': kind = TokenKind::Assign; break;
                case '+': kind = TokenKind::Plus; break;
                case '-': kind = TokenKind::Minus; break;
                case '*': kind = TokenKind::Star; break;
                case '/': kind = TokenKind::Slash; break;
                case '%': kind = TokenKind::Percent; break;
                case '<': kind = TokenKind::Less; break;
                case '>': kind = TokenKind::Greater; break;
                default: break;
            }
            if (kind == TokenKind::LeftBrace && !fstring_braces.empty()) fstring_braces.back()++;
            if (kind == TokenKind::RightBrace && !fstring_braces.empty()) fstring_braces.back()--;
            if (fstring_braces.empty() && (kind == TokenKind::LeftParen || kind == TokenKind::LeftBracket || kind == TokenKind::LeftBrace)) open_brackets++;
            if (fstring_braces.empty() && open_brackets > 0 && (kind == TokenKind::RightParen || kind == TokenKind::RightBracket || kind == TokenKind::RightBrace)) open_brackets--;
            if (kind == TokenKind::End) {
                errors.add({line, static_cast<int>(at - line_start), 1}, describe_letter(letter));
            } else {
                add(kind, at, 1);
            }
            at++;
        }
    }

    if (!fstring_braces.empty()) errors.add({line, static_cast<int>(at - line_start), 1}, "This f-string has a { that never gets its }.");
    add(TokenKind::Newline, source.size(), 0);
    add(TokenKind::End, source.size(), 0);
    for (Token& token : tokens) token.spot.file = errors.current_file();
    return tokens;
}
