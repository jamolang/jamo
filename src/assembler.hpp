// Assembler: writes x86-64 instructions as bytes, and fills in the distances to labels.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// Things the code points at, whose addresses only the .exe writer knows
enum class DataRef {
    GetStdHandle, WriteFile, ExitProcess, GetProcessHeap, HeapAlloc, CreateFileW, ReadFile, CloseHandle, GetFileAttributesW, GetCommandLineW, MultiByteToWideChar, HeapFree,
    LoadLibraryA, GetProcAddress,
    HeapHandle, CurrentLine, Builder, VisitHead, Stdin, Cleaner, Letters, ErrorText, CallTable, String, Import,
    Globals,  // Where the variables outside functions are, set at startup
    TryFrame,  // The newest open try's record, or 0
};

// A 4-byte gap in the code, to be filled with the distance from the end of the gap to the thing
struct DataFixup {
    size_t offset = 0;
    DataRef target = DataRef::HeapHandle;
    int string = 0;  // Which one, for DataRef::String and DataRef::Import
    int extra = 0;   // Bytes past the start of the thing
};

// A place a Jamo function call returns to, and the string saying where that call was
struct CallSite {
    size_t offset = 0;
    int string = 0;
};

struct MachineCode {
    std::vector<uint8_t> bytes;
    std::vector<DataFixup> fixups;
    std::vector<CallSite> call_sites;
    std::vector<std::pair<std::string, std::string>> imports;  // Each extern function's DLL and name
    std::vector<std::string> strings;
    bool window = false;  // A window program, with no console
};

using Label = int;

enum Reg : uint8_t { RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSI = 6, RDI = 7, R8 = 8, R9 = 9 };

class Assembler {
public:
    void bytes(std::initializer_list<uint8_t> list) {
        code.bytes.insert(code.bytes.end(), list);
    }

    void bytes_from(const std::vector<uint8_t>& list) {
        code.bytes.insert(code.bytes.end(), list.begin(), list.end());
    }

    void int32(int32_t value) {
        const uint32_t bits = static_cast<uint32_t>(value);
        for (int shift = 0; shift < 32; shift += 8) code.bytes.push_back(static_cast<uint8_t>(bits >> shift));
    }

    void int64(int64_t value) {
        const uint64_t bits = static_cast<uint64_t>(value);
        for (int shift = 0; shift < 64; shift += 8) code.bytes.push_back(static_cast<uint8_t>(bits >> shift));
    }

    Label new_label() {
        places.push_back(-1);
        return static_cast<Label>(places.size()) - 1;
    }

    void place(Label label) {
        places[static_cast<size_t>(label)] = static_cast<int64_t>(code.bytes.size());
    }

    // Adds a 4-byte distance to a label, filled in at the end
    void label_gap(Label label) {
        gaps.push_back({code.bytes.size(), label});
        int32(0);
    }

    void data_gap(DataRef target, int string = 0) {
        code.fixups.push_back({code.bytes.size(), target, string});
        int32(0);
    }

    void note_call_site(int string) {
        code.call_sites.push_back({code.bytes.size(), string});
    }

    MachineCode finish(std::vector<std::string> strings, std::vector<std::pair<std::string, std::string>> imports) {
        code.imports = std::move(imports);
        for (const Gap& gap : gaps) {
            const int64_t distance = places[static_cast<size_t>(gap.label)] - static_cast<int64_t>(gap.offset + 4);
            const uint32_t bits = static_cast<uint32_t>(static_cast<int32_t>(distance));
            for (size_t i = 0; i < 4; i++) code.bytes[gap.offset + i] = static_cast<uint8_t>(bits >> (i * 8));
        }
        code.strings = std::move(strings);
        return std::move(code);
    }

    // --- Instructions ---
    void push_rax() { bytes({0x50}); }
    void pop_rax() { bytes({0x58}); }
    void push_rbp() { bytes({0x55}); }
    void mov_rbp_rsp() { bytes({0x48, 0x89, 0xE5}); }
    void leave_and_return() { bytes({0xC9, 0xC3}); }

    // Dividing by a constant with a multiply
    void mov_r8_rax() { bytes({0x49, 0x89, 0xC0}); }
    void mov_rax_r8() { bytes({0x4C, 0x89, 0xC0}); }
    void mov_r11_value(int64_t value) {
        bytes({0x49, 0xBB});
        int64(value);
    }
    void imul_r11() { bytes({0x49, 0xF7, 0xEB}); }                 // rdx:rax = rax * r11
    void add_rdx_r8() { bytes({0x4C, 0x01, 0xC2}); }
    void sar_rdx(int count) { bytes({0x48, 0xC1, 0xFA, static_cast<uint8_t>(count)}); }
    void shr_rax_63() { bytes({0x48, 0xC1, 0xE8, 0x3F}); }
    void add_rdx_rax() { bytes({0x48, 0x01, 0xC2}); }
    // Variables outside functions: r11 = where they are, then one of them by its place. r11 holds nothing in Jamo code
    void load_r11_globals() {
        bytes({0x4C, 0x8B, 0x1D});                                            // mov r11, [rip+globals]
        data_gap(DataRef::Globals);
    }
    void load_rax_global(int offset) {                                        // mov rax, [r11+offset]
        bytes({0x49, 0x8B, 0x83});
        int32(offset);
    }
    void load_rcx_global(int offset) {                                        // mov rcx, [r11+offset]
        bytes({0x49, 0x8B, 0x8B});
        int32(offset);
    }
    void store_rax_global(int offset) {                                       // mov [r11+offset], rax
        bytes({0x49, 0x89, 0x83});
        int32(offset);
    }
    // Zeroes count words from rsp up, then keeps rsp as where the variables outside functions are
    void start_globals(int count) {
        bytes({0x48, 0x89, 0xE7});                                            // mov rdi, rsp
        bytes({0xB9});                                                        // mov ecx, count
        int32(count);
        bytes({0x31, 0xC0, 0xF3, 0x48, 0xAB});                                // xor eax, eax; rep stosq
        bytes({0x48, 0x89, 0x25});                                            // mov [rip+globals], rsp
        data_gap(DataRef::Globals);
    }

    void imul_rdx_r11() { bytes({0x49, 0x0F, 0xAF, 0xD3}); }

    // rax = text. FNV-1a over its bytes, leaving the number in rax. Uses rcx, rdx, r8 and r9
    void hash_text_rax() {
        bytes({0x48, 0x8B, 0x08});                                            // mov rcx, [rax]: the length
        bytes({0x48, 0x8D, 0x50, 0x08});                                      // lea rdx, [rax+8]: the bytes
        bytes({0x48, 0xB8, 0x25, 0x23, 0x22, 0x84, 0xE4, 0x9C, 0xF2, 0xCB});  // mov rax, 0xcbf29ce484222325
        bytes({0x49, 0xB8, 0xB3, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00});  // mov r8, 0x100000001b3
        bytes({0x48, 0x85, 0xC9});                                            // next: test rcx, rcx
        bytes({0x74, 0x13});                                                  // jz done
        bytes({0x44, 0x0F, 0xB6, 0x0A});                                      // movzx r9d, byte [rdx]
        bytes({0x4C, 0x31, 0xC8});                                            // xor rax, r9
        bytes({0x49, 0x0F, 0xAF, 0xC0});                                      // imul rax, r8
        bytes({0x48, 0xFF, 0xC2});                                            // inc rdx
        bytes({0x48, 0xFF, 0xC9});                                            // dec rcx
        bytes({0xEB, 0xE8});                                                  // jmp next
    }

    // Spreads rax's bits (splitmix64's last steps), then halves it so it's never below 0. Uses rcx
    void mix_hash_rax() {
        bytes({0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE9, 0x1E, 0x48, 0x31, 0xC8});  // rax ^= rax >> 30
        bytes({0x48, 0xB9, 0xB9, 0xE5, 0xE4, 0x1C, 0x6D, 0x47, 0x58, 0xBF});  // mov rcx, 0xbf58476d1ce4e5b9
        bytes({0x48, 0x0F, 0xAF, 0xC1});                                      // imul rax, rcx
        bytes({0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE9, 0x1B, 0x48, 0x31, 0xC8});  // rax ^= rax >> 27
        bytes({0x48, 0xB9, 0xEB, 0x11, 0x31, 0x13, 0xBB, 0x49, 0xD0, 0x94});  // mov rcx, 0x94d049bb133111eb
        bytes({0x48, 0x0F, 0xAF, 0xC1});                                      // imul rax, rcx
        bytes({0x48, 0x89, 0xC1, 0x48, 0xC1, 0xE9, 0x1F, 0x48, 0x31, 0xC8});  // rax ^= rax >> 31
        bytes({0x48, 0xD1, 0xE8});                                            // shr rax, 1
    }
    void imul_rdx_value(int32_t value) {                                                      // imul rdx, rdx, value
        bytes({0x48, 0x69, 0xD2});
        int32(value);
    }
    void sub_rax_rdx() { bytes({0x48, 0x29, 0xD0}); }

    // r12 to r15 hold a function's busiest variables, numbered 0 to 3
    void mov_reg_rax(int reg) { bytes({0x49, 0x89, static_cast<uint8_t>(0xC4 + reg)}); }      // mov r12+reg, rax
    void mov_rax_reg(int reg) { bytes({0x4C, 0x89, static_cast<uint8_t>(0xE0 + reg * 8)}); }  // mov rax, r12+reg
    void mov_rcx_reg(int reg) { bytes({0x4C, 0x89, static_cast<uint8_t>(0xE1 + reg * 8)}); }  // mov rcx, r12+reg
    void push_reg(int reg) { bytes({0x41, static_cast<uint8_t>(0x54 + reg)}); }               // push r12+reg
    // Math straight on a variable held in a register
    void inc_reg(int reg) { bytes({0x49, 0xFF, static_cast<uint8_t>(0xC4 + reg)}); }          // inc r12+reg
    void dec_reg(int reg) { bytes({0x49, 0xFF, static_cast<uint8_t>(0xCC + reg)}); }          // dec r12+reg
    void add_reg_rax(int reg) { bytes({0x49, 0x01, static_cast<uint8_t>(0xC4 + reg)}); }      // add r12+reg, rax
    void sub_reg_rax(int reg) { bytes({0x49, 0x29, static_cast<uint8_t>(0xC4 + reg)}); }      // sub r12+reg, rax
    void cmp_reg_rcx(int reg) { bytes({0x49, 0x39, static_cast<uint8_t>(0xCC + reg)}); }      // cmp r12+reg, rcx
    void cmp_reg_reg(int reg, int other) {                                                    // cmp r12+reg, r12+other
        bytes({0x4D, 0x39, static_cast<uint8_t>(0xE4 + other * 8 + reg)});
    }
    void add_reg_value(int reg, int32_t value) {                                              // add r12+reg, value
        bytes({0x49, 0x81, static_cast<uint8_t>(0xC4 + reg)});
        int32(value);
    }
    void sub_reg_value(int reg, int32_t value) {                                              // sub r12+reg, value
        bytes({0x49, 0x81, static_cast<uint8_t>(0xEC + reg)});
        int32(value);
    }
    void cmp_reg_value(int reg, int32_t value) {                                              // cmp r12+reg, value
        bytes({0x49, 0x81, static_cast<uint8_t>(0xFC + reg)});
        int32(value);
    }
    void mov_reg_value(int reg, int32_t value) {                                              // mov r12+reg, value
        bytes({0x49, 0xC7, static_cast<uint8_t>(0xC4 + reg)});
        int32(value);
    }
    void load_reg_from_frame(int reg, int offset) {                                           // mov r12+reg, [rbp+offset]
        bytes({0x4C, 0x8B, static_cast<uint8_t>(0xA5 + reg * 8)});
        int32(offset);
    }
    void store_reg_to_frame(int reg, int offset) {                                            // mov [rbp+offset], r12+reg
        bytes({0x4C, 0x89, static_cast<uint8_t>(0xA5 + reg * 8)});
        int32(offset);
    }
    void mov_rax_rbp() { bytes({0x48, 0x89, 0xE8}); }
    void store_rcx_data(DataRef target) {                                                     // mov [rip+target], rcx
        bytes({0x48, 0x89, 0x0D});
        data_gap(target);
    }
    void mov_rax_rsp() { bytes({0x48, 0x89, 0xE0}); }
    void lea_rax_frame(int offset) {                                                          // lea rax, [rbp+offset]
        bytes({0x48, 0x8D, 0x85});
        int32(offset);
    }
    void mov_rcx_rax() { bytes({0x48, 0x89, 0xC1}); }
    void mov_rax_rdx() { bytes({0x48, 0x89, 0xD0}); }
    void pop_rcx() { bytes({0x59}); }
    void add_rax_rcx() { bytes({0x48, 0x01, 0xC8}); }
    void sub_rax_rcx() { bytes({0x48, 0x29, 0xC8}); }
    void imul_rax_rcx() { bytes({0x48, 0x0F, 0xAF, 0xC1}); }
    void cmp_rax_rcx() { bytes({0x48, 0x39, 0xC8}); }
    void cqo() { bytes({0x48, 0x99}); }
    void mov_rbx_rsp() { bytes({0x48, 0x89, 0xE3}); }
    void mov_rsp_rbx() { bytes({0x48, 0x89, 0xDC}); }
    void align_rsp() { bytes({0x48, 0x83, 0xE4, 0xF0}); }
    void sign_extend_eax() { bytes({0x48, 0x63, 0xC0}); }
    void eax_to_bool() { bytes({0x85, 0xC0, 0x0F, 0x95, 0xC0, 0x0F, 0xB6, 0xC0}); }
    void pop_rdx() { bytes({0x5A}); }
    void add_rax_rcx_only() { bytes({0x48, 0x01, 0xC8}); }
    void add_rax_rdx() { bytes({0x48, 0x01, 0xD0}); }
    void mov_rcx_from_rax() { bytes({0x48, 0x8B, 0x08}); }
    void load_rax_items() { bytes({0x48, 0x8B, 0x40, 0x10}); }
    void load_rax_item_rcx() { bytes({0x48, 0x8B, 0x04, 0xC8}); }
    void load_rax_from_stack_second() { bytes({0x48, 0x8B, 0x44, 0x24, 0x08}); }
    void load_rcx_from_stack_top() { bytes({0x48, 0x8B, 0x0C, 0x24}); }
    void mov_r9_rax() { bytes({0x49, 0x89, 0xC1}); }
    void store_r9_at_rax() { bytes({0x4C, 0x89, 0x08}); }
    void inc_rax() { bytes({0x48, 0xFF, 0xC0}); }
    void zero_eax() { bytes({0x31, 0xC0}); }
    void idiv_rcx() { bytes({0x48, 0xF7, 0xF9}); }
    void neg_rax() { bytes({0x48, 0xF7, 0xD8}); }
    void test_rax_rax() { bytes({0x48, 0x85, 0xC0}); }
    void test_rcx_rcx() { bytes({0x48, 0x85, 0xC9}); }
    void cmp_rcx_minus_one() { bytes({0x48, 0x83, 0xF9, 0xFF}); }
    void movzx_eax_al() { bytes({0x0F, 0xB6, 0xC0}); }
    void flip_rax_bit_zero() { bytes({0x48, 0x83, 0xF0, 0x01}); }
    void load_rax_from_rax() { bytes({0x48, 0x8B, 0x00}); }

    // Arrays without a runtime call: an array is its length, its room and a pointer to its items
    void cmp_rcx_length() { bytes({0x48, 0x3B, 0x08}); }                        // cmp rcx, [rax]
    void lea_rax_item_rcx() { bytes({0x48, 0x8D, 0x04, 0xC8}); }                // lea rax, [rax+rcx*8]
    void load_rdx_length() { bytes({0x48, 0x8B, 0x10}); }                       // mov rdx, [rax]
    void cmp_rdx_room() { bytes({0x48, 0x3B, 0x50, 0x08}); }                    // cmp rdx, [rax+8]
    void store_rcx_as_item_rdx() { bytes({0x4C, 0x8B, 0x40, 0x10, 0x49, 0x89, 0x0C, 0xD0}); }  // mov r8, [rax+16], mov [r8+rdx*8], rcx
    void grow_length() { bytes({0x48, 0xFF, 0x00}); }                           // inc qword [rax]

    // A field reached through rdx or rcx, so the object can stay in a register
    void mov_rdx_rax() { bytes({0x48, 0x89, 0xC2}); }
    void load_rax_field_of_rdx(int number) {
        bytes({0x48, 0x8B, 0x82});
        int32(number * 8);
    }
    void store_rax_in_rdx_field(int number) {
        bytes({0x48, 0x89, 0x82});
        int32(number * 8);
    }
    void load_rcx_field(int number) {
        bytes({0x48, 0x8B, 0x89});
        int32(number * 8);
    }

    // An int that fits in 32 bits, used straight from the instruction
    void cmp_rax_value(int32_t value) {
        bytes({0x48, 0x3D});
        int32(value);
    }

    void add_rax_value(int32_t value) {
        bytes({0x48, 0x05});
        int32(value);
    }

    void sub_rax_value(int32_t value) {
        bytes({0x48, 0x2D});
        int32(value);
    }

    void flip_rax_sign_bit() { bytes({0x48, 0x0F, 0xBA, 0xF8, 0x3F}); }
    void mov_xmm0_rax() { bytes({0x66, 0x48, 0x0F, 0x6E, 0xC0}); }
    void mov_xmm1_rcx() { bytes({0x66, 0x48, 0x0F, 0x6E, 0xC9}); }
    void mov_rax_xmm0() { bytes({0x66, 0x48, 0x0F, 0x7E, 0xC0}); }
    void addsd() { bytes({0xF2, 0x0F, 0x58, 0xC1}); }
    void subsd() { bytes({0xF2, 0x0F, 0x5C, 0xC1}); }
    void mulsd() { bytes({0xF2, 0x0F, 0x59, 0xC1}); }
    void divsd() { bytes({0xF2, 0x0F, 0x5E, 0xC1}); }
    void int_to_float() { bytes({0xF2, 0x48, 0x0F, 0x2A, 0xC0}); }
    void mov_xmm0_rcx() { bytes({0x66, 0x48, 0x0F, 0x6E, 0xC1}); }
    void mov_xmm1_xmm0() { bytes({0x66, 0x0F, 0x28, 0xC8}); }
    void load_xmm0_field_of_rdx(int number) {
        bytes({0xF2, 0x0F, 0x10, 0x82});
        int32(number * 8);
    }
    void store_xmm0_in_rdx_field(int number) {
        bytes({0xF2, 0x0F, 0x11, 0x82});
        int32(number * 8);
    }
    void compare_xmm0_xmm1() { bytes({0x66, 0x0F, 0x2E, 0xC1}); }
    void compare_xmm1_xmm0() { bytes({0x66, 0x0F, 0x2E, 0xC8}); }
    void and_al_cl() { bytes({0x20, 0xC8}); }
    void or_al_cl() { bytes({0x08, 0xC8}); }

    void setcc_cl(uint8_t condition) {
        bytes({0x0F, condition, 0xC1});
    }

    void setcc_al(uint8_t condition) {
        bytes({0x0F, condition, 0xC0});
    }

    void mov_rax_number(int64_t value) {
        if (value >= std::numeric_limits<int32_t>::min() && value <= std::numeric_limits<int32_t>::max()) {
            bytes({0x48, 0xC7, 0xC0});
            int32(static_cast<int32_t>(value));
        } else {
            bytes({0x48, 0xB8});
            int64(value);
        }
    }

    void load_rax_from_frame(int offset) {
        bytes({0x48, 0x8B, 0x85});
        int32(offset);
    }

    void mov_rcx_number(int value) {
        bytes({0x48, 0xC7, 0xC1});
        int32(value);
    }

    void load_rax_field(int number) {
        bytes({0x48, 0x8B, 0x80});
        int32(number * 8);
    }

    void store_r9_in_rax_field(int number) {
        bytes({0x4C, 0x89, 0x88});
        int32(number * 8);
    }

    // Saves a field into a struct being built: the struct is on top of the stack
    void store_rax_in_new_struct(int number) {
        bytes({0x48, 0x8B, 0x0C, 0x24});  // mov rcx, [rsp]
        bytes({0x48, 0x89, 0x81});        // mov [rcx+number], rax
        int32(number * 8);
    }

    void load_rax_from_stack_top() { bytes({0x48, 0x8B, 0x04, 0x24}); }

    void mov_rcx_value(int64_t value) {
        if (value >= std::numeric_limits<int32_t>::min() && value <= std::numeric_limits<int32_t>::max()) {
            bytes({0x48, 0xC7, 0xC1});
            int32(static_cast<int32_t>(value));
        } else {
            bytes({0x48, 0xB9});
            int64(value);
        }
    }

    void load_rcx_from_frame(int offset) {
        bytes({0x48, 0x8B, 0x8D});
        int32(offset);
    }

    // Saves an item into an array being built: the array is on top of the stack
    void store_rax_in_new_array(int index) {
        bytes({0x48, 0x8B, 0x0C, 0x24});  // mov rcx, [rsp]
        bytes({0x48, 0x8B, 0x49, 0x10});  // mov rcx, [rcx+16]
        bytes({0x48, 0x89, 0x81});        // mov [rcx+index], rax
        int32(index * 8);
    }

    void store_rax_to_frame(int offset) {
        bytes({0x48, 0x89, 0x85});
        int32(offset);
    }

    // Up to 127 fits in one byte
    void sub_rsp(int amount) {
        if (amount >= 0 && amount <= 127) {
            bytes({0x48, 0x83, 0xEC, static_cast<uint8_t>(amount)});
            return;
        }
        bytes({0x48, 0x81, 0xEC});
        int32(amount);
    }

    void add_rsp(int amount) {
        if (amount >= 0 && amount <= 127) {
            bytes({0x48, 0x83, 0xC4, static_cast<uint8_t>(amount)});
            return;
        }
        bytes({0x48, 0x81, 0xC4});
        int32(amount);
    }

    // Touches memory below the stack, so a big frame can't jump past Windows' stack guard
    void touch_stack_below(int amount) {
        bytes({0x48, 0x8B, 0x84, 0x24});
        int32(-amount);
    }

    void jump(Label label) {
        bytes({0xE9});
        label_gap(label);
    }

    void jump_when(uint8_t condition, Label label) {
        bytes({0x0F, condition});
        label_gap(label);
    }

    void jump_if_equal(Label label) { jump_when(0x84, label); }
    void jump_if_not_equal(Label label) { jump_when(0x85, label); }
    void jump_if_not_negative(Label label) { jump_when(0x89, label); }
    void jump_if_negative(Label label) { jump_when(0x88, label); }
    void jump_if_below(Label label) { jump_when(0x82, label); }
    void jump_if_at_least(Label label) { jump_when(0x83, label); }
    void jump_if_at_most(Label label) { jump_when(0x86, label); }
    void jump_if_above(Label label) { jump_when(0x87, label); }
    void jump_if_unordered(Label label) { jump_when(0x8A, label); }
    void jump_if_less(Label label) { jump_when(0x8C, label); }
    void jump_if_less_equal(Label label) { jump_when(0x8E, label); }
    void jump_if_greater(Label label) { jump_when(0x8F, label); }
    void jump_if_greater_equal(Label label) { jump_when(0x8D, label); }

    void call(Label label) {
        bytes({0xE8});
        label_gap(label);
    }

    void call_windows(DataRef function) {
        bytes({0xFF, 0x15});
        data_gap(function);
    }

    void lea_data(Reg reg, DataRef target, int string = 0) {
        bytes({static_cast<uint8_t>(0x48 | (reg >> 3) << 2), 0x8D, static_cast<uint8_t>((reg & 7) << 3 | 5)});
        data_gap(target, string);
    }

    void lea_string(Reg reg, int string) { lea_data(reg, DataRef::String, string); }

    void call_import(int import) {
        bytes({0xFF, 0x15});
        data_gap(DataRef::Import, import);
    }

    // A spare 8 bytes in the data section, which only the code generator uses
    void load_rax_string_bytes(int string) {
        bytes({0x48, 0x8B, 0x05});  // mov rax, [rip+string]
        int32_gap_string(string, 8);
    }

    void store_rax_string_bytes(int string) {
        bytes({0x48, 0x89, 0x05});  // mov [rip+string], rax
        int32_gap_string(string, 8);
    }

    // A gap pointing at a string's bytes, past its 8-byte length
    void int32_gap_string(int string, int extra) {
        code.fixups.push_back({code.bytes.size(), DataRef::String, string, extra});
        int32(0);
    }

    void call_r11() { bytes({0x41, 0xFF, 0xD3}); }
    void mov_r11_rax() { bytes({0x49, 0x89, 0xC3}); }

    void lea_rax_label(Label label) {
        bytes({0x48, 0x8D, 0x05});
        label_gap(label);
    }

    // Loads input number index of count, all pushed on the stack before rbx was set
    void load_reg_from_inputs(int reg_number, bool is_float, int offset) {
        static const uint8_t ints[4][3] = {{0x48, 0x8B, 0x8B}, {0x48, 0x8B, 0x93}, {0x4C, 0x8B, 0x83}, {0x4C, 0x8B, 0x8B}};
        static const uint8_t floats[4][4] = {{0xF2, 0x0F, 0x10, 0x83}, {0xF2, 0x0F, 0x10, 0x8B}, {0xF2, 0x0F, 0x10, 0x93}, {0xF2, 0x0F, 0x10, 0x9B}};
        if (is_float) {
            bytes({floats[reg_number][0], floats[reg_number][1], floats[reg_number][2], floats[reg_number][3]});
        } else {
            bytes({ints[reg_number][0], ints[reg_number][1], ints[reg_number][2]});
        }
        int32(offset);
    }

    // An f32 input: the float shrinks to 32 bits in its register
    void shrink_float_in(int reg_number) {
        bytes({0xF2, 0x0F, 0x5A, static_cast<uint8_t>(0xC0 | reg_number << 3 | reg_number)});  // cvtsd2ss xmmN, xmmN
    }

    void copy_f32_input_to_stack(int from, int to) {
        bytes({0xF2, 0x0F, 0x10, 0x83});  // movsd xmm0, [rbx+from]
        int32(from);
        bytes({0xF2, 0x0F, 0x5A, 0xC0});  // cvtsd2ss xmm0, xmm0
        bytes({0xF3, 0x0F, 0x11, 0x84, 0x24});  // movss [rsp+to], xmm0
        int32(to);
    }

    void grow_float_answer() { bytes({0xF3, 0x0F, 0x5A, 0xC0}); }  // cvtss2sd xmm0, xmm0

    void copy_input_to_stack(int from, int to) {
        bytes({0x48, 0x8B, 0x83});  // mov rax, [rbx+from]
        int32(from);
        bytes({0x48, 0x89, 0x84, 0x24});  // mov [rsp+to], rax
        int32(to);
    }

    void lea_rax_string(int string) {
        bytes({0x48, 0x8D, 0x05});
        data_gap(DataRef::String, string);
    }

    void load_rax_data(DataRef target) {
        bytes({0x48, 0x8B, 0x05});
        data_gap(target);
    }

    void load_rcx_data(DataRef target) {
        bytes({0x48, 0x8B, 0x0D});
        data_gap(target);
    }

    void store_rax_data(DataRef target) {
        bytes({0x48, 0x89, 0x05});
        data_gap(target);
    }

    // Remembers the file and line for run errors. Uses r11, which holds nothing in Jamo code
    void note_line(int64_t line) {
        bytes({0x49, 0xBB});
        int64(line);
        bytes({0x4C, 0x89, 0x1D});
        data_gap(DataRef::CurrentLine);
    }

private:
    struct Gap {
        size_t offset;
        Label label;
    };

    MachineCode code;
    std::vector<int64_t> places;
    std::vector<Gap> gaps;
};
