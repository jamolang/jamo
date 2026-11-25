#include "parser.hpp"

#include <algorithm>

namespace {

const int DEEPEST_NESTING = 200;      // Stops garbage like ((((((... from using up the compiler's own stack
const int TALLEST_EXPRESSION = 1000;  // The same, for a+a+a+a...

// A marker can only run along one line, so a mistake across several lines marks where it starts
int length_to(Spot start, Spot end) {
    return start.line == end.line ? end.column + end.length - start.column : start.length;
}

struct StopParsing {};

class Parser {
public:
    Parser(const std::vector<Token>& all_tokens, Errors& all_errors, Program& the_program) : tokens(all_tokens), errors(all_errors), program(the_program) {}

    void parse() {
        skip_newlines();
        while (peek().kind != TokenKind::End) {
            try {
                parse_top_level();
            } catch (const StopParsing&) {
                if (next_is(TokenKind::End) || errors.is_full()) throw;
                nesting = 0;
                skip_to_next_top_level();
            }
            skip_newlines();
        }
    }

private:
    void parse_top_level() {
        {
            if (next_is(TokenKind::Use)) {
                parse_use();
            } else if (next_is(TokenKind::Struct)) {
                parse_struct();
            } else if (next_is(TokenKind::Enum)) {
                parse_enum();
            } else if (next_is(TokenKind::Const)) {
                parse_const();
            } else if (next_is(TokenKind::Extern)) {
                program.functions.push_back(parse_extern());
            } else if (next_is(TokenKind::Func)) {
                program.functions.push_back(parse_function());
            } else if (next_is(TokenKind::Var)) {
                program.globals.push_back(parse_var());
            } else {
                fail("Only functions, structs, enums, constants, variables, extern and use lines can go at the top level of a file.", "Put this code inside a function, like func main().");
            }
        }
    }

    void skip_to_next_top_level() {
        // A mistake in a function's first line leaves the rest of it, so this skips past all of it
        take();
        while (!next_is(TokenKind::End) && !next_is(TokenKind::Func) && !next_is(TokenKind::Struct) && !next_is(TokenKind::Enum) && !next_is(TokenKind::Const) && !next_is(TokenKind::Use)) take();
    }

    void skip_rest_of_line() {
        if (next_is(TokenKind::EndWord) || next_is(TokenKind::Else) || next_is(TokenKind::Elif) || next_is(TokenKind::Case)) return;
        while (!next_is(TokenKind::Newline) && !next_is(TokenKind::End)) take();
        skip_newlines();
    }

    // After a mistake on a line that opens a block, its lines still get read, and its end doesn't close the wrong thing
    void recover_from_statement(TokenKind first_word, Spot start) {
        skip_rest_of_line();
        const bool opens_block = first_word == TokenKind::If || first_word == TokenKind::While || first_word == TokenKind::For || first_word == TokenKind::Match || first_word == TokenKind::Func;
        if (!opens_block) return;
        while (true) {
            parse_block(start, "block");
            if (next_is(TokenKind::EndWord)) {
                take();
                skip_rest_of_line();
                return;
            }
            take();
            skip_rest_of_line();
        }
    }

    const Token& peek() const { return tokens[at]; }

    const Token& take() {
        const Token& token = tokens[at];
        if (at + 1 < tokens.size()) at++;
        return token;
    }

    bool next_is(TokenKind kind) const { return peek().kind == kind; }

    void skip_newlines() {
        while (next_is(TokenKind::Newline)) take();
    }

    [[noreturn]] void fail(const std::string& words, const std::string& hint = "") {
        fail_at(peek().spot, words, hint);
    }

    [[noreturn]] void fail_at(Spot spot, const std::string& words, const std::string& hint = "") {
        errors.add(spot, words, hint);
        throw StopParsing{};
    }

    const Token& expect(TokenKind kind, const std::string& words, const std::string& hint = "") {
        if (!next_is(kind)) fail(words, hint);
        return take();
    }

    void expect_line_end() {
        // A line ending with > carries on to the next one, so after types like Box<T> the line ends where the next word starts on a new line
        if (!next_is(TokenKind::Newline) && at > 0 && tokens[at - 1].kind == TokenKind::Greater && peek().spot.line > tokens[at - 1].spot.line) return;
        if (!next_is(TokenKind::Newline)) fail("Each statement goes on its own line, but there's more on this one.");
        skip_newlines();
    }

    static bool is_reserved(TokenKind kind) {
        switch (kind) {
            case TokenKind::Func: case TokenKind::Extern: case TokenKind::Use: case TokenKind::Struct: case TokenKind::Enum: case TokenKind::Const: case TokenKind::Match: case TokenKind::Case: case TokenKind::EndWord: case TokenKind::Var: case TokenKind::If:
            case TokenKind::For: case TokenKind::In: case TokenKind::Break: case TokenKind::Continue:
            case TokenKind::Elif: case TokenKind::And: case TokenKind::Or: case TokenKind::Not:
            case TokenKind::Else: case TokenKind::While: case TokenKind::Return: case TokenKind::True:
            case TokenKind::False: case TokenKind::Print: case TokenKind::Int: case TokenKind::FloatWord: case TokenKind::Bool:
            case TokenKind::StringWord: case TokenKind::Len:
                return true;
            default:
                return false;
        }
    }

    const Token& expect_name(const std::string& what) {
        if (is_reserved(peek().kind)) {
            fail(peek().text + " is a reserved word, so it can't be used as a name.", "Try a different name.");
        }
        return expect(TokenKind::Name, "This needs " + what + ".");
    }

    Type parse_type() {
        if (next_is(TokenKind::Int)) {
            take();
            return Type::Int;
        }
        if (next_is(TokenKind::Bool)) {
            take();
            return Type::Bool;
        }
        if (next_is(TokenKind::FloatWord)) {
            take();
            return Type::Float;
        }
        if (next_is(TokenKind::StringWord)) {
            take();
            return Type::String;
        }
        if (next_is(TokenKind::LeftBracket)) {
            take();
            if (++nesting > DEEPEST_NESTING) fail("This type is nested too deeply.");
            const Type element = parse_type();
            nesting--;
            expect(TokenKind::RightBracket, "This array type needs a closing ].");
            return program.types.array_of(element);
        }
        if (next_is(TokenKind::Name)) {
            const Token& name = take();
            if (!next_is(TokenKind::Less)) return program.types.named(name.text);
            if (++nesting > DEEPEST_NESTING) fail("This type is nested too deeply.");
            std::vector<Type> args = parse_type_args();
            nesting--;
            return program.types.generic(name.text, args);
        }
        fail("This needs a type here.", "Jamo's types are int, float, bool and string, and [int] is an array of ints.");
    }

    // <string, int> after a generic struct's or function's name
    std::vector<Type> parse_type_args() {
        take();
        std::vector<Type> args;
        while (true) {
            args.push_back(parse_type());
            if (!next_is(TokenKind::Comma)) break;
            take();
        }
        expect(TokenKind::Greater, "These types need a closing >.", "Like this: Pair<string, int>");
        return args;
    }

    // <T, U> after the name of a generic function or struct that's being made
    void parse_type_params(std::vector<Type>& params, std::vector<Spot>& spots) {
        take();
        while (true) {
            const Token& param = expect_name("a name for a type, like T");
            params.push_back(program.types.named(param.text));
            spots.push_back(param.spot);
            if (!next_is(TokenKind::Comma)) break;
            take();
        }
        expect(TokenKind::Greater, "These types need a closing >.", "Like this: func first<T>(items: [T]): T");
    }

    // After a name, < starts types only when its matching > comes straight before ( or {, the way C# decides.
    // So a < b stays a comparison
    bool looks_like_type_args() const {
        int depth = 0;
        for (size_t i = at; i < tokens.size(); i++) {
            switch (tokens[i].kind) {
                case TokenKind::Less:
                    depth++;
                    break;
                case TokenKind::Greater:
                    depth--;
                    if (depth == 0) return i + 1 < tokens.size() && (tokens[i + 1].kind == TokenKind::LeftParen || tokens[i + 1].kind == TokenKind::LeftBrace);
                    break;
                case TokenKind::Name: case TokenKind::Int: case TokenKind::FloatWord: case TokenKind::Bool: case TokenKind::StringWord:
                case TokenKind::LeftBracket: case TokenKind::RightBracket: case TokenKind::Comma:
                    break;
                default:
                    return false;
            }
        }
        return false;
    }

    void parse_use() {
        take();
        const Token& path = expect(TokenKind::String, "use needs a file name in quotes.", "Like this: use \"enemy.jamo\"");
        program.uses.push_back({path.value, path.spot});
        expect_line_end();
    }

    void parse_struct() {
        const Token& keyword = take();
        StructDecl decl;
        const Token& name = expect_name("a struct name");
        decl.name = name.text;
        decl.spot = name.spot;
        decl.type = program.types.named(name.text);
        if (next_is(TokenKind::Less)) parse_type_params(decl.type_params, decl.type_param_spots);
        expect_line_end();
        while (!next_is(TokenKind::EndWord)) {
            if (next_is(TokenKind::End)) fail_at(keyword.spot, "This struct never gets its end.", "Add a line with end to finish it.");
            StructField field;
            const Token& field_name = expect_name("a field name, like health: int");
            field.name = field_name.text;
            field.spot = field_name.spot;
            expect(TokenKind::Colon, "A field needs a type.", "Like this: " + field.name + ": int");
            field.type = parse_type();
            expect_line_end();
            decl.fields.push_back(field);
        }
        take();
        expect_line_end();
        program.structs.push_back(std::move(decl));
    }

    void parse_enum() {
        const Token& keyword = take();
        EnumDecl decl;
        const Token& name = expect_name("an enum name");
        decl.name = name.text;
        decl.spot = name.spot;
        decl.type = program.types.named(name.text);
        expect_line_end();
        while (!next_is(TokenKind::EndWord)) {
            if (next_is(TokenKind::End)) fail_at(keyword.spot, "This enum never gets its end.", "Add a line with end to finish it.");
            const Token& member = expect_name("a member name, like CALM");
            decl.members.push_back(member.text);
            decl.member_spots.push_back(member.spot);
            expect_line_end();
        }
        take();
        expect_line_end();
        program.enums.push_back(std::move(decl));
    }

    // const MAX = 64
    void parse_const() {
        take();
        ConstDecl decl;
        const Token& name = expect_name("a constant's name");
        decl.name = name.text;
        decl.spot = name.spot;
        expect(TokenKind::Assign, "A constant needs = and its value.", "Like this: const " + name.text + " = 64");
        decl.value = parse_expression();
        expect_line_end();
        program.consts.push_back(std::move(decl));
    }

    // extern "user32.dll" func MessageBoxW(window: int, text: int, title: int, kind: i32): i32
    std::unique_ptr<Function> parse_extern() {
        take();
        auto function = std::make_unique<Function>();
        const Token& dll = expect(TokenKind::String, "extern needs the DLL's name in quotes.", "Like this: extern \"user32.dll\" func MessageBeep(kind: i32): i32");
        function->dll = dll.value;
        expect(TokenKind::Func, "extern needs func after the DLL's name.");
        const Token& name = expect_name("a function name");
        function->name = name.text;
        function->spot = name.spot;
        expect(TokenKind::LeftParen, "A function name needs ( after it.");
        if (!next_is(TokenKind::RightParen)) {
            while (true) {
                Input input;
                const Token& input_name = expect_name("an input name");
                input.name = input_name.text;
                input.spot = input_name.spot;
                expect(TokenKind::Colon, "Inputs need a type.", "Like this: " + input.name + ": int");
                bool is_i32 = false;
                bool is_f32 = false;
                input.type = parse_extern_type(is_i32, is_f32);
                function->inputs.push_back(input);
                function->input_is_i32.push_back(is_i32);
                function->input_is_f32.push_back(is_f32);
                if (!next_is(TokenKind::Comma)) break;
                take();
            }
        }
        expect(TokenKind::RightParen, "The inputs need a closing ).");
        if (next_is(TokenKind::Colon)) {
            take();
            function->return_type = parse_extern_type(function->returns_i32, function->returns_f32);
        }
        expect_line_end();
        return function;
    }

    Type parse_extern_type(bool& is_i32, bool& is_f32) {
        if (next_is(TokenKind::Name) && peek().text == "i32") {
            take();
            is_i32 = true;
            return Type::Int;
        }
        if (next_is(TokenKind::Name) && peek().text == "f32") {
            take();
            is_f32 = true;
            return Type::Float;
        }
        return parse_type();
    }

    std::unique_ptr<Function> parse_function() {
        const Token& keyword = take();
        auto function = std::make_unique<Function>();
        const Token& name = expect_name("a function name");
        function->name = name.text;
        function->spot = name.spot;
        if (next_is(TokenKind::Less)) parse_type_params(function->type_params, function->type_param_spots);
        if (next_is(TokenKind::Dot)) {
            // A method's first input is always self, the struct it was called on. A generic struct's method has its types
            take();
            const Token& method = expect_name("a method name");
            if (next_is(TokenKind::Less)) fail("A method can't have types of its own.", "Give its struct the types instead, like func Box<T>.get(): T");
            function->owner = function->type_params.empty() ? program.types.named(name.text) : program.types.generic(name.text, function->type_params);
            function->owner_spot = name.spot;
            function->name = name.text + "." + method.text;
            function->spot = method.spot;
            Input self;
            self.name = "self";
            self.spot = name.spot;
            self.type = function->owner;
            function->inputs.push_back(self);
        }
        expect(TokenKind::LeftParen, "A function name needs ( after it.", "Like this: func " + name.text + "()");
        if (!next_is(TokenKind::RightParen)) {
            while (true) {
                Input input;
                const Token& input_name = expect_name("an input name");
                input.name = input_name.text;
                input.spot = input_name.spot;
                expect(TokenKind::Colon, "Inputs need a type.", "Like this: " + input.name + ": int");
                input.type = parse_type();
                function->inputs.push_back(input);
                if (!next_is(TokenKind::Comma)) break;
                take();
            }
        }
        expect(TokenKind::RightParen, "The inputs need a closing ).");
        if (next_is(TokenKind::Colon)) {
            take();
            function->return_type = parse_type();
        }
        expect_line_end();
        function->body = parse_block(keyword.spot, "func");
        if (next_is(TokenKind::Case)) fail("A case can only go inside a match.");
        expect(TokenKind::EndWord, "This function needs an end.");
        expect_line_end();
        return function;
    }

    Block parse_block(Spot opener, const std::string& opener_name) {
        if (++nesting > DEEPEST_NESTING) fail("This code is nested too deeply.");
        Block block;
        skip_newlines();
        while (!next_is(TokenKind::EndWord) && !next_is(TokenKind::Else) && !next_is(TokenKind::Elif) && !next_is(TokenKind::Case)) {
            if (next_is(TokenKind::End)) {
                fail_at(opener, "This " + opener_name + " never gets its end.", "Add a line with end to finish it.");
            }
            const TokenKind first_word = peek().kind;
            const Spot start = peek().spot;
            const int nesting_here = nesting;
            try {
                block.push_back(parse_statement());
            } catch (const StopParsing&) {
                if (next_is(TokenKind::End) || errors.is_full()) throw;
                nesting = nesting_here;
                recover_from_statement(first_word, start);
            }
        }
        nesting--;
        return block;
    }

    std::unique_ptr<Stmt> parse_statement() {
        switch (peek().kind) {
            case TokenKind::Var: return parse_var();
            case TokenKind::If: return parse_if();
            case TokenKind::While: return parse_while();
            case TokenKind::For: return parse_for();
            case TokenKind::Match: return parse_match();
            case TokenKind::Break: return parse_loop_word(StmtKind::Break);
            case TokenKind::Continue: return parse_loop_word(StmtKind::Continue);
            case TokenKind::Return: return parse_return();
            case TokenKind::Print: return parse_print();
            case TokenKind::Name: return parse_name_statement();
            case TokenKind::Use:
                fail("use can only go at the top level of a file, outside functions.");
            case TokenKind::Func:
                fail("Functions can't go inside functions.", "Move this one outside, and finish the function above with end.");
            default:
                fail("A statement can't start with this.");
        }
    }

    std::unique_ptr<Stmt> parse_var() {
        const Token& keyword = take();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::Var;
        stmt->spot = keyword.spot;
        const Token& name = expect_name("a variable name");
        stmt->name = name.text;
        stmt->name_spot = name.spot;
        if (next_is(TokenKind::Colon)) {
            take();
            stmt->has_type = true;
            stmt->declared_type = parse_type();
        }
        expect(TokenKind::Assign, "A variable always needs a value.", "Like this: var " + stmt->name + " = 0");
        stmt->value = parse_expression();
        expect_line_end();
        return stmt;
    }

    std::unique_ptr<Stmt> parse_if() {
        const Token& keyword = take();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::If;
        stmt->spot = keyword.spot;
        stmt->value = parse_expression();
        expect_line_end();
        stmt->body = parse_block(keyword.spot, "if");
        while (next_is(TokenKind::Elif)) {
            take();
            ElifPart part;
            part.condition = parse_expression();
            expect_line_end();
            part.body = parse_block(keyword.spot, "if");
            stmt->elifs.push_back(std::move(part));
        }
        if (next_is(TokenKind::Else)) {
            take();
            stmt->has_else = true;
            expect_line_end();
            stmt->other_body = parse_block(keyword.spot, "if");
            if (next_is(TokenKind::Else)) fail("An if can only have one else.");
            if (next_is(TokenKind::Elif)) fail("An elif must go before the else.");
        }
        if (next_is(TokenKind::Case)) fail("A case can only go inside a match, and this if needs its end first.");
        expect(TokenKind::EndWord, "This if needs an end.");
        expect_line_end();
        return stmt;
    }

    std::unique_ptr<Stmt> parse_while() {
        const Token& keyword = take();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::While;
        stmt->spot = keyword.spot;
        stmt->value = parse_expression();
        expect_line_end();
        stmt->body = parse_block(keyword.spot, "while");
        if (next_is(TokenKind::Else)) fail("A while can't have an else.");
        if (next_is(TokenKind::Elif)) fail("A while can't have an elif.");
        if (next_is(TokenKind::Case)) fail("A case can only go inside a match.");
        expect(TokenKind::EndWord, "This while needs an end.");
        expect_line_end();
        return stmt;
    }

    std::unique_ptr<Stmt> parse_for() {
        const Token& keyword = take();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::For;
        stmt->spot = keyword.spot;
        const Token& name = expect_name("a variable name");
        stmt->name = name.text;
        stmt->name_spot = name.spot;
        expect(TokenKind::In, "A for loop needs in after its variable.", "Like this: for i in 0..10");
        stmt->value = parse_expression();
        if (next_is(TokenKind::DotDot)) {
            take();
            stmt->range_end = parse_expression();
        }
        expect_line_end();
        stmt->body = parse_block(keyword.spot, "for");
        if (next_is(TokenKind::Else)) fail("A for can't have an else.");
        if (next_is(TokenKind::Elif)) fail("A for can't have an elif.");
        if (next_is(TokenKind::Case)) fail("A case can only go inside a match.");
        expect(TokenKind::EndWord, "This for needs an end.");
        expect_line_end();
        return stmt;
    }

    std::unique_ptr<Stmt> parse_match() {
        const Token& keyword = take();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::Match;
        stmt->spot = keyword.spot;
        stmt->value = parse_expression();
        expect_line_end();
        while (next_is(TokenKind::Case)) {
            take();
            CasePart part;
            part.value = parse_expression();
            expect_line_end();
            part.body = parse_block(keyword.spot, "match");
            stmt->cases.push_back(std::move(part));
        }
        if (next_is(TokenKind::Else)) {
            take();
            stmt->has_else = true;
            expect_line_end();
            stmt->other_body = parse_block(keyword.spot, "match");
            if (next_is(TokenKind::Case)) fail("A case must go before the else.");
        }
        if (next_is(TokenKind::End)) fail_at(keyword.spot, "This match never gets its end.", "Add a line with end to finish it.");
        expect(TokenKind::EndWord, "A match can only hold case and else lines here.", "Like this: case Mood.CALM");
        expect_line_end();
        return stmt;
    }

    std::unique_ptr<Stmt> parse_loop_word(StmtKind kind) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = kind;
        stmt->spot = take().spot;
        expect_line_end();
        return stmt;
    }

    static bool is_operation_assign(TokenKind kind) {
        return kind == TokenKind::PlusAssign || kind == TokenKind::MinusAssign || kind == TokenKind::StarAssign || kind == TokenKind::SlashAssign;
    }

    static BinaryOp operation_of(TokenKind kind) {
        switch (kind) {
            case TokenKind::PlusAssign: return BinaryOp::Add;
            case TokenKind::MinusAssign: return BinaryOp::Subtract;
            case TokenKind::StarAssign: return BinaryOp::Multiply;
            default: return BinaryOp::Divide;
        }
    }

    std::unique_ptr<Stmt> parse_return() {
        const Token& keyword = take();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::Return;
        stmt->spot = keyword.spot;
        if (!next_is(TokenKind::Newline)) stmt->value = parse_expression();
        expect_line_end();
        return stmt;
    }

    std::unique_ptr<Stmt> parse_print() {
        const Token& keyword = take();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::Print;
        stmt->spot = keyword.spot;
        expect(TokenKind::LeftParen, "print needs ( after it.", "Like this: print(42)");
        if (next_is(TokenKind::RightParen)) fail("print needs one value.", "Like this: print(42)");
        stmt->value = parse_expression();
        if (next_is(TokenKind::Comma)) fail("print takes one value at a time.", "Use one print for each value.");
        expect(TokenKind::RightParen, "print needs a closing ).");
        expect_line_end();
        return stmt;
    }

    std::unique_ptr<Stmt> parse_name_statement() {
        const Token& name = peek();
        auto stmt = std::make_unique<Stmt>();
        stmt->spot = name.spot;
        ExprPtr start = parse_postfix();
        if (next_is(TokenKind::Percent) && tokens[at + 1].kind == TokenKind::Assign) {
            fail_at(name.spot, "Jamo doesn't have %=.", "Write it out: " + name.text + " = " + name.text + " % 2");
        }
        if (next_is(TokenKind::Assign) || is_operation_assign(peek().kind)) {
            if (start->kind != ExprKind::Name && start->kind != ExprKind::Index && start->kind != ExprKind::Field) {
                fail_at(start->spot, "Only a variable, a field or an item can be set.");
            }
            const TokenKind kind = take().kind;
            stmt->kind = StmtKind::Assign;
            stmt->has_operation = kind != TokenKind::Assign;
            stmt->operation = operation_of(kind);
            if (start->kind == ExprKind::Name) {
                stmt->name = start->name;
                stmt->name_spot = start->spot;
            } else {
                stmt->target = std::move(start);
            }
            stmt->value = parse_expression();
        } else if (start->kind == ExprKind::Call || start->kind == ExprKind::MethodCall) {
            stmt->kind = StmtKind::Call;
            stmt->value = std::move(start);
        } else if (start->kind == ExprKind::Name) {
            fail_at(name.spot, "This line doesn't do anything.", "A statement can set a variable, like " + name.text + " = 1, or call a function, like " + name.text + "().");
        } else {
            fail_at(start->spot, "This line doesn't do anything.");
        }
        expect_line_end();
        return stmt;
    }

    ExprPtr parse_expression() {
        if (++nesting > DEEPEST_NESTING) fail("This expression is nested too deeply.");
        ExprPtr value = parse_or();
        nesting--;
        return value;
    }

    ExprPtr parse_or() {
        ExprPtr left = parse_and();
        while (next_is(TokenKind::Or)) {
            take();
            left = make_binary(BinaryOp::Or, std::move(left), parse_and());
        }
        return left;
    }

    ExprPtr parse_and() {
        ExprPtr left = parse_not();
        while (next_is(TokenKind::And)) {
            take();
            left = make_binary(BinaryOp::And, std::move(left), parse_not());
        }
        return left;
    }

    ExprPtr parse_not() {
        if (!next_is(TokenKind::Not)) return parse_equality();
        const Token& word = take();
        if (++nesting > DEEPEST_NESTING) fail("This expression is nested too deeply.");
        auto expr = std::make_unique<Expr>();
        expr->kind = ExprKind::Not;
        expr->left = parse_not();
        nesting--;
        expr->spot = word.spot;
        expr->spot.length = length_to(word.spot, expr->left->spot);
        expr->depth = expr->left->depth + 1;
        return expr;
    }

    ExprPtr make_binary(BinaryOp op, ExprPtr left, ExprPtr right) {
        auto expr = std::make_unique<Expr>();
        expr->kind = ExprKind::Binary;
        expr->op = op;
        expr->spot = left->spot;
        expr->spot.length = length_to(left->spot, right->spot);
        expr->depth = std::max(left->depth, right->depth) + 1;
        if (expr->depth > TALLEST_EXPRESSION) fail_at(left->spot, "This expression is too long.", "Split it across a few variables.");
        expr->left = std::move(left);
        expr->right = std::move(right);
        return expr;
    }

    ExprPtr parse_equality() {
        ExprPtr left = parse_comparison();
        while (next_is(TokenKind::Equal) || next_is(TokenKind::NotEqual)) {
            const BinaryOp op = take().kind == TokenKind::Equal ? BinaryOp::Equal : BinaryOp::NotEqual;
            left = make_binary(op, std::move(left), parse_comparison());
        }
        return left;
    }

    ExprPtr parse_comparison() {
        ExprPtr left = parse_sum();
        while (true) {
            BinaryOp op;
            switch (peek().kind) {
                case TokenKind::Less: op = BinaryOp::Less; break;
                case TokenKind::Greater: op = BinaryOp::Greater; break;
                case TokenKind::LessEqual: op = BinaryOp::LessEqual; break;
                case TokenKind::GreaterEqual: op = BinaryOp::GreaterEqual; break;
                default: return left;
            }
            take();
            left = make_binary(op, std::move(left), parse_sum());
        }
    }

    ExprPtr parse_sum() {
        ExprPtr left = parse_product();
        while (next_is(TokenKind::Plus) || next_is(TokenKind::Minus)) {
            const BinaryOp op = take().kind == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Subtract;
            left = make_binary(op, std::move(left), parse_product());
        }
        return left;
    }

    ExprPtr parse_product() {
        ExprPtr left = parse_unary();
        while (next_is(TokenKind::Star) || next_is(TokenKind::Slash) || next_is(TokenKind::Percent)) {
            const TokenKind kind = take().kind;
            const BinaryOp op = kind == TokenKind::Star ? BinaryOp::Multiply : kind == TokenKind::Slash ? BinaryOp::Divide : BinaryOp::Remainder;
            left = make_binary(op, std::move(left), parse_unary());
        }
        return left;
    }

    ExprPtr parse_unary() {
        if (!next_is(TokenKind::Minus)) return parse_postfix();
        const Token& minus = take();
        if (++nesting > DEEPEST_NESTING) fail("This expression is nested too deeply.");
        auto expr = std::make_unique<Expr>();
        expr->kind = ExprKind::Negate;
        expr->left = parse_unary();
        nesting--;
        expr->spot = minus.spot;
        expr->spot.length = length_to(minus.spot, expr->left->spot);
        expr->depth = expr->left->depth + 1;
        return expr;
    }

    // What can follow a value: text[i], items.add(1)
    ExprPtr parse_postfix() {
        ExprPtr value = parse_primary();
        while (next_is(TokenKind::LeftBracket) || next_is(TokenKind::Dot)) {
            if (next_is(TokenKind::Dot)) {
                value = parse_after_dot(std::move(value));
                continue;
            }
            take();
            auto expr = std::make_unique<Expr>();
            expr->kind = ExprKind::Index;
            expr->right = parse_expression();
            const Token& closing = expect(TokenKind::RightBracket, "This needs a closing ].");
            expr->spot = value->spot;
            expr->spot.length = length_to(value->spot, closing.spot);
            expr->depth = std::max(value->depth, expr->right->depth) + 1;
            if (expr->depth > TALLEST_EXPRESSION) fail_at(value->spot, "This expression is too long.", "Split it across a few variables.");
            expr->left = std::move(value);
            value = std::move(expr);
        }
        return value;
    }

    ExprPtr parse_after_dot(ExprPtr value) {
        take();
        const Token& name = expect(TokenKind::Name, "This needs a name after its dot.");
        auto expr = std::make_unique<Expr>();
        expr->name = name.text;
        expr->spot = value->spot;
        expr->depth = value->depth + 1;
        if (next_is(TokenKind::LeftParen)) {
            expr = parse_call(std::move(expr));
            expr->kind = ExprKind::MethodCall;
        } else {
            expr->kind = ExprKind::Field;
            expr->spot.length = length_to(expr->spot, name.spot);
        }
        expr->depth = std::max(expr->depth, value->depth + 1);
        if (expr->depth > TALLEST_EXPRESSION) fail_at(value->spot, "This expression is too long.", "Split it across a few variables.");
        expr->left = std::move(value);
        return expr;
    }

    ExprPtr parse_struct_literal(ExprPtr expr) {
        take();
        expr->kind = ExprKind::StructLiteral;
        while (!next_is(TokenKind::RightBrace)) {
            const Token& field = expect_name("a field name, like health: 3");
            expr->field_names.push_back(field.text);
            expr->field_spots.push_back(field.spot);
            expect(TokenKind::Colon, "A field needs : before its value.", "Like this: " + field.text + ": 3");
            expr->inputs.push_back(parse_expression());
            if (!next_is(TokenKind::Comma)) break;
            take();
        }
        const Token& closing = expect(TokenKind::RightBrace, "This struct needs a closing }.");
        expr->spot.length = length_to(expr->spot, closing.spot);
        for (const ExprPtr& value : expr->inputs) expr->depth = std::max(expr->depth, value->depth + 1);
        return expr;
    }

    ExprPtr parse_array_literal(ExprPtr expr) {
        take();
        while (!next_is(TokenKind::RightBracket)) {
            expr->inputs.push_back(parse_expression());
            if (!next_is(TokenKind::Comma)) break;
            take();
        }
        const Token& closing = expect(TokenKind::RightBracket, "This array needs a closing ].");
        expr->kind = ExprKind::ArrayLiteral;
        expr->spot.length = length_to(expr->spot, closing.spot);
        for (const ExprPtr& item : expr->inputs) expr->depth = std::max(expr->depth, item->depth + 1);
        return expr;
    }

    ExprPtr parse_primary() {
        const Token& token = peek();
        auto expr = std::make_unique<Expr>();
        expr->spot = token.spot;
        switch (token.kind) {
            case TokenKind::Number:
                take();
                expr->kind = ExprKind::Number;
                expr->number = token.number;
                return expr;
            case TokenKind::Float:
                take();
                expr->kind = ExprKind::Float;
                expr->real = token.real;
                return expr;
            case TokenKind::True:
            case TokenKind::False:
                take();
                expr->kind = ExprKind::Bool;
                expr->truth = token.kind == TokenKind::True;
                return expr;
            case TokenKind::String:
                take();
                expr->kind = ExprKind::String;
                expr->text = token.value;
                return expr;
            case TokenKind::LeftBracket:
                return parse_array_literal(std::move(expr));
            case TokenKind::FStringStart:
                return parse_fstring(std::move(expr));
            case TokenKind::Not:
                fail("not can't go here without parentheses.", "Like this: x == (not y)");
            case TokenKind::RightBrace:
                fail("This { } is empty.", "Put a value inside, like {score}.");
            case TokenKind::StringWord:
                return parse_builtin(std::move(expr), ExprKind::Convert, Type::String);
            case TokenKind::Int:
                return parse_builtin(std::move(expr), ExprKind::Convert, Type::Int);
            case TokenKind::FloatWord:
                return parse_builtin(std::move(expr), ExprKind::Convert, Type::Float);
            case TokenKind::Len:
                return parse_builtin(std::move(expr), ExprKind::Length, Type::Nothing);
            case TokenKind::Name:
                take();
                expr->name = token.text;
                if (next_is(TokenKind::Less) && looks_like_type_args()) {
                    expr->type_args = parse_type_args();
                    if (next_is(TokenKind::LeftParen)) return parse_call(std::move(expr));
                    return parse_struct_literal(std::move(expr));
                }
                if (next_is(TokenKind::LeftParen)) return parse_call(std::move(expr));
                if (next_is(TokenKind::LeftBrace)) return parse_struct_literal(std::move(expr));
                expr->kind = ExprKind::Name;
                return expr;
            case TokenKind::LeftParen: {
                take();
                ExprPtr inner = parse_expression();
                const Token& closing = expect(TokenKind::RightParen, "This needs a closing ).");
                inner->spot.column = token.spot.column;
                inner->spot.line = token.spot.line;
                inner->spot.length = length_to(token.spot, closing.spot);
                return inner;
            }
            case TokenKind::Print:
                fail("print doesn't give back a value, so it can only be its own statement.");
            case TokenKind::Newline:
            case TokenKind::EndWord:
            case TokenKind::End:
                fail("This line stops before it's finished. It needs a value here.");
            default:
                fail("This needs a value here, like a number, a name or a function call.");
        }
    }

    // The lexer has already checked the f-string's text and that every { has a }
    ExprPtr parse_fstring(ExprPtr expr) {
        take();
        expr->kind = ExprKind::FString;
        while (!next_is(TokenKind::FStringEnd)) {
            if (next_is(TokenKind::FStringText)) {
                const Token& part = take();
                auto text = std::make_unique<Expr>();
                text->kind = ExprKind::String;
                text->spot = part.spot;
                text->text = part.value;
                expr->inputs.push_back(std::move(text));
            } else {
                expect(TokenKind::LeftBrace, "This f-string has something unexpected here.");
                expr->inputs.push_back(parse_expression());
                expect(TokenKind::RightBrace, "This { needs a closing }.");
            }
        }
        const Token& closing = take();
        expr->spot.length = length_to(expr->spot, closing.spot);
        for (const ExprPtr& part : expr->inputs) expr->depth = std::max(expr->depth, part->depth + 1);
        return expr;
    }

    // len(x), string(x), int(x) and float(x): each takes exactly one value
    ExprPtr parse_builtin(ExprPtr expr, ExprKind kind, Type convert_to) {
        const std::string word = take().text;
        expr->kind = kind;
        expr->convert_to = convert_to;
        expect(TokenKind::LeftParen, word + " needs ( after it.", "Like this: " + word + "(x)");
        if (next_is(TokenKind::RightParen)) fail(word + " needs one value.", "Like this: " + word + "(x)");
        expr->left = parse_expression();
        if (next_is(TokenKind::Comma)) fail(word + " takes one value.");
        const Token& closing = expect(TokenKind::RightParen, word + " needs a closing ).");
        expr->spot.length = length_to(expr->spot, closing.spot);
        expr->depth = expr->left->depth + 1;
        return expr;
    }

    ExprPtr parse_call(ExprPtr call) {
        call->kind = ExprKind::Call;
        take();
        if (!next_is(TokenKind::RightParen)) {
            while (true) {
                call->inputs.push_back(parse_expression());
                if (!next_is(TokenKind::Comma)) break;
                take();
            }
        }
        const Token& closing = expect(TokenKind::RightParen, "This call needs a closing ).");
        call->spot.length = length_to(call->spot, closing.spot);
        for (const ExprPtr& input : call->inputs) call->depth = std::max(call->depth, input->depth + 1);
        return call;
    }

    const std::vector<Token>& tokens;
    Errors& errors;
    Program& program;
    size_t at = 0;
    int nesting = 0;
};

}  // namespace

void parse_into(const std::vector<Token>& tokens, Errors& errors, Program& program) {
    Parser parser(tokens, errors, program);
    try {
        parser.parse();
    } catch (const StopParsing&) {
    }
}

Program parse_program(const std::vector<Token>& tokens, Errors& errors) {
    Program program;
    Parser parser(tokens, errors, program);
    try {
        parser.parse();
    } catch (const StopParsing&) {
        return Program{};
    }
    return program;
}
