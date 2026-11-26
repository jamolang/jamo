#include "checker.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace {

const uint64_t LARGEST_POSITIVE = 9223372036854775807ULL;
const int MOST_DIFFERENT_LETTERS = 2;  // How close a name must be for "Did you mean"
const size_t MOST_EXTERN_INPUTS = 16;
const size_t MOST_CALLBACK_INPUTS = 4;
const int MOST_GENERIC_COPIES = 500;  // Stops a generic that makes ever bigger types of itself, like Node<[T]> inside Node<T>
const char* const MATCH_NUMBERS = "Use float(x) or int(x) to make them match.";

bool is_number(Type type) {
    return type == Type::Int || type == Type::Float;
}

int letters_different(const std::string& a, const std::string& b) {
    std::vector<int> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) row[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); i++) {
        int diagonal = row[0];
        row[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); j++) {
            const int above = row[j];
            row[j] = std::min({above + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

bool is_literal_zero(const Expr& expr) {
    if (expr.kind == ExprKind::Negate) return is_literal_zero(*expr.left);
    return expr.kind == ExprKind::Number && expr.number == 0;
}

bool is_order_comparison(const Expr& expr) {
    if (expr.kind != ExprKind::Binary) return false;
    return expr.op == BinaryOp::Less || expr.op == BinaryOp::Greater || expr.op == BinaryOp::LessEqual || expr.op == BinaryOp::GreaterEqual;
}

bool always_returns(const Block& block);

bool statement_always_returns(const Stmt* stmt) {
    {
        if (stmt->kind == StmtKind::Return) return true;
        if (stmt->kind == StmtKind::If && stmt->has_else && always_returns(stmt->body) && always_returns(stmt->other_body)) {
            bool every_elif_returns = true;
            for (const ElifPart& part : stmt->elifs) every_elif_returns = every_elif_returns && always_returns(part.body);
            if (every_elif_returns) return true;
        }
        if (stmt->kind == StmtKind::Try && always_returns(stmt->body) && always_returns(stmt->other_body)) return true;
        if (stmt->kind == StmtKind::Match && stmt->covers_all) {
            bool every_case_returns = !stmt->has_else || always_returns(stmt->other_body);
            for (const CasePart& part : stmt->cases) every_case_returns = every_case_returns && always_returns(part.body);
            if (every_case_returns) return true;
        }
    }
    return false;
}

bool always_returns(const Block& block) {
    for (const auto& stmt : block) {
        if (statement_always_returns(stmt.get())) return true;
    }
    return false;
}

bool always_leaves(const Stmt& stmt) {
    return stmt.kind == StmtKind::Break || stmt.kind == StmtKind::Continue || statement_always_returns(&stmt);
}

struct Variable {
    Type type = Type::Int;
    int slot = 0;
    bool read_only = false;  // A for loop's variable, or self
    bool used = false;
    bool warn_if_unused = false;  // Only a var, since inputs and loop variables are often left unused on purpose
    Spot spot;
};

class Checker {
public:
    Checker(Program& the_program, Errors& the_errors) : program(the_program), errors(the_errors) {}

    void check() {
        check_structs();
        check_enums();
        check_generic_struct_params();
        for (ConstDecl& decl : program.consts) {
            if (claim_name(decl.name, decl.spot, "constant")) constants[decl.name] = &decl;
        }
        for (ConstDecl& decl : program.consts) {
            if (constants.count(decl.name) && constants[decl.name] == &decl) check_constant(decl);
        }
        std::vector<const Function*> repeats;
        for (const auto& function : program.functions) {
            if (!function->type_params.empty()) {
                add_generic_function(*function);
                repeats.push_back(function.get());
                continue;
            }
            if (const Function* left_out = extern_repeat(*function)) {
                repeats.push_back(left_out);
                continue;
            }
            if (claim_name(function->name, function->spot, "function")) functions[function->name] = function.get();
        }
        // A repeat of an extern is the same Windows function, so only the first one is kept. Generic functions
        // move out too: only their copies for real types get checked and made into code
        for (auto& function : program.functions) {
            if (!function->type_params.empty()) generic_templates.push_back(std::move(function));
        }
        std::erase_if(program.functions, [&](const auto& function) { return !function || std::find(repeats.begin(), repeats.end(), function.get()) != repeats.end(); });
        make_setup_function();
        check_main();
        for (const StructDecl& decl : program.structs) {
            if (!decl.type_params.empty()) continue;
            for (const StructField& field : decl.fields) check_type_exists(field.type, field.spot);
        }
        for (const StructDecl& decl : program.structs) {
            if (decl.type_params.empty()) check_struct_isnt_endless(decl.type, decl.name, decl.spot);
        }
        // Copies of generic functions join the end of the list as they're needed, and get checked in turn.
        // The setup comes first, so every function knows the variables outside functions
        for (size_t i = 0; i < program.functions.size(); i++) {
            if (program.functions[i].get() == setup) {
                check_setup();
            } else {
                check_function(*program.functions[i]);
            }
        }
        if (setup) check_setup_order();
    }

    // --- Variables outside functions ---
    // Their var lines become one function, (setup), which the program runs before main. Each gets a slot from
    // -2 down. They're set up in order, so a value can only use the ones above it, even through a function it calls

    void make_setup_function() {
        if (program.globals.empty()) return;
        // Each file's come after those of the files it uses, and stay in order within a file
        if (!program.setup_order.empty()) {
            std::map<int, size_t> rank;
            for (size_t i = 0; i < program.setup_order.size(); i++) rank[program.setup_order[i]] = i;
            std::stable_sort(program.globals.begin(), program.globals.end(), [&](const auto& a, const auto& b) { return rank[a->spot.file] < rank[b->spot.file]; });
        }
        for (size_t i = 0; i < program.globals.size(); i++) {
            const Stmt& stmt = *program.globals[i];
            if (claim_name(stmt.name, stmt.name_spot, "variable")) global_order[stmt.name] = i;
        }
        auto made = std::make_unique<Function>();
        made->name = SETUP_NAME;
        made->spot = program.globals.front()->spot;
        for (auto& stmt : program.globals) made->body.push_back(std::move(stmt));
        program.globals.clear();
        setup = made.get();
        program.functions.insert(program.functions.begin(), std::move(made));
    }

    void check_setup() {
        current = setup;
        next_slot = 0;
        loops_open = 0;
        scopes.clear();
        scopes.emplace_back();
        quiet = false;
        for (size_t i = 0; i < setup->body.size(); i++) {
            Stmt& stmt = *setup->body[i];
            user = SETUP_NAME + std::to_string(i);
            setting_up = static_cast<int>(i);
            if (stmt.has_type) check_type_exists(stmt.declared_type, stmt.name_spot);
            Type type = check_wanting(*stmt.value, stmt.has_type ? stmt.declared_type : Type::Nothing);
            if (type == Type::Nothing) {
                errors.add(stmt.value->spot, "This doesn't give back a value, so it can't go in a variable.");
                type = Type::Unknown;
            }
            if (stmt.has_type) {
                if (type != Type::Unknown && type != stmt.declared_type) {
                    errors.add(stmt.value->spot, stmt.name + " is declared as " + with_article(stmt.declared_type) + ", but this value is " + with_article(type) + ".");
                }
                type = stmt.declared_type;
            }
            stmt.slot = FIRST_GLOBAL_SLOT - static_cast<int>(i);
            const auto order = global_order.find(stmt.name);
            if (order == global_order.end() || order->second != i) continue;
            global_vars.push_back({type, stmt.slot, false, false, false, stmt.name_spot});
            global_places[stmt.name] = global_vars.size() - 1;
        }
        setting_up = -1;
        user.clear();
        setup->slot_count = next_slot;
    }

    // A value that calls a function using a variable outside functions that isn't set up yet would find it empty
    void check_setup_order() {
        for (size_t i = 0; i < setup->body.size(); i++) {
            const Stmt& stmt = *setup->body[i];
            std::vector<std::string> to_visit;
            const std::vector<std::string>& first_calls = calls_by[SETUP_NAME + std::to_string(i)];
            for (auto call = first_calls.rbegin(); call != first_calls.rend(); ++call) to_visit.push_back(*call);
            std::set<std::string> seen;
            bool reported = false;
            while (!to_visit.empty() && !reported) {
                const std::string name = to_visit.back();
                to_visit.pop_back();
                if (!seen.insert(name).second) continue;
                for (const int read : globals_read_by[name]) {
                    if (read < static_cast<int>(i)) continue;
                    const std::string& later = setup->body[static_cast<size_t>(read)]->name;
                    errors.add(stmt.value->spot, name + " uses " + later + ", which isn't set up yet when this runs.", "Variables outside functions are set up in order, top to bottom, so move " + later + " above " + stmt.name + ".");
                    reported = true;
                    break;
                }
                const std::vector<std::string>& calls = calls_by[name];
                for (auto call = calls.rbegin(); call != calls.rend(); ++call) to_visit.push_back(*call);
            }
        }
    }

    void note_call(const std::string& name) {
        if (!user.empty()) calls_by[user].push_back(name);
    }

    void note_global_read(int slot) {
        if (!user.empty() && slot <= FIRST_GLOBAL_SLOT) globals_read_by[user].push_back(FIRST_GLOBAL_SLOT - slot);
    }

private:
    std::string type_name(Type type) const {
        return program.types.name(type);
    }

    std::string with_article(Type type) const {
        const std::string name = type_name(type);
        if (type == Type::Nothing || type == Type::Unknown) return name;
        return (std::string("aeiouAEIOU").find(name[0]) != std::string::npos ? "an " : "a ") + name;
    }

    // Every function, struct and enum shares one set of names, across all files
    bool claim_name(const std::string& name, Spot spot, const std::string& word) {
        if (is_builtin(name)) {
            errors.add(spot, name + " is built into Jamo, so this needs a different name.");
            return false;
        }
        const auto found = names.find(name);
        if (found == names.end()) {
            names[name] = {word, spot};
            return true;
        }
        const Spot other = found->second.second;
        if (other.file != spot.file) {
            errors.add(spot, name + " is used in both " + errors.name_of_file(other.file) + " and " + errors.name_of_file(spot.file) + ".", "Each name can only be used once in a program, so rename one of them.");
        } else {
            errors.add(spot, "There's already a " + found->second.first + " called " + name + ", on line " + std::to_string(other.line) + ".", "Give this one a different name.");
        }
        return false;
    }

    // --- Constants ---
    // Checks a constant's value once. It can only use literals, other constants and operators, so it's
    // known before the program runs. Answers false when it can't be used
    bool check_constant(ConstDecl& decl) {
        const auto state = constant_state.find(decl.name);
        if (state != constant_state.end()) {
            if (state->second == 1) {
                errors.add(decl.spot, decl.name + " is worked out from itself.", "A constant can use other constants, but not in a circle.");
                constant_state[decl.name] = 3;
                return false;
            }
            return state->second == 2;
        }
        constant_state[decl.name] = 1;
        const size_t errors_before = errors.count();
        const Type type = check_expression(*decl.value);
        if (constant_state[decl.name] == 3) return false;
        if (errors.count() != errors_before) {
            constant_state[decl.name] = 3;
            return false;
        }
        if (type == Type::Unknown) {
            constant_state[decl.name] = 3;
            return false;
        }
        if (!is_constant_value(*decl.value)) {
            errors.add(decl.value->spot, "A constant needs a value that's known before the program runs.", "Use numbers, text, true or false, other constants, and operators like + and *.");
            constant_state[decl.name] = 3;
            return false;
        }
        constant_state[decl.name] = 2;
        return true;
    }

    static bool is_constant_value(const Expr& expr) {
        switch (expr.kind) {
            case ExprKind::Number: case ExprKind::Float: case ExprKind::String: case ExprKind::Bool: case ExprKind::EnumValue:
                return true;
            case ExprKind::Negate: case ExprKind::Not:
                return is_constant_value(*expr.left);
            case ExprKind::Binary:
                return is_constant_value(*expr.left) && is_constant_value(*expr.right);
            default:
                return false;
        }
    }

    // A fresh copy of a checked constant's value, which only holds the kinds above
    static Expr copy_constant(const Expr& from) {
        Expr copy;
        copy.kind = from.kind;
        copy.spot = from.spot;
        copy.number = from.number;
        copy.real = from.real;
        copy.truth = from.truth;
        copy.text = from.text;
        copy.name = from.name;
        copy.op = from.op;
        copy.type = from.type;
        copy.depth = from.depth;
        if (from.left) copy.left = std::make_unique<Expr>(copy_constant(*from.left));
        if (from.right) copy.right = std::make_unique<Expr>(copy_constant(*from.right));
        return copy;
    }

    // Errors in a constant's use point at the use, not at the constant
    static void set_spot(Expr& expr, Spot spot) {
        expr.spot = spot;
        if (expr.left) set_spot(*expr.left, spot);
        if (expr.right) set_spot(*expr.right, spot);
    }

    // --- An extern declared again, like sqrt in a program and in gfx.jamo ---
    static std::string extern_type_name(Type type, bool is_i32, bool is_f32) {
        if (is_i32) return "i32";
        if (is_f32) return "f32";
        if (type == Type::Int) return "int";
        if (type == Type::Float) return "float";
        if (type == Type::Bool) return "bool";
        if (type == Type::Nothing) return "nothing";
        return "";
    }

    std::string extern_input_type(const Function& function, size_t i) const {
        const std::string name = extern_type_name(function.inputs[i].type, function.input_is_i32[i], function.input_is_f32[i]);
        return name.empty() ? type_name(function.inputs[i].type) : name;
    }

    std::string extern_answer_type(const Function& function) const {
        const std::string name = extern_type_name(function.return_type, function.returns_i32, function.returns_f32);
        return name.empty() ? type_name(function.return_type) : name;
    }

    // How two externs with one name differ, or "" when they're the same function
    std::string extern_difference(const Function& first, const Function& again) const {
        if (first.dll != again.dll) return "it comes from " + again.dll + " here but " + first.dll + " there";
        if (first.inputs.size() != again.inputs.size()) {
            const auto inputs = [](size_t count) { return std::to_string(count) + (count == 1 ? " input" : " inputs"); };
            return "it takes " + inputs(again.inputs.size()) + " here but " + inputs(first.inputs.size()) + " there";
        }
        for (size_t i = 0; i < first.inputs.size(); i++) {
            const std::string here = extern_input_type(again, i);
            const std::string there = extern_input_type(first, i);
            if (here != there) return "its input " + std::to_string(i + 1) + " is " + here + " here but " + there + " there";
        }
        const std::string here = extern_answer_type(again);
        const std::string there = extern_answer_type(first);
        if (here != there) return "it answers with " + here + " here but " + there + " there";
        return "";
    }

    bool is_builtin_file(int file) const {
        const std::string& name = errors.name_of_file(file);
        return name.size() > 11 && name.compare(name.size() - 11, 11, " (built in)") == 0;
    }

    // When this extern has the name of an earlier one, gives back the one to leave out, or nullptr if it's a new name.
    // A different one gets an error that says how it differs, on the program's own line rather than a built-in library's
    Function* extern_repeat(Function& function) {
        if (function.dll.empty()) return nullptr;
        const auto found = functions.find(function.name);
        if (found == functions.end() || found->second->dll.empty()) return nullptr;
        Function* first = found->second;
        if (extern_difference(*first, function).empty()) return &function;
        Function* mine = &function;
        Function* other = first;
        if (is_builtin_file(function.spot.file) && !is_builtin_file(first->spot.file)) {
            std::swap(mine, other);
            found->second = other;  // The library keeps its own, so its calls give no extra errors
        }
        const std::string where = other->spot.file != mine->spot.file ? "in " + errors.name_of_file(other->spot.file) + " line " : "on line ";
        errors.add(mine->spot, mine->name + " is already declared " + where + std::to_string(other->spot.line) + ", and this one is different: " + extern_difference(*other, *mine) + ".",
                   "An extern can be declared again only if its DLL, inputs and answer are exactly the same.");
        return mine;
    }

    void check_structs() {
        for (StructDecl& decl : program.structs) {
            if (!claim_name(decl.name, decl.spot, "struct")) continue;
            for (size_t i = 0; i < decl.fields.size(); i++) {
                for (size_t j = 0; j < i; j++) {
                    if (decl.fields[j].name == decl.fields[i].name) errors.add(decl.fields[i].spot, "There's already a field called " + decl.fields[i].name + " in this struct.");
                }
            }
            if (!decl.type_params.empty()) {
                // Only its copies for real types, like Pair<string, int>, become structs
                generic_structs[decl.name] = &decl;
                continue;
            }
            structs[decl.name] = &decl;
            program.types.make_struct(decl.type, decl.fields);
        }
    }

    // A generic struct's type names can only be checked once every struct and enum has its name
    void check_generic_struct_params() {
        for (const StructDecl& decl : program.structs) {
            if (generic_structs.count(decl.name) && generic_structs[decl.name] == &decl) check_type_params(decl.type_params, decl.type_param_spots);
        }
    }

    // --- Generics ---
    // A generic function or struct is a pattern. Each set of real types it's used with gets its own copy, with
    // the types swapped in, and only the copies are checked and made into code, like C++ templates

    // T in first<T> can't be a real type's name, or used twice
    void check_type_params(const std::vector<Type>& params, const std::vector<Spot>& spots) {
        for (size_t i = 0; i < params.size(); i++) {
            const std::string name = type_name(params[i]);
            if (structs.count(name) || enums.count(name) || generic_structs.count(name)) {
                errors.add(spots[i], name + " is already a type, so it can't name a generic's type too.", "Use a different name, like T.");
            }
            for (size_t j = 0; j < i; j++) {
                if (params[j] == params[i]) errors.add(spots[i], "There's already a type called " + name + " here.");
            }
        }
    }

    void add_generic_function(Function& function) {
        if (function.owner == Type::Nothing) {
            check_type_params(function.type_params, function.type_param_spots);
            // A generic main is still kept, so there's no second error about main being missing
            if (function.name == "main") errors.add(function.spot, "main can't have types.");
            if (claim_name(function.name, function.spot, "function")) generic_functions[function.name] = &function;
            return;
        }
        // A method of a generic struct, like Box<T>.get
        const std::string base = program.types.base_of(function.owner);
        const auto found = generic_structs.find(base);
        if (found == generic_structs.end()) {
            errors.add(function.owner_spot, structs.count(base) ? base + " has no types, so its methods don't either." : "There's no generic struct called " + base + ", so it can't have this method.");
            return;
        }
        if (found->second->type_params.size() != function.type_params.size()) {
            errors.add(function.owner_spot, base + " has " + type_words(found->second->type_params.size()) + ", but this gives it " + std::to_string(function.type_params.size()) + ".");
            return;
        }
        check_type_params(function.type_params, function.type_param_spots);
        if (claim_name(function.name, function.spot, "function")) generic_methods[function.name] = &function;
    }

    static std::string type_words(size_t count) {
        return std::to_string(count) + (count == 1 ? " type" : " types");
    }

    bool uses_params(Type type, const std::vector<Type>& params) const {
        if (std::find(params.begin(), params.end(), type) != params.end()) return true;
        if (program.types.is_array(type)) return uses_params(program.types.element_of(type), params);
        if (program.types.is_generic(type)) {
            for (const Type arg : program.types.args_of(type)) {
                if (uses_params(arg, params)) return true;
            }
        }
        return false;
    }

    // The type with each of from swapped for the one in the same place in to
    Type substitute(Type type, const std::vector<Type>& from, const std::vector<Type>& to) {
        for (size_t i = 0; i < from.size(); i++) {
            if (type == from[i]) return to[i];
        }
        if (program.types.is_array(type)) {
            const Type element = substitute(program.types.element_of(type), from, to);
            return program.types.array_of(element);
        }
        if (program.types.is_generic(type)) {
            const std::string base = program.types.base_of(type);
            std::vector<Type> args = program.types.args_of(type);
            for (Type& arg : args) arg = substitute(arg, from, to);
            return program.types.generic(base, args);
        }
        if (program.types.is_function(type)) {
            std::vector<Type> inputs = program.types.inputs_of(type);
            for (Type& input : inputs) input = substitute(input, from, to);
            const Type answer = substitute(program.types.answer_of(type), from, to);
            return program.types.function_of(inputs, answer);
        }
        return type;
    }

    // Works out a generic function's types by matching an input's type in the pattern with what was given
    void work_out_types(Type pattern, Type given, const std::vector<Type>& params, std::vector<Type>& found) const {
        if (given == Type::Unknown || given == Type::Nothing) return;
        for (size_t i = 0; i < params.size(); i++) {
            if (pattern == params[i]) {
                if (found[i] == Type::Nothing) found[i] = given;
                return;
            }
        }
        if (program.types.is_array(pattern) && program.types.is_array(given)) {
            work_out_types(program.types.element_of(pattern), program.types.element_of(given), params, found);
            return;
        }
        if (program.types.is_generic(pattern) && program.types.is_generic(given) && program.types.base_of(pattern) == program.types.base_of(given)) {
            const std::vector<Type> pattern_args = program.types.args_of(pattern);
            const std::vector<Type> given_args = program.types.args_of(given);
            if (pattern_args.size() != given_args.size()) return;
            for (size_t i = 0; i < pattern_args.size(); i++) work_out_types(pattern_args[i], given_args[i], params, found);
        }
        if (program.types.is_function(pattern) && program.types.is_function(given)) {
            const std::vector<Type> pattern_inputs = program.types.inputs_of(pattern);
            const std::vector<Type> given_inputs = program.types.inputs_of(given);
            if (pattern_inputs.size() != given_inputs.size()) return;
            for (size_t i = 0; i < pattern_inputs.size(); i++) work_out_types(pattern_inputs[i], given_inputs[i], params, found);
            work_out_types(program.types.answer_of(pattern), program.types.answer_of(given), params, found);
        }
    }

    // Fills in a generic struct's fields once it's given real types. Answers false when it can't be
    bool make_generic_struct(Type type, Spot spot) {
        if (program.types.is_struct(type)) return true;
        const std::string base = program.types.base_of(type);
        const std::vector<Type> args = program.types.args_of(type);
        const auto found = generic_structs.find(base);
        if (found == generic_structs.end()) {
            if (structs.count(base)) {
                errors.add(spot, base + " has no types, so it can't be given any.");
            } else {
                std::vector<std::string> options;
                for (const auto& item : generic_structs) options.push_back(item.first);
                const std::string close = closest(base, options);
                errors.add(spot, "There's no generic struct called " + base + ".", close.empty() ? "" : "Did you mean " + close + "?");
            }
            return false;
        }
        const StructDecl& decl = *found->second;
        if (args.size() != decl.type_params.size()) {
            errors.add(spot, base + " takes " + type_words(decl.type_params.size()) + ", but this gives it " + std::to_string(args.size()) + ".");
            return false;
        }
        const size_t errors_before = errors.count();
        for (const Type arg : args) check_type_exists(arg, spot);
        if (errors.count() != errors_before) return false;
        if (++copies_made > MOST_GENERIC_COPIES) {
            if (copies_made == MOST_GENERIC_COPIES + 1) errors.add(spot, "This makes too many copies of generics, like a struct that holds a bigger copy of itself.", "A program can use up to 500 different sets of types.");
            return false;
        }
        std::vector<StructField> fields = decl.fields;
        for (StructField& field : fields) field.type = substitute(field.type, decl.type_params, args);
        program.types.make_struct(type, fields);
        for (const StructField& field : fields) check_type_exists(field.type, field.spot);
        check_struct_isnt_endless(type, type_name(type), decl.spot);
        return true;
    }

    ExprPtr copy_expr(const Expr* from, const std::vector<Type>& params, const std::vector<Type>& args) {
        if (!from) return nullptr;
        auto copy = std::make_unique<Expr>();
        copy->kind = from->kind;
        copy->spot = from->spot;
        copy->number = from->number;
        copy->real = from->real;
        copy->truth = from->truth;
        copy->text = from->text;
        copy->name = from->name;
        copy->field_names = from->field_names;
        copy->field_spots = from->field_spots;
        copy->op = from->op;
        copy->convert_to = from->convert_to;
        copy->depth = from->depth;
        for (const Type type : from->type_args) copy->type_args.push_back(substitute(type, params, args));
        copy->left = copy_expr(from->left.get(), params, args);
        copy->right = copy_expr(from->right.get(), params, args);
        for (const auto& input : from->inputs) copy->inputs.push_back(copy_expr(input.get(), params, args));
        return copy;
    }

    Block copy_block(const Block& from, const std::vector<Type>& params, const std::vector<Type>& args) {
        Block copy;
        for (const auto& stmt : from) {
            auto s = std::make_unique<Stmt>();
            s->kind = stmt->kind;
            s->spot = stmt->spot;
            s->name = stmt->name;
            s->name_spot = stmt->name_spot;
            s->has_operation = stmt->has_operation;
            s->operation = stmt->operation;
            s->has_type = stmt->has_type;
            s->declared_type = substitute(stmt->declared_type, params, args);
            s->has_else = stmt->has_else;
            s->target = copy_expr(stmt->target.get(), params, args);
            s->value = copy_expr(stmt->value.get(), params, args);
            s->range_end = copy_expr(stmt->range_end.get(), params, args);
            s->body = copy_block(stmt->body, params, args);
            for (const ElifPart& part : stmt->elifs) s->elifs.push_back({copy_expr(part.condition.get(), params, args), copy_block(part.body, params, args)});
            for (const CasePart& part : stmt->cases) s->cases.push_back({copy_expr(part.value.get(), params, args), copy_block(part.body, params, args)});
            s->other_body = copy_block(stmt->other_body, params, args);
            copy.push_back(std::move(s));
        }
        return copy;
    }

    // The copy of a generic function or method for these types, made the first time it's needed. It joins the
    // program's functions, so it gets checked after the ones before it
    Function* make_generic_copy(const Function& pattern, const std::vector<Type>& args, const std::string& name, Spot spot) {
        const auto found = functions.find(name);
        if (found != functions.end()) return found->second;
        if (++copies_made > MOST_GENERIC_COPIES) {
            if (copies_made == MOST_GENERIC_COPIES + 1) errors.add(spot, "This makes too many copies of generics, like a function that calls itself with a bigger type.", "A program can use up to 500 different sets of types.");
            return nullptr;
        }
        auto copy = std::make_unique<Function>();
        copy->name = name;
        copy->spot = pattern.spot;
        copy->owner = substitute(pattern.owner, pattern.type_params, args);
        copy->owner_spot = pattern.owner_spot;
        for (Input input : pattern.inputs) {
            input.type = substitute(input.type, pattern.type_params, args);
            copy->inputs.push_back(input);
        }
        copy->return_type = substitute(pattern.return_type, pattern.type_params, args);
        copy->body = copy_block(pattern.body, pattern.type_params, args);
        Function* made = copy.get();
        program.functions.push_back(std::move(copy));
        functions[name] = made;
        copy_of[made] = &pattern;
        made_from[name] = {current ? current->name : "", spot};
        for (const Input& input : made->inputs) check_type_exists(input.type, spot);
        check_type_exists(made->return_type, spot);
        return made;
    }

    std::string generic_name(const std::string& base, const std::vector<Type>& args) const {
        std::string name = base + "<";
        for (size_t i = 0; i < args.size(); i++) name += (i > 0 ? ", " : "") + type_name(args[i]);
        return name + ">";
    }

    Type check_generic_call(Expr& expr, const Function& pattern) {
        const std::vector<Type>& params = pattern.type_params;
        const bool types_given = !expr.type_args.empty();
        std::vector<Type> found(params.size(), Type::Nothing);
        if (types_given && expr.type_args.size() != params.size()) {
            for (auto& input : expr.inputs) check_expression(*input);
            errors.add(expr.spot, pattern.name + " takes " + type_words(params.size()) + ", but this gives it " + std::to_string(expr.type_args.size()) + ".");
            return Type::Unknown;
        }
        if (types_given) {
            const size_t errors_before = errors.count();
            for (size_t i = 0; i < params.size(); i++) {
                check_type_exists(expr.type_args[i], expr.spot);
                found[i] = expr.type_args[i];
            }
            if (errors.count() != errors_before) {
                for (auto& input : expr.inputs) check_expression(*input);
                return Type::Unknown;
            }
        }
        // An input whose type is already known is checked wanting it, so an empty [ ] works there
        std::vector<Type> given;
        for (size_t i = 0; i < expr.inputs.size(); i++) {
            Type wanted = Type::Nothing;
            if (i < pattern.inputs.size()) {
                const Type type = pattern.inputs[i].type;
                if (types_given) {
                    wanted = substitute(type, params, found);
                } else if (!uses_params(type, params)) {
                    wanted = type;
                }
            }
            given.push_back(check_wanting(*expr.inputs[i], wanted));
        }
        if (given.size() != pattern.inputs.size()) {
            errors.add(expr.spot, pattern.name + " takes " + count_words(pattern.inputs.size()) + ", but this gives it " + std::to_string(given.size()) + ".");
            return Type::Unknown;
        }
        if (!types_given) {
            // An input that gives back nothing can't tell what a type is, so that's the mistake to show
            for (size_t i = 0; i < given.size(); i++) {
                if (given[i] == Type::Nothing) {
                    errors.add(expr.inputs[i]->spot, "This doesn't give back a value, so it can't be an input.");
                    return Type::Unknown;
                }
            }
            for (size_t i = 0; i < given.size(); i++) work_out_types(pattern.inputs[i].type, given[i], params, found);
            for (size_t i = 0; i < params.size(); i++) {
                if (found[i] != Type::Nothing) continue;
                bool input_failed = false;
                for (const Type type : given) input_failed = input_failed || type == Type::Unknown;
                if (input_failed) return Type::Unknown;
                // An input of the wrong shape, like 5 for [T], says more than the missing type
                for (size_t j = 0; j < given.size(); j++) {
                    const Type wanted = pattern.inputs[j].type;
                    const bool wrong_shape = (program.types.is_array(wanted) && !program.types.is_array(given[j])) ||
                        (program.types.is_generic(wanted) && !(program.types.is_generic(given[j]) && program.types.base_of(given[j]) == program.types.base_of(wanted)));
                    if (wrong_shape && given[j] != Type::Nothing) {
                        errors.add(expr.inputs[j]->spot, "The input " + pattern.inputs[j].name + " must be " + with_article(wanted) + ", but this is " + with_article(given[j]) + ".");
                        return Type::Unknown;
                    }
                }
                errors.add(expr.spot, "Jamo can't work out " + type_name(params[i]) + " for " + pattern.name + " from its inputs.", "Give the types in < >, like " + pattern.name + "<int>(...)");
                return Type::Unknown;
            }
        }
        const std::string name = generic_name(pattern.name, found);
        const Function* made = make_generic_copy(pattern, found, name, expr.spot);
        if (!made) return Type::Unknown;
        expr.name = name;
        note_call(name);
        for (size_t i = 0; i < given.size(); i++) {
            if (given[i] == Type::Nothing) {
                errors.add(expr.inputs[i]->spot, "This doesn't give back a value, so it can't be an input.");
            } else if (given[i] != Type::Unknown && given[i] != made->inputs[i].type) {
                errors.add(expr.inputs[i]->spot, "The input " + made->inputs[i].name + " must be " + with_article(made->inputs[i].type) + ", but this is " + with_article(given[i]) + ".");
            }
        }
        return made->return_type;
    }

    void check_enums() {
        for (EnumDecl& decl : program.enums) {
            if (!claim_name(decl.name, decl.spot, "enum")) continue;
            enums[decl.name] = &decl;
            if (decl.members.empty()) errors.add(decl.spot, "An enum needs at least one member.");
            for (size_t i = 0; i < decl.members.size(); i++) {
                for (size_t j = 0; j < i; j++) {
                    if (decl.members[j] == decl.members[i]) errors.add(decl.member_spots[i], "There's already a member called " + decl.members[i] + " in this enum.");
                }
            }
            program.types.make_enum(decl.type, decl.members);
        }
    }

    void check_type_exists(Type type, Spot spot) {
        Type named = type;
        while (program.types.is_array(named)) named = program.types.element_of(named);
        if (program.types.is_function(named)) {
            const std::vector<Type> inputs = program.types.inputs_of(named);
            for (Type input : inputs) check_type_exists(input, spot);
            if (program.types.answer_of(named) != Type::Nothing) check_type_exists(program.types.answer_of(named), spot);
            return;
        }
        if (program.types.is_generic(named)) {
            make_generic_struct(named, spot);
            return;
        }
        if (program.types.is_known(type)) return;
        if (generic_structs.count(type_name(named))) {
            errors.add(spot, type_name(named) + " needs its types, like " + type_name(named) + "<int>.");
            return;
        }
        std::vector<std::string> options;
        for (const auto& item : structs) options.push_back(item.first);
        for (const auto& item : enums) options.push_back(item.first);
        for (const auto& item : generic_structs) options.push_back(item.first);
        const std::string close = closest(type_name(named), options);
        errors.add(spot, "There's no type called " + type_name(named) + ".", close.empty() ? "" : "Did you mean " + close + "?");
    }

    // A struct holding itself would need another inside that, forever. An array can start out empty, so it's fine
    void check_struct_isnt_endless(Type struct_type, const std::string& struct_name, Spot struct_spot) {
        std::vector<Type> to_visit = {struct_type};
        std::vector<Type> visited;
        while (!to_visit.empty()) {
            const Type type = to_visit.back();
            to_visit.pop_back();
            if (!program.types.is_struct(type) || std::find(visited.begin(), visited.end(), type) != visited.end()) continue;
            visited.push_back(type);
            for (const StructField& field : program.types.fields_of(type)) {
                if (field.type == struct_type) {
                    errors.add(struct_spot, struct_name + " can't contain itself, since each one would need another one inside it, forever.", "An array is fine, because it can be empty: [" + struct_name + "]");
                    return;
                }
                to_visit.push_back(field.type);
            }
        }
    }

    void check_extern(const Function& function) {
        if (function.name == "main") errors.add(function.spot, "main can't be an extern function.");
        if (function.inputs.size() > MOST_EXTERN_INPUTS) errors.add(function.spot, "An extern function can take up to 16 inputs.");
        for (const Input& input : function.inputs) {
            if (input.type != Type::Int && input.type != Type::Float && input.type != Type::Bool) {
                errors.add(input.spot, "An extern function's inputs can be int, i32, float, f32 or bool, but this is " + with_article(input.type) + ".", "Pass text with wide(text), and other things with memory( ) and poke.");
            }
        }
        const Type answer = function.return_type;
        if (answer != Type::Nothing && answer != Type::Int && answer != Type::Float && answer != Type::Bool) {
            errors.add(function.spot, "An extern function can answer with int, i32, float, f32 or bool, but this is " + with_article(answer) + ".");
        }
    }

    void check_main() {
        const auto found = functions.find("main");
        if (found == functions.end() && generic_functions.count("main")) return;
        if (found == functions.end()) {
            errors.add({1, 0, 1}, "This program has no main function, so it has nowhere to start.", "Add one:\n    func main()\n        print(42)\n    end");
            return;
        }
        const Function& function = *found->second;
        if (!function.inputs.empty()) errors.add(function.spot, "main can't have any inputs.");
        if (function.return_type != Type::Nothing) errors.add(function.spot, "main can't return a value.");
    }

    void check_function(Function& function) {
        if (!function.dll.empty()) {
            check_extern(function);
            return;
        }
        current = &function;
        user = function.name;
        const size_t errors_before = errors.count();
        next_slot = 0;
        loops_open = 0;
        scopes.clear();
        scopes.emplace_back();
        // Every copy of a generic would give the same warnings, so only its first copy gives them
        quiet = false;
        const auto pattern = copy_of.find(&function);
        if (pattern != copy_of.end()) {
            quiet = std::find(warned_patterns.begin(), warned_patterns.end(), pattern->second) != warned_patterns.end();
            if (!quiet) warned_patterns.push_back(pattern->second);
        }
        if (function.owner != Type::Nothing && !program.types.is_struct(function.owner)) {
            if (generic_structs.count(type_name(function.owner))) {
                errors.add(function.owner_spot, type_name(function.owner) + " has types, so its methods need them too.", "Like this: func " + type_name(function.owner) + "<T>." + function.name.substr(function.name.find('.') + 1) + "()");
                return;
            }
            errors.add(function.owner_spot, "There's no struct called " + type_name(function.owner) + ", so it can't have this method.");
            return;
        }
        check_type_exists(function.return_type, function.spot);
        for (const Input& input : function.inputs) {
            check_type_exists(input.type, input.spot);
            if (scopes.back().count(input.name)) {
                errors.add(input.spot, "There's already an input called " + input.name + ".");
            } else {
                scopes.back()[input.name] = {input.type, next_slot, function.owner != Type::Nothing && next_slot == 0, false, false, input.spot};
            }
            next_slot++;
        }
        check_statements(function.body);
        warn_about_unused_variables();
        function.slot_count = next_slot;
        if (function.return_type != Type::Nothing && !always_returns(function.body)) {
            errors.add(function.spot, function.name + " must return " + with_article(function.return_type) + " on every path, but it can reach its end without one.", "Add a return before its end. A while doesn't count, and an if needs an else that returns too.");
        }
        if (copy_of.count(&function) && errors.count() > errors_before) point_at_use(function.name);
    }

    // A mistake inside a copy of a generic, maybe in a library, also points at the line in the program's own code
    // that asked for it. A copy made by another copy follows that one back, until it reaches code that isn't a copy
    void point_at_use(const std::string& name) {
        std::string at = name;
        for (int steps = 0; steps < MOST_GENERIC_COPIES; steps++) {
            const auto found = made_from.find(at);
            if (found == made_from.end()) return;
            const auto maker = functions.find(found->second.first);
            if (maker == functions.end() || !copy_of.count(maker->second)) {
                if (std::find(pointed_at.begin(), pointed_at.end(), at) == pointed_at.end()) {
                    pointed_at.push_back(at);
                    errors.add(found->second.second, at + " doesn't work with these types, because of the mistake above.");
                }
                return;
            }
            at = found->second.first;
        }
    }

    void check_block(Block& block) {
        scopes.emplace_back();
        check_statements(block);
        warn_about_unused_variables();
        scopes.pop_back();
    }

    void check_statements(Block& block) {
        bool warned = false;
        for (size_t i = 0; i < block.size(); i++) {
            check_statement(*block[i]);
            if (!warned && i + 1 < block.size() && always_leaves(*block[i])) {
                if (!quiet) errors.warn(block[i + 1]->spot, "This code can never run, because the code above it always leaves first.");
                warned = true;
            }
        }
    }

    void warn_about_unused_variables() {
        if (quiet) return;
        for (const auto& item : scopes.back()) {
            if (item.second.warn_if_unused && !item.second.used) errors.warn(item.second.spot, item.first + " is never used.", "Use it, or remove it.");
        }
    }

    void check_statement(Stmt& stmt) {
        switch (stmt.kind) {
            case StmtKind::Var: check_var(stmt); break;
            case StmtKind::Assign: check_assign(stmt); break;
            case StmtKind::If:
                check_condition(*stmt.value, "if");
                check_block(stmt.body);
                for (ElifPart& part : stmt.elifs) {
                    check_condition(*part.condition, "elif");
                    check_block(part.body);
                }
                if (stmt.has_else) check_block(stmt.other_body);
                break;
            case StmtKind::While:
                check_condition(*stmt.value, "while");
                loops_open++;
                check_block(stmt.body);
                loops_open--;
                break;
            case StmtKind::For: check_for(stmt); break;
            case StmtKind::Match: check_match(stmt); break;
            case StmtKind::Try: check_try(stmt); break;
            case StmtKind::Break:
                if (loops_open == 0) errors.add(stmt.spot, "break can only go inside a loop.");
                break;
            case StmtKind::Continue:
                if (loops_open == 0) errors.add(stmt.spot, "continue can only go inside a loop.");
                break;
            case StmtKind::Return: check_return(stmt); break;
            case StmtKind::Call: check_expression(*stmt.value); break;
            case StmtKind::Print: {
                const Type type = check_expression(*stmt.value);
                if (type == Type::Nothing) errors.add(stmt.value->spot, "This doesn't give back a value, so there's nothing to print.");
                break;
            }
        }
    }

    void check_var(Stmt& stmt) {
        if (stmt.has_type) check_type_exists(stmt.declared_type, stmt.name_spot);
        // The value comes first, so var x = x means an outer x, or a mistake
        Type type = check_wanting(*stmt.value, stmt.has_type ? stmt.declared_type : Type::Nothing);
        if (type == Type::Nothing) {
            errors.add(stmt.value->spot, "This doesn't give back a value, so it can't go in a variable.");
            type = Type::Unknown;
        }
        if (stmt.has_type) {
            if (type != Type::Unknown && type != stmt.declared_type) {
                errors.add(stmt.value->spot, stmt.name + " is declared as " + with_article(stmt.declared_type) + ", but this value is " + with_article(type) + ".");
            }
            type = stmt.declared_type;
        }
        if (constants.count(stmt.name)) {
            errors.add(stmt.name_spot, stmt.name + " is already a constant, so a variable can't use that name.", "Pick a different name.");
            stmt.slot = next_slot++;
            return;
        }
        if (scopes.back().count(stmt.name)) {
            errors.add(stmt.name_spot, "There's already a variable called " + stmt.name + " in this block.", "Use " + stmt.name + " = ... to change it, or pick a different name.");
            stmt.slot = scopes.back()[stmt.name].slot;
            return;
        }
        stmt.slot = next_slot++;
        scopes.back()[stmt.name] = {type, stmt.slot, false, false, true, stmt.name_spot};
    }

    // The try part, then the catch part with its variable holding the error's words. A try keeps 8 slots for
    // jumping to its catch: the try before it, rbp, rsp, where the catch is, and r12 to r15
    void check_try(Stmt& stmt) {
        stmt.end_slot = next_slot;
        next_slot += TRY_SLOTS;
        check_block(stmt.body);
        scopes.emplace_back();
        if (!stmt.name.empty()) {
            stmt.slot = next_slot++;
            if (constants.count(stmt.name)) {
                errors.add(stmt.name_spot, stmt.name + " is already a constant, so a catch can't use that name.", "Pick a different name.");
            } else {
                scopes.back()[stmt.name] = {Type::String, stmt.slot, false, false, true, stmt.name_spot};
            }
        }
        check_statements(stmt.other_body);
        warn_about_unused_variables();
        scopes.pop_back();
    }

    void check_match(Stmt& stmt) {
        const Type type = check_expression(*stmt.value);
        const bool is_enum = program.types.is_enum(type);
        if (type == Type::Nothing) {
            errors.add(stmt.value->spot, "This doesn't give back a value, so it can't be matched.");
        } else if (type != Type::Unknown && !is_enum && type != Type::Int && type != Type::String) {
            errors.add(stmt.value->spot, "match works on enums, ints and strings, but this is " + with_article(type) + ".");
        }
        stmt.slot = next_slot++;
        std::map<std::string, int> seen;
        for (CasePart& part : stmt.cases) {
            const Type case_type = check_expression(*part.value);
            const std::string key = fixed_value_text(*part.value);
            if (key.empty()) {
                if (case_type != Type::Unknown) errors.add(part.value->spot, "A case needs a fixed value, like 1, \"a\" or Mood.CALM.", "Use if and elif to compare with anything else.");
            } else if (seen.count(key)) {
                errors.add(part.value->spot, "This case is already covered on line " + std::to_string(seen[key]) + ".");
            } else {
                seen[key] = part.value->spot.line;
            }
            if (case_type != Type::Unknown && type != Type::Unknown && case_type != type) {
                errors.add(part.value->spot, "This match is on " + with_article(type) + ", but this case is " + with_article(case_type) + ".");
            }
            check_block(part.body);
        }
        if (stmt.has_else) check_block(stmt.other_body);
        stmt.covers_all = stmt.has_else;
        if (!is_enum || stmt.has_else) return;
        stmt.covers_all = true;
        for (const std::string& member : program.types.members_of(type)) {
            if (!seen.count(type_name(type) + "." + member)) {
                errors.add(stmt.spot, "This match doesn't cover " + type_name(type) + "." + member + ".", "Add a case for it, or an else.");
            }
        }
    }

    // How a case's value reads, or nothing if it isn't fixed
    std::string fixed_value_text(const Expr& value) const {
        switch (value.kind) {
            case ExprKind::Number: return std::to_string(value.number);
            case ExprKind::String: return "\"" + value.text;
            case ExprKind::EnumValue: return type_name(value.type) + "." + program.types.members_of(value.type)[value.number];
            case ExprKind::Negate:
                if (value.left->kind == ExprKind::Number) return "-" + std::to_string(value.left->number);
                return "";
            default: return "";
        }
    }

    void check_range_end(Expr& value) {
        const Type type = check_expression(value);
        if (type == Type::Nothing) {
            errors.add(value.spot, "This doesn't give back a value, so it can't be used here.");
        } else if (type != Type::Int && type != Type::Unknown) {
            errors.add(value.spot, "A for loop's start and end must be ints, but this is " + with_article(type) + ".");
        }
    }

    void check_for(Stmt& stmt) {
        Type item_type = Type::Int;
        if (stmt.range_end) {
            check_range_end(*stmt.value);
            check_range_end(*stmt.range_end);
        } else {
            const Type type = check_expression(*stmt.value);
            item_type = Type::Unknown;
            if (program.types.is_array(type)) {
                item_type = program.types.element_of(type);
            } else if (type != Type::Unknown) {
                errors.add(stmt.value->spot, "A for loop needs an array or a range, but this is " + with_article(type) + ".", "Like this: for i in 0..10");
            }
            stmt.array_slot = next_slot++;
        }
        stmt.slot = next_slot++;
        stmt.end_slot = next_slot++;
        scopes.emplace_back();
        scopes.back()[stmt.name] = {item_type, stmt.slot, true, false, false, stmt.name_spot};
        loops_open++;
        check_statements(stmt.body);
        warn_about_unused_variables();
        loops_open--;
        scopes.pop_back();
    }

    void check_assign(Stmt& stmt) {
        if (stmt.target) {
            check_assign_to_target(stmt);
            return;
        }
        const Variable* variable = find_variable(stmt.name);
        const Type type = check_wanting(*stmt.value, variable ? variable->type : Type::Nothing);
        if (!variable && constants.count(stmt.name)) {
            errors.add(stmt.name_spot, stmt.name + " is a constant, so it can't be changed.");
            return;
        }
        if (!variable) {
            report_unknown_name(stmt.name, stmt.name_spot, "Use var to make a new variable: var " + stmt.name + " = ...");
            return;
        }
        stmt.slot = variable->slot;
        if (stmt.has_operation) {
            mark_used(stmt.name);
            note_global_read(variable->slot);
        }
        if (variable->read_only) {
            if (current->owner != Type::Nothing && stmt.name == "self" && stmt.slot == 0) {
                errors.add(stmt.name_spot, "self can't be changed, but its fields can.", "Like this: self.health = 1");
            } else {
                errors.add(stmt.name_spot, stmt.name + " is a for loop's variable, so it can't be changed.", "Copy it into a new variable first.");
            }
            return;
        }
        const bool joins_text = variable->type == Type::String && stmt.operation == BinaryOp::Add;
        if (stmt.has_operation && variable->type != Type::Unknown && !is_number(variable->type) && !joins_text) {
            errors.add(stmt.name_spot, "This only works on numbers, but " + stmt.name + " holds " + with_article(variable->type) + ".", variable->type == Type::String ? "Text only takes +=." : "");
            return;
        }
        if (stmt.has_operation && stmt.operation == BinaryOp::Divide && is_literal_zero(*stmt.value)) {
            errors.add(stmt.value->spot, "This divides by zero, which has no answer.");
        }
        if (type == Type::Nothing) {
            errors.add(stmt.value->spot, "This doesn't give back a value, so it can't go in a variable.");
        } else if (type != Type::Unknown && variable->type != Type::Unknown && type != variable->type) {
            errors.add(stmt.value->spot, stmt.name + " holds " + with_article(variable->type) + ", but this value is " + with_article(type) + ".");
        }
    }

    void check_assign_to_target(Stmt& stmt) {
        const Type wanted = check_expression(*stmt.target);
        const Type type = check_wanting(*stmt.value, wanted);
        if (stmt.target->kind == ExprKind::Index && stmt.target->left->type == Type::String) {
            errors.add(stmt.target->spot, "Strings can't be changed in place.", "Build a new string with + instead.");
            return;
        }
        if (stmt.has_operation && wanted != Type::Unknown && !is_number(wanted) && !(wanted == Type::String && stmt.operation == BinaryOp::Add)) {
            errors.add(stmt.target->spot, "This only works on numbers, but this holds " + with_article(wanted) + ".", wanted == Type::String ? "Text only takes +=." : "");
            return;
        }
        if (stmt.has_operation && stmt.operation == BinaryOp::Divide && is_literal_zero(*stmt.value)) {
            errors.add(stmt.value->spot, "This divides by zero, which has no answer.");
        }
        if (type == Type::Nothing) {
            errors.add(stmt.value->spot, "This doesn't give back a value, so it can't be stored.");
        } else if (type != Type::Unknown && wanted != Type::Unknown && type != wanted) {
            errors.add(stmt.value->spot, "This holds " + with_article(wanted) + ", but this value is " + with_article(type) + ".");
        }
    }

    void check_condition(Expr& condition, const std::string& word) {
        const Type type = check_expression(condition);
        if (type != Type::Bool && type != Type::Unknown) {
            errors.add(condition.spot, (word == "while" ? "A " : "An ") + word + " condition must be a bool, but this is " + with_article(type) + ".", type == Type::Int ? "Try comparing it, like x != 0." : "");
        }
    }

    void check_return(Stmt& stmt) {
        const Type wanted = current->return_type;
        if (!stmt.value) {
            if (wanted != Type::Nothing) errors.add(stmt.spot, current->name + " must return " + with_article(wanted) + " here.");
            return;
        }
        const Type type = check_wanting(*stmt.value, wanted);
        if (wanted == Type::Nothing) {
            errors.add(stmt.value->spot, current->name + " doesn't return a value.", "Add a return type to it, like func " + current->name + "(): int");
        } else if (type == Type::Nothing) {
            errors.add(stmt.value->spot, "This doesn't give back a value, so it can't be returned.");
        } else if (type != Type::Unknown && type != wanted) {
            errors.add(stmt.value->spot, current->name + " returns " + with_article(wanted) + ", but this value is " + with_article(type) + ".");
        }
    }

    // An empty [ ] only has a type if where it's going says what type that is
    Type check_wanting(Expr& expr, Type wanted) {
        if (expr.kind != ExprKind::ArrayLiteral) return check_expression(expr);
        expr.type = check_array_literal(expr, wanted);
        return expr.type;
    }

    Type check_array_literal(Expr& expr, Type wanted) {
        const bool knows_type = program.types.is_array(wanted);
        if (expr.inputs.empty()) {
            if (knows_type) return wanted;
            errors.add(expr.spot, "An empty [ ] needs a type.", "Like this: var items: [int] = []");
            return Type::Unknown;
        }
        const Type wanted_item = knows_type ? program.types.element_of(wanted) : Type::Nothing;
        Type item_type = Type::Unknown;
        for (auto& item : expr.inputs) {
            const Type type = check_wanting(*item, item_type == Type::Unknown ? wanted_item : item_type);
            if (type == Type::Nothing) {
                errors.add(item->spot, "This doesn't give back a value, so it can't go in an array.");
            } else if (item_type == Type::Unknown) {
                item_type = type;
            } else if (type != Type::Unknown && type != item_type) {
                errors.add(item->spot, "All the items in an array share one type, but this is " + with_article(type) + " and the first is " + with_article(item_type) + ".");
            }
        }
        return item_type == Type::Unknown ? Type::Unknown : program.types.array_of(item_type);
    }

    Type check_expression(Expr& expr) {
        expr.type = work_out_type(expr);
        return expr.type;
    }

    Type work_out_type(Expr& expr) {
        switch (expr.kind) {
            case ExprKind::Number:
                if (expr.number > LARGEST_POSITIVE) {
                    errors.add(expr.spot, "This number is too large for 64 bits.", "The largest int is 9223372036854775807.");
                    return Type::Unknown;
                }
                return Type::Int;
            case ExprKind::Float:
                return Type::Float;
            case ExprKind::Bool:
                return Type::Bool;
            case ExprKind::String:
                return Type::String;
            case ExprKind::FString:
                for (auto& part : expr.inputs) {
                    if (check_expression(*part) == Type::Nothing) errors.add(part->spot, "This doesn't give back a value, so it can't go in an f-string.");
                }
                return Type::String;
            case ExprKind::ArrayLiteral:
                return check_array_literal(expr, Type::Nothing);
            case ExprKind::MethodCall:
                return check_method_call(expr);
            case ExprKind::Field:
                return check_field(expr);
            case ExprKind::StructLiteral:
                return check_struct_literal(expr);
            case ExprKind::EnumValue:
            case ExprKind::BuiltinCall:
            case ExprKind::FunctionValue:
            case ExprKind::CallValue:
                return expr.type;
            case ExprKind::Index:
                return check_index(expr);
            case ExprKind::Length:
                return check_length(expr);
            case ExprKind::Convert:
                return check_convert(expr);
            case ExprKind::Name:
                return check_name(expr);
            case ExprKind::Negate:
                return check_negate(expr);
            case ExprKind::Not:
                check_needs_bool(*expr.left, "not");
                return Type::Bool;
            case ExprKind::Binary:
                return check_binary(expr);
            case ExprKind::Call:
                return check_call(expr);
        }
        return Type::Unknown;
    }

    Type check_name(Expr& expr) {
        const Variable* variable = find_variable(expr.name);
        if (variable) {
            mark_used(expr.name);
            expr.slot = variable->slot;
            note_global_read(variable->slot);
            return variable->type;
        }
        const auto later = global_order.find(expr.name);
        if (setting_up >= 0 && later != global_order.end() && static_cast<int>(later->second) == setting_up) {
            errors.add(expr.spot, expr.name + " can't use itself while it's being set up.");
            return Type::Unknown;
        }
        if (setting_up >= 0 && later != global_order.end() && static_cast<int>(later->second) > setting_up) {
            errors.add(expr.spot, expr.name + " isn't set up yet here.", "Variables outside functions are set up in order, top to bottom, so move " + expr.name + " above this one.");
            return Type::Unknown;
        }
        if (constants.count(expr.name)) {
            ConstDecl& decl = *constants[expr.name];
            if (!check_constant(decl)) return Type::Unknown;
            const Spot spot = expr.spot;
            expr = copy_constant(*decl.value);
            set_spot(expr, spot);
            return expr.type;
        }
        if (enums.count(expr.name)) {
            errors.add(expr.spot, expr.name + " is an enum, so it needs one of its members.", "Like this: " + expr.name + "." + (enums[expr.name]->members.empty() ? "VALUE" : enums[expr.name]->members.front()));
            return Type::Unknown;
        }
        const auto function = functions.find(expr.name);
        if (function != functions.end()) {
            if (!function->second->dll.empty()) {
                errors.add(expr.spot, expr.name + " is a Windows function, so it can't be used as a value.", "Write a Jamo function that calls it, and use that.");
                return Type::Unknown;
            }
            note_call(function->second->name);
            std::vector<Type> inputs;
            for (const Input& input : function->second->inputs) inputs.push_back(input.type);
            expr.kind = ExprKind::FunctionValue;
            expr.text = expr.name;
            return program.types.function_of(inputs, function->second->return_type);
        }
        if (generic_functions.count(expr.name)) {
            errors.add(expr.spot, expr.name + " is generic, so it can't be used as a value.", "Write a function for the types you need that calls it, and use that.");
            return Type::Unknown;
        }
        report_unknown_name(expr.name, expr.spot);
        return Type::Unknown;
    }

    Type check_negate(Expr& expr) {
        Expr& value = *expr.left;
        // -9223372036854775808 is the only place that number is allowed
        if (value.kind == ExprKind::Number && value.number == LARGEST_POSITIVE + 1) {
            value.type = Type::Int;
            return Type::Int;
        }
        const Type type = check_expression(value);
        if (type == Type::Unknown) return Type::Unknown;
        if (!is_number(type)) {
            errors.add(value.spot, "A minus sign needs a number, but this is " + with_article(type) + ".");
            return Type::Unknown;
        }
        return type;
    }

    Type check_enum_value(Expr& expr, const EnumDecl& decl) {
        const auto found = std::find(decl.members.begin(), decl.members.end(), expr.name);
        if (found == decl.members.end()) {
            const std::string close = closest(expr.name, decl.members);
            errors.add(expr.spot, decl.name + " has no member called " + expr.name + ".", close.empty() ? "" : "Did you mean " + close + "?");
            return Type::Unknown;
        }
        expr.kind = ExprKind::EnumValue;
        expr.number = static_cast<uint64_t>(found - decl.members.begin());
        return decl.type;
    }

    Type check_field(Expr& expr) {
        if (expr.left->kind == ExprKind::Name && !find_variable(expr.left->name) && enums.count(expr.left->name)) {
            return check_enum_value(expr, *enums[expr.left->name]);
        }
        const Type type = check_expression(*expr.left);
        if (type == Type::Unknown) return Type::Unknown;
        if (!program.types.is_struct(type)) {
            errors.add(expr.left->spot, "Only structs have fields, but this is " + with_article(type) + ".");
            return Type::Unknown;
        }
        const std::vector<StructField>& fields = program.types.fields_of(type);
        const int number = field_number(fields, expr.name);
        if (number < 0) {
            report_unknown_field(type, expr.name, expr.spot);
            return Type::Unknown;
        }
        expr.slot = number;
        return fields[static_cast<size_t>(number)].type;
    }

    static int field_number(const std::vector<StructField>& fields, const std::string& name) {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    void report_unknown_field(Type type, const std::string& name, Spot spot) {
        std::vector<std::string> options;
        for (const StructField& field : program.types.fields_of(type)) options.push_back(field.name);
        const std::string close = closest(name, options);
        errors.add(spot, type_name(type) + " has no field called " + name + ".", close.empty() ? "" : "Did you mean " + close + "?");
    }

    Type check_struct_method(Expr& expr, Type type) {
        const std::string prefix = type_name(type) + ".";
        auto found = functions.find(prefix + expr.name);
        if (found == functions.end() && program.types.is_generic(type)) {
            // A generic struct's method gets its copy for these types the first time it's called
            const std::string base = program.types.base_of(type);
            const auto pattern = generic_methods.find(base + "." + expr.name);
            if (pattern != generic_methods.end()) {
                const std::vector<Type> args = program.types.args_of(type);
                if (!make_generic_copy(*pattern->second, args, prefix + expr.name, expr.spot)) {
                    for (auto& input : expr.inputs) check_expression(*input);
                    return Type::Unknown;
                }
                found = functions.find(prefix + expr.name);
            }
        }
        if (found == functions.end() && program.types.is_struct(type)) {
            const std::vector<StructField>& fields = program.types.fields_of(type);
            for (size_t i = 0; i < fields.size(); i++) {
                if (fields[i].name != expr.name || !program.types.is_function(fields[i].type)) continue;
                auto field = std::make_unique<Expr>();
                field->kind = ExprKind::Field;
                field->spot = expr.spot;
                field->name = expr.name;
                field->left = std::move(expr.left);
                field->slot = static_cast<int>(i);
                field->type = fields[i].type;
                field->depth = field->left->depth + 1;
                expr.left = std::move(field);
                return check_value_call(expr, fields[i].type);
            }
        }
        if (found == functions.end()) {
            for (auto& input : expr.inputs) check_expression(*input);
            std::vector<std::string> options;
            for (const auto& item : functions) {
                if (item.first.rfind(prefix, 0) == 0) options.push_back(item.first.substr(prefix.size()));
            }
            if (program.types.is_generic(type)) {
                const std::string generic_prefix = program.types.base_of(type) + ".";
                for (const auto& item : generic_methods) {
                    if (item.first.rfind(generic_prefix, 0) == 0) options.push_back(item.first.substr(generic_prefix.size()));
                }
                std::sort(options.begin(), options.end());
            }
            const std::string close = closest(expr.name, options);
            errors.add(expr.spot, type_name(type) + " has no function called " + expr.name + ".", close.empty() ? "" : "Did you mean " + close + "?");
            return Type::Unknown;
        }
        const Function& function = *found->second;
        note_call(function.name);
        const size_t wanted = function.inputs.size() - 1;
        if (expr.inputs.size() != wanted) {
            for (auto& input : expr.inputs) check_expression(*input);
            errors.add(expr.spot, expr.name + " takes " + count_words(wanted) + ", but this gives it " + std::to_string(expr.inputs.size()) + ".");
            return function.return_type;
        }
        for (size_t i = 0; i < wanted; i++) {
            const Input& input = function.inputs[i + 1];
            const Type given = check_wanting(*expr.inputs[i], input.type);
            if (given == Type::Nothing) {
                errors.add(expr.inputs[i]->spot, "This doesn't give back a value, so it can't be an input.");
            } else if (given != Type::Unknown && given != input.type) {
                errors.add(expr.inputs[i]->spot, "The input " + input.name + " must be " + with_article(input.type) + ", but this is " + with_article(given) + ".");
            }
        }
        return function.return_type;
    }

    Type check_struct_literal(Expr& expr) {
        if (generic_structs.count(expr.name)) {
            if (expr.type_args.empty()) {
                for (auto& value : expr.inputs) check_expression(*value);
                errors.add(expr.spot, expr.name + " needs its types, like " + expr.name + "<int>{...}.");
                return Type::Unknown;
            }
            const Type type = program.types.generic(expr.name, expr.type_args);
            check_type_exists(type, expr.spot);
            if (!program.types.is_struct(type)) {
                for (auto& value : expr.inputs) check_expression(*value);
                return Type::Unknown;
            }
            return check_struct_fields(expr, type);
        }
        const auto found = structs.find(expr.name);
        if (found != structs.end() && !expr.type_args.empty()) {
            for (auto& value : expr.inputs) check_expression(*value);
            errors.add(expr.spot, expr.name + " has no types, so it can't be given any.");
            return Type::Unknown;
        }
        if (found == structs.end()) {
            for (auto& value : expr.inputs) check_expression(*value);
            std::vector<std::string> options;
            for (const auto& item : structs) options.push_back(item.first);
            for (const auto& item : generic_structs) options.push_back(item.first);
            std::sort(options.begin(), options.end());
            const std::string close = closest(expr.name, options);
            errors.add(expr.spot, "There's no struct called " + expr.name + ".", close.empty() ? "" : "Did you mean " + close + "?");
            return Type::Unknown;
        }
        return check_struct_fields(expr, found->second->type);
    }

    Type check_struct_fields(Expr& expr, Type type) {
        const std::vector<StructField> fields = program.types.fields_of(type);
        std::vector<bool> given(fields.size(), false);
        for (size_t i = 0; i < expr.inputs.size(); i++) {
            const int number = field_number(fields, expr.field_names[i]);
            expr.field_numbers.push_back(number);
            if (number < 0) {
                check_expression(*expr.inputs[i]);
                report_unknown_field(type, expr.field_names[i], expr.field_spots[i]);
                continue;
            }
            const StructField& field = fields[static_cast<size_t>(number)];
            if (given[static_cast<size_t>(number)]) errors.add(expr.field_spots[i], "This sets " + field.name + " twice.");
            given[static_cast<size_t>(number)] = true;
            const Type value = check_wanting(*expr.inputs[i], field.type);
            if (value == Type::Nothing) {
                errors.add(expr.inputs[i]->spot, "This doesn't give back a value, so it can't go in a field.");
            } else if (value != Type::Unknown && value != field.type) {
                errors.add(expr.inputs[i]->spot, "The field " + field.name + " must be " + with_article(field.type) + ", but this is " + with_article(value) + ".");
            }
        }
        for (size_t i = 0; i < fields.size(); i++) {
            if (!given[i]) errors.add(expr.spot, expr.name + " needs every field, but this leaves out " + fields[i].name + ".");
        }
        return type;
    }

    Type check_index(Expr& expr) {
        const Type type = check_expression(*expr.left);
        const Type index = check_expression(*expr.right);
        if (index != Type::Int && index != Type::Unknown) {
            errors.add(expr.right->spot, "An index must be an int, but this is " + with_article(index) + ".");
        }
        if (program.types.is_array(type)) return program.types.element_of(type);
        if (type != Type::String && type != Type::Unknown) {
            errors.add(expr.left->spot, "Only strings and arrays can use [ ], but this is " + with_article(type) + ".");
            return Type::Unknown;
        }
        return type;
    }

    Type check_method_call(Expr& expr) {
        const Type type = check_expression(*expr.left);
        if (type == Type::Unknown) {
            for (auto& input : expr.inputs) check_expression(*input);
            return Type::Unknown;
        }
        if (program.types.is_struct(type)) return check_struct_method(expr, type);
        if (!program.types.is_array(type)) {
            errors.add(expr.spot, type_name(type) + " has no function called " + expr.name + ".");
            for (auto& input : expr.inputs) check_expression(*input);
            return Type::Unknown;
        }
        const Type item = program.types.element_of(type);
        if (expr.name == "pop") {
            if (!expr.inputs.empty()) errors.add(expr.spot, "pop takes no inputs.");
            return item;
        }
        if (expr.name != "add") {
            errors.add(expr.spot, "Arrays have no function called " + expr.name + ".", "They have add and pop.");
            return Type::Unknown;
        }
        if (expr.inputs.size() != 1) {
            errors.add(expr.spot, "add takes 1 input, but this gives it " + std::to_string(expr.inputs.size()) + ".");
            return Type::Nothing;
        }
        const Type given = check_wanting(*expr.inputs[0], item);
        if (given == Type::Nothing) {
            errors.add(expr.inputs[0]->spot, "This doesn't give back a value, so it can't be an input.");
        } else if (given != Type::Unknown && given != item) {
            errors.add(expr.inputs[0]->spot, "This array holds " + with_article(item) + ", but this is " + with_article(given) + ".");
        }
        return Type::Nothing;
    }

    Type check_length(Expr& expr) {
        const Type type = check_expression(*expr.left);
        if (type == Type::Nothing) {
            errors.add(expr.left->spot, "This doesn't give back a value, so it can't be used here.");
        } else if (type != Type::String && type != Type::Unknown && !program.types.is_array(type)) {
            errors.add(expr.left->spot, "len needs a string or an array, but this is " + with_article(type) + ".");
        }
        return Type::Int;
    }

    Type check_convert(Expr& expr) {
        const Type type = check_expression(*expr.left);
        const Type target = expr.convert_to;
        const std::string word = type_name(target);
        if (type == Type::Nothing) {
            errors.add(expr.left->spot, "This doesn't give back a value, so it can't be converted.");
        } else if (type == target) {
            errors.add(expr.left->spot, "This is already " + with_article(target) + ", so it doesn't need " + word + "( ).");
        } else if (type != Type::Unknown && target != Type::String && !is_number(type) && type != Type::String) {
            errors.add(expr.left->spot, word + "( ) can't convert " + with_article(type) + ".");
        }
        return target;
    }

    void check_needs_bool(Expr& value, const std::string& word) {
        const Type type = check_expression(value);
        if (type == Type::Nothing) {
            errors.add(value.spot, "This doesn't give back a value, so it can't be used here.");
        } else if (type != Type::Bool && type != Type::Unknown) {
            errors.add(value.spot, word + " needs a bool, but this is " + with_article(type) + ".", type == Type::Int ? "Try comparing it, like x != 0." : "");
        }
    }

    Type check_binary(Expr& expr) {
        if (expr.op == BinaryOp::And || expr.op == BinaryOp::Or) {
            const std::string word = expr.op == BinaryOp::And ? "and" : "or";
            check_needs_bool(*expr.left, word);
            check_needs_bool(*expr.right, word);
            return Type::Bool;
        }
        const Type left = check_expression(*expr.left);
        const Type right = check_expression(*expr.right);
        if ((expr.op == BinaryOp::Divide || expr.op == BinaryOp::Remainder) && is_literal_zero(*expr.right)) {
            errors.add(expr.right->spot, "This divides by zero, which has no answer.");
        }
        if (left == Type::Nothing || right == Type::Nothing) {
            errors.add((left == Type::Nothing ? expr.left : expr.right)->spot, "This doesn't give back a value, so it can't be used here.");
            return Type::Unknown;
        }
        const bool is_equality = expr.op == BinaryOp::Equal || expr.op == BinaryOp::NotEqual;
        const bool is_comparison = is_equality || is_order_comparison(expr);
        const bool joins_strings = expr.op == BinaryOp::Add && (left == Type::String || right == Type::String);
        const Type answer = is_comparison ? Type::Bool : joins_strings ? Type::String : (left == Type::Float || right == Type::Float) ? Type::Float : Type::Int;
        if (left == Type::Unknown || right == Type::Unknown) return answer;
        if (is_equality) {
            if (left == right && program.types.is_array(left)) errors.add(expr.spot, "Arrays can't be compared with == or !=.", "Compare their items in a loop.");
            if (left == right && program.types.is_struct(left)) errors.add(expr.spot, "Structs can't be compared with == or !=.", "Compare their fields.");
            if (left == right && program.types.is_function(left)) errors.add(expr.spot, "Function values can't be compared with == or !=.");
            if (left != right) errors.add(expr.spot, "== and != need both sides to be the same type, but this compares " + type_name(left) + " and " + type_name(right) + ".", is_number(left) && is_number(right) ? MATCH_NUMBERS : "");
            return Type::Bool;
        }
        if (joins_strings) {
            const bool left_is_wrong = left != Type::String;
            if (left_is_wrong || right != Type::String) {
                errors.add((left_is_wrong ? expr.left : expr.right)->spot, "+ can only join a string to another string, but this is " + with_article(left_is_wrong ? left : right) + ".", "Use string(x) to convert it.");
            }
            return Type::String;
        }
        if (is_order_comparison(expr) && is_order_comparison(*expr.left)) {
            errors.add(expr.spot, "This can't compare in a chain, because the first comparison gives a bool.", "Compare one pair at a time, in separate steps.");
            return Type::Bool;
        }
        if (is_number(left) && is_number(right) && left != right) {
            errors.add(expr.spot, "This mixes an int and a float, which Jamo doesn't do by itself.", MATCH_NUMBERS);
        }
        if (!is_number(left)) errors.add(expr.left->spot, "This operator needs a number here, but this is " + with_article(left) + ".");
        if (!is_number(right)) errors.add(expr.right->spot, "This operator needs a number here, but this is " + with_article(right) + ".");
        return answer;
    }

    Type check_call(Expr& expr) {
        if (const Variable* variable = find_variable(expr.name); variable && program.types.is_function(variable->type) && expr.type_args.empty()) {
            auto callee = std::make_unique<Expr>();
            callee->kind = ExprKind::Name;
            callee->spot = expr.spot;
            callee->name = expr.name;
            callee->depth = 1;
            check_expression(*callee);
            expr.left = std::move(callee);
            return check_value_call(expr, variable->type);
        }
        if (expr.name == "callback" && !find_variable(expr.name)) return check_callback(expr);
        const auto generic = generic_functions.find(expr.name);
        if (generic != generic_functions.end() && !find_variable(expr.name)) return check_generic_call(expr, *generic->second);
        if (!expr.type_args.empty() && !find_variable(expr.name)) {
            for (auto& input : expr.inputs) check_expression(*input);
            if (functions.count(expr.name) || is_builtin(expr.name)) {
                errors.add(expr.spot, expr.name + " has no types, so it can't be given any.");
            } else {
                report_unknown_function(expr);
            }
            return Type::Unknown;
        }
        std::vector<Type> given;
        const auto named = functions.find(expr.name);
        for (size_t i = 0; i < expr.inputs.size(); i++) {
            const bool has_wanted = named != functions.end() && i < named->second->inputs.size();
            given.push_back(check_wanting(*expr.inputs[i], has_wanted ? named->second->inputs[i].type : Type::Nothing));
        }
        if (find_variable(expr.name)) {
            errors.add(expr.spot, expr.name + " is a variable, so it can't be called like a function.", expr.type_args.empty() ? "" : "A < with a > and ( after it reads as types, like first<int>(...). To compare, take away the ( ) after the >.");
            return Type::Unknown;
        }
        if (expr.name == "hash_of") return check_hash_of(expr, given);
        if (is_builtin(expr.name)) return check_builtin(expr, given);
        const auto found = functions.find(expr.name);
        if (found == functions.end()) {
            report_unknown_function(expr);
            return Type::Unknown;
        }
        const Function& function = *found->second;
        note_call(function.name);
        if (given.size() != function.inputs.size()) {
            errors.add(expr.spot, function.name + " takes " + count_words(function.inputs.size()) + ", but this gives it " + std::to_string(given.size()) + ".");
            return function.return_type;
        }
        for (size_t i = 0; i < given.size(); i++) {
            if (given[i] == Type::Nothing) {
                errors.add(expr.inputs[i]->spot, "This doesn't give back a value, so it can't be an input.");
            } else if (given[i] != Type::Unknown && given[i] != function.inputs[i].type) {
                errors.add(expr.inputs[i]->spot, "The input " + function.inputs[i].name + " must be " + with_article(function.inputs[i].type) + ", but this is " + with_article(given[i]) + ".");
            }
        }
        return function.return_type;
    }

    // A call through a function value in expr.left, which is already checked
    Type check_value_call(Expr& expr, Type function) {
        expr.kind = ExprKind::CallValue;
        const std::vector<Type> inputs = program.types.inputs_of(function);
        std::vector<Type> given;
        for (size_t i = 0; i < expr.inputs.size(); i++) given.push_back(check_wanting(*expr.inputs[i], i < inputs.size() ? inputs[i] : Type::Nothing));
        const std::string what = expr.left->kind == ExprKind::Field ? "The field " + expr.name : expr.name;
        if (given.size() != inputs.size()) {
            errors.add(expr.spot, what + " takes " + count_words(inputs.size()) + ", but this gives it " + std::to_string(given.size()) + ".");
            return program.types.answer_of(function);
        }
        for (size_t i = 0; i < given.size(); i++) {
            if (given[i] == Type::Nothing) {
                errors.add(expr.inputs[i]->spot, "This doesn't give back a value, so it can't be an input.");
            } else if (given[i] != Type::Unknown && given[i] != inputs[i]) {
                errors.add(expr.inputs[i]->spot, "Input " + std::to_string(i + 1) + " of " + expr.name + " must be " + with_article(inputs[i]) + ", but this is " + with_article(given[i]) + ".");
            }
        }
        return program.types.answer_of(function);
    }

    static bool is_builtin(const std::string& name) {
        static const char* const names[] = {"input", "args", "read_file", "write_file", "exists", "fail", "byte", "callback", "wide", "memory", "memory_text",
            "peek_i64", "peek_i32", "peek_u16", "peek_u8", "peek_f64", "peek_f32", "poke_i64", "poke_i32", "poke_u16", "poke_u8", "poke_f64", "poke_f32", "hash_of"};
        return std::find(std::begin(names), std::end(names), name) != std::end(names);
    }

    // callback(f) gives Windows the address of a Jamo function to call back
    Type check_callback(Expr& expr) {
        expr.kind = ExprKind::BuiltinCall;
        if (expr.inputs.size() != 1 || expr.inputs[0]->kind != ExprKind::Name) {
            errors.add(expr.spot, "callback takes the name of one function.", "Like this: callback(on_message)");
            return Type::Int;
        }
        const std::string& name = expr.inputs[0]->name;
        const auto found = functions.find(name);
        if (found == functions.end() || !found->second->dll.empty()) {
            errors.add(expr.inputs[0]->spot, "There's no Jamo function called " + name + ".");
            return Type::Int;
        }
        const Function& function = *found->second;
        if (function.inputs.size() > MOST_CALLBACK_INPUTS) errors.add(expr.inputs[0]->spot, name + " takes " + count_words(function.inputs.size()) + ", but a callback can take up to 4.");
        for (const Input& input : function.inputs) {
            if (input.type != Type::Int && input.type != Type::Bool) errors.add(expr.inputs[0]->spot, "A callback's inputs must be ints or bools, but " + input.name + " is " + with_article(input.type) + ".");
        }
        if (function.return_type != Type::Nothing && function.return_type != Type::Int && function.return_type != Type::Bool) errors.add(expr.inputs[0]->spot, "A callback can answer with int or bool, but " + name + " answers with " + with_article(function.return_type) + ".");
        expr.text = name;
        expr.inputs.clear();
        return Type::Int;
    }

    // The type lists are built whole, since assigning { } lists here gives g++ 16 false -Wstringop-overflow warnings
    Type check_builtin(Expr& expr, const std::vector<Type>& given) {
        expr.kind = ExprKind::BuiltinCall;
        std::vector<std::string> wanted;
        Type answer = Type::String;
        std::vector<Type> types;
        const std::string& name = expr.name;
        if (name == "args") answer = program.types.array_of(Type::String);
        if (name == "read_file" || name == "exists") wanted = {"path"};
        if (name == "write_file") wanted = {"path", "text"};
        if (name == "write_file") answer = Type::Nothing;
        if (name == "exists") answer = Type::Bool;
        if (name == "byte") {
            wanted = {"text", "index"};
            types = std::vector<Type>{Type::String, Type::Int};
            answer = Type::Int;
        }
        if (name == "fail") {
            wanted = {"message"};
            answer = Type::Nothing;
        }
        if (name == "wide") {
            wanted = {"text"};
            answer = Type::Int;
        }
        if (name == "memory_text") {
            wanted = {"address", "bytes"};
            types = std::vector<Type>{Type::Int, Type::Int};
            answer = Type::String;
        }
        if (name == "memory") {
            wanted = {"bytes"};
            types = std::vector<Type>{Type::Int};
            answer = Type::Int;
        }
        const bool is_float = name.size() > 7 && name.substr(name.size() - 3, 1) == "f";
        if (name.rfind("peek_", 0) == 0) {
            wanted = {"address", "offset"};
            types = std::vector<Type>{Type::Int, Type::Int};
            answer = is_float ? Type::Float : Type::Int;
        }
        if (name.rfind("poke_", 0) == 0) {
            wanted = {"address", "offset", "value"};
            types = std::vector<Type>{Type::Int, Type::Int, is_float ? Type::Float : Type::Int};
            answer = Type::Nothing;
        }
        if (given.size() != wanted.size()) {
            errors.add(expr.spot, name + " takes " + count_words(wanted.size()) + ", but this gives it " + std::to_string(given.size()) + ".");
            return answer;
        }
        for (size_t i = 0; i < given.size(); i++) {
            const Type type = types.empty() ? Type::String : types[i];
            if (given[i] != type && given[i] != Type::Unknown) {
                errors.add(expr.inputs[i]->spot, "The input " + wanted[i] + " must be " + with_article(type) + ", but this is " + with_article(given[i]) + ".");
            }
        }
        return answer;
    }

    // hash_of(key) gives a number made from a key, for hash tables like Map. It's never below 0
    Type check_hash_of(Expr& expr, const std::vector<Type>& given) {
        expr.kind = ExprKind::BuiltinCall;
        if (given.size() != 1) {
            errors.add(expr.spot, "hash_of takes 1 input, but this gives it " + std::to_string(given.size()) + ".");
        } else if (given[0] != Type::Int && given[0] != Type::String && given[0] != Type::Bool && given[0] != Type::Unknown && !program.types.is_enum(given[0])) {
            errors.add(expr.inputs[0]->spot, "hash_of takes an int, string, bool or enum, but this is " + with_article(given[0]) + ".");
        }
        return Type::Int;
    }

    static std::string count_words(size_t count) {
        return std::to_string(count) + (count == 1 ? " input" : " inputs");
    }

    void mark_used(const std::string& name) {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) {
                found->second.used = true;
                return;
            }
        }
    }

    const Variable* find_variable(const std::string& name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return &found->second;
        }
        const auto global = global_places.find(name);
        if (global != global_places.end()) return &global_vars[global->second];
        return nullptr;
    }

    void report_unknown_name(const std::string& name, Spot spot, const std::string& fallback = "") {
        std::vector<std::string> options;
        for (const auto& scope : scopes) {
            for (const auto& item : scope) options.push_back(item.first);
        }
        for (const auto& item : global_places) options.push_back(item.first);
        const std::string close = closest(name, options);
        errors.add(spot, "nothing has this name: " + name, close.empty() ? fallback : "Did you mean " + close + "?");
    }

    void report_unknown_function(const Expr& expr) {
        std::vector<std::string> options;
        for (const auto& item : functions) {
            if (item.first.find('<') == std::string::npos) options.push_back(item.first);
        }
        for (const auto& item : generic_functions) options.push_back(item.first);
        std::sort(options.begin(), options.end());
        const std::string close = closest(expr.name, options);
        errors.add(expr.spot, "There's no function called " + expr.name + ".", close.empty() ? "" : "Did you mean " + close + "?");
    }

    static std::string closest(const std::string& name, const std::vector<std::string>& options) {
        std::string best;
        int best_score = MOST_DIFFERENT_LETTERS + 1;
        for (const std::string& option : options) {
            const int score = letters_different(name, option);
            if (score < best_score && score < static_cast<int>(std::max(name.size(), option.size()))) {
                best = option;
                best_score = score;
            }
        }
        return best;
    }

    Program& program;
    Errors& errors;
    std::map<std::string, std::pair<std::string, Spot>> names;
    std::map<std::string, Function*> functions;
    std::map<std::string, ConstDecl*> constants;
    std::map<std::string, int> constant_state;  // 1 while being checked, 2 when fine, 3 when it has a mistake
    std::map<std::string, StructDecl*> structs;
    std::map<std::string, EnumDecl*> enums;
    std::map<std::string, StructDecl*> generic_structs;
    std::map<std::string, Function*> generic_functions;  // first<T>
    std::map<std::string, Function*> generic_methods;    // Box.get, for Box<T>
    std::vector<std::unique_ptr<Function>> generic_templates;
    std::map<const Function*, const Function*> copy_of;  // Each copy's generic
    std::vector<const Function*> warned_patterns;
    int copies_made = 0;
    std::map<std::string, std::pair<std::string, Spot>> made_from;  // Each copy: the function that asked for it, and where
    std::vector<std::string> pointed_at;
    Function* setup = nullptr;                           // The function that sets up the variables outside functions
    std::deque<Variable> global_vars;
    std::map<std::string, size_t> global_places;         // The ones set up so far
    std::map<std::string, size_t> global_order;          // Every one, by its place in order
    int setting_up = -1;                                 // Which one's value is being checked
    std::string user;                                    // Whose calls and reads of variables outside functions are noted
    std::map<std::string, std::vector<std::string>> calls_by;
    std::map<std::string, std::vector<int>> globals_read_by;
    bool quiet = false;                                  // A later copy of a generic, which gives no warnings
    std::vector<std::map<std::string, Variable>> scopes;
    Function* current = nullptr;
    int next_slot = 0;
    int loops_open = 0;
};

}  // namespace

void check_program(Program& program, Errors& errors) {
    Checker checker(program, errors);
    checker.check();
}
