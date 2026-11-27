// AST: the structure of a Jamo program, as a tree.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "errors.hpp"

// Unknown is for a value that already has an error, so one mistake doesn't cause a pile of others
// Types past FirstCustom are looked up in the TypeTable, so types can still be compared with ==
enum class Type : int { Nothing, Int, Float, Bool, String, Unknown, FirstCustom };

struct StructField {
    std::string name;
    Spot spot;
    Type type = Type::Int;
};

class TypeTable {
public:
    Type array_of(Type element) {
        for (size_t i = 0; i < entries.size(); i++) {
            if (entries[i].is_array && entries[i].element == element) return custom(i);
        }
        Entry entry;
        entry.is_array = true;
        entry.element = element;
        entries.push_back(entry);
        return custom(entries.size() - 1);
    }

    // A type written by name. The checker finds out later what it is, if it's anything
    Type named(const std::string& name) {
        for (size_t i = 0; i < entries.size(); i++) {
            if (!entries[i].is_array && !entries[i].is_function && entries[i].name == name) return custom(i);
        }
        Entry entry;
        entry.name = name;
        entries.push_back(entry);
        return custom(entries.size() - 1);
    }

    // A generic struct given its types, like Pair<string, int>. Its name is written out in full, so the same
    // types always give the same entry. The checker fills in its fields once every type is known
    Type generic(const std::string& base, const std::vector<Type>& args) {
        std::string full = base + "<";
        for (size_t i = 0; i < args.size(); i++) full += (i > 0 ? ", " : "") + name(args[i]);
        full += ">";
        for (size_t i = 0; i < entries.size(); i++) {
            if (!entries[i].is_array && entries[i].name == full) return custom(i);
        }
        Entry entry;
        entry.name = full;
        entry.is_generic = true;
        entry.base = base;
        entry.args = args;
        entries.push_back(entry);
        return custom(entries.size() - 1);
    }

    // A function type, like func(int, int): bool. Its inputs are kept as args and its answer as element
    Type function_of(const std::vector<Type>& inputs, Type answer) {
        for (size_t i = 0; i < entries.size(); i++) {
            if (entries[i].is_function && entries[i].args == inputs && entries[i].element == answer) return custom(i);
        }
        Entry entry;
        entry.is_function = true;
        entry.args = inputs;
        entry.element = answer;
        entries.push_back(entry);
        return custom(entries.size() - 1);
    }
    bool is_function(Type type) const { return is_custom(type) && entry(type).is_function; }
    const std::vector<Type>& inputs_of(Type function) const { return entry(function).args; }
    Type answer_of(Type function) const { return entry(function).element; }

    bool is_generic(Type type) const { return is_custom(type) && entry(type).is_generic; }
    const std::string& base_of(Type type) const { return entry(type).base; }
    const std::vector<Type>& args_of(Type type) const { return entry(type).args; }

    static bool is_custom(Type type) { return type >= Type::FirstCustom; }
    bool exists(Type type) const { return is_custom(type) && index_of(type) < entries.size(); }
    bool is_array(Type type) const { return is_custom(type) && entry(type).is_array; }
    bool is_struct(Type type) const { return is_custom(type) && entry(type).is_struct; }
    bool is_enum(Type type) const { return is_custom(type) && entry(type).is_enum; }
    bool is_known(Type type) const {
        if (!is_custom(type)) return true;
        if (entry(type).is_function) {
            for (Type input : entry(type).args) {
                if (!is_known(input)) return false;
            }
            return is_known(entry(type).element);
        }
        return entry(type).is_array ? is_known(entry(type).element) : entry(type).is_struct || entry(type).is_enum;
    }
    Type element_of(Type array) const { return entry(array).element; }
    const std::vector<StructField>& fields_of(Type type) const { return entry(type).fields; }

    const std::vector<std::string>& members_of(Type type) const { return entry(type).members; }

    void make_enum(Type type, std::vector<std::string> members) {
        Entry& found = entries[index_of(type)];
        found.is_enum = true;
        found.members = std::move(members);
    }

    void make_struct(Type type, std::vector<StructField> fields) {
        Entry& found = entries[index_of(type)];
        found.is_struct = true;
        found.fields = std::move(fields);
    }

    std::string name(Type type) const {
        switch (type) {
            case Type::Int: return "int";
            case Type::Float: return "float";
            case Type::Bool: return "bool";
            case Type::String: return "string";
            case Type::Nothing: case Type::Unknown: return "nothing";
            default:
                if (entry(type).is_function) {
                    std::string words = "func(";
                    for (size_t i = 0; i < entry(type).args.size(); i++) words += (i > 0 ? ", " : "") + name(entry(type).args[i]);
                    words += ")";
                    if (entry(type).element != Type::Nothing) words += ": " + name(entry(type).element);
                    return words;
                }
                return entry(type).is_array ? "[" + name(entry(type).element) + "]" : entry(type).name;
        }
    }

private:
    struct Entry {
        bool is_array = false;
        bool is_struct = false;
        bool is_enum = false;
        bool is_generic = false;      // A generic struct given its types
        bool is_function = false;     // A function type, with its inputs in args and its answer in element
        Type element = Type::Nothing;
        std::string name;
        std::string base;             // A generic one's struct, like Pair
        std::vector<Type> args;       // and its types
        std::vector<StructField> fields;
        std::vector<std::string> members;
    };

    static Type custom(size_t index) { return static_cast<Type>(static_cast<int>(Type::FirstCustom) + static_cast<int>(index)); }
    static size_t index_of(Type type) { return static_cast<size_t>(static_cast<int>(type) - static_cast<int>(Type::FirstCustom)); }
    const Entry& entry(Type type) const { return entries[index_of(type)]; }

    std::vector<Entry> entries;
};

// Variables outside functions get slots from -2 down (-1 means no slot), and their values are set up by a
// function the checker makes with this name, which no Jamo name can match
const int FIRST_GLOBAL_SLOT = -2;
inline const std::string SETUP_NAME = "(setup)";

// A try keeps this many slots: the try around it, rbp, rsp, where its catch is, and r12 to r15
const int TRY_SLOTS = 8;

// FunctionValue is a function's name used as a value (its name in text). CallValue calls the function value in left with inputs.
// Lambda is a function written in place: its inputs' names in field_names and types in type_args, its answer in convert_to
// when truth says one was written, and what it gives back in left. The checker makes it a function named in text, with
// the values it captures as inputs. Capture reads captured value number from inside one
enum class ExprKind { Number, Float, Bool, String, Name, Negate, Binary, Call, Length, Convert, FString, Not, Index, ArrayLiteral, MethodCall, Field, StructLiteral, EnumValue, BuiltinCall, FunctionValue, CallValue, Lambda, Capture };
enum class BinaryOp { Add, Subtract, Multiply, Divide, Remainder, Equal, NotEqual, Less, Greater, LessEqual, GreaterEqual, And, Or };

struct Expr {
    ExprKind kind = ExprKind::Number;
    Spot spot;
    uint64_t number = 0;              // Number, and which member an EnumValue is
    double real = 0;                  // Float
    bool truth = false;               // Bool
    std::string text;                 // String, and the function a callback calls
    std::string name;                 // Name, Call, MethodCall, Field, StructLiteral, BuiltinCall
    std::vector<std::string> field_names;  // StructLiteral
    std::vector<Spot> field_spots;    // StructLiteral
    std::vector<int> field_numbers;   // StructLiteral, filled in by the checker
    BinaryOp op = BinaryOp::Add;      // Binary
    Type convert_to = Type::Nothing;  // Convert
    std::unique_ptr<Expr> left;       // Binary, what an Index looks in, what a MethodCall acts on, what a Field is in, and the value of Negate, Not, Length and Convert
    std::unique_ptr<Expr> right;      // Binary, and which item an Index wants
    std::vector<std::unique_ptr<Expr>> inputs;  // Call, MethodCall, BuiltinCall, an ArrayLiteral's items, a StructLiteral's values, and the parts of an FString
    std::vector<Type> type_args;      // Call and StructLiteral: types given in < >, like make_map<string, int>()
    int depth = 1;                    // How tall this part of the tree is

    // Filled in by the checker
    Type type = Type::Nothing;
    int slot = -1;                    // Name: where the variable lives. Field: which field
};

using ExprPtr = std::unique_ptr<Expr>;

enum class StmtKind { Var, Assign, If, While, For, Match, Break, Continue, Return, Call, Print, Try };

struct Stmt;
using Block = std::vector<std::unique_ptr<Stmt>>;

struct ElifPart {
    ExprPtr condition;
    Block body;
};

struct CasePart {
    ExprPtr value;
    Block body;
};

struct Stmt {
    StmtKind kind = StmtKind::Var;
    Spot spot;
    std::string name;                 // Var, Assign, For
    Spot name_spot;                   // Var, Assign, For
    bool has_operation = false;       // Assign, for += -= *= /=
    BinaryOp operation = BinaryOp::Add;  // Assign
    bool has_type = false;            // Var
    Type declared_type = Type::Nothing;  // Var
    ExprPtr target;                   // Assign to anything but a plain variable, like items[0]
    ExprPtr value;                    // Var, Assign, Return, Call, Print. Also the condition of If and While, and where a For starts
    ExprPtr range_end;                // For. Without one, a For goes through the array in value
    Block body;                       // If, While, For, and the try part of a Try
    std::vector<ElifPart> elifs;      // If
    std::vector<CasePart> cases;      // Match
    Block other_body;                 // The else part of an If or Match, and the catch part of a Try
    bool has_else = false;

    // Filled in by the checker
    int slot = -1;                    // Var, Assign, For, and a Try's catch variable
    int end_slot = -1;                // For: keeps the end, worked out once before the loop, or the place in an array. Try: the first of its 8 slots, where it keeps what's needed to jump to its catch
    int array_slot = -1;              // For: keeps the array
    bool covers_all = false;          // Match: some case always runs
};

struct Input {
    std::string name;
    Spot spot;
    Type type = Type::Int;
};

struct Function {
    std::string name;                 // A method's name includes its struct: Enemy.hit
    Spot spot;
    Type owner = Type::Nothing;       // A method's struct
    Spot owner_spot;
    std::vector<Input> inputs;        // A method's first input is self
    Type return_type = Type::Nothing;
    std::string dll;                  // An extern function's DLL, like user32.dll. It has no body
    std::vector<bool> input_is_i32;   // An extern input written as i32
    std::vector<bool> input_is_f32;   // An extern input written as f32, a 32-bit float
    bool returns_i32 = false;         // An extern function that answers with a 32-bit int
    bool returns_f32 = false;         // An extern function that answers with a 32-bit float
    Block body;
    std::vector<Type> type_params;    // A generic function's types, like T in first<T>, or a generic struct's for its method
    std::vector<Spot> type_param_spots;
    int slot_count = 0;               // Filled in by the checker: variables, including inputs
    bool is_lambda = false;           // Made by the checker from a function written in place, whose first input is its block
};

struct StructDecl {
    std::string name;
    Spot spot;
    Type type = Type::Nothing;
    std::vector<StructField> fields;
    std::vector<Type> type_params;    // A generic struct's types, like A and B in Pair<A, B>
    std::vector<Spot> type_param_spots;
};

struct EnumDecl {
    std::string name;
    Spot spot;
    Type type = Type::Nothing;
    std::vector<std::string> members;
    std::vector<Spot> member_spots;
};

// const MAX = 64: a name for a value known before the program runs
struct ConstDecl {
    std::string name;
    Spot spot;
    std::unique_ptr<Expr> value;
};

struct UseLine {
    std::string path;
    Spot spot;
    std::string alias;  // use "enemies.jamo" as foes: foes
    Spot alias_spot;
};

// use ... as: in file, alias means the file target
struct Alias {
    int file = 0;
    std::string name;
    int target = 0;
};

struct Program {
    TypeTable types;
    std::vector<std::string> file_names;
    std::vector<UseLine> uses;  // The ones in the file just read
    std::vector<StructDecl> structs;
    std::vector<EnumDecl> enums;
    std::vector<ConstDecl> consts;
    std::vector<std::unique_ptr<Function>> functions;
    std::vector<std::unique_ptr<Stmt>> globals;  // var lines outside functions, in order. The checker moves them into the setup function
    std::vector<Block> lambda_bodies;  // The blocks of functions written in place with one: a Lambda's number is its place + 1
    std::vector<int> setup_order;                 // Files in the order their variables are set up: each one after the files it uses
    std::vector<std::pair<std::string, int>> private_names;  // Made with private: the name, and the file it can be used in
    std::vector<int> own_files;    // Files used with as, whose functions, constants and variables are their own
    std::vector<Alias> aliases;
    std::string own_label;         // While a file used with as is read: its name, so its names become "spawn (in enemies.jamo)"
};
