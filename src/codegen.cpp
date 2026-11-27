#include "codegen.hpp"

#include <bit>
#include <cmath>
#include <functional>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "runtime.hpp"

// How the generated code works:
// - Every expression leaves its answer in rax. Half-finished sums wait on the stack.
// - Jamo functions get their inputs on the stack, left to right, and answer in rax.
// - Only the startup and runtime code talk to Windows, keeping its stack rules there.

namespace {

const int WORD_SIZE = 8;
const int CLEANER_FLAGS = 7;
const int WINDOWS_SPACE = 32;  // The space every Windows function may use above its inputs
const int NO_OPTIMIZING = 8;
const int WINDOW_PROGRAM = 16;  // No console, and a run error shows in a message box
const int TRACING = 32;         // Each function of the program's own files writes its name when it starts
const uint8_t JUMP_FROM_SET = 0x10;  // A jump's condition number is its set instruction's, less this
const int PAGE_SIZE = 4096;
const int SHOW_FRAME = 32;
const int SHOW_PROGRESS = -8;
const int VARIABLE_REGISTERS = 4;
const int INLINE_MOST_PARTS = 64;  // Statements and expressions, together, in a function written in place
const int REGISTER_MIN_USES = 3;
const int SHOW_VALUE = -16;
const uint8_t SET_ABOVE = 0x97;
const uint8_t SET_AT_LEAST = 0x93;
const uint8_t SET_EQUAL = 0x94;
const uint8_t SET_NOT_EQUAL = 0x95;
const uint8_t SET_UNORDERED = 0x9A;
const uint8_t SET_ORDERED = 0x9B;

uint8_t condition_of(BinaryOp op) {
    switch (op) {
        case BinaryOp::Equal: return 0x94;
        case BinaryOp::NotEqual: return 0x95;
        case BinaryOp::Less: return 0x9C;
        case BinaryOp::Greater: return 0x9F;
        case BinaryOp::LessEqual: return 0x9E;
        default: return 0x9D;  // GreaterEqual
    }
}

class Generator {
public:
    Generator(const Program& the_program, const std::string& the_file_name, int test_flags) : program(the_program), file_name(the_file_name), cleaner_mode(test_flags & (CLEANER_FLAGS | WINDOW_PROGRAM)), optimizing((test_flags & NO_OPTIMIZING) == 0), tracing((test_flags & TRACING) != 0) {}

    MachineCode generate() {
        runtime = make_runtime_labels(out);
        text.true_word = string_number("true");
        text.false_word = string_number("false");
        if (program.file_names.empty()) text.file_names.push_back(string_number(file_name));
        for (const std::string& name : program.file_names) text.file_names.push_back(string_number(name));
        text.line_word = string_number(" line ");
        text.error_word = string_number(": Error: ");
        text.divide_by_zero = string_number("division by zero");
        text.out_of_memory = string_number("the program ran out of memory");
        text.float_too_large = string_number("this float doesn't fit in an int");
        text.index_word = string_number("index ");
        text.outside_string = string_number(" is outside this string, which has ");
        text.bytes_word = string_number(" bytes");
        text.pop_empty = string_number("pop needs an item, but this array is empty");
        text.outside_array = string_number(" is outside this array, which has ");
        text.items_word = string_number(" items");
        text.item_word = string_number(" item");
        text.byte_word = string_number(" byte");
        text.quote = string_number("\"");
        text.new_line = string_number("\n");
        text.cant_turn = string_number("can't turn \"");
        text.into_int = string_number("\" into an int");
        text.into_float = string_number("\" into a float");
        text.cant_read = string_number("can't read the file ");
        text.cant_write = string_number("can't write the file ");
        text.period = string_number(".");
        text.cleaner_bad_header = string_number("the memory cleaner found a damaged block header");
        text.cleaner_freed_changed = string_number("the memory cleaner found that freed memory was changed");
        text.cleaner_freed_reference = string_number("the memory cleaner found a reference to freed memory");
        text.cleaner_double_free = string_number("the memory cleaner tried to free the same block twice");
        text.more_calls = string_number("    and more calls before those\n");
        // For a window program's message box. Windows wants these ending in a zero byte. Other programs never
        // reach that code, so they point it at a text they already have, and mention no other DLL
        text.user32_dll = text.message_box = text.error_title = text.new_line;
        if (cleaner_mode & WINDOW_PROGRAM) {
            text.user32_dll = string_number(std::string("user32.dll") + std::string(1, '\0'));
            text.message_box = string_number(std::string("MessageBoxA") + std::string(1, '\0'));
            const bool has_ending = file_name.size() > 5 && file_name.compare(file_name.size() - 5, 5, ".jamo") == 0;
            text.error_title = string_number((has_ending ? file_name.substr(0, file_name.size() - 5) : file_name) + " stopped" + std::string(1, '\0'));
        }
        for (const auto& function : program.functions) {
            function_labels[function->name] = out.new_label();
            function_named[function->name] = function.get();
        }

        write_startup();
        write_runtime(out, runtime, text);
        if (optimizing) find_functions_in_use();
        for (const auto& function : program.functions) {
            if (function->dll.empty() && (!optimizing || functions_in_use.count(function->name))) write_function(*function);
        }
        while (!shows_to_write.empty()) {
            const Type type = shows_to_write.back();
            shows_to_write.pop_back();
            write_show_function(type);
        }
        for (const auto& [name, label] : callback_labels) write_callback(*function_named.at(name), label);
        if (opengl_lookup >= 0) write_opengl_lookup();
        MachineCode code = out.finish(strings, imports);
        code.window = (cleaner_mode & WINDOW_PROGRAM) != 0;
        return code;
    }

private:
    void write_startup() {
        out.sub_rsp(40);  // The space Windows functions may use, keeping the stack aligned
        out.call_windows(DataRef::GetProcessHeap);
        out.store_rax_data(DataRef::HeapHandle);
        out.add_rsp(40);
        out.mov_rax_number(cleaner_mode);
        out.call(runtime.cleaner_start);
        if (function_labels.count(SETUP_NAME)) {
            // The variables outside functions go on the stack below the cleaner's starting point, so it
            // looks at them like any other value in use. They start as zeros, then the setup gives them their values.
            // The cleaner noted its start inside its own call, a word below here, so one spare word keeps them all under it
            const Function& setup = *function_named.at(SETUP_NAME);
            const int count = static_cast<int>(setup.body.size());
            const int bytes = (count * WORD_SIZE + WORD_SIZE + 15) / 16 * 16;
            for (int touched = PAGE_SIZE; touched < bytes; touched += PAGE_SIZE) out.touch_stack_below(touched);
            out.sub_rsp(bytes);
            out.start_globals(count);
            out.call(function_labels.at(SETUP_NAME));
        }
        out.call(function_labels.at("main"));
        out.bytes({0x31, 0xC9});  // xor ecx, ecx: exit code 0
        out.jump(runtime.exit);
    }

    // A run error's file and line, kept together in one value
    static int64_t place_of(Spot spot) {
        return static_cast<int64_t>(spot.file < 0 ? 0 : spot.file) << 32 | spot.line;
    }

    // The same text always shares one entry in the string table
    int string_number(const std::string& words) {
        const auto found = string_numbers.find(words);
        if (found != string_numbers.end()) return found->second;
        const int number = static_cast<int>(strings.size());
        strings.push_back(words);
        string_numbers[words] = number;
        return number;
    }

    // --- Leaving out functions that can never be called ---
    void find_functions_in_use() {
        std::vector<std::string> to_visit = {"main"};
        if (function_labels.count(SETUP_NAME)) to_visit.push_back(SETUP_NAME);
        std::map<std::string, const Function*> by_name;
        for (const auto& function : program.functions) by_name[function->name] = function.get();
        while (!to_visit.empty()) {
            const std::string name = to_visit.back();
            to_visit.pop_back();
            if (!functions_in_use.insert(name).second) continue;
            std::vector<std::string> called;
            if (by_name.count(name)) find_calls_in_block(by_name.at(name)->body, called);
            to_visit.insert(to_visit.end(), called.begin(), called.end());
        }
    }

    void find_calls_in_block(const Block& block, std::vector<std::string>& called) const {
        for (const auto& stmt : block) {
            find_calls(stmt->target.get(), called);
            find_calls(stmt->value.get(), called);
            find_calls(stmt->range_end.get(), called);
            find_calls_in_block(stmt->body, called);
            for (const ElifPart& part : stmt->elifs) {
                find_calls(part.condition.get(), called);
                find_calls_in_block(part.body, called);
            }
            for (const CasePart& part : stmt->cases) {
                find_calls(part.value.get(), called);
                find_calls_in_block(part.body, called);
            }
            find_calls_in_block(stmt->other_body, called);
        }
    }

    void find_calls(const Expr* expr, std::vector<std::string>& called) const {
        if (!expr) return;
        if (expr->kind == ExprKind::Call) called.push_back(expr->name);
        if (expr->kind == ExprKind::BuiltinCall && expr->name == "callback") called.push_back(expr->text);
        if (expr->kind == ExprKind::FunctionValue || expr->kind == ExprKind::Lambda) called.push_back(expr->text);
        if (expr->kind == ExprKind::MethodCall && program.types.is_struct(expr->left->type)) called.push_back(program.types.name(expr->left->type) + "." + expr->name);
        find_calls(expr->left.get(), called);
        find_calls(expr->right.get(), called);
        for (const auto& input : expr->inputs) find_calls(input.get(), called);
    }

    // --- Working out constants ---
    // Answers true if the value is known now, with its bits. Leaves anything that would be a run error for the program to find
    bool constant_of(const Expr& expr, int64_t& bits) const {
        if (!optimizing) return false;
        switch (expr.kind) {
            case ExprKind::Number:
                bits = static_cast<int64_t>(expr.number);
                return true;
            case ExprKind::Float:
                bits = std::bit_cast<int64_t>(expr.real);
                return true;
            case ExprKind::Bool:
                bits = expr.truth ? 1 : 0;
                return true;
            case ExprKind::Negate:
                if (!constant_of(*expr.left, bits)) return false;
                bits = expr.type == Type::Float ? static_cast<int64_t>(static_cast<uint64_t>(bits) ^ (1ULL << 63)) : static_cast<int64_t>(0 - static_cast<uint64_t>(bits));
                return true;
            case ExprKind::Not:
                if (!constant_of(*expr.left, bits)) return false;
                bits ^= 1;
                return true;
            case ExprKind::Binary:
                return constant_of_binary(expr, bits);
            default:
                return false;
        }
    }

    bool constant_of_binary(const Expr& expr, int64_t& bits) const {
        int64_t left = 0;
        int64_t right = 0;
        if (!constant_of(*expr.left, left) || !constant_of(*expr.right, right)) return false;
        const Type type = expr.left->type;
        if (type == Type::Int) return constant_of_ints(expr.op, left, right, bits);
        if (type == Type::Float) return constant_of_floats(expr.op, std::bit_cast<double>(left), std::bit_cast<double>(right), bits);
        if (expr.op == BinaryOp::And) bits = left & right;
        else if (expr.op == BinaryOp::Or) bits = left | right;
        else if (expr.op == BinaryOp::Equal) bits = left == right;
        else if (expr.op == BinaryOp::NotEqual) bits = left != right;
        else return false;
        return true;
    }

    static bool constant_of_ints(BinaryOp op, int64_t left, int64_t right, int64_t& bits) {
        const uint64_t a = static_cast<uint64_t>(left);
        const uint64_t b = static_cast<uint64_t>(right);
        switch (op) {
            case BinaryOp::Add: bits = static_cast<int64_t>(a + b); return true;
            case BinaryOp::Subtract: bits = static_cast<int64_t>(a - b); return true;
            case BinaryOp::Multiply: bits = static_cast<int64_t>(a * b); return true;
            case BinaryOp::Divide:
                if (right == 0) return false;
                bits = right == -1 ? static_cast<int64_t>(0 - a) : left / right;
                return true;
            case BinaryOp::Remainder:
                if (right == 0) return false;
                bits = right == -1 ? 0 : left % right;
                return true;
            case BinaryOp::Equal: bits = left == right; return true;
            case BinaryOp::NotEqual: bits = left != right; return true;
            case BinaryOp::Less: bits = left < right; return true;
            case BinaryOp::Greater: bits = left > right; return true;
            case BinaryOp::LessEqual: bits = left <= right; return true;
            case BinaryOp::GreaterEqual: bits = left >= right; return true;
            default: return false;
        }
    }

    static bool constant_of_floats(BinaryOp op, double left, double right, int64_t& bits) {
        double answer = 0;
        switch (op) {
            case BinaryOp::Add: answer = left + right; break;
            case BinaryOp::Subtract: answer = left - right; break;
            case BinaryOp::Multiply: answer = left * right; break;
            case BinaryOp::Divide: answer = left / right; break;
            case BinaryOp::Equal: bits = left == right; return true;
            case BinaryOp::NotEqual: bits = left != right; return true;
            case BinaryOp::Less: bits = left < right; return true;
            case BinaryOp::Greater: bits = left > right; return true;
            case BinaryOp::LessEqual: bits = left <= right; return true;
            case BinaryOp::GreaterEqual: bits = left >= right; return true;
            default: return false;
        }
        if (std::isnan(answer)) return false;
        bits = std::bit_cast<int64_t>(answer);
        return true;
    }

    // Something that can load into rcx without touching rax or running any code
    bool is_simple(const Expr& expr) const {
        int64_t bits = 0;
        return optimizing && (expr.kind == ExprKind::Name || expr.kind == ExprKind::String || expr.kind == ExprKind::EnumValue || is_variable_field(expr) || constant_of(expr, bits));
    }

    // A field of a struct held by a variable, like self.x, which reads without running any code
    bool is_variable_field(const Expr& expr) const {
        return expr.kind == ExprKind::Field && expr.left->kind == ExprKind::Name && expr.left->slot >= 0 && program.types.is_struct(expr.left->type);
    }

    // True when running the expression could call a function, which could change a field
    static bool has_call(const Expr* expr) {
        if (!expr) return false;
        if (expr->kind == ExprKind::Call || expr->kind == ExprKind::MethodCall || expr->kind == ExprKind::BuiltinCall) return true;
        if (has_call(expr->left.get()) || has_call(expr->right.get())) return true;
        for (const auto& input : expr->inputs) {
            if (has_call(input.get())) return true;
        }
        return false;
    }

    void write_simple_to_rcx(const Expr& expr) {
        int64_t bits = 0;
        if (expr.kind == ExprKind::Name) {
            load_slot_rcx(expr.slot);
        } else if (is_variable_field(expr)) {
            load_slot_rcx(expr.left->slot);
            out.load_rcx_field(expr.slot);
        } else if (expr.kind == ExprKind::String) {
            out.lea_string(RCX, string_number(expr.text));
        } else if (expr.kind == ExprKind::EnumValue) {
            out.mov_rcx_value(static_cast<int64_t>(expr.number));
        } else {
            constant_of(expr, bits);
            out.mov_rcx_value(bits);
        }
    }

    // An int constant small enough to go straight into an instruction, leaving it in bits
    bool small_constant(const Expr& expr, int64_t& bits) const {
        return expr.type == Type::Int && constant_of(expr, bits) && bits >= std::numeric_limits<int32_t>::min() && bits <= std::numeric_limits<int32_t>::max();
    }

    // rax = the array, rcx = the index. Answers with the item, calling the runtime only to stop with the error
    void write_array_read(int64_t line) {
        const Label inside = out.new_label();
        out.cmp_rcx_length();
        out.jump_if_below(inside);
        out.note_line(line);
        out.call(runtime.array_at);
        out.place(inside);
        out.load_rax_items();
        out.load_rax_item_rcx();
    }

    // Leaves the left side in rax and the right side in rcx
    void write_both_sides(const Expr& right, const std::function<void()>& write_left) {
        write_left();
        if (is_simple(right)) {
            write_simple_to_rcx(right);
            return;
        }
        out.push_rax();
        write_expression(right);
        out.mov_rcx_rax();
        out.pop_rax();
    }

    // --- Jumping on a condition, without making a bool first ---
    static bool is_int_comparison(const Expr& expr) {
        if (expr.kind != ExprKind::Binary || expr.left->type == Type::Float || expr.left->type == Type::String) return false;
        return expr.op == BinaryOp::Equal || expr.op == BinaryOp::NotEqual || expr.op == BinaryOp::Less || expr.op == BinaryOp::Greater || expr.op == BinaryOp::LessEqual || expr.op == BinaryOp::GreaterEqual;
    }

    void write_jump(const Expr& condition, bool jump_when, Label target) {
        int64_t bits = 0;
        if (constant_of(condition, bits)) {
            if ((bits != 0) == jump_when) out.jump(target);
            return;
        }
        if (optimizing && condition.kind == ExprKind::Not) {
            write_jump(*condition.left, !jump_when, target);
            return;
        }
        if (optimizing && condition.kind == ExprKind::Binary && (condition.op == BinaryOp::And || condition.op == BinaryOp::Or)) {
            const bool is_and = condition.op == BinaryOp::And;
            if (is_and != jump_when) {
                write_jump(*condition.left, jump_when, target);
                write_jump(*condition.right, jump_when, target);
            } else {
                const Label skip = out.new_label();
                write_jump(*condition.left, !jump_when, skip);
                write_jump(*condition.right, jump_when, target);
                out.place(skip);
            }
            return;
        }
        if (optimizing && condition.kind == ExprKind::Binary && condition.left->type == Type::Float &&
            (condition.op == BinaryOp::Less || condition.op == BinaryOp::Greater || condition.op == BinaryOp::LessEqual || condition.op == BinaryOp::GreaterEqual)) {
            write_float_sides(*condition.left, *condition.right);
            if (condition.op == BinaryOp::Less || condition.op == BinaryOp::LessEqual) {
                out.compare_xmm1_xmm0();
            } else {
                out.compare_xmm0_xmm1();
            }
            const uint8_t jump_if_true = (condition.op == BinaryOp::Less || condition.op == BinaryOp::Greater) ? 0x87 : 0x83;  // above, or at least
            out.jump_when(jump_when ? jump_if_true : static_cast<uint8_t>(jump_if_true ^ 1), target);
            return;
        }
        if (optimizing && is_int_comparison(condition)) {
            const int reg = register_name(*condition.left);
            const int other = register_name(*condition.right);
            if (reg >= 0 && other >= 0) {
                out.cmp_reg_reg(reg, other);
            } else if (reg >= 0 && small_constant(*condition.right, bits)) {
                out.cmp_reg_value(reg, static_cast<int32_t>(bits));
            } else if (reg >= 0 && is_simple(*condition.right)) {
                write_simple_to_rcx(*condition.right);
                out.cmp_reg_rcx(reg);
            } else if (small_constant(*condition.right, bits)) {
                write_expression(*condition.left);
                out.cmp_rax_value(static_cast<int32_t>(bits));
            } else {
                write_both_sides(*condition.right, [&] { write_expression(*condition.left); });
                out.cmp_rax_rcx();
            }
            const uint8_t jump_if_true = static_cast<uint8_t>(condition_of(condition.op) - JUMP_FROM_SET);
            out.jump_when(jump_when ? jump_if_true : static_cast<uint8_t>(jump_if_true ^ 1), target);
            return;
        }
        write_expression(condition);
        out.test_rax_rax();
        if (jump_when) {
            out.jump_if_not_equal(target);
        } else {
            out.jump_if_equal(target);
        }
    }

    // --- Jamo code ---
    void write_function(const Function& function) {
        input_count = static_cast<int>(function.inputs.size());
        current_function = function.name;
        choose_registers(function);
        out.place(function_labels.at(function.name));
        out.push_rbp();
        out.mov_rbp_rsp();
        for (int reg = 0; reg < saved_count; reg++) out.push_reg(reg);
        inline_top = function.slot_count;
        const int spare = optimizing ? inline_slots_block(function.body) : 0;
        const int local_bytes = (function.slot_count + spare - input_count) * WORD_SIZE;
        for (int touched = PAGE_SIZE; touched < local_bytes; touched += PAGE_SIZE) out.touch_stack_below(touched);
        if (local_bytes > 0) out.sub_rsp(local_bytes);
        if (tracing && traced(function)) {
            out.lea_rax_string(string_number(function.name == SETUP_NAME ? "(setting up the variables outside functions)" : shown_name(function.name)));
            out.call(runtime.trace_call);
        }
        for (const auto& [slot, reg] : slot_registers) {
            if (slot < input_count) out.load_reg_from_frame(reg, frame_offset(slot));
        }
        write_block(function.body);
        if (!always_leaves(function.body)) write_return();
    }

    // A function written inside another with func name(...) is hidden as "name (lambda N)": the program sees just name
    static std::string shown_name(std::string name) {
        size_t at = name.find(" (in ");
        while (at != std::string::npos) {
            const size_t close = name.find(')', at);
            if (close == std::string::npos) break;
            name.erase(at, close + 1 - at);
            at = name.find(" (in ");
        }
        const size_t cut = name.find(" (lambda ");
        return cut == std::string::npos || cut == 0 ? name : name.substr(0, cut);
    }

    // --trace leaves out the built-in libraries' functions, so only the program's own show
    bool traced(const Function& function) const {
        if (program.file_names.empty()) return true;
        const std::string& name = program.file_names[static_cast<size_t>(function.spot.file < 0 ? 0 : function.spot.file)];
        return !(name.size() > 11 && name.compare(name.size() - 11, 11, " (built in)") == 0);
    }

    // Puts back the registers this function used, then returns
    void write_return() {
        for (int reg = 0; reg < saved_count; reg++) out.load_reg_from_frame(reg, -(reg + 1) * WORD_SIZE);
        out.leave_and_return();
    }

    // Inputs sit above the saved rbp and return address. Below rbp come the saved registers, then the locals
    int frame_offset(int slot) const {
        if (slot < input_count) return 2 * WORD_SIZE + (input_count - 1 - slot) * WORD_SIZE;
        return -(slot - input_count + 1 + saved_count) * WORD_SIZE;
    }

    // The register holding a variable, or -1 when it lives on the stack
    int register_of(int slot) {
        const auto found = slot_registers.find(real_slot(slot));
        return found == slot_registers.end() ? -1 : found->second;
    }

    // A variable held in a register, read with nothing else running
    int register_name(const Expr& expr) {
        if (!optimizing || expr.kind != ExprKind::Name || expr.slot < 0) return -1;
        return register_of(expr.slot);
    }

    // Variables outside functions have slots from -2 down, and live where the startup put them
    static bool is_global(int slot) { return slot <= FIRST_GLOBAL_SLOT; }
    static int global_offset(int slot) { return (FIRST_GLOBAL_SLOT - slot) * WORD_SIZE; }

    void load_slot_rax(int slot) {
        if (is_global(slot)) {
            out.load_r11_globals();
            out.load_rax_global(global_offset(slot));
            return;
        }
        slot = real_slot(slot);
        const auto found = slot_registers.find(slot);
        if (found != slot_registers.end()) {
            out.mov_rax_reg(found->second);
        } else {
            out.load_rax_from_frame(frame_offset(slot));
        }
    }

    void load_slot_rcx(int slot) {
        if (is_global(slot)) {
            out.load_r11_globals();
            out.load_rcx_global(global_offset(slot));
            return;
        }
        slot = real_slot(slot);
        const auto found = slot_registers.find(slot);
        if (found != slot_registers.end()) {
            out.mov_rcx_reg(found->second);
        } else {
            out.load_rcx_from_frame(frame_offset(slot));
        }
    }

    void store_slot_rax(int slot) {
        if (is_global(slot)) {
            out.load_r11_globals();
            out.store_rax_global(global_offset(slot));
            return;
        }
        slot = real_slot(slot);
        const auto found = slot_registers.find(slot);
        if (found != slot_registers.end()) {
            out.mov_reg_rax(found->second);
        } else {
            out.store_rax_to_frame(frame_offset(slot));
        }
    }

    // --- Inlining small functions ---
    // A function is written into its caller, instead of called, when it's small and can never stop with an error.
    // A run error lists the calls that led to it, so a function that can't stop is the only kind whose call can
    // disappear without changing what a program shows. Its inputs and variables go in spare slots of the caller's
    // frame, and each return jumps to the end of its body
    bool can_inline(const Function& function) {
        if (!optimizing || tracing || !function.dll.empty() || function.name == "main") return false;
        const auto found = inline_ok.find(function.name);
        if (found != inline_ok.end()) return found->second;
        inline_ok[function.name] = false;
        int count = 0;
        const bool ok = inline_safe_block(function.body, count);
        inline_ok[function.name] = ok;
        return ok;
    }

    bool inline_safe_block(const Block& block, int& count) {
        for (const auto& stmt : block) {
            if (!inline_safe_stmt(*stmt, count)) return false;
        }
        return true;
    }

    bool inline_safe_stmt(const Stmt& stmt, int& count) {
        if (++count > INLINE_MOST_PARTS) return false;
        switch (stmt.kind) {
            case StmtKind::Var:
            case StmtKind::Call:
                return inline_safe_expr(stmt.value.get(), count);
            case StmtKind::Return:
                return !stmt.value || inline_safe_expr(stmt.value.get(), count);
            case StmtKind::Assign: {
                if (stmt.target && !(stmt.target->kind == ExprKind::Field && stmt.target->left->kind == ExprKind::Name)) return false;
                const Type type = stmt.value->type;
                if (stmt.has_operation && type == Type::String) return false;
                if (stmt.has_operation && type == Type::Int && (stmt.operation == BinaryOp::Divide || stmt.operation == BinaryOp::Remainder) && !is_nonzero_constant(*stmt.value)) return false;
                return inline_safe_expr(stmt.value.get(), count);
            }
            case StmtKind::If: {
                if (!inline_safe_expr(stmt.value.get(), count) || !inline_safe_block(stmt.body, count) || !inline_safe_block(stmt.other_body, count)) return false;
                for (const ElifPart& part : stmt.elifs) {
                    if (!inline_safe_expr(part.condition.get(), count) || !inline_safe_block(part.body, count)) return false;
                }
                return true;
            }
            default:
                return false;
        }
    }

    bool is_nonzero_constant(const Expr& expr) const {
        int64_t bits = 0;
        return constant_of(expr, bits) && bits != 0;
    }

    bool inline_safe_expr(const Expr* expr, int& count) {
        if (++count > INLINE_MOST_PARTS) return false;
        switch (expr->kind) {
            case ExprKind::Number:
            case ExprKind::Float:
            case ExprKind::Bool:
            case ExprKind::String:
            case ExprKind::Name:
            case ExprKind::EnumValue:
                return true;
            case ExprKind::Field:
            case ExprKind::Negate:
            case ExprKind::Not:
            case ExprKind::Length:
                return inline_safe_expr(expr->left.get(), count);
            case ExprKind::Convert:
                return expr->convert_to == Type::Float && expr->left->type == Type::Int && inline_safe_expr(expr->left.get(), count);
            case ExprKind::Binary:
                if (expr->op == BinaryOp::Add && expr->type == Type::String) return false;
                if ((expr->op == BinaryOp::Divide || expr->op == BinaryOp::Remainder) && expr->left->type == Type::Int && !is_nonzero_constant(*expr->right)) return false;
                return inline_safe_expr(expr->left.get(), count) && inline_safe_expr(expr->right.get(), count);
            case ExprKind::Call:
            case ExprKind::MethodCall: {
                // Calls Windows, or a function that's written in place too
                const bool is_windows = expr->kind == ExprKind::Call && !function_named.at(expr->name)->dll.empty();
                if (!is_windows && !inlined_function(*expr)) return false;
                if (expr->kind == ExprKind::MethodCall && !inline_safe_expr(expr->left.get(), count)) return false;
                for (const auto& input : expr->inputs) {
                    if (!inline_safe_expr(input.get(), count)) return false;
                }
                return true;
            }
            default:
                return false;
        }
    }

    // The function a call goes to, when it's written in place, or nullptr
    const Function* inlined_function(const Expr& expr) {
        if (expr.kind == ExprKind::Call) {
            const Function* function = function_named.at(expr.name);
            return can_inline(*function) ? function : nullptr;
        }
        if (expr.kind == ExprKind::MethodCall && program.types.is_struct(expr.left->type)) {
            const Function* function = function_named.at(program.types.name(expr.left->type) + "." + expr.name);
            return can_inline(*function) ? function : nullptr;
        }
        return nullptr;
    }

    // How many spare slots a function's frame needs for the calls written in place. A call's inputs run while its
    // own slots are taken, so calls in them need slots past those
    int inline_slots_block(const Block& block) {
        int most = 0;
        for (const auto& stmt : block) {
            most = std::max({most, inline_slots_expr(stmt->target.get()), inline_slots_expr(stmt->value.get()), inline_slots_expr(stmt->range_end.get()),
                             inline_slots_block(stmt->body), inline_slots_block(stmt->other_body)});
            for (const ElifPart& part : stmt->elifs) most = std::max({most, inline_slots_expr(part.condition.get()), inline_slots_block(part.body)});
            for (const CasePart& part : stmt->cases) most = std::max({most, inline_slots_expr(part.value.get()), inline_slots_block(part.body)});
        }
        return most;
    }

    int inline_slots_expr(const Expr* expr) {
        if (!expr) return 0;
        int most = std::max(inline_slots_expr(expr->left.get()), inline_slots_expr(expr->right.get()));
        for (const auto& input : expr->inputs) most = std::max(most, inline_slots_expr(input.get()));
        const Function* function = inlined_function(*expr);
        return function ? function->slot_count + std::max(most, inline_slots_block(function->body)) : most;
    }

    // Writes the function's body in place. object is the struct a method was called on, or nullptr
    void write_inline_call(const Function& function, const Expr* object, const std::vector<std::unique_ptr<Expr>>& inputs) {
        const int base = inline_top;
        inline_top += function.slot_count;
        int slot = base;
        if (object) {
            write_expression(*object);
            store_own_slot_rax(slot++);
        }
        for (const auto& input : inputs) {
            write_expression(*input);
            store_own_slot_rax(slot++);
        }
        const Label finished = out.new_label();
        const int outer_shift = slot_shift;
        const Label outer_end = inline_end;
        slot_shift = base;
        inline_end = finished;
        write_block(function.body);
        out.place(finished);
        slot_shift = outer_shift;
        inline_end = outer_end;
        inline_top = base;
    }

    // Stores rax in one of the frame's own slots, even while a function is being written in place
    void store_own_slot_rax(int slot) {
        const int outer_shift = slot_shift;
        slot_shift = -1;
        store_slot_rax(slot);
        slot_shift = outer_shift;
    }

    // A slot of the function being written in place is one of the caller's spare slots
    int real_slot(int slot) const {
        return slot_shift >= 0 && slot >= 0 ? slot + slot_shift : slot;
    }

    // --- Registers for the busiest variables ---
    // Counts each variable's uses, with uses inside loops counting 10 times for each loop, and gives the
    // busiest ones r12 to r15. Windows functions keep these registers, so calls can't spoil them, and the
    // memory cleaner sees them because allocating saves every register on the stack first
    void choose_registers(const Function& function) {
        slot_registers.clear();
        saved_count = 0;
        // A catch can't know what a function's own variables held in registers, so a function with a try keeps
        // them all in memory
        if (!optimizing || has_try(function.body)) return;
        std::map<int, int64_t> uses;
        count_uses_in_block(function.body, 0, uses);
        std::vector<std::pair<int64_t, int>> busiest;
        for (const auto& [slot, count] : uses) {
            if (count >= REGISTER_MIN_USES) busiest.push_back({-count, slot});
        }
        std::sort(busiest.begin(), busiest.end());
        for (const auto& [count, slot] : busiest) {
            if (saved_count == VARIABLE_REGISTERS) break;
            slot_registers[slot] = saved_count++;
        }
    }

    static int64_t loop_weight(int depth) {
        int64_t weight = 1;
        for (int i = 0; i < depth && i < 4; i++) weight *= 10;
        return weight;
    }

    void count_uses_in_block(const Block& block, int depth, std::map<int, int64_t>& uses) const {
        for (const auto& stmt : block) {
            const int64_t here = loop_weight(depth);
            const bool loop = stmt->kind == StmtKind::While || stmt->kind == StmtKind::For;
            count_uses(stmt->target.get(), here, uses);
            count_uses(stmt->value.get(), stmt->kind == StmtKind::While ? loop_weight(depth + 1) : here, uses);
            count_uses(stmt->range_end.get(), here, uses);
            if (stmt->kind == StmtKind::For) {
                for (int slot : {stmt->slot, stmt->end_slot, stmt->array_slot}) {
                    if (slot >= 0) uses[slot] += 3 * loop_weight(depth + 1);
                }
            } else if (stmt->slot >= 0 && !stmt->target && (stmt->kind == StmtKind::Var || stmt->kind == StmtKind::Assign || stmt->kind == StmtKind::Match)) {
                uses[stmt->slot] += here;
            }
            count_uses_in_block(stmt->body, loop ? depth + 1 : depth, uses);
            for (const ElifPart& part : stmt->elifs) {
                count_uses(part.condition.get(), here, uses);
                count_uses_in_block(part.body, depth, uses);
            }
            for (const CasePart& part : stmt->cases) {
                count_uses(part.value.get(), here, uses);
                count_uses_in_block(part.body, depth, uses);
            }
            count_uses_in_block(stmt->other_body, depth, uses);
        }
    }

    void count_uses(const Expr* expr, int64_t weight, std::map<int, int64_t>& uses) const {
        if (!expr) return;
        if (expr->kind == ExprKind::Name && expr->slot >= 0) uses[expr->slot] += weight;
        count_uses(expr->left.get(), weight, uses);
        count_uses(expr->right.get(), weight, uses);
        for (const auto& input : expr->inputs) count_uses(input.get(), weight, uses);
    }

    // True when the block always leaves, by return, break or continue, so nothing needs to follow it.
    // write_block stops at the first of these, so only the block's own statements count
    bool always_leaves(const Block& block) const {
        if (!optimizing) return false;
        for (const auto& stmt : block) {
            if (stmt->kind == StmtKind::Return || stmt->kind == StmtKind::Break || stmt->kind == StmtKind::Continue) return true;
        }
        return false;
    }

    void write_block(const Block& block) {
        for (const auto& stmt : block) {
            write_statement(*stmt);
            // Anything after these can never run
            if (optimizing && (stmt->kind == StmtKind::Return || stmt->kind == StmtKind::Break || stmt->kind == StmtKind::Continue)) return;
        }
    }

    void write_statement(const Stmt& stmt) {
        switch (stmt.kind) {
            case StmtKind::Var: {
                int64_t bits = 0;
                const int reg = optimizing ? register_of(stmt.slot) : -1;
                if (reg >= 0 && small_constant(*stmt.value, bits)) {
                    out.mov_reg_value(reg, static_cast<int32_t>(bits));
                } else {
                    write_expression(*stmt.value);
                    store_slot_rax(stmt.slot);
                }
                break;
            }
            case StmtKind::Assign:
                write_assign(stmt);
                break;
            case StmtKind::If:
                write_if(stmt);
                break;
            case StmtKind::While:
                write_while(stmt);
                break;
            case StmtKind::For:
                write_for(stmt);
                break;
            case StmtKind::Match:
                write_match(stmt);
                break;
            case StmtKind::Break:
                if (tries_open.size() > loops.back().tries) close_tries_from(loops.back().tries);
                out.jump(loops.back().finished);
                break;
            case StmtKind::Continue:
                if (tries_open.size() > loops.back().tries) close_tries_from(loops.back().tries);
                out.jump(loops.back().next);
                break;
            case StmtKind::Return:
                if (stmt.value) write_expression(*stmt.value);
                if (inline_end >= 0) {
                    out.jump(inline_end);
                } else {
                    if (!tries_open.empty()) {
                        out.push_rax();
                        close_tries_from(0);
                        out.pop_rax();
                    }
                    write_return();
                }
                break;
            case StmtKind::Try:
                write_try(stmt);
                break;
            case StmtKind::Call:
                write_expression(*stmt.value);
                break;
            case StmtKind::Print:
                write_expression(*stmt.value);
                out.note_line(place_of(stmt.spot));
                write_print(stmt.value->type);
                break;
        }
    }

    void write_assign(const Stmt& stmt) {
        if (stmt.target && stmt.target->kind == ExprKind::Field) {
            write_assign_to_field(stmt);
            return;
        }
        if (stmt.target) {
            write_assign_to_item(stmt);
            return;
        }
        int64_t bits = 0;
        const int reg = optimizing ? register_of(stmt.slot) : -1;
        if (reg >= 0 && write_register_assign(stmt, reg)) return;
        if (stmt.has_operation && optimizing && (stmt.operation == BinaryOp::Add || stmt.operation == BinaryOp::Subtract) && small_constant(*stmt.value, bits)) {
            load_slot_rax(stmt.slot);
            if (stmt.operation == BinaryOp::Add) {
                out.add_rax_value(static_cast<int32_t>(bits));
            } else {
                out.sub_rax_value(static_cast<int32_t>(bits));
            }
        } else if (stmt.has_operation && optimizing && stmt.value->type == Type::Float && stmt.operation != BinaryOp::Remainder && !has_call(stmt.value.get())) {
            // The variable can't change while the value runs, so it's read after it
            write_float_xmm1(*stmt.value);
            load_slot_rax(stmt.slot);
            out.mov_xmm0_rax();
            write_float_math(stmt.operation);
            out.mov_rax_xmm0();
        } else if (stmt.has_operation) {
            write_both_sides(*stmt.value, [&] { load_slot_rax(stmt.slot); });
            write_operation(stmt.operation, stmt.value->type, place_of(stmt.spot));
        } else {
            write_expression(*stmt.value);
        }
        store_slot_rax(stmt.slot);
    }

    // Changes a variable held in a register straight in its register, without going through rax.
    // Ints wrap, and nothing can change a local variable while its value runs, so reading it after is the same.
    // Answers false when the assignment needs the usual way
    bool write_register_assign(const Stmt& stmt, int reg) {
        int64_t bits = 0;
        if (!stmt.has_operation) {
            if (!small_constant(*stmt.value, bits)) return false;
            out.mov_reg_value(reg, static_cast<int32_t>(bits));
            return true;
        }
        if (stmt.value->type != Type::Int || (stmt.operation != BinaryOp::Add && stmt.operation != BinaryOp::Subtract)) return false;
        const bool adding = stmt.operation == BinaryOp::Add;
        if (small_constant(*stmt.value, bits)) {
            if (bits == 1) {
                if (adding) out.inc_reg(reg); else out.dec_reg(reg);
            } else if (adding) {
                out.add_reg_value(reg, static_cast<int32_t>(bits));
            } else {
                out.sub_reg_value(reg, static_cast<int32_t>(bits));
            }
            return true;
        }
        write_expression(*stmt.value);
        if (adding) out.add_reg_rax(reg); else out.sub_reg_rax(reg);
        return true;
    }

    void write_assign_to_field(const Stmt& stmt) {
        const Expr& target = *stmt.target;
        const Type type = stmt.value->type;
        const bool simple_operation = stmt.has_operation && !has_call(stmt.value.get()) &&
            ((type == Type::Int && (stmt.operation == BinaryOp::Add || stmt.operation == BinaryOp::Subtract || stmt.operation == BinaryOp::Multiply)) ||
             (type == Type::Float && (stmt.operation == BinaryOp::Add || stmt.operation == BinaryOp::Subtract || stmt.operation == BinaryOp::Multiply || stmt.operation == BinaryOp::Divide)));
        if (optimizing && target.left->kind == ExprKind::Name && target.left->slot >= 0 && (!stmt.has_operation || simple_operation)) {
            // The variable can't change while the value runs, and with an operation the value has no calls, so the field is read after it safely
            if (stmt.has_operation && type == Type::Float) {
                write_float_xmm1(*stmt.value);
                load_slot_rax(target.left->slot);
                out.mov_rdx_rax();
                out.load_xmm0_field_of_rdx(target.slot);
                write_float_math(stmt.operation);
                out.store_xmm0_in_rdx_field(target.slot);
                return;
            }
            write_expression(*stmt.value);
            if (stmt.has_operation) {
                out.mov_rcx_rax();
                load_slot_rax(target.left->slot);
                out.mov_rdx_rax();
                out.load_rax_field_of_rdx(target.slot);
                write_operation(stmt.operation, type, place_of(stmt.spot));
                out.store_rax_in_rdx_field(target.slot);
            } else {
                out.mov_r9_rax();
                load_slot_rax(target.left->slot);
                out.store_r9_in_rax_field(target.slot);
            }
            return;
        }
        write_expression(*target.left);
        out.push_rax();
        if (stmt.has_operation) {
            out.load_rax_from_stack_top();
            out.load_rax_field(target.slot);
            out.push_rax();
        }
        write_expression(*stmt.value);
        if (stmt.has_operation) {
            out.mov_rcx_rax();
            out.pop_rax();
            write_operation(stmt.operation, stmt.value->type, place_of(stmt.spot));
        }
        out.mov_r9_rax();
        out.pop_rax();
        out.store_r9_in_rax_field(target.slot);
    }

    // The item's place is only worked out after the value, since the value could grow the array
    void write_assign_to_item(const Stmt& stmt) {
        const Expr& target = *stmt.target;
        const int64_t line = place_of(stmt.spot);
        write_expression(*target.left);
        out.push_rax();
        write_expression(*target.right);
        out.push_rax();
        if (stmt.has_operation) {
            out.load_rax_from_stack_second();
            out.load_rcx_from_stack_top();
            if (optimizing) {
                write_array_read(line);
            } else {
                out.note_line(line);
                out.call(runtime.array_at);
            }
            out.push_rax();
        }
        write_expression(*stmt.value);
        if (stmt.has_operation) {
            out.mov_rcx_rax();
            out.pop_rax();
            write_operation(stmt.operation, stmt.value->type, line);
        }
        out.mov_r9_rax();
        out.pop_rcx();
        out.pop_rax();
        if (optimizing) {
            // The runtime is only called to stop with the error
            const Label inside = out.new_label();
            out.cmp_rcx_length();
            out.jump_if_below(inside);
            out.note_line(line);
            out.call(runtime.array_address);
            out.place(inside);
            out.load_rax_items();
            out.lea_rax_item_rcx();
        } else {
            out.note_line(line);
            out.call(runtime.array_address);
        }
        out.store_r9_at_rax();
    }

    void write_print(Type type) {
        if (TypeTable::is_custom(type)) {
            out.push_rax();
            out.call(runtime.builder_start);
            out.pop_rax();
            write_show(type);
            out.call(runtime.builder_print);
            return;
        }
        switch (type) {
            case Type::Bool: out.call(runtime.print_bool); break;
            case Type::Float: out.call(runtime.print_float); break;
            case Type::String: out.call(runtime.print_string); break;
            default: out.call(runtime.print_int); break;
        }
    }

    void write_if(const Stmt& stmt) {
        const Label finished = out.new_label();
        // With nothing after the last part, its jump to the end would land on the very next instruction
        const bool nothing_after = optimizing && stmt.other_body.empty();
        write_if_part(*stmt.value, stmt.body, finished, nothing_after && stmt.elifs.empty());
        for (size_t i = 0; i < stmt.elifs.size(); i++) {
            write_if_part(*stmt.elifs[i].condition, stmt.elifs[i].body, finished, nothing_after && i + 1 == stmt.elifs.size());
        }
        write_block(stmt.other_body);
        out.place(finished);
    }

    void write_if_part(const Expr& condition, const Block& body, Label finished, bool last) {
        const Label next = out.new_label();
        write_jump(condition, false, next);
        write_block(body);
        if (!last && !always_leaves(body)) out.jump(finished);
        out.place(next);
    }

    void write_match(const Stmt& stmt) {
        const Label finished = out.new_label();
        write_expression(*stmt.value);
        store_slot_rax(stmt.slot);
        for (const CasePart& part : stmt.cases) {
            const Label next = out.new_label();
            write_expression(*part.value);
            out.mov_rcx_rax();
            load_slot_rax(stmt.slot);
            if (stmt.value->type == Type::String) {
                out.call(runtime.strings_equal);
                out.test_rax_rax();
                out.jump_if_equal(next);
            } else {
                out.cmp_rax_rcx();
                out.jump_if_not_equal(next);
            }
            write_block(part.body);
            if (!always_leaves(part.body)) out.jump(finished);
            out.place(next);
        }
        write_block(stmt.other_body);
        out.place(finished);
    }

    // --- try and catch ---
    // A try fills in 8 slots, as a record the runtime can follow: [0] the try around this one, [8] rbp, [16] rsp,
    // [24] where the catch is, then r12 to r15. The newest try's record is kept in the data section. A run error
    // with a try open goes back to that try's rbp and rsp and jumps to its catch with the error's words in rax,
    // and the catch puts r12 to r15 back, since the functions in between may have changed them

    // The frame offset of one field of a try's record
    int try_field(int first_slot, int field) const { return frame_offset(first_slot + TRY_SLOTS - 1 - field); }

    // Makes the try around the ones from this place on the newest again, as when leaving them
    void close_tries_from(size_t place) {
        out.load_rax_from_frame(try_field(tries_open[place], 0));
        out.store_rax_data(DataRef::TryFrame);
    }

    void write_try(const Stmt& stmt) {
        const int first = stmt.end_slot;
        const Label catcher = out.new_label();
        const Label finished = out.new_label();
        out.load_rax_data(DataRef::TryFrame);
        out.store_rax_to_frame(try_field(first, 0));
        out.mov_rax_rbp();
        out.store_rax_to_frame(try_field(first, 1));
        out.mov_rax_rsp();
        out.store_rax_to_frame(try_field(first, 2));
        out.lea_rax_label(catcher);
        out.store_rax_to_frame(try_field(first, 3));
        for (int reg = 0; reg < VARIABLE_REGISTERS; reg++) out.store_reg_to_frame(reg, try_field(first, 4 + reg));
        out.lea_rax_frame(try_field(first, 0));
        out.store_rax_data(DataRef::TryFrame);
        tries_open.push_back(first);
        write_block(stmt.body);
        tries_open.pop_back();
        if (!always_leaves(stmt.body)) {
            out.load_rax_from_frame(try_field(first, 0));
            out.store_rax_data(DataRef::TryFrame);
            out.jump(finished);
        }
        out.place(catcher);
        for (int reg = 0; reg < VARIABLE_REGISTERS; reg++) out.load_reg_from_frame(reg, try_field(first, 4 + reg));
        if (stmt.slot >= 0) store_slot_rax(stmt.slot);
        write_block(stmt.other_body);
        out.place(finished);
    }

    static bool has_try(const Block& block) {
        for (const auto& stmt : block) {
            if (stmt->kind == StmtKind::Try || has_try(stmt->body) || has_try(stmt->other_body)) return true;
            for (const ElifPart& part : stmt->elifs) {
                if (has_try(part.body)) return true;
            }
            for (const CasePart& part : stmt->cases) {
                if (has_try(part.body)) return true;
            }
        }
        return false;
    }

    void write_while(const Stmt& stmt) {
        if (optimizing) {
            // The check sits after the body, so each round takes one jump instead of two
            const Label body = out.new_label();
            const Label check = out.new_label();
            const Label finished = out.new_label();
            push_loop({check, finished});
            out.jump(check);
            out.place(body);
            write_block(stmt.body);
            out.place(check);
            write_jump(*stmt.value, true, body);
            out.place(finished);
            loops.pop_back();
            return;
        }
        const Label start = out.new_label();
        const Label finished = out.new_label();
        push_loop({start, finished});
        out.place(start);
        write_jump(*stmt.value, false, finished);
        write_block(stmt.body);
        out.jump(start);
        out.place(finished);
        loops.pop_back();
    }

    // Looks at the array's length every round, since the loop's body can grow it
    void write_for_array(const Stmt& stmt) {
        const Label start = out.new_label();
        const Label next = out.new_label();
        const Label finished = out.new_label();
        write_expression(*stmt.value);
        store_slot_rax(stmt.array_slot);
        out.zero_eax();
        store_slot_rax(stmt.end_slot);
        push_loop({next, finished});
        if (optimizing) {
            // The check sits after the body, so each round takes one jump instead of two
            const Label body = out.new_label();
            out.jump(start);
            out.place(body);
            load_slot_rax(stmt.array_slot);
            out.load_rax_items();
            load_slot_rcx(stmt.end_slot);
            out.load_rax_item_rcx();
            store_slot_rax(stmt.slot);
            write_block(stmt.body);
            out.place(next);
            const int reg = register_of(stmt.end_slot);
            if (reg >= 0) {
                out.inc_reg(reg);
            } else {
                load_slot_rax(stmt.end_slot);
                out.inc_rax();
                store_slot_rax(stmt.end_slot);
            }
            out.place(start);
            load_slot_rax(stmt.array_slot);
            out.mov_rcx_from_rax();
            load_slot_rax(stmt.end_slot);
            out.cmp_rax_rcx();
            out.jump_if_less(body);
            out.place(finished);
            loops.pop_back();
            return;
        }
        out.place(start);
        load_slot_rax(stmt.array_slot);
        out.mov_rcx_from_rax();
        load_slot_rax(stmt.end_slot);
        out.cmp_rax_rcx();
        out.jump_if_greater_equal(finished);
        load_slot_rax(stmt.array_slot);
        out.load_rax_items();
        load_slot_rcx(stmt.end_slot);
        out.load_rax_item_rcx();
        store_slot_rax(stmt.slot);
        write_block(stmt.body);
        out.place(next);
        load_slot_rax(stmt.end_slot);
        out.inc_rax();
        store_slot_rax(stmt.end_slot);
        out.jump(start);
        out.place(finished);
        loops.pop_back();
    }

    // The variable stays below the end, so adding one to it can't overflow
    void write_for(const Stmt& stmt) {
        if (!stmt.range_end) {
            write_for_array(stmt);
            return;
        }
        const Label start = out.new_label();
        const Label next = out.new_label();
        const Label finished = out.new_label();
        write_expression(*stmt.value);
        store_slot_rax(stmt.slot);
        write_expression(*stmt.range_end);
        store_slot_rax(stmt.end_slot);
        push_loop({next, finished});
        if (optimizing) {
            // The check sits after the body, and a variable in a register is counted and compared where it is
            const Label body = out.new_label();
            out.jump(start);
            out.place(body);
            write_block(stmt.body);
            out.place(next);
            const int reg = register_of(stmt.slot);
            if (reg >= 0) {
                out.inc_reg(reg);
            } else {
                load_slot_rax(stmt.slot);
                out.inc_rax();
                store_slot_rax(stmt.slot);
            }
            out.place(start);
            const int end_reg = register_of(stmt.end_slot);
            if (reg >= 0 && end_reg >= 0) {
                out.cmp_reg_reg(reg, end_reg);
            } else {
                load_slot_rcx(stmt.end_slot);
                load_slot_rax(stmt.slot);
                out.cmp_rax_rcx();
            }
            out.jump_if_less(body);
            out.place(finished);
            loops.pop_back();
            return;
        }
        out.place(start);
        load_slot_rcx(stmt.end_slot);
        load_slot_rax(stmt.slot);
        out.cmp_rax_rcx();
        out.jump_if_greater_equal(finished);
        write_block(stmt.body);
        out.place(next);
        load_slot_rax(stmt.slot);
        out.inc_rax();
        store_slot_rax(stmt.slot);
        out.jump(start);
        out.place(finished);
        loops.pop_back();
    }

    void write_expression(const Expr& expr) {
        int64_t bits = 0;
        if (constant_of(expr, bits)) {
            out.mov_rax_number(bits);
            return;
        }
        switch (expr.kind) {
            case ExprKind::Number:
                out.mov_rax_number(static_cast<int64_t>(expr.number));
                break;
            case ExprKind::Float:
                out.mov_rax_number(std::bit_cast<int64_t>(expr.real));
                break;
            case ExprKind::Bool:
                out.mov_rax_number(expr.truth ? 1 : 0);
                break;
            case ExprKind::String:
                out.lea_rax_string(string_number(expr.text));
                break;
            case ExprKind::Index:
                if (optimizing) {
                    write_both_sides(*expr.right, [&] { write_expression(*expr.left); });
                    if (expr.left->type == Type::String) {
                        out.note_line(place_of(expr.spot));
                        out.call(runtime.string_at);
                    } else {
                        write_array_read(place_of(expr.spot));
                    }
                    break;
                }
                write_expression(*expr.left);
                out.push_rax();
                write_expression(*expr.right);
                out.mov_rcx_rax();
                out.pop_rax();
                out.note_line(place_of(expr.spot));
                out.call(expr.left->type == Type::String ? runtime.string_at : runtime.array_at);
                break;
            case ExprKind::ArrayLiteral:
                write_array_literal(expr);
                break;
            case ExprKind::MethodCall:
                write_method_call(expr);
                break;
            case ExprKind::Field:
                write_expression(*expr.left);
                out.load_rax_field(expr.slot);
                break;
            case ExprKind::StructLiteral:
                write_struct_literal(expr);
                break;
            case ExprKind::BuiltinCall:
                write_builtin_call(expr);
                break;
            case ExprKind::EnumValue:
                out.mov_rax_number(static_cast<int64_t>(expr.number));
                break;
            case ExprKind::Length:
                write_expression(*expr.left);
                out.load_rax_from_rax();
                break;
            case ExprKind::Convert:
                write_expression(*expr.left);
                write_convert(expr);
                break;
            case ExprKind::FString:
                write_fstring(expr);
                break;
            case ExprKind::Name:
                load_slot_rax(expr.slot);
                break;
            case ExprKind::Negate:
                write_expression(*expr.left);
                if (expr.type == Type::Float) {
                    out.flip_rax_sign_bit();
                } else {
                    out.neg_rax();
                }
                break;
            case ExprKind::Not:
                write_expression(*expr.left);
                out.flip_rax_bit_zero();
                break;
            case ExprKind::Binary:
                write_binary(expr);
                break;
            case ExprKind::Call:
                if (!function_named.at(expr.name)->dll.empty()) {
                    write_extern_call(expr);
                    break;
                }
                if (const Function* inlined = inlined_function(expr)) {
                    write_inline_call(*inlined, nullptr, expr.inputs);
                    break;
                }
                for (const auto& input : expr.inputs) {
                    write_expression(*input);
                    out.push_rax();
                }
                out.call(function_labels.at(expr.name));
                note_call_site(expr.spot);
                if (!expr.inputs.empty()) out.add_rsp(static_cast<int>(expr.inputs.size()) * WORD_SIZE);
                break;
            case ExprKind::FunctionValue:
            case ExprKind::Lambda: {
                // A block with the code's address, then any captured values
                out.note_line(place_of(expr.spot));
                out.mov_rcx_number(static_cast<int>(1 + expr.inputs.size()) * WORD_SIZE);
                out.call(runtime.alloc);
                out.push_rax();
                out.lea_rax_label(function_labels.at(expr.text));
                out.store_rax_in_new_struct(0);
                for (size_t i = 0; i < expr.inputs.size(); i++) {
                    write_expression(*expr.inputs[i]);
                    out.store_rax_in_new_struct(static_cast<int>(i) + 1);
                }
                out.pop_rax();
                break;
            }
            case ExprKind::Capture:
                load_slot_rax(0);
                out.load_rax_field(static_cast<int>(expr.number) + 1);
                break;
            case ExprKind::CallValue: {
                // The block goes first, under the inputs, where a function written in place finds it as its first
                // input. Named functions never look that far. r11 holds nothing in Jamo code
                write_expression(*expr.left);
                out.push_rax();
                for (const auto& input : expr.inputs) {
                    write_expression(*input);
                    out.push_rax();
                }
                out.load_rax_from_rsp(static_cast<int>(expr.inputs.size()) * WORD_SIZE);
                out.load_rax_field(0);
                out.mov_r11_rax();
                out.call_r11();
                note_call_site(expr.spot);
                out.add_rsp(static_cast<int>(expr.inputs.size() + 1) * WORD_SIZE);
                break;
            }
        }
    }

    void note_call_site(Spot spot) {
        const std::string& file = program.file_names.empty() ? file_name : program.file_names[static_cast<size_t>(spot.file < 0 ? 0 : spot.file)];
        const std::string caller = current_function == SETUP_NAME ? "setting up the variables outside functions" : shown_name(current_function);
        out.note_call_site(string_number("    called from " + caller + ", " + file + " line " + std::to_string(spot.line) + "\n"));
    }

    // --- Calling Windows, by its x64 rules ---
    // The inputs are pushed first. rbx keeps where they are, since Windows functions keep rbx as it was
    void write_extern_call(const Expr& expr) {
        const Function& function = *function_named.at(expr.name);
        for (const auto& input : expr.inputs) {
            write_expression(*input);
            out.push_rax();
        }
        const int count = static_cast<int>(expr.inputs.size());
        const int on_stack = count > 4 ? count - 4 : 0;
        const auto offset_of = [&](int i) { return (count - 1 - i) * WORD_SIZE; };
        out.mov_rbx_rsp();
        if (function.dll == "OpenGL") {
            write_opengl_address(function);
            out.mov_r11_rax();
        }
        out.sub_rsp(WINDOWS_SPACE + on_stack * WORD_SIZE + WORD_SIZE);
        out.align_rsp();
        const auto is_f32 = [&](int i) { return function.input_is_f32[static_cast<size_t>(i)]; };
        for (int i = 4; i < count; i++) {
            if (is_f32(i)) {
                out.copy_f32_input_to_stack(offset_of(i), WINDOWS_SPACE + (i - 4) * WORD_SIZE);
            } else {
                out.copy_input_to_stack(offset_of(i), WINDOWS_SPACE + (i - 4) * WORD_SIZE);
            }
        }
        for (int i = 0; i < count && i < 4; i++) {
            const bool is_float = function.inputs[static_cast<size_t>(i)].type == Type::Float;
            out.load_reg_from_inputs(i, is_float, offset_of(i));
            if (is_f32(i)) out.shrink_float_in(i);
        }
        if (function.dll == "OpenGL") {
            out.call_r11();
        } else {
            out.call_import(import_number(function));
        }
        out.mov_rsp_rbx();
        if (count) out.add_rsp(count * WORD_SIZE);
        if (function.return_type == Type::Float) {
            if (function.returns_f32) out.grow_float_answer();
            out.mov_rax_xmm0();
        }
        if (function.return_type == Type::Bool) out.eax_to_bool();
        if (function.return_type == Type::Int && function.returns_i32) out.sign_extend_eax();
    }

    int import_number(const Function& function) {
        return import_of(function.dll, function.name);
    }

    int import_of(const std::string& dll, const std::string& name) {
        const auto found = import_numbers.find(name);
        if (found != import_numbers.end()) return found->second;
        imports.push_back({dll, name});
        import_numbers[name] = static_cast<int>(imports.size()) - 1;
        return static_cast<int>(imports.size()) - 1;
    }

    // OpenGL 3.3's functions only exist once a window has OpenGL, so each is looked up on its first call.
    // Its address is kept in a string entry made only for it, which lives in the data section like any string
    void write_opengl_address(const Function& function) {
        const Label known = out.new_label();
        const int slot = string_number(std::string(8, '\0') + "OpenGL address of " + function.name);
        out.load_rax_string_bytes(slot);
        out.test_rax_rax();
        out.jump_if_not_equal(known);
        out.lea_rax_string(string_number(function.name + std::string(1, '\0')));
        out.call(opengl_lookup_label());
        out.store_rax_string_bytes(slot);
        out.place(known);
    }

    Label opengl_lookup_label() {
        if (opengl_lookup < 0) opengl_lookup = out.new_label();
        return opengl_lookup;
    }

    // rax = the name, as a string ending in a zero byte. Answers with the function's address
    void write_opengl_lookup() {
        const Label fallback = out.new_label();
        const Label found = out.new_label();
        out.place(opengl_lookup);
        out.bytes({0x55, 0x48, 0x89, 0xE5, 0x53, 0x56});          // push rbp, mov rbp, rsp, push rbx, push rsi
        out.align_rsp();
        out.sub_rsp(WINDOWS_SPACE);
        out.bytes({0x48, 0x8D, 0x58, 0x08});                      // lea rbx, [rax+8]: the name's bytes
        out.bytes({0x48, 0x89, 0xD9});                            // mov rcx, rbx
        out.call_import(import_of("opengl32.dll", "wglGetProcAddress"));
        out.bytes({0x48, 0x83, 0xF8, 0x03});                      // cmp rax, 3: these mean "not found" too
        out.jump_if_at_most(fallback);
        out.bytes({0x48, 0x83, 0xF8, 0xFF});                      // cmp rax, -1
        out.jump_if_not_equal(found);
        out.place(fallback);
        out.lea_string(RCX, string_number(std::string("opengl32.dll") + std::string(1, '\0')));
        out.bytes({0x48, 0x83, 0xC1, 0x08});                      // add rcx, 8
        out.call_import(import_of("kernel32.dll", "GetModuleHandleA"));
        out.bytes({0x48, 0x89, 0xC1, 0x48, 0x89, 0xDA});          // mov rcx, rax, mov rdx, rbx
        out.call_import(import_of("kernel32.dll", "GetProcAddress"));
        out.test_rax_rax();
        out.jump_if_not_equal(found);
        out.lea_rax_string(string_number("OpenGL 3.3 isn't available on this PC, so it can't draw."));
        out.call(runtime.run_error);
        out.place(found);
        out.bytes({0x48, 0x8D, 0x65, 0xF0, 0x5E, 0x5B, 0x5D, 0xC3});  // lea rsp, [rbp-16], pop rsi, pop rbx, pop rbp, ret
    }

    // Windows calls this with its own rules, and it calls the Jamo function with Jamo's
    void write_callback(const Function& function, Label label) {
        out.place(label);
        out.bytes({0x55, 0x48, 0x89, 0xE5});              // push rbp, mov rbp, rsp
        out.bytes({0x53, 0x56, 0x57});                    // push rbx, rsi, rdi: Windows expects these kept
        out.bytes({0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});  // push r12 to r15
        out.bytes({0x48, 0x83, 0xEC, 0x08});              // sub rsp, 8
        // A try open outside doesn't reach in here: a run error must never jump back across Windows' own code
        out.load_rax_data(DataRef::TryFrame);
        out.bytes({0x48, 0x89, 0x45, 0xC0});              // mov [rbp-64], rax
        out.zero_eax();
        out.store_rax_data(DataRef::TryFrame);
        static const std::vector<std::vector<uint8_t>> pushes = {{0x51}, {0x52}, {0x41, 0x50}, {0x41, 0x51}};  // rcx, rdx, r8, r9
        for (size_t i = 0; i < function.inputs.size(); i++) out.bytes_from(pushes[i]);
        out.call(function_labels.at(function.name));
        if (!function.inputs.empty()) out.add_rsp(static_cast<int>(function.inputs.size()) * WORD_SIZE);
        out.bytes({0x48, 0x8B, 0x4D, 0xC0});              // mov rcx, [rbp-64]
        out.store_rcx_data(DataRef::TryFrame);
        out.bytes({0x48, 0x8D, 0x65, 0xC8});              // lea rsp, [rbp-56]
        out.bytes({0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5F, 0x5E, 0x5B, 0x5D, 0xC3});  // pop them back, ret
    }

    void write_peek(const std::string& name) {
        if (name == "peek_i64" || name == "peek_f64") out.bytes({0x48, 0x8B, 0x00});        // mov rax, [rax]
        if (name == "peek_i32") out.bytes({0x48, 0x63, 0x00});                             // movsxd rax, dword [rax]
        if (name == "peek_u16") out.bytes({0x0F, 0xB7, 0x00});                             // movzx eax, word [rax]
        if (name == "peek_u8") out.bytes({0x0F, 0xB6, 0x00});                              // movzx eax, byte [rax]
        if (name == "peek_f32") {
            out.bytes({0xF3, 0x0F, 0x5A, 0x00});                                           // cvtss2sd xmm0, [rax]
            out.mov_rax_xmm0();
        }
    }

    // rax = where, rcx = the value
    void write_poke(const std::string& name) {
        if (name == "poke_i64" || name == "poke_f64") out.bytes({0x48, 0x89, 0x08});       // mov [rax], rcx
        if (name == "poke_i32") out.bytes({0x89, 0x08});                                   // mov [rax], ecx
        if (name == "poke_u16") out.bytes({0x66, 0x89, 0x08});                             // mov [rax], cx
        if (name == "poke_u8") out.bytes({0x88, 0x08});                                    // mov [rax], cl
        if (name == "poke_f32") out.bytes({0x66, 0x48, 0x0F, 0x6E, 0xC1, 0xF2, 0x0F, 0x5A, 0xC0, 0xF3, 0x0F, 0x11, 0x00});  // movq xmm0, rcx; cvtsd2ss; movss [rax], xmm0
    }

    void write_builtin_call(const Expr& expr) {
        const std::string& name = expr.name;
        if (name == "callback") {
            if (!callback_labels.count(expr.text)) callback_labels[expr.text] = out.new_label();
            out.lea_rax_label(callback_labels.at(expr.text));
            return;
        }
        if (name.rfind("peek_", 0) == 0) {
            write_both_sides(*expr.inputs[1], [&] { write_expression(*expr.inputs[0]); });
            out.add_rax_rcx_only();
            write_peek(name);
            return;
        }
        if (name.rfind("poke_", 0) == 0) {
            write_expression(*expr.inputs[0]);
            out.push_rax();
            write_expression(*expr.inputs[1]);
            out.push_rax();
            write_expression(*expr.inputs[2]);
            out.mov_rcx_rax();
            out.pop_rdx();
            out.pop_rax();
            out.add_rax_rdx();
            write_poke(name);
            return;
        }
        if (name == "hash_of") {
            write_expression(*expr.inputs[0]);
            if (expr.inputs[0]->type == Type::String) out.hash_text_rax();
            out.mix_hash_rax();
            return;
        }
        if (name == "jamo_frame") {
            out.bytes({0x48, 0x89, 0xE8});  // mov rax, rbp
            return;
        }
        if (name == "memory") {
            write_expression(*expr.inputs[0]);
            out.mov_rcx_rax();
            out.note_line(place_of(expr.spot));
            out.call(runtime.alloc_text);
            return;
        }
        if (name == "memory_text") {
            write_both_sides(*expr.inputs[1], [&] { write_expression(*expr.inputs[0]); });
            out.bytes({0x48, 0x89, 0xC2});                        // mov rdx, rax
            out.bytes({0x49, 0x89, 0xC8});                        // mov r8, rcx
            out.bytes({0x4D, 0x85, 0xC0});                        // test r8, r8
            out.bytes({0x79, 0x03});                              // jns past the next line
            out.bytes({0x45, 0x31, 0xC0});                        // xor r8d, r8d: below 0 bytes gives empty text
            out.note_line(place_of(expr.spot));
            out.call(runtime.make_string);
            return;
        }
        if (name == "byte") {
            const Label inside = out.new_label();
            write_both_sides(*expr.inputs[1], [&] { write_expression(*expr.inputs[0]); });
            out.bytes({0x48, 0x3B, 0x08});                        // cmp rcx, [rax]
            out.jump_if_below(inside);
            out.note_line(place_of(expr.spot));
            out.call(runtime.string_at);                          // Stops with the usual error
            out.place(inside);
            out.bytes({0x0F, 0xB6, 0x44, 0x08, 0x08});            // movzx eax, byte [rax+rcx+8]
            return;
        }
        if (name == "fail") {
            write_expression(*expr.inputs[0]);
            out.note_line(place_of(expr.spot));
            out.call(runtime.run_error);
            return;
        }
        if (name == "wide") {
            write_expression(*expr.inputs[0]);
            out.note_line(place_of(expr.spot));
            out.call(runtime.to_wide);
            return;
        }
        if (expr.name == "write_file") {
            write_expression(*expr.inputs[0]);
            out.push_rax();
            write_expression(*expr.inputs[1]);
            out.mov_rcx_rax();
            out.pop_rax();
        } else if (!expr.inputs.empty()) {
            write_expression(*expr.inputs[0]);
        }
        out.note_line(place_of(expr.spot));
        if (expr.name == "input") out.call(runtime.read_line);
        if (expr.name == "args") out.call(runtime.get_args);
        if (expr.name == "read_file") out.call(runtime.read_whole_file);
        if (expr.name == "write_file") out.call(runtime.write_whole_file);
        if (expr.name == "exists") out.call(runtime.file_exists);
    }

    void write_struct_literal(const Expr& expr) {
        const size_t field_count = program.types.fields_of(expr.type).size();
        out.note_line(place_of(expr.spot));
        out.mov_rcx_number(static_cast<int>(field_count == 0 ? 1 : field_count) * WORD_SIZE);
        out.call(runtime.alloc);
        out.push_rax();
        for (size_t i = 0; i < expr.inputs.size(); i++) {
            write_expression(*expr.inputs[i]);
            out.store_rax_in_new_struct(expr.field_numbers[i]);
        }
        out.pop_rax();
    }

    void write_array_literal(const Expr& expr) {
        out.note_line(place_of(expr.spot));
        out.mov_rcx_number(static_cast<int>(expr.inputs.size()));
        out.call(runtime.new_array);
        out.push_rax();
        for (size_t i = 0; i < expr.inputs.size(); i++) {
            write_expression(*expr.inputs[i]);
            out.store_rax_in_new_array(static_cast<int>(i));
        }
        out.pop_rax();
    }

    void write_method_call(const Expr& expr) {
        if (const Function* inlined = inlined_function(expr)) {
            write_inline_call(*inlined, expr.left.get(), expr.inputs);
            return;
        }
        write_expression(*expr.left);
        if (program.types.is_struct(expr.left->type)) {
            out.push_rax();
            for (const auto& input : expr.inputs) {
                write_expression(*input);
                out.push_rax();
            }
            out.call(function_labels.at(program.types.name(expr.left->type) + "." + expr.name));
            note_call_site(expr.spot);
            out.add_rsp(static_cast<int>(expr.inputs.size() + 1) * WORD_SIZE);
            return;
        }
        if (expr.name == "pop") {
            out.note_line(place_of(expr.spot));
            out.call(runtime.array_pop);
            return;
        }
        if (optimizing) {
            // When there's room, the item goes straight in. Only growing calls the runtime
            const Label has_room = out.new_label();
            const Label finished = out.new_label();
            write_both_sides(*expr.inputs[0], [] {});
            out.load_rdx_length();
            out.cmp_rdx_room();
            out.jump_if_below(has_room);
            out.note_line(place_of(expr.spot));
            out.call(runtime.array_add);
            out.jump(finished);
            out.place(has_room);
            out.store_rcx_as_item_rdx();
            out.grow_length();
            out.place(finished);
            return;
        }
        out.push_rax();
        write_expression(*expr.inputs[0]);
        out.mov_rcx_rax();
        out.pop_rax();
        out.note_line(place_of(expr.spot));
        out.call(runtime.array_add);
    }

    // --- Showing arrays and other custom types ---
    // rax = the value, added to the text builder
    void write_show(Type type) {
        switch (type) {
            case Type::Int: out.call(runtime.builder_add_int); break;
            case Type::Float: out.call(runtime.builder_add_float); break;
            case Type::Bool: out.call(runtime.builder_add_bool); break;
            case Type::String: out.call(runtime.builder_add_quoted); break;
            default: out.call(show_label(type)); break;
        }
    }

    Label show_label(Type type) {
        const auto found = show_labels.find(type);
        if (found != show_labels.end()) return found->second;
        const Label label = out.new_label();
        show_labels[type] = label;
        shows_to_write.push_back(type);
        return label;
    }

    void write_show_word(const std::string& word) {
        out.lea_rax_string(string_number(word));
        out.call(runtime.builder_add_string);
    }

    // The frame: [rbp-8] is how far along it is, and show_enter uses [rbp-16] and [rbp-24]
    void write_show_function(Type type) {
        if (program.types.is_enum(type)) {
            write_show_enum(type);
            return;
        }
        if (program.types.is_function(type)) {
            // A function value shows as its type, like func(int): bool
            out.place(show_labels.at(type));
            out.lea_rax_string(string_number(program.types.name(type)));
            out.jump(runtime.builder_add_string);
            return;
        }
        const Label not_shown_yet = out.new_label();
        out.place(show_labels.at(type));
        out.push_rbp();
        out.mov_rbp_rsp();
        out.sub_rsp(SHOW_FRAME);
        out.call(runtime.show_enter);
        out.test_rax_rax();
        out.jump_if_equal(not_shown_yet);
        write_show_word("...");
        out.leave_and_return();
        out.place(not_shown_yet);
        if (program.types.is_struct(type)) {
            write_show_struct(type);
        } else {
            write_show_array(type);
        }
        out.call(runtime.show_leave);
        out.leave_and_return();
    }

    void write_show_enum(Type type) {
        out.place(show_labels.at(type));
        const std::vector<std::string>& members = program.types.members_of(type);
        for (size_t i = 0; i < members.size(); i++) {
            const Label next = out.new_label();
            out.mov_rcx_number(static_cast<int>(i));
            out.cmp_rax_rcx();
            out.jump_if_not_equal(next);
            out.lea_rax_string(string_number(program.types.name(type) + "." + members[i]));
            out.jump(runtime.builder_add_string);
            out.place(next);
        }
    }

    void write_show_struct(Type type) {
        std::string opening = program.types.name(type) + "{";
        const std::vector<StructField>& fields = program.types.fields_of(type);
        for (size_t i = 0; i < fields.size(); i++) {
            write_show_word(opening + (i == 0 ? "" : ", ") + fields[i].name + ": ");
            opening.clear();
            out.load_rax_from_frame(SHOW_VALUE);
            out.load_rax_field(static_cast<int>(i));
            write_show(fields[i].type);
        }
        write_show_word(opening + "}");
    }

    void write_show_array(Type type) {
        const Label start = out.new_label();
        const Label no_comma = out.new_label();
        const Label finished = out.new_label();
        write_show_word("[");
        out.zero_eax();
        out.store_rax_to_frame(SHOW_PROGRESS);
        out.place(start);
        out.load_rax_from_frame(SHOW_VALUE);
        out.mov_rcx_from_rax();
        out.load_rax_from_frame(SHOW_PROGRESS);
        out.cmp_rax_rcx();
        out.jump_if_greater_equal(finished);
        out.test_rax_rax();
        out.jump_if_equal(no_comma);
        write_show_word(", ");
        out.place(no_comma);
        out.load_rax_from_frame(SHOW_VALUE);
        out.load_rax_items();
        out.load_rcx_from_frame(SHOW_PROGRESS);
        out.load_rax_item_rcx();
        write_show(program.types.element_of(type));
        out.load_rax_from_frame(SHOW_PROGRESS);
        out.inc_rax();
        out.store_rax_to_frame(SHOW_PROGRESS);
        out.jump(start);
        out.place(finished);
        write_show_word("]");
    }

    void write_convert(const Expr& expr) {
        if (expr.convert_to == Type::String) {
            write_to_string(expr.left->type, place_of(expr.spot));
        } else if (expr.left->type == Type::String) {
            out.note_line(place_of(expr.spot));
            out.call(expr.convert_to == Type::Int ? runtime.string_to_int : runtime.string_to_float);
        } else if (expr.convert_to == Type::Int) {
            out.note_line(place_of(expr.spot));
            out.call(runtime.float_to_int);
        } else {
            out.int_to_float();
            out.mov_rax_xmm0();
        }
    }

    void write_to_string(Type type, int64_t line) {
        if (type == Type::String) return;
        out.note_line(line);
        if (TypeTable::is_custom(type)) {
            out.push_rax();
            out.call(runtime.builder_start);
            out.pop_rax();
            write_show(type);
            out.call(runtime.builder_to_string);
            return;
        }
        switch (type) {
            case Type::Bool: out.call(runtime.bool_to_string); break;
            case Type::Float: out.call(runtime.float_to_string); break;
            default: out.call(runtime.int_to_string); break;
        }
    }

    void write_fstring(const Expr& expr) {
        if (optimizing && expr.inputs.size() >= 2) {
            std::vector<const Expr*> parts;
            for (const auto& part : expr.inputs) parts.push_back(part.get());
            write_join_many(parts, place_of(expr.spot), true);
            return;
        }
        out.lea_rax_string(string_number(""));
        for (const auto& part : expr.inputs) {
            out.push_rax();
            write_expression(*part);
            write_to_string(part->type, place_of(expr.spot));
            out.mov_rcx_rax();
            out.pop_rax();
            out.note_line(place_of(expr.spot));
            out.call(runtime.join_strings);
        }
    }

    // The right side only runs when the left side doesn't already decide the answer
    void write_and_or(const Expr& expr) {
        const Label finished = out.new_label();
        write_expression(*expr.left);
        out.test_rax_rax();
        if (expr.op == BinaryOp::And) {
            out.jump_if_equal(finished);
        } else {
            out.jump_if_not_equal(finished);
        }
        write_expression(*expr.right);
        out.place(finished);
    }

    // The pieces of a chain of + on text, like "a" + b + "c", left to right
    static void collect_joined(const Expr& expr, std::vector<const Expr*>& parts) {
        if (expr.kind == ExprKind::Binary && expr.op == BinaryOp::Add && expr.type == Type::String) {
            collect_joined(*expr.left, parts);
            collect_joined(*expr.right, parts);
        } else {
            parts.push_back(&expr);
        }
    }

    // Pushes each piece, then makes the text once, instead of making a new string at every +
    void write_join_many(const std::vector<const Expr*>& parts, int64_t line, bool to_text) {
        for (const Expr* part : parts) {
            write_expression(*part);
            if (to_text) write_to_string(part->type, line);
            out.push_rax();
        }
        out.mov_rcx_number(static_cast<int>(parts.size()));
        out.note_line(line);
        out.call(runtime.join_many);
        out.add_rsp(static_cast<int>(parts.size()) * WORD_SIZE);
    }

    void write_binary(const Expr& expr) {
        if (expr.op == BinaryOp::And || expr.op == BinaryOp::Or) {
            write_and_or(expr);
            return;
        }
        if (optimizing && expr.op == BinaryOp::Add && expr.type == Type::String) {
            std::vector<const Expr*> parts;
            collect_joined(expr, parts);
            if (parts.size() > 2) {
                write_join_many(parts, place_of(expr.spot), false);
                return;
            }
        }
        int64_t divisor = 0;
        if ((expr.op == BinaryOp::Divide || expr.op == BinaryOp::Remainder) && expr.left->type == Type::Int && constant_of(*expr.right, divisor) && divisor >= 2) {
            write_expression(*expr.left);
            write_divide_by_constant(divisor, expr.op == BinaryOp::Remainder);
            return;
        }
        const Type type = expr.left->type;
        int64_t bits = 0;
        if (optimizing && type != Type::Float && type != Type::String && expr.op != BinaryOp::Multiply && expr.op != BinaryOp::Divide && expr.op != BinaryOp::Remainder && small_constant(*expr.right, bits)) {
            write_expression(*expr.left);
            if (expr.op == BinaryOp::Add) {
                out.add_rax_value(static_cast<int32_t>(bits));
            } else if (expr.op == BinaryOp::Subtract) {
                out.sub_rax_value(static_cast<int32_t>(bits));
            } else {
                out.cmp_rax_value(static_cast<int32_t>(bits));
                out.setcc_al(condition_of(expr.op));
                out.movzx_eax_al();
            }
            return;
        }
        if (optimizing && type == Type::Float && expr.op != BinaryOp::Remainder) {
            write_float_sides(*expr.left, *expr.right);
            write_float_finish(expr.op);
            return;
        }
        write_both_sides(*expr.right, [&] { write_expression(*expr.left); });
        write_operation(expr.op, expr.left->type, place_of(expr.spot));
    }

    // --- Dividing by a constant ---
    // A number to multiply by and a shift that give the same answer as dividing by d, for d of 2 or more,
    // from Hacker's Delight. Dividing by a constant can't fail, so there are no checks either
    struct Magic {
        int64_t multiply = 0;
        int shift = 0;
    };

    static Magic magic_for(int64_t d) {
        const uint64_t two63 = 1ULL << 63;
        const uint64_t ad = static_cast<uint64_t>(d);
        const uint64_t anc = two63 - 1 - two63 % ad;
        int p = 63;
        uint64_t q1 = two63 / anc;
        uint64_t r1 = two63 - q1 * anc;
        uint64_t q2 = two63 / ad;
        uint64_t r2 = two63 - q2 * ad;
        uint64_t delta = 0;
        do {
            p++;
            q1 *= 2;
            r1 *= 2;
            if (r1 >= anc) {
                q1++;
                r1 -= anc;
            }
            q2 *= 2;
            r2 *= 2;
            if (r2 >= ad) {
                q2++;
                r2 -= ad;
            }
            delta = ad - r2;
        } while (q1 < delta || (q1 == delta && r1 == 0));
        return {static_cast<int64_t>(q2 + 1), p - 64};
    }

    // rax = the number. Leaves the answer in rax
    void write_divide_by_constant(int64_t d, bool remainder) {
        const Magic magic = magic_for(d);
        out.mov_r8_rax();
        out.mov_r11_value(magic.multiply);
        out.imul_r11();
        if (magic.multiply < 0) out.add_rdx_r8();
        if (magic.shift > 0) out.sar_rdx(magic.shift);
        out.mov_rax_rdx();
        out.shr_rax_63();
        out.add_rdx_rax();
        if (remainder) {
            if (d <= std::numeric_limits<int32_t>::max()) {
                out.imul_rdx_value(static_cast<int32_t>(d));
            } else {
                out.mov_r11_value(d);
                out.imul_rdx_r11();
            }
            out.mov_rax_r8();
            out.sub_rax_rdx();
        } else {
            out.mov_rax_rdx();
        }
    }

    // rax = the left side, rcx = the right side
    void write_operation(BinaryOp op, Type type, int64_t line) {
        if (type == Type::String) {
            write_string_operation(op, line);
        } else if (type == Type::Float) {
            write_float_operation(op);
        } else {
            write_int_operation(op, line);
        }
    }

    void write_string_operation(BinaryOp op, int64_t line) {
        if (op == BinaryOp::Add) {
            out.note_line(line);
            out.call(runtime.join_strings);
            return;
        }
        out.call(runtime.strings_equal);
        if (op == BinaryOp::NotEqual) out.flip_rax_bit_zero();
    }

    void write_float_operation(BinaryOp op) {
        if (op == BinaryOp::Remainder) {
            out.call(runtime.float_mod);
            return;
        }
        out.mov_xmm0_rax();
        out.mov_xmm1_rcx();
        write_float_finish(op);
    }

    // xmm0 = the left side, xmm1 = the right side. Leaves the answer in rax
    void write_float_finish(BinaryOp op) {
        if (is_float_math(op)) {
            write_float_math(op);
            out.mov_rax_xmm0();
            return;
        }
        // These flags say "below" for NaN, so only "above" answers can be true when a NaN is compared
        switch (op) {
            case BinaryOp::Less: out.compare_xmm1_xmm0(); out.setcc_al(SET_ABOVE); break;
            case BinaryOp::Greater: out.compare_xmm0_xmm1(); out.setcc_al(SET_ABOVE); break;
            case BinaryOp::LessEqual: out.compare_xmm1_xmm0(); out.setcc_al(SET_AT_LEAST); break;
            case BinaryOp::GreaterEqual: out.compare_xmm0_xmm1(); out.setcc_al(SET_AT_LEAST); break;
            case BinaryOp::Equal:
                out.compare_xmm0_xmm1();
                out.setcc_al(SET_EQUAL);
                out.setcc_cl(SET_ORDERED);
                out.and_al_cl();
                break;
            default:  // NotEqual
                out.compare_xmm0_xmm1();
                out.setcc_al(SET_NOT_EQUAL);
                out.setcc_cl(SET_UNORDERED);
                out.or_al_cl();
                break;
        }
        out.movzx_eax_al();
    }

    // --- Float math kept in float registers ---
    static bool is_float_math(BinaryOp op) {
        return op == BinaryOp::Add || op == BinaryOp::Subtract || op == BinaryOp::Multiply || op == BinaryOp::Divide;
    }

    // xmm0 = xmm0 (op) xmm1
    void write_float_math(BinaryOp op) {
        switch (op) {
            case BinaryOp::Add: out.addsd(); break;
            case BinaryOp::Subtract: out.subsd(); break;
            case BinaryOp::Multiply: out.mulsd(); break;
            default: out.divsd(); break;
        }
    }

    // Leaves a float's value in xmm0, without moving it through rax between operations
    void write_float_xmm0(const Expr& expr) {
        int64_t bits = 0;
        if (expr.kind == ExprKind::Binary && expr.left->type == Type::Float && is_float_math(expr.op) && !constant_of(expr, bits)) {
            write_float_sides(*expr.left, *expr.right);
            write_float_math(expr.op);
        } else if (expr.kind == ExprKind::Convert && expr.convert_to == Type::Float && expr.left->type == Type::Int) {
            write_expression(*expr.left);
            out.int_to_float();
        } else {
            write_expression(expr);
            out.mov_xmm0_rax();
        }
    }

    // Leaves the left side in xmm0 and the right in xmm1. A simple left side is read last when the right side
    // has no calls, since then nothing can change it
    void write_float_sides(const Expr& left, const Expr& right) {
        if (is_simple(left) && !is_simple(right) && !has_call(&right)) {
            write_float_xmm0(right);
            out.mov_xmm1_xmm0();
            write_simple_to_rcx(left);
            out.mov_xmm0_rcx();
            return;
        }
        write_float_xmm0(left);
        if (is_simple(right)) {
            write_simple_to_rcx(right);
            out.mov_xmm1_rcx();
            return;
        }
        out.mov_rax_xmm0();
        out.push_rax();
        write_float_xmm0(right);
        out.mov_xmm1_xmm0();
        out.pop_rax();
        out.mov_xmm0_rax();
    }

    // Leaves a float's value in xmm1
    void write_float_xmm1(const Expr& expr) {
        if (is_simple(expr)) {
            write_simple_to_rcx(expr);
            out.mov_xmm1_rcx();
            return;
        }
        write_float_xmm0(expr);
        out.mov_xmm1_xmm0();
    }

    void write_int_operation(BinaryOp op, int64_t line) {
        switch (op) {
            case BinaryOp::Add: out.add_rax_rcx(); break;
            case BinaryOp::Subtract: out.sub_rax_rcx(); break;
            case BinaryOp::Multiply: out.imul_rax_rcx(); break;
            case BinaryOp::Divide: write_divide(line); break;
            case BinaryOp::Remainder: write_remainder(line); break;
            default:
                out.cmp_rax_rcx();
                out.setcc_al(condition_of(op));
                out.movzx_eax_al();
                break;
        }
    }

    // The line is only noted when it's needed, on the way to the error
    void write_divide_by_zero_check(int64_t line) {
        const Label not_zero = out.new_label();
        out.test_rcx_rcx();
        out.jump_if_not_equal(not_zero);
        out.note_line(line);
        out.jump(runtime.divide_by_zero);
        out.place(not_zero);
    }

    void write_divide(int64_t line) {
        const Label normal = out.new_label();
        const Label finished = out.new_label();
        write_divide_by_zero_check(line);
        // The processor crashes on the smallest int / -1, and x / -1 is always just -x
        out.cmp_rcx_minus_one();
        out.jump_if_not_equal(normal);
        out.neg_rax();
        out.jump(finished);
        out.place(normal);
        out.cqo();
        out.idiv_rcx();
        out.place(finished);
    }

    void write_remainder(int64_t line) {
        const Label normal = out.new_label();
        const Label finished = out.new_label();
        write_divide_by_zero_check(line);
        // The same crash as dividing, and x % -1 is always 0
        out.cmp_rcx_minus_one();
        out.jump_if_not_equal(normal);
        out.zero_eax();
        out.jump(finished);
        out.place(normal);
        out.cqo();
        out.idiv_rcx();
        out.mov_rax_rdx();
        out.place(finished);
    }

    const Program& program;
    Assembler out;
    std::map<std::string, Label> function_labels;
    const std::string& file_name;
    int cleaner_mode;
    bool optimizing;
    bool tracing = false;
    std::set<std::string> functions_in_use;
    std::map<std::string, const Function*> function_named;
    std::map<std::string, Label> callback_labels;
    std::map<int, int> slot_registers;
    int saved_count = 0;
    std::vector<std::pair<std::string, std::string>> imports;
    std::map<std::string, int> import_numbers;
    Label opengl_lookup = -1;
    std::string current_function;
    std::map<std::string, bool> inline_ok;
    int slot_shift = -1;   // While a function is written in place, where its slots start in the caller's frame
    Label inline_end = -1; // While a function is written in place, the end of its body, where its returns go
    int inline_top = 0;    // The next spare slot for a call written in place
    struct Loop {
        Label next;
        Label finished;
        size_t tries = 0;  // How many trys were open when it started, so break and continue close the ones inside it
    };

    std::vector<Loop> loops;
    std::vector<int> tries_open;  // The first slot of each try being written, outermost first

    void push_loop(Loop loop) {
        loop.tries = tries_open.size();
        loops.push_back(loop);
    }
    std::map<Type, Label> show_labels;
    std::vector<Type> shows_to_write;
    Runtime runtime;
    RuntimeText text;
    std::vector<std::string> strings;
    std::map<std::string, int> string_numbers;
    int input_count = 0;
};

}  // namespace

MachineCode generate_code(const Program& program, const std::string& file_name, int test_flags) {
    Generator generator(program, file_name, test_flags);
    return generator.generate();
}
