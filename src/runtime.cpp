#include "runtime.hpp"

// Each runtime routine is real machine code, written out byte by byte, with its instruction beside it.
// Jamo code keeps nothing in registers across these calls, so routines can use any of them.

namespace {

// rcx = exit code. A window program that collected a run error shows it in a message box first, since it has no console
void write_exit(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label box_done = out.new_label();
    const Label plain_exit = out.new_label();
    out.place(runtime.exit);
    out.lea_data(RAX, DataRef::Cleaner);
    out.bytes({0xF6, 0x40, 0x20, 0x10});  // test byte ptr [rax+32], 16
    out.jump_if_equal(plain_exit);
    out.lea_data(RAX, DataRef::ErrorText);
    out.bytes({0x48, 0x83, 0x38, 0x00});  // cmp qword ptr [rax], 0
    out.jump_if_equal(plain_exit);
    out.bytes({0x48, 0x89, 0xCB});  // mov rbx, rcx
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x20});  // sub rsp, 32
    out.lea_string(RCX, text.user32_dll);
    out.bytes({0x48, 0x83, 0xC1, 0x08});  // add rcx, 8
    out.call_windows(DataRef::LoadLibraryA);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(box_done);
    out.bytes({0x48, 0x89, 0xC1});  // mov rcx, rax
    out.lea_string(RDX, text.message_box);
    out.bytes({0x48, 0x83, 0xC2, 0x08});  // add rdx, 8
    out.call_windows(DataRef::GetProcAddress);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(box_done);
    out.bytes({0x49, 0x89, 0xC3});  // mov r11, rax
    out.bytes({0x31, 0xC9});  // xor ecx, ecx
    out.lea_data(RDX, DataRef::ErrorText);
    out.bytes({0x48, 0x83, 0xC2, 0x08});  // add rdx, 8
    out.lea_string(R8, text.error_title);
    out.bytes({0x49, 0x83, 0xC0, 0x08});  // add r8, 8
    out.bytes({0x41, 0xB9, 0x10, 0x00, 0x00, 0x00});  // mov r9d, 16
    out.call_r11();
    out.place(box_done);
    out.bytes({0x48, 0x89, 0xD9});  // mov rcx, rbx
    out.place(plain_exit);
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x20});  // sub rsp, 32
    out.call_windows(DataRef::ExitProcess);
}

// rcx = which stream, rdx = the text, r8 = its length. Keeps rbx, rsi and rdi for its callers. A window program has no console, so what goes to the error stream is kept for exit's message box, up to 4095 bytes
void write_write(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label to_stream = out.new_label();
    const Label next_part = out.new_label();
    const Label console = out.new_label();
    const Label console_part = out.new_label();
    const Label back_off = out.new_label();
    const Label whole_letters = out.new_label();
    const Label finished = out.new_label();
    out.place(runtime.write);
    out.bytes({0x48, 0x83, 0xF9, 0xF4});  // cmp rcx, -12
    out.jump_if_not_equal(to_stream);
    out.lea_data(R9, DataRef::Cleaner);
    out.bytes({0x41, 0xF6, 0x41, 0x20, 0x10});  // test byte ptr [r9+32], 16
    out.jump_if_equal(to_stream);
    out.bytes({0x56});  // push rsi
    out.bytes({0x57});  // push rdi
    out.lea_data(R9, DataRef::ErrorText);
    out.bytes({0x49, 0x8B, 0x39});  // mov rdi, qword ptr [r9]
    out.bytes({0x48, 0xC7, 0xC1, 0xFF, 0x0F, 0x00, 0x00});  // mov rcx, 4095
    out.bytes({0x48, 0x29, 0xF9});  // sub rcx, rdi
    out.bytes({0x4C, 0x39, 0xC1});  // cmp rcx, r8
    out.bytes({0x49, 0x0F, 0x47, 0xC8});  // cmova rcx, r8
    out.bytes({0x49, 0x01, 0x09});  // add qword ptr [r9], rcx
    out.bytes({0x49, 0x8D, 0x7C, 0x39, 0x08});  // lea rdi, [r9+rdi+8]
    out.bytes({0x48, 0x89, 0xD6});  // mov rsi, rdx
    out.bytes({0xF3, 0xA4});  // rep movsb
    out.bytes({0x5F});  // pop rdi
    out.bytes({0x5E});  // pop rsi
    out.bytes({0xC3});  // ret
    out.place(to_stream);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x53});  // push rbx
    out.bytes({0x56});  // push rsi
    out.bytes({0x57});  // push rdi
    out.bytes({0x48, 0x89, 0xD3});  // mov rbx, rdx
    out.bytes({0x4C, 0x89, 0xC6});  // mov rsi, r8
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x30});  // sub rsp, 48
    out.call_windows(DataRef::GetStdHandle);
    out.bytes({0x48, 0x89, 0xC7});  // mov rdi, rax
    out.bytes({0x48, 0x89, 0xF9});  // mov rcx, rdi
    out.bytes({0x48, 0x8D, 0x54, 0x24, 0x28});  // lea rdx, [rsp+40]
    out.call_windows(DataRef::GetConsoleMode);
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_not_equal(console);
    out.place(next_part);
    out.bytes({0x48, 0x85, 0xF6});  // test rsi, rsi
    out.jump_if_equal(finished);
    out.bytes({0x49, 0x89, 0xF0});  // mov r8, rsi
    out.bytes({0xB8, 0x00, 0x00, 0x00, 0x40});  // mov eax, 0x40000000
    out.bytes({0x49, 0x39, 0xC0});  // cmp r8, rax
    out.bytes({0x4C, 0x0F, 0x47, 0xC0});  // cmova r8, rax
    out.bytes({0x48, 0x89, 0xF9});  // mov rcx, rdi
    out.bytes({0x48, 0x89, 0xDA});  // mov rdx, rbx
    out.bytes({0x4C, 0x8D, 0x4C, 0x24, 0x28});  // lea r9, [rsp+40]
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+32], 0
    out.call_windows(DataRef::WriteFile);
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_equal(finished);
    out.bytes({0x8B, 0x44, 0x24, 0x28});  // mov eax, dword ptr [rsp+40]
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(finished);
    out.bytes({0x48, 0x01, 0xC3});  // add rbx, rax
    out.bytes({0x48, 0x29, 0xC6});  // sub rsi, rax
    out.jump(next_part);
    out.place(console);
    out.bytes({0x48, 0x81, 0xEC, 0x10, 0x08, 0x00, 0x00});  // sub rsp, 2064
    out.place(console_part);
    out.bytes({0x48, 0x85, 0xF6});  // test rsi, rsi
    out.jump_if_equal(finished);
    out.bytes({0x49, 0x89, 0xF0});  // mov r8, rsi
    out.bytes({0xB8, 0xE8, 0x03, 0x00, 0x00});  // mov eax, 1000
    out.bytes({0x49, 0x39, 0xC0});  // cmp r8, rax
    out.jump_if_at_most(whole_letters);
    out.bytes({0x49, 0x89, 0xC0});  // mov r8, rax
    out.place(back_off);
    out.bytes({0x42, 0x0F, 0xB6, 0x04, 0x03});  // movzx eax, byte ptr [rbx+r8]
    out.bytes({0x25, 0xC0, 0x00, 0x00, 0x00});  // and eax, 192
    out.bytes({0x3D, 0x80, 0x00, 0x00, 0x00});  // cmp eax, 128
    out.jump_if_not_equal(whole_letters);
    out.bytes({0x49, 0xFF, 0xC8});  // dec r8
    out.jump_if_not_equal(back_off);
    out.bytes({0x41, 0xB8, 0xE8, 0x03, 0x00, 0x00});  // mov r8d, 1000
    out.place(whole_letters);
    out.bytes({0x4C, 0x89, 0x84, 0x24, 0x08, 0x08, 0x00, 0x00});  // mov qword ptr [rsp+2056], r8
    out.bytes({0xB9, 0xE9, 0xFD, 0x00, 0x00});  // mov ecx, 65001
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x4D, 0x89, 0xC1});  // mov r9, r8
    out.bytes({0x49, 0x89, 0xD8});  // mov r8, rbx
    out.bytes({0x48, 0x8D, 0x44, 0x24, 0x30});  // lea rax, [rsp+48]
    out.bytes({0x48, 0x89, 0x44, 0x24, 0x20});  // mov qword ptr [rsp+32], rax
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x28, 0xE8, 0x03, 0x00, 0x00});  // mov qword ptr [rsp+40], 1000
    out.call_windows(DataRef::MultiByteToWideChar);
    out.bytes({0x48, 0x89, 0xF9});  // mov rcx, rdi
    out.bytes({0x48, 0x8D, 0x54, 0x24, 0x30});  // lea rdx, [rsp+48]
    out.bytes({0x41, 0x89, 0xC0});  // mov r8d, eax
    out.bytes({0x4C, 0x8D, 0x8C, 0x24, 0x00, 0x08, 0x00, 0x00});  // lea r9, [rsp+2048]
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+32], 0
    out.call_windows(DataRef::WriteConsoleW);
    out.bytes({0x48, 0x8B, 0x84, 0x24, 0x08, 0x08, 0x00, 0x00});  // mov rax, qword ptr [rsp+2056]
    out.bytes({0x48, 0x01, 0xC3});  // add rbx, rax
    out.bytes({0x48, 0x29, 0xC6});  // sub rsi, rax
    out.jump(console_part);
    out.place(finished);
    out.bytes({0x48, 0x8D, 0x65, 0xE8});  // lea rsp, [rbp-24]
    out.bytes({0x5F});  // pop rdi
    out.bytes({0x5E});  // pop rsi
    out.bytes({0x5B});  // pop rbx
    out.bytes({0x5D});  // pop rbp
    out.bytes({0xC3});  // ret
}

// rcx = bytes wanted, for the text builder and the like, which the cleaner never touches. Keeps rbx, rsi and rdi
void write_raw_alloc(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.raw_alloc);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x20});  // sub rsp, 32
    out.bytes({0x49, 0x89, 0xC8});  // mov r8, rcx
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.load_rcx_data(DataRef::HeapHandle);
    out.call_windows(DataRef::HeapAlloc);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(runtime.out_of_memory);
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
}

// rax = the int, rdi = where the text must end. Answers with its start in rdx, building it backward
void write_format_int(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label positive = out.new_label();
    const Label next_digit = out.new_label();
    const Label no_sign = out.new_label();
    out.place(runtime.format_int);
    out.bytes({0x49, 0x89, 0xFA});  // mov r10, rdi
    out.bytes({0x45, 0x31, 0xDB});  // xor r11d, r11d
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_not_negative(positive);
    out.bytes({0x41, 0xBB, 0x01, 0x00, 0x00, 0x00});  // mov r11d, 1
    out.bytes({0x48, 0xF7, 0xD8});  // neg rax
    out.place(positive);
    out.bytes({0xB9, 0x0A, 0x00, 0x00, 0x00});  // mov ecx, 10
    out.place(next_digit);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x48, 0xF7, 0xF1});  // div rcx
    out.bytes({0x80, 0xC2, 0x30});  // add dl, 48
    out.bytes({0x49, 0xFF, 0xCA});  // dec r10
    out.bytes({0x41, 0x88, 0x12});  // mov byte ptr [r10], dl
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_not_equal(next_digit);
    out.bytes({0x4D, 0x85, 0xDB});  // test r11, r11
    out.jump_if_equal(no_sign);
    out.bytes({0x49, 0xFF, 0xCA});  // dec r10
    out.bytes({0x41, 0xC6, 0x02, 0x2D});  // mov byte ptr [r10], 45
    out.place(no_sign);
    out.bytes({0x4C, 0x89, 0xD2});  // mov rdx, r10
    out.bytes({0xC3});  // ret
}

// rdx = the text, r8 = its length. Answers with a new string in rax
void write_make_string(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.make_string);
    out.bytes({0x48, 0x89, 0xD3});  // mov rbx, rdx
    out.bytes({0x4C, 0x89, 0xC6});  // mov rsi, r8
    out.bytes({0x49, 0x8D, 0x48, 0x09});  // lea rcx, [r8+9]
    out.call(runtime.alloc_text);
    out.bytes({0x49, 0x89, 0xC1});  // mov r9, rax
    out.bytes({0x49, 0x89, 0x31});  // mov qword ptr [r9], rsi
    out.bytes({0x49, 0x8D, 0x79, 0x08});  // lea rdi, [r9+8]
    out.bytes({0x48, 0x89, 0xF1});  // mov rcx, rsi
    out.bytes({0x48, 0x89, 0xDE});  // mov rsi, rbx
    out.bytes({0xF3, 0xA4});  // rep movsb
    out.bytes({0xC6, 0x07, 0x0A});  // mov byte ptr [rdi], 10
    out.bytes({0x4C, 0x89, 0xC8});  // mov rax, r9
    out.bytes({0xC3});  // ret
}

// rax = the int
void write_print_int(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.print_int);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xEC, 0x30});  // sub rsp, 48
    out.bytes({0xC6, 0x45, 0xFF, 0x0A});  // mov byte ptr [rbp-1], 10
    out.bytes({0x48, 0x8D, 0x7D, 0xFF});  // lea rdi, [rbp-1]
    out.call(runtime.format_int);
    out.bytes({0x49, 0x89, 0xE8});  // mov r8, rbp
    out.bytes({0x49, 0x29, 0xD0});  // sub r8, rdx
    out.bytes({0x48, 0xC7, 0xC1, 0xF5, 0xFF, 0xFF, 0xFF});  // mov rcx, -11
    out.call(runtime.write);
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
}

// rax = the int
void write_int_to_string(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.int_to_string);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xEC, 0x30});  // sub rsp, 48
    out.bytes({0x48, 0x89, 0xEF});  // mov rdi, rbp
    out.call(runtime.format_int);
    out.bytes({0x49, 0x89, 0xE8});  // mov r8, rbp
    out.bytes({0x49, 0x29, 0xD0});  // sub r8, rdx
    out.call(runtime.make_string);
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
}

// rax = the bool. Answers with the shared true or false string
void write_bool_to_string(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label is_false = out.new_label();
    out.place(runtime.bool_to_string);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(is_false);
    out.lea_rax_string(text.true_word);
    out.bytes({0xC3});  // ret
    out.place(is_false);
    out.lea_rax_string(text.false_word);
    out.bytes({0xC3});  // ret
}

// rax = the bool
void write_print_bool(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.print_bool);
    out.call(runtime.bool_to_string);
    out.jump(runtime.print_string);
}

// rax = the string. Every string keeps a hidden new line just past its end
void write_print_string(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.print_string);
    out.bytes({0x48, 0x8D, 0x50, 0x08});  // lea rdx, [rax+8]
    out.bytes({0x4C, 0x8B, 0x00});  // mov r8, qword ptr [rax]
    out.bytes({0x49, 0xFF, 0xC0});  // inc r8
    out.bytes({0x48, 0xC7, 0xC1, 0xF5, 0xFF, 0xFF, 0xFF});  // mov rcx, -11
    out.jump(runtime.write);
}

// rax = the left string, rcx = the right string
void write_join_strings(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.join_strings);
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.bytes({0x48, 0x89, 0xCE});  // mov rsi, rcx
    out.bytes({0x48, 0x8B, 0x0B});  // mov rcx, qword ptr [rbx]
    out.bytes({0x48, 0x03, 0x0E});  // add rcx, qword ptr [rsi]
    out.bytes({0x48, 0x83, 0xC1, 0x09});  // add rcx, 9
    out.call(runtime.alloc_text);
    out.bytes({0x49, 0x89, 0xC0});  // mov r8, rax
    out.bytes({0x48, 0x8B, 0x0B});  // mov rcx, qword ptr [rbx]
    out.bytes({0x48, 0x03, 0x0E});  // add rcx, qword ptr [rsi]
    out.bytes({0x49, 0x89, 0x08});  // mov qword ptr [r8], rcx
    out.bytes({0x49, 0x8D, 0x78, 0x08});  // lea rdi, [r8+8]
    out.bytes({0x48, 0x89, 0xF2});  // mov rdx, rsi
    out.bytes({0x48, 0x8D, 0x73, 0x08});  // lea rsi, [rbx+8]
    out.bytes({0x48, 0x8B, 0x0B});  // mov rcx, qword ptr [rbx]
    out.bytes({0xF3, 0xA4});  // rep movsb
    out.bytes({0x48, 0x8D, 0x72, 0x08});  // lea rsi, [rdx+8]
    out.bytes({0x48, 0x8B, 0x0A});  // mov rcx, qword ptr [rdx]
    out.bytes({0xF3, 0xA4});  // rep movsb
    out.bytes({0xC6, 0x07, 0x0A});  // mov byte ptr [rdi], 10
    out.bytes({0x4C, 0x89, 0xC0});  // mov rax, r8
    out.bytes({0xC3});  // ret
}

// rcx = how many strings, 2 or more, pushed in order before the call. Answers with them joined, made once. The caller takes them off the stack
void write_join_many(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label add_length = out.new_label();
    const Label copy_part = out.new_label();
    out.place(runtime.join_many);
    out.bytes({0x48, 0x89, 0xCB});  // mov rbx, rcx
    out.bytes({0x31, 0xC9});  // xor ecx, ecx
    out.bytes({0x48, 0x8D, 0x74, 0x24, 0x08});  // lea rsi, [rsp+8]
    out.bytes({0x48, 0x89, 0xDA});  // mov rdx, rbx
    out.place(add_length);
    out.bytes({0x48, 0x8B, 0x06});  // mov rax, qword ptr [rsi]
    out.bytes({0x48, 0x03, 0x08});  // add rcx, qword ptr [rax]
    out.bytes({0x48, 0x83, 0xC6, 0x08});  // add rsi, 8
    out.bytes({0x48, 0xFF, 0xCA});  // dec rdx
    out.jump_if_not_equal(add_length);
    out.bytes({0x49, 0x89, 0xC8});  // mov r8, rcx
    out.bytes({0x48, 0x83, 0xC1, 0x09});  // add rcx, 9
    out.call(runtime.alloc_text);
    out.bytes({0x4C, 0x89, 0x00});  // mov qword ptr [rax], r8
    out.bytes({0x48, 0x8D, 0x78, 0x08});  // lea rdi, [rax+8]
    out.bytes({0x48, 0x8D, 0x14, 0xDC});  // lea rdx, [rsp+rbx*8]
    out.place(copy_part);
    out.bytes({0x48, 0x8B, 0x32});  // mov rsi, qword ptr [rdx]
    out.bytes({0x48, 0x8B, 0x0E});  // mov rcx, qword ptr [rsi]
    out.bytes({0x48, 0x83, 0xC6, 0x08});  // add rsi, 8
    out.bytes({0xF3, 0xA4});  // rep movsb
    out.bytes({0x48, 0x83, 0xEA, 0x08});  // sub rdx, 8
    out.bytes({0x48, 0xFF, 0xCB});  // dec rbx
    out.jump_if_not_equal(copy_part);
    out.bytes({0xC6, 0x07, 0x0A});  // mov byte ptr [rdi], 10
    out.bytes({0xC3});  // ret
}

// rax = one string, rcx = the other. Answers with a bool. The same string is equal to itself without looking
void write_strings_equal(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label same = out.new_label();
    const Label different = out.new_label();
    out.place(runtime.strings_equal);
    out.bytes({0x48, 0x39, 0xC8});  // cmp rax, rcx
    out.jump_if_equal(same);
    out.bytes({0x48, 0x89, 0xC6});  // mov rsi, rax
    out.bytes({0x48, 0x89, 0xCF});  // mov rdi, rcx
    out.bytes({0x48, 0x8B, 0x0E});  // mov rcx, qword ptr [rsi]
    out.bytes({0x48, 0x3B, 0x0F});  // cmp rcx, qword ptr [rdi]
    out.jump_if_not_equal(different);
    out.bytes({0x48, 0x83, 0xC6, 0x08});  // add rsi, 8
    out.bytes({0x48, 0x83, 0xC7, 0x08});  // add rdi, 8
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.bytes({0xF3, 0xA6});  // repe cmpsb
    out.bytes({0x0F, 0x94, 0xC0});  // sete al
    out.bytes({0xC3});  // ret
    out.place(same);
    out.bytes({0xB8, 0x01, 0x00, 0x00, 0x00});  // mov eax, 1
    out.bytes({0xC3});  // ret
    out.place(different);
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.bytes({0xC3});  // ret
}

// rax = the string, written to the error stream with no new line
void write_error_text(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.error_text);
    out.bytes({0x48, 0x8D, 0x50, 0x08});  // lea rdx, [rax+8]
    out.bytes({0x4C, 0x8B, 0x00});  // mov r8, qword ptr [rax]
    out.bytes({0x48, 0xC7, 0xC1, 0xF4, 0xFF, 0xFF, 0xFF});  // mov rcx, -12
    out.jump(runtime.write);
}

// rax = which file. Answers with its name
void write_file_name_of(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    out.place(runtime.file_name_of);
    for (size_t i = 1; i < text.file_names.size(); i++) {
        const Label next = out.new_label();
        out.mov_rcx_number(static_cast<int>(i));
        out.cmp_rax_rcx();
        out.jump_if_not_equal(next);
        out.lea_rax_string(text.file_names[i]);
        out.bytes({0xC3});
        out.place(next);
    }
    out.lea_rax_string(text.file_names[0]);
    out.bytes({0xC3});  // ret
}

// rax = the message. With a try open, goes back to that try's rbp and rsp and jumps to its catch with the message still in rax, making the try around it the newest. Otherwise shows the file and line, then stops with exit code 1
void write_run_error(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label not_caught = out.new_label();
    out.place(runtime.run_error);
    out.lea_data(RCX, DataRef::TryFrame);
    out.bytes({0x48, 0x8B, 0x11});  // mov rdx, qword ptr [rcx]
    out.bytes({0x48, 0x85, 0xD2});  // test rdx, rdx
    out.jump_if_equal(not_caught);
    out.bytes({0x4C, 0x8B, 0x02});  // mov r8, qword ptr [rdx]
    out.bytes({0x4C, 0x89, 0x01});  // mov qword ptr [rcx], r8
    out.bytes({0x48, 0x8B, 0x6A, 0x08});  // mov rbp, qword ptr [rdx+8]
    out.bytes({0x48, 0x8B, 0x62, 0x10});  // mov rsp, qword ptr [rdx+16]
    out.bytes({0xFF, 0x72, 0x18});  // push qword ptr [rdx+24]
    out.bytes({0xC3});  // ret
    out.place(not_caught);
    out.jump(runtime.stop_with_error);
}

// rax = the message. Shows the file and line, then stops with exit code 1, even with a try open
void write_stop_with_error(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    out.place(runtime.stop_with_error);
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.load_rax_data(DataRef::CurrentLine);
    out.bytes({0x49, 0x89, 0xC4});  // mov r12, rax
    out.bytes({0x48, 0xC1, 0xE8, 0x20});  // shr rax, 32
    out.call(runtime.file_name_of);
    out.call(runtime.error_text);
    out.lea_rax_string(text.line_word);
    out.call(runtime.error_text);
    out.bytes({0x48, 0x83, 0xEC, 0x30});  // sub rsp, 48
    out.bytes({0x44, 0x89, 0xE0});  // mov eax, r12d
    out.bytes({0x48, 0x8D, 0x7C, 0x24, 0x30});  // lea rdi, [rsp+48]
    out.call(runtime.format_int);
    out.bytes({0x4C, 0x8D, 0x44, 0x24, 0x30});  // lea r8, [rsp+48]
    out.bytes({0x49, 0x29, 0xD0});  // sub r8, rdx
    out.bytes({0x48, 0xC7, 0xC1, 0xF4, 0xFF, 0xFF, 0xFF});  // mov rcx, -12
    out.call(runtime.write);
    out.lea_rax_string(text.error_word);
    out.call(runtime.error_text);
    out.bytes({0x48, 0x8D, 0x53, 0x08});  // lea rdx, [rbx+8]
    out.bytes({0x4C, 0x8B, 0x03});  // mov r8, qword ptr [rbx]
    out.bytes({0x49, 0xFF, 0xC0});  // inc r8
    out.bytes({0x48, 0xC7, 0xC1, 0xF4, 0xFF, 0xFF, 0xFF});  // mov rcx, -12
    out.call(runtime.write);
    out.call(runtime.show_calls);
    out.bytes({0xB9, 0x01, 0x00, 0x00, 0x00});  // mov ecx, 1
    out.jump(runtime.exit);
}

void write_divide_by_zero(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    out.place(runtime.divide_by_zero);
    out.lea_rax_string(text.divide_by_zero);
    out.jump(runtime.run_error);
}

// Can't be caught, since it can happen half way through the memory cleaner's work
void write_out_of_memory(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    out.place(runtime.out_of_memory);
    out.lea_rax_string(text.out_of_memory);
    out.jump(runtime.stop_with_error);
}

// rax = the float, rdi = where the text must end. Answers with its start in rdx, building it backward. Rounds exactly, like %.6f, using whole numbers only
void write_format_float(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label has_fraction = out.new_label();
    const Label only_fraction = out.new_label();
    const Label long_shift = out.new_label();
    const Label half_is_high = out.new_label();
    const Label half_added = out.new_label();
    const Label carry_check = out.new_label();
    const Label write_fixed = out.new_label();
    const Label has_decimals = out.new_label();
    const Label next_trim = out.new_label();
    const Label next_place = out.new_label();
    const Label write_dot = out.new_label();
    const Label add_sign = out.new_label();
    const Label all_done = out.new_label();
    const Label not_finite = out.new_label();
    const Label is_infinite = out.new_label();
    const Label scientific = out.new_label();
    const Label next_big_digit = out.new_label();
    const Label next_word = out.new_label();
    const Label check_rest = out.new_label();
    const Label is_tie = out.new_label();
    const Label round_up = out.new_label();
    const Label next_carry = out.new_label();
    const Label plain_increase = out.new_label();
    const Label rounded = out.new_label();
    const Label trim_zero = out.new_label();
    const Label copy_digit = out.new_label();
    out.place(runtime.format_float);
    out.bytes({0x49, 0x89, 0xC0});  // mov r8, rax
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.bytes({0x48, 0x0F, 0xBA, 0xF3, 0x3F});  // btr rbx, 63
    out.bytes({0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x7F});  // movabs rax, 0x7FF0000000000000
    out.bytes({0x48, 0x39, 0xC3});  // cmp rbx, rax
    out.jump_if_at_least(not_finite);
    out.bytes({0x48, 0xB8, 0x00, 0x80, 0xE0, 0x37, 0x79, 0xC3, 0x41, 0x43});  // movabs rax, 0x4341C37937E08000
    out.bytes({0x48, 0x39, 0xC3});  // cmp rbx, rax
    out.jump_if_at_least(scientific);
    out.bytes({0x45, 0x31, 0xC9});  // xor r9d, r9d
    out.bytes({0x31, 0xF6});  // xor esi, esi
    out.bytes({0x48, 0x89, 0xD9});  // mov rcx, rbx
    out.bytes({0x48, 0xC1, 0xE9, 0x34});  // shr rcx, 52
    out.bytes({0x48, 0x81, 0xF9, 0xEA, 0x03, 0x00, 0x00});  // cmp rcx, 1002
    out.jump_if_below(write_fixed);
    out.bytes({0x48, 0x89, 0xD8});  // mov rax, rbx
    out.bytes({0x48, 0xBA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x00});  // movabs rdx, 0x000FFFFFFFFFFFFF
    out.bytes({0x48, 0x21, 0xD0});  // and rax, rdx
    out.bytes({0x48, 0x0F, 0xBA, 0xE8, 0x34});  // bts rax, 52
    out.bytes({0x48, 0x81, 0xE9, 0x33, 0x04, 0x00, 0x00});  // sub rcx, 1075
    out.jump_if_less(has_fraction);
    out.bytes({0x48, 0xD3, 0xE0});  // shl rax, cl
    out.bytes({0x49, 0x89, 0xC1});  // mov r9, rax
    out.jump(write_fixed);
    out.place(has_fraction);
    out.bytes({0x48, 0xF7, 0xD9});  // neg rcx
    out.bytes({0x48, 0x83, 0xF9, 0x40});  // cmp rcx, 64
    out.jump_if_at_least(only_fraction);
    out.bytes({0x49, 0x89, 0xC1});  // mov r9, rax
    out.bytes({0x49, 0xD3, 0xE9});  // shr r9, cl
    out.bytes({0xBA, 0x01, 0x00, 0x00, 0x00});  // mov edx, 1
    out.bytes({0x48, 0xD3, 0xE2});  // shl rdx, cl
    out.bytes({0x48, 0xFF, 0xCA});  // dec rdx
    out.bytes({0x48, 0x21, 0xD0});  // and rax, rdx
    out.place(only_fraction);
    out.bytes({0x49, 0x89, 0xCA});  // mov r10, rcx
    out.bytes({0xBA, 0x40, 0x42, 0x0F, 0x00});  // mov edx, 1000000
    out.bytes({0x48, 0xF7, 0xE2});  // mul rdx
    out.bytes({0x49, 0x83, 0xFA, 0x40});  // cmp r10, 64
    out.jump_if_at_least(long_shift);
    out.bytes({0x49, 0x8D, 0x4A, 0xFF});  // lea rcx, [r10-1]
    out.bytes({0x41, 0xBB, 0x01, 0x00, 0x00, 0x00});  // mov r11d, 1
    out.bytes({0x49, 0xD3, 0xE3});  // shl r11, cl
    out.bytes({0x4C, 0x01, 0xD8});  // add rax, r11
    out.bytes({0x48, 0x83, 0xD2, 0x00});  // adc rdx, 0
    out.bytes({0x4C, 0x89, 0xD1});  // mov rcx, r10
    out.bytes({0x48, 0x89, 0xC6});  // mov rsi, rax
    out.bytes({0x48, 0x0F, 0xAD, 0xD6});  // shrd rsi, rdx, cl
    out.bytes({0x41, 0xBB, 0x01, 0x00, 0x00, 0x00});  // mov r11d, 1
    out.bytes({0x49, 0xD3, 0xE3});  // shl r11, cl
    out.bytes({0x49, 0xFF, 0xCB});  // dec r11
    out.bytes({0x4C, 0x85, 0xD8});  // test rax, r11
    out.jump_if_not_equal(carry_check);
    out.bytes({0x48, 0x83, 0xE6, 0xFE});  // and rsi, -2
    out.jump(carry_check);
    out.place(long_shift);
    out.bytes({0x49, 0x8D, 0x4A, 0xC0});  // lea rcx, [r10-64]
    out.bytes({0x48, 0x85, 0xC9});  // test rcx, rcx
    out.jump_if_not_equal(half_is_high);
    out.bytes({0x49, 0xBB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80});  // movabs r11, 0x8000000000000000
    out.bytes({0x4C, 0x01, 0xD8});  // add rax, r11
    out.bytes({0x48, 0x83, 0xD2, 0x00});  // adc rdx, 0
    out.jump(half_added);
    out.place(half_is_high);
    out.bytes({0x49, 0x8D, 0x4A, 0xBF});  // lea rcx, [r10-65]
    out.bytes({0x41, 0xBB, 0x01, 0x00, 0x00, 0x00});  // mov r11d, 1
    out.bytes({0x49, 0xD3, 0xE3});  // shl r11, cl
    out.bytes({0x4C, 0x01, 0xDA});  // add rdx, r11
    out.place(half_added);
    out.bytes({0x49, 0x8D, 0x4A, 0xC0});  // lea rcx, [r10-64]
    out.bytes({0x48, 0x89, 0xD6});  // mov rsi, rdx
    out.bytes({0x48, 0xD3, 0xEE});  // shr rsi, cl
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_not_equal(carry_check);
    out.bytes({0x41, 0xBB, 0x01, 0x00, 0x00, 0x00});  // mov r11d, 1
    out.bytes({0x49, 0xD3, 0xE3});  // shl r11, cl
    out.bytes({0x49, 0xFF, 0xCB});  // dec r11
    out.bytes({0x4C, 0x85, 0xDA});  // test rdx, r11
    out.jump_if_not_equal(carry_check);
    out.bytes({0x48, 0x83, 0xE6, 0xFE});  // and rsi, -2
    out.place(carry_check);
    out.bytes({0x48, 0x81, 0xFE, 0x40, 0x42, 0x0F, 0x00});  // cmp rsi, 1000000
    out.jump_if_not_equal(write_fixed);
    out.bytes({0x49, 0xFF, 0xC1});  // inc r9
    out.bytes({0x31, 0xF6});  // xor esi, esi
    out.place(write_fixed);
    out.bytes({0x49, 0x89, 0xFA});  // mov r10, rdi
    out.bytes({0x48, 0x85, 0xF6});  // test rsi, rsi
    out.jump_if_not_equal(has_decimals);
    out.bytes({0x49, 0xFF, 0xCA});  // dec r10
    out.bytes({0x41, 0xC6, 0x02, 0x30});  // mov byte ptr [r10], 48
    out.jump(write_dot);
    out.place(has_decimals);
    out.bytes({0x41, 0xBB, 0x06, 0x00, 0x00, 0x00});  // mov r11d, 6
    out.bytes({0xB9, 0x0A, 0x00, 0x00, 0x00});  // mov ecx, 10
    out.place(next_trim);
    out.bytes({0x48, 0x89, 0xF0});  // mov rax, rsi
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x48, 0xF7, 0xF1});  // div rcx
    out.bytes({0x48, 0x85, 0xD2});  // test rdx, rdx
    out.jump_if_not_equal(next_place);
    out.bytes({0x48, 0x89, 0xC6});  // mov rsi, rax
    out.bytes({0x49, 0xFF, 0xCB});  // dec r11
    out.jump(next_trim);
    out.place(next_place);
    out.bytes({0x48, 0x89, 0xF0});  // mov rax, rsi
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x48, 0xF7, 0xF1});  // div rcx
    out.bytes({0x48, 0x89, 0xC6});  // mov rsi, rax
    out.bytes({0x80, 0xC2, 0x30});  // add dl, 48
    out.bytes({0x49, 0xFF, 0xCA});  // dec r10
    out.bytes({0x41, 0x88, 0x12});  // mov byte ptr [r10], dl
    out.bytes({0x49, 0xFF, 0xCB});  // dec r11
    out.jump_if_not_equal(next_place);
    out.place(write_dot);
    out.bytes({0x49, 0xFF, 0xCA});  // dec r10
    out.bytes({0x41, 0xC6, 0x02, 0x2E});  // mov byte ptr [r10], 46
    out.bytes({0x4C, 0x89, 0xD7});  // mov rdi, r10
    out.bytes({0x4C, 0x89, 0xC8});  // mov rax, r9
    out.call(runtime.format_int);
    out.place(add_sign);
    out.bytes({0x4D, 0x85, 0xC0});  // test r8, r8
    out.jump_if_not_negative(all_done);
    out.bytes({0x48, 0xFF, 0xCA});  // dec rdx
    out.bytes({0xC6, 0x02, 0x2D});  // mov byte ptr [rdx], 45
    out.place(all_done);
    out.bytes({0xC3});  // ret
    out.place(not_finite);
    out.bytes({0x48, 0x8D, 0x57, 0xFD});  // lea rdx, [rdi-3]
    out.bytes({0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x7F});  // movabs rax, 0x7FF0000000000000
    out.bytes({0x48, 0x39, 0xC3});  // cmp rbx, rax
    out.jump_if_equal(is_infinite);
    out.bytes({0xC6, 0x02, 0x6E});  // mov byte ptr [rdx], 110
    out.bytes({0xC6, 0x42, 0x01, 0x61});  // mov byte ptr [rdx+1], 97
    out.bytes({0xC6, 0x42, 0x02, 0x6E});  // mov byte ptr [rdx+2], 110
    out.bytes({0xC3});  // ret
    out.place(is_infinite);
    out.bytes({0xC6, 0x02, 0x69});  // mov byte ptr [rdx], 105
    out.bytes({0xC6, 0x42, 0x01, 0x6E});  // mov byte ptr [rdx+1], 110
    out.bytes({0xC6, 0x42, 0x02, 0x66});  // mov byte ptr [rdx+2], 102
    out.jump(add_sign);
    out.place(scientific);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x81, 0xEC, 0x00, 0x02, 0x00, 0x00});  // sub rsp, 512
    out.bytes({0x49, 0x89, 0xF9});  // mov r9, rdi
    out.bytes({0x48, 0x8D, 0x3C, 0x24});  // lea rdi, [rsp]
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.bytes({0xB9, 0x10, 0x00, 0x00, 0x00});  // mov ecx, 16
    out.bytes({0xF3, 0x48, 0xAB});  // rep stosq
    out.bytes({0x48, 0x89, 0xD9});  // mov rcx, rbx
    out.bytes({0x48, 0xC1, 0xE9, 0x34});  // shr rcx, 52
    out.bytes({0x48, 0x81, 0xE9, 0x33, 0x04, 0x00, 0x00});  // sub rcx, 1075
    out.bytes({0x48, 0x89, 0xD8});  // mov rax, rbx
    out.bytes({0x48, 0xBA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x00});  // movabs rdx, 0x000FFFFFFFFFFFFF
    out.bytes({0x48, 0x21, 0xD0});  // and rax, rdx
    out.bytes({0x48, 0x0F, 0xBA, 0xE8, 0x34});  // bts rax, 52
    out.bytes({0x48, 0x89, 0xCA});  // mov rdx, rcx
    out.bytes({0x48, 0xC1, 0xEA, 0x06});  // shr rdx, 6
    out.bytes({0x83, 0xE1, 0x3F});  // and ecx, 63
    out.bytes({0x45, 0x31, 0xD2});  // xor r10d, r10d
    out.bytes({0x49, 0x0F, 0xA5, 0xC2});  // shld r10, rax, cl
    out.bytes({0x48, 0xD3, 0xE0});  // shl rax, cl
    out.bytes({0x48, 0x89, 0x04, 0xD4});  // mov qword ptr [rsp+rdx*8], rax
    out.bytes({0x4C, 0x89, 0x54, 0xD4, 0x08});  // mov qword ptr [rsp+rdx*8+8], r10
    out.bytes({0x48, 0x8D, 0xBC, 0x24, 0x00, 0x02, 0x00, 0x00});  // lea rdi, [rsp+512]
    out.bytes({0x41, 0xBA, 0x0A, 0x00, 0x00, 0x00});  // mov r10d, 10
    out.place(next_big_digit);
    out.bytes({0xB9, 0x0F, 0x00, 0x00, 0x00});  // mov ecx, 15
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x45, 0x31, 0xDB});  // xor r11d, r11d
    out.place(next_word);
    out.bytes({0x48, 0x8B, 0x04, 0xCC});  // mov rax, qword ptr [rsp+rcx*8]
    out.bytes({0x49, 0xF7, 0xF2});  // div r10
    out.bytes({0x48, 0x89, 0x04, 0xCC});  // mov qword ptr [rsp+rcx*8], rax
    out.bytes({0x49, 0x09, 0xC3});  // or r11, rax
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.jump_if_not_negative(next_word);
    out.bytes({0x80, 0xC2, 0x30});  // add dl, 48
    out.bytes({0x48, 0xFF, 0xCF});  // dec rdi
    out.bytes({0x88, 0x17});  // mov byte ptr [rdi], dl
    out.bytes({0x4D, 0x85, 0xDB});  // test r11, r11
    out.jump_if_not_equal(next_big_digit);
    out.bytes({0x48, 0x89, 0xFE});  // mov rsi, rdi
    out.bytes({0x4C, 0x8D, 0x94, 0x24, 0xFF, 0x01, 0x00, 0x00});  // lea r10, [rsp+511]
    out.bytes({0x49, 0x29, 0xF2});  // sub r10, rsi
    out.bytes({0x8A, 0x46, 0x07});  // mov al, byte ptr [rsi+7]
    out.bytes({0x3C, 0x35});  // cmp al, 53
    out.jump_if_above(round_up);
    out.jump_if_below(rounded);
    out.bytes({0x48, 0x8D, 0x7E, 0x08});  // lea rdi, [rsi+8]
    out.bytes({0x48, 0x8D, 0x8C, 0x24, 0x00, 0x02, 0x00, 0x00});  // lea rcx, [rsp+512]
    out.place(check_rest);
    out.bytes({0x48, 0x39, 0xCF});  // cmp rdi, rcx
    out.jump_if_at_least(is_tie);
    out.bytes({0x80, 0x3F, 0x30});  // cmp byte ptr [rdi], 48
    out.jump_if_not_equal(round_up);
    out.bytes({0x48, 0xFF, 0xC7});  // inc rdi
    out.jump(check_rest);
    out.place(is_tie);
    out.bytes({0xF6, 0x46, 0x06, 0x01});  // test byte ptr [rsi+6], 1
    out.jump_if_equal(rounded);
    out.place(round_up);
    out.bytes({0xB9, 0x06, 0x00, 0x00, 0x00});  // mov ecx, 6
    out.place(next_carry);
    out.bytes({0x80, 0x3C, 0x0E, 0x39});  // cmp byte ptr [rsi+rcx], 57
    out.jump_if_not_equal(plain_increase);
    out.bytes({0xC6, 0x04, 0x0E, 0x30});  // mov byte ptr [rsi+rcx], 48
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.jump_if_not_negative(next_carry);
    out.bytes({0xC6, 0x06, 0x31});  // mov byte ptr [rsi], 49
    out.bytes({0x49, 0xFF, 0xC2});  // inc r10
    out.jump(rounded);
    out.place(plain_increase);
    out.bytes({0xFE, 0x04, 0x0E});  // inc byte ptr [rsi+rcx]
    out.place(rounded);
    out.bytes({0x4C, 0x89, 0xCF});  // mov rdi, r9
    out.bytes({0x4C, 0x89, 0xD0});  // mov rax, r10
    out.call(runtime.format_int);
    out.bytes({0x48, 0xFF, 0xCA});  // dec rdx
    out.bytes({0xC6, 0x02, 0x2B});  // mov byte ptr [rdx], 43
    out.bytes({0x48, 0xFF, 0xCA});  // dec rdx
    out.bytes({0xC6, 0x02, 0x65});  // mov byte ptr [rdx], 101
    out.bytes({0xB9, 0x06, 0x00, 0x00, 0x00});  // mov ecx, 6
    out.place(trim_zero);
    out.bytes({0x48, 0x83, 0xF9, 0x01});  // cmp rcx, 1
    out.jump_if_equal(copy_digit);
    out.bytes({0x80, 0x3C, 0x0E, 0x30});  // cmp byte ptr [rsi+rcx], 48
    out.jump_if_not_equal(copy_digit);
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.jump(trim_zero);
    out.place(copy_digit);
    out.bytes({0x8A, 0x04, 0x0E});  // mov al, byte ptr [rsi+rcx]
    out.bytes({0x48, 0xFF, 0xCA});  // dec rdx
    out.bytes({0x88, 0x02});  // mov byte ptr [rdx], al
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.jump_if_not_equal(copy_digit);
    out.bytes({0x48, 0xFF, 0xCA});  // dec rdx
    out.bytes({0xC6, 0x02, 0x2E});  // mov byte ptr [rdx], 46
    out.bytes({0x8A, 0x06});  // mov al, byte ptr [rsi]
    out.bytes({0x48, 0xFF, 0xCA});  // dec rdx
    out.bytes({0x88, 0x02});  // mov byte ptr [rdx], al
    out.bytes({0xC9});  // leave
    out.jump(add_sign);
}

// rax = the float
void write_print_float(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.print_float);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xEC, 0x40});  // sub rsp, 64
    out.bytes({0xC6, 0x45, 0xFF, 0x0A});  // mov byte ptr [rbp-1], 10
    out.bytes({0x48, 0x8D, 0x7D, 0xFF});  // lea rdi, [rbp-1]
    out.call(runtime.format_float);
    out.bytes({0x49, 0x89, 0xE8});  // mov r8, rbp
    out.bytes({0x49, 0x29, 0xD0});  // sub r8, rdx
    out.bytes({0x48, 0xC7, 0xC1, 0xF5, 0xFF, 0xFF, 0xFF});  // mov rcx, -11
    out.call(runtime.write);
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
}

// rax = the float
void write_float_to_string(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.float_to_string);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xEC, 0x40});  // sub rsp, 64
    out.bytes({0x48, 0x89, 0xEF});  // mov rdi, rbp
    out.call(runtime.format_float);
    out.bytes({0x49, 0x89, 0xE8});  // mov r8, rbp
    out.bytes({0x49, 0x29, 0xD0});  // sub r8, rdx
    out.call(runtime.make_string);
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
}

// rax = the float. The processor answers with the smallest int when a float doesn't fit
void write_float_to_int(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label too_large = out.new_label();
    const Label fits = out.new_label();
    out.place(runtime.float_to_int);
    out.bytes({0x66, 0x48, 0x0F, 0x6E, 0xC0});  // movq xmm0, rax
    out.bytes({0xF2, 0x48, 0x0F, 0x2C, 0xC0});  // cvttsd2si rax, xmm0
    out.bytes({0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80});  // movabs rcx, 0x8000000000000000
    out.bytes({0x48, 0x39, 0xC8});  // cmp rax, rcx
    out.jump_if_not_equal(fits);
    out.bytes({0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0xC3});  // movabs rcx, 0xC3E0000000000000
    out.bytes({0x66, 0x48, 0x0F, 0x6E, 0xC9});  // movq xmm1, rcx
    out.bytes({0x66, 0x0F, 0x2E, 0xC1});  // ucomisd xmm0, xmm1
    out.jump_if_unordered(too_large);
    out.jump_if_equal(fits);
    out.place(too_large);
    out.lea_rax_string(text.float_too_large);
    out.jump(runtime.run_error);
    out.place(fits);
    out.bytes({0xC3});  // ret
}

// rax = the left float, rcx = the right float. The remainder keeps the left side's sign, like % on ints
void write_float_mod(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label next_step = out.new_label();
    out.place(runtime.float_mod);
    out.bytes({0x51});  // push rcx
    out.bytes({0xDD, 0x04, 0x24});  // fld qword ptr [rsp]
    out.bytes({0x48, 0x89, 0x04, 0x24});  // mov qword ptr [rsp], rax
    out.bytes({0xDD, 0x04, 0x24});  // fld qword ptr [rsp]
    out.place(next_step);
    out.bytes({0xD9, 0xF8});  // fprem
    out.bytes({0xDF, 0xE0});  // fnstsw ax
    out.bytes({0xF6, 0xC4, 0x04});  // test ah, 4
    out.jump_if_not_equal(next_step);
    out.bytes({0xDD, 0xD9});  // fstp st(1)
    out.bytes({0xDD, 0x1C, 0x24});  // fstp qword ptr [rsp]
    out.bytes({0x58});  // pop rax
    out.bytes({0xC3});  // ret
}

// The one text builder: its pointer, length and room. Clears it for new text
void write_builder_start(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.builder_start);
    out.lea_data(RAX, DataRef::Builder);
    out.bytes({0x48, 0xC7, 0x40, 0x08, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rax+8], 0
    out.bytes({0xC3});  // ret
}

// rdx = the text, r8 = its length. Keeps rbx, rsi and rdi
void write_builder_add(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label big_enough = out.new_label();
    const Label has_room = out.new_label();
    out.place(runtime.builder_add);
    out.bytes({0x53});  // push rbx
    out.bytes({0x56});  // push rsi
    out.bytes({0x57});  // push rdi
    out.bytes({0x41, 0x54});  // push r12
    out.bytes({0x41, 0x55});  // push r13
    out.lea_data(RBX, DataRef::Builder);
    out.bytes({0x48, 0x89, 0xD6});  // mov rsi, rdx
    out.bytes({0x4D, 0x89, 0xC4});  // mov r12, r8
    out.bytes({0x48, 0x8B, 0x43, 0x08});  // mov rax, qword ptr [rbx+8]
    out.bytes({0x4C, 0x01, 0xE0});  // add rax, r12
    out.bytes({0x48, 0x3B, 0x43, 0x10});  // cmp rax, qword ptr [rbx+16]
    out.jump_if_at_most(has_room);
    out.bytes({0x48, 0x8D, 0x0C, 0x00});  // lea rcx, [rax+rax]
    out.bytes({0x48, 0x81, 0xF9, 0x00, 0x01, 0x00, 0x00});  // cmp rcx, 256
    out.jump_if_at_least(big_enough);
    out.bytes({0xB9, 0x00, 0x01, 0x00, 0x00});  // mov ecx, 256
    out.place(big_enough);
    out.bytes({0x48, 0x89, 0x4B, 0x10});  // mov qword ptr [rbx+16], rcx
    out.call(runtime.raw_alloc);
    out.bytes({0x48, 0x89, 0xC7});  // mov rdi, rax
    out.bytes({0x49, 0x89, 0xF0});  // mov r8, rsi
    out.bytes({0x48, 0x8B, 0x33});  // mov rsi, qword ptr [rbx]
    out.bytes({0x49, 0x89, 0xF5});  // mov r13, rsi
    out.bytes({0x48, 0x89, 0x3B});  // mov qword ptr [rbx], rdi
    out.bytes({0x48, 0x8B, 0x4B, 0x08});  // mov rcx, qword ptr [rbx+8]
    out.bytes({0xF3, 0xA4});  // rep movsb
    out.bytes({0x4C, 0x89, 0xC6});  // mov rsi, r8
    out.bytes({0x4C, 0x89, 0xE9});  // mov rcx, r13
    out.call(runtime.raw_free);
    out.place(has_room);
    out.bytes({0x48, 0x8B, 0x3B});  // mov rdi, qword ptr [rbx]
    out.bytes({0x48, 0x03, 0x7B, 0x08});  // add rdi, qword ptr [rbx+8]
    out.bytes({0x4C, 0x89, 0xE1});  // mov rcx, r12
    out.bytes({0xF3, 0xA4});  // rep movsb
    out.bytes({0x4C, 0x01, 0x63, 0x08});  // add qword ptr [rbx+8], r12
    out.bytes({0x41, 0x5D});  // pop r13
    out.bytes({0x41, 0x5C});  // pop r12
    out.bytes({0x5F});  // pop rdi
    out.bytes({0x5E});  // pop rsi
    out.bytes({0x5B});  // pop rbx
    out.bytes({0xC3});  // ret
}

// rax = the string
void write_builder_add_string(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.builder_add_string);
    out.bytes({0x48, 0x8D, 0x50, 0x08});  // lea rdx, [rax+8]
    out.bytes({0x4C, 0x8B, 0x00});  // mov r8, qword ptr [rax]
    out.jump(runtime.builder_add);
}

// Answers with the built text as a new string
void write_builder_to_string(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.builder_to_string);
    out.lea_data(RAX, DataRef::Builder);
    out.bytes({0x48, 0x8B, 0x10});  // mov rdx, qword ptr [rax]
    out.bytes({0x4C, 0x8B, 0x40, 0x08});  // mov r8, qword ptr [rax+8]
    out.jump(runtime.make_string);
}

// rax = the int. Keeps rbx, rsi, r12 and r13
void write_builder_add_int(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.builder_add_int);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xEC, 0x30});  // sub rsp, 48
    out.bytes({0x48, 0x89, 0xEF});  // mov rdi, rbp
    out.call(runtime.format_int);
    out.bytes({0x49, 0x89, 0xE8});  // mov r8, rbp
    out.bytes({0x49, 0x29, 0xD0});  // sub r8, rdx
    out.call(runtime.builder_add);
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
}

// rax = the index, rcx = the size, rdx = the words after the index, r8 = the words after the size
void write_index_error(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label not_items = out.new_label();
    const Label words_chosen = out.new_label();
    out.place(runtime.index_error);
    out.bytes({0x48, 0x83, 0xF9, 0x01});  // cmp rcx, 1
    out.jump_if_not_equal(words_chosen);
    out.lea_string(R9, text.items_word);
    out.bytes({0x4D, 0x39, 0xC8});  // cmp r8, r9
    out.jump_if_not_equal(not_items);
    out.lea_string(R8, text.item_word);
    out.jump(words_chosen);
    out.place(not_items);
    out.lea_string(R9, text.bytes_word);
    out.bytes({0x4D, 0x39, 0xC8});  // cmp r8, r9
    out.jump_if_not_equal(words_chosen);
    out.lea_string(R8, text.byte_word);
    out.place(words_chosen);
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.bytes({0x48, 0x89, 0xCE});  // mov rsi, rcx
    out.bytes({0x49, 0x89, 0xD4});  // mov r12, rdx
    out.bytes({0x4D, 0x89, 0xC5});  // mov r13, r8
    out.call(runtime.builder_start);
    out.lea_string(RAX, text.index_word);
    out.call(runtime.builder_add_string);
    out.bytes({0x48, 0x89, 0xD8});  // mov rax, rbx
    out.call(runtime.builder_add_int);
    out.bytes({0x4C, 0x89, 0xE0});  // mov rax, r12
    out.call(runtime.builder_add_string);
    out.bytes({0x48, 0x89, 0xF0});  // mov rax, rsi
    out.call(runtime.builder_add_int);
    out.bytes({0x4C, 0x89, 0xE8});  // mov rax, r13
    out.call(runtime.builder_add_string);
    out.call(runtime.builder_to_string);
    out.jump(runtime.run_error);
}

// rax = the string, rcx = the index. Answers with that byte's one-letter string, which is shared, since text never changes
void write_string_at(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label outside = out.new_label();
    out.place(runtime.string_at);
    out.bytes({0x48, 0x3B, 0x08});  // cmp rcx, qword ptr [rax]
    out.jump_if_at_least(outside);
    out.bytes({0x0F, 0xB6, 0x44, 0x08, 0x08});  // movzx eax, byte ptr [rax+rcx+8]
    out.bytes({0xC1, 0xE0, 0x04});  // shl eax, 4
    out.lea_data(RDX, DataRef::Letters);
    out.bytes({0x48, 0x01, 0xD0});  // add rax, rdx
    out.bytes({0xC3});  // ret
    out.place(outside);
    out.bytes({0x48, 0x89, 0xCA});  // mov rdx, rcx
    out.bytes({0x48, 0x8B, 0x08});  // mov rcx, qword ptr [rax]
    out.bytes({0x48, 0x89, 0xD0});  // mov rax, rdx
    out.lea_string(RDX, text.outside_string);
    out.lea_string(R8, text.bytes_word);
    out.jump(runtime.index_error);
}

// rcx = how many items. An array is its length, its room and a pointer to its items, so it can grow and stay shared. An empty one gets its items on its first add, since most never get any
void write_new_array(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label enough_room = out.new_label();
    const Label stays_empty = out.new_label();
    out.place(runtime.new_array);
    out.bytes({0x48, 0x89, 0xCB});  // mov rbx, rcx
    out.bytes({0xB9, 0x18, 0x00, 0x00, 0x00});  // mov ecx, 24
    out.call(runtime.alloc);
    out.bytes({0x48, 0x89, 0xC6});  // mov rsi, rax
    out.bytes({0x48, 0x89, 0xD9});  // mov rcx, rbx
    out.bytes({0x48, 0x85, 0xC9});  // test rcx, rcx
    out.jump_if_equal(stays_empty);
    out.bytes({0x48, 0x83, 0xF9, 0x04});  // cmp rcx, 4
    out.jump_if_at_least(enough_room);
    out.bytes({0xB9, 0x04, 0x00, 0x00, 0x00});  // mov ecx, 4
    out.place(enough_room);
    out.bytes({0x48, 0x89, 0xCF});  // mov rdi, rcx
    out.bytes({0x48, 0xC1, 0xE1, 0x03});  // shl rcx, 3
    out.call(runtime.alloc);
    out.bytes({0x48, 0x89, 0x46, 0x10});  // mov qword ptr [rsi+16], rax
    out.bytes({0x48, 0x89, 0x7E, 0x08});  // mov qword ptr [rsi+8], rdi
    out.bytes({0x48, 0x89, 0x1E});  // mov qword ptr [rsi], rbx
    out.bytes({0x48, 0x89, 0xF0});  // mov rax, rsi
    out.bytes({0xC3});  // ret
    out.place(stays_empty);
    out.bytes({0x48, 0x89, 0xF0});  // mov rax, rsi
    out.bytes({0xC3});  // ret
}

// rax = the array, rcx = the new item
void write_array_add(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label has_items = out.new_label();
    const Label has_room = out.new_label();
    out.place(runtime.array_add);
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.bytes({0x48, 0x89, 0xCE});  // mov rsi, rcx
    out.bytes({0x48, 0x8B, 0x03});  // mov rax, qword ptr [rbx]
    out.bytes({0x48, 0x3B, 0x43, 0x08});  // cmp rax, qword ptr [rbx+8]
    out.jump_if_below(has_room);
    out.bytes({0x48, 0x8B, 0x4B, 0x08});  // mov rcx, qword ptr [rbx+8]
    out.bytes({0x48, 0x85, 0xC9});  // test rcx, rcx
    out.jump_if_not_equal(has_items);
    out.bytes({0x48, 0xC7, 0x43, 0x08, 0x02, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+8], 2
    out.bytes({0xB9, 0x02, 0x00, 0x00, 0x00});  // mov ecx, 2
    out.place(has_items);
    out.bytes({0x48, 0xC1, 0xE1, 0x04});  // shl rcx, 4
    out.call(runtime.alloc);
    out.bytes({0x48, 0x89, 0xC7});  // mov rdi, rax
    out.bytes({0x48, 0xD1, 0x63, 0x08});  // shl qword ptr [rbx+8], 1
    out.bytes({0x49, 0x89, 0xF0});  // mov r8, rsi
    out.bytes({0x48, 0x8B, 0x73, 0x10});  // mov rsi, qword ptr [rbx+16]
    out.bytes({0x48, 0x89, 0x7B, 0x10});  // mov qword ptr [rbx+16], rdi
    out.bytes({0x48, 0x8B, 0x0B});  // mov rcx, qword ptr [rbx]
    out.bytes({0xF3, 0x48, 0xA5});  // rep movsq
    out.bytes({0x4C, 0x89, 0xC6});  // mov rsi, r8
    out.place(has_room);
    out.bytes({0x48, 0x8B, 0x03});  // mov rax, qword ptr [rbx]
    out.bytes({0x48, 0x8B, 0x53, 0x10});  // mov rdx, qword ptr [rbx+16]
    out.bytes({0x48, 0x89, 0x34, 0xC2});  // mov qword ptr [rdx+rax*8], rsi
    out.bytes({0x48, 0xFF, 0x03});  // inc qword ptr [rbx]
    out.bytes({0xC3});  // ret
}

// rax = the array. Answers with its last item
void write_array_pop(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label is_empty = out.new_label();
    out.place(runtime.array_pop);
    out.bytes({0x48, 0x8B, 0x08});  // mov rcx, qword ptr [rax]
    out.bytes({0x48, 0x85, 0xC9});  // test rcx, rcx
    out.jump_if_equal(is_empty);
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.bytes({0x48, 0x89, 0x08});  // mov qword ptr [rax], rcx
    out.bytes({0x48, 0x8B, 0x50, 0x10});  // mov rdx, qword ptr [rax+16]
    out.bytes({0x48, 0x8B, 0x04, 0xCA});  // mov rax, qword ptr [rdx+rcx*8]
    out.bytes({0x48, 0xC7, 0x04, 0xCA, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rdx+rcx*8], 0
    out.bytes({0xC3});  // ret
    out.place(is_empty);
    out.lea_string(RAX, text.pop_empty);
    out.jump(runtime.run_error);
}

// rax = the array, rcx = the index. Answers with where that item lives, and keeps r9
void write_array_address(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label outside = out.new_label();
    out.place(runtime.array_address);
    out.bytes({0x48, 0x3B, 0x08});  // cmp rcx, qword ptr [rax]
    out.jump_if_at_least(outside);
    out.bytes({0x48, 0x8B, 0x40, 0x10});  // mov rax, qword ptr [rax+16]
    out.bytes({0x48, 0x8D, 0x04, 0xC8});  // lea rax, [rax+rcx*8]
    out.bytes({0xC3});  // ret
    out.place(outside);
    out.bytes({0x48, 0x89, 0xCA});  // mov rdx, rcx
    out.bytes({0x48, 0x8B, 0x08});  // mov rcx, qword ptr [rax]
    out.bytes({0x48, 0x89, 0xD0});  // mov rax, rdx
    out.lea_string(RDX, text.outside_array);
    out.lea_string(R8, text.items_word);
    out.jump(runtime.index_error);
}

// rax = the array, rcx = the index. Answers with that item
void write_array_at(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.array_at);
    out.call(runtime.array_address);
    out.bytes({0x48, 0x8B, 0x00});  // mov rax, qword ptr [rax]
    out.bytes({0xC3});  // ret
}

// rax = the float
void write_builder_add_float(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.builder_add_float);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xEC, 0x40});  // sub rsp, 64
    out.bytes({0x48, 0x89, 0xEF});  // mov rdi, rbp
    out.call(runtime.format_float);
    out.bytes({0x49, 0x89, 0xE8});  // mov r8, rbp
    out.bytes({0x49, 0x29, 0xD0});  // sub r8, rdx
    out.call(runtime.builder_add);
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
}

// rax = the bool
void write_builder_add_bool(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.builder_add_bool);
    out.call(runtime.bool_to_string);
    out.jump(runtime.builder_add_string);
}

// rax = the string, shown inside an array or struct
void write_builder_add_quoted(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    out.place(runtime.builder_add_quoted);
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.lea_string(RAX, text.quote);
    out.call(runtime.builder_add_string);
    out.bytes({0x48, 0x89, 0xD8});  // mov rax, rbx
    out.call(runtime.builder_add_string);
    out.lea_string(RAX, text.quote);
    out.jump(runtime.builder_add_string);
}

// Writes the built text and a new line
void write_builder_print(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    out.place(runtime.builder_print);
    out.lea_string(RAX, text.new_line);
    out.call(runtime.builder_add_string);
    out.lea_data(RAX, DataRef::Builder);
    out.bytes({0x48, 0x8B, 0x10});  // mov rdx, qword ptr [rax]
    out.bytes({0x4C, 0x8B, 0x40, 0x08});  // mov r8, qword ptr [rax+8]
    out.bytes({0x48, 0xC7, 0xC1, 0xF5, 0xFF, 0xFF, 0xFF});  // mov rcx, -11
    out.jump(runtime.write);
}

// rax = the array or struct being shown. Answers with 1 if it's already being shown, so a loop of items can't go on forever. Otherwise links it in, using [rbp-16] and [rbp-24] of its caller
void write_show_enter(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label next_visit = out.new_label();
    const Label found = out.new_label();
    const Label not_found = out.new_label();
    out.place(runtime.show_enter);
    out.lea_data(RDX, DataRef::VisitHead);
    out.bytes({0x48, 0x8B, 0x0A});  // mov rcx, qword ptr [rdx]
    out.place(next_visit);
    out.bytes({0x48, 0x85, 0xC9});  // test rcx, rcx
    out.jump_if_equal(not_found);
    out.bytes({0x48, 0x39, 0x41, 0x08});  // cmp qword ptr [rcx+8], rax
    out.jump_if_equal(found);
    out.bytes({0x48, 0x8B, 0x09});  // mov rcx, qword ptr [rcx]
    out.jump(next_visit);
    out.place(found);
    out.bytes({0xB8, 0x01, 0x00, 0x00, 0x00});  // mov eax, 1
    out.bytes({0xC3});  // ret
    out.place(not_found);
    out.bytes({0x48, 0x89, 0x45, 0xF0});  // mov qword ptr [rbp-16], rax
    out.bytes({0x48, 0x8B, 0x0A});  // mov rcx, qword ptr [rdx]
    out.bytes({0x48, 0x89, 0x4D, 0xE8});  // mov qword ptr [rbp-24], rcx
    out.bytes({0x48, 0x8D, 0x4D, 0xE8});  // lea rcx, [rbp-24]
    out.bytes({0x48, 0x89, 0x0A});  // mov qword ptr [rdx], rcx
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.bytes({0xC3});  // ret
}

// Unlinks what show_enter linked in
void write_show_leave(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.show_leave);
    out.lea_data(RDX, DataRef::VisitHead);
    out.bytes({0x48, 0x8B, 0x45, 0xE8});  // mov rax, qword ptr [rbp-24]
    out.bytes({0x48, 0x89, 0x02});  // mov qword ptr [rdx], rax
    out.bytes({0xC3});  // ret
}

// rax = some text from the program, rdx = the words before it, r8 = the words after it
void write_text_error(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.text_error);
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.bytes({0x49, 0x89, 0xD4});  // mov r12, rdx
    out.bytes({0x4D, 0x89, 0xC5});  // mov r13, r8
    out.call(runtime.builder_start);
    out.bytes({0x4C, 0x89, 0xE0});  // mov rax, r12
    out.call(runtime.builder_add_string);
    out.bytes({0x48, 0x89, 0xD8});  // mov rax, rbx
    out.call(runtime.builder_add_string);
    out.bytes({0x4C, 0x89, 0xE8});  // mov rax, r13
    out.call(runtime.builder_add_string);
    out.call(runtime.builder_to_string);
    out.jump(runtime.run_error);
}

// rax = the text. Takes digits with an optional minus sign, and nothing else
void write_string_to_int(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label next_digit = out.new_label();
    const Label fits = out.new_label();
    const Label positive = out.new_label();
    const Label bad_number = out.new_label();
    out.place(runtime.string_to_int);
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.bytes({0x48, 0x8D, 0x70, 0x08});  // lea rsi, [rax+8]
    out.bytes({0x48, 0x8B, 0x08});  // mov rcx, qword ptr [rax]
    out.bytes({0x48, 0x85, 0xC9});  // test rcx, rcx
    out.jump_if_equal(bad_number);
    out.bytes({0x31, 0xFF});  // xor edi, edi
    out.bytes({0x45, 0x31, 0xC0});  // xor r8d, r8d
    out.bytes({0x41, 0xB9, 0x0A, 0x00, 0x00, 0x00});  // mov r9d, 10
    out.bytes({0x80, 0x3E, 0x2D});  // cmp byte ptr [rsi], 45
    out.jump_if_not_equal(next_digit);
    out.bytes({0xBF, 0x01, 0x00, 0x00, 0x00});  // mov edi, 1
    out.bytes({0x48, 0xFF, 0xC6});  // inc rsi
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.jump_if_equal(bad_number);
    out.place(next_digit);
    out.bytes({0x0F, 0xB6, 0x06});  // movzx eax, byte ptr [rsi]
    out.bytes({0x83, 0xE8, 0x30});  // sub eax, 48
    out.bytes({0x83, 0xF8, 0x09});  // cmp eax, 9
    out.jump_if_above(bad_number);
    out.bytes({0x49, 0x89, 0xC2});  // mov r10, rax
    out.bytes({0x4C, 0x89, 0xC0});  // mov rax, r8
    out.bytes({0x49, 0xF7, 0xE1});  // mul r9
    out.jump_if_below(bad_number);
    out.bytes({0x4C, 0x01, 0xD0});  // add rax, r10
    out.jump_if_below(bad_number);
    out.bytes({0x49, 0x89, 0xC0});  // mov r8, rax
    out.bytes({0x48, 0xFF, 0xC6});  // inc rsi
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.jump_if_not_equal(next_digit);
    out.bytes({0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80});  // movabs rax, 0x8000000000000000
    out.bytes({0x49, 0x39, 0xC0});  // cmp r8, rax
    out.jump_if_above(bad_number);
    out.jump_if_not_equal(fits);
    out.bytes({0x85, 0xFF});  // test edi, edi
    out.jump_if_equal(bad_number);
    out.place(fits);
    out.bytes({0x4C, 0x89, 0xC0});  // mov rax, r8
    out.bytes({0x85, 0xFF});  // test edi, edi
    out.jump_if_equal(positive);
    out.bytes({0x48, 0xF7, 0xD8});  // neg rax
    out.place(positive);
    out.bytes({0xC3});  // ret
    out.place(bad_number);
    out.bytes({0x48, 0x89, 0xD8});  // mov rax, rbx
    out.lea_string(RDX, text.cant_turn);
    out.lea_string(R8, text.into_int);
    out.jump(runtime.text_error);
}

// rdi = a whole number of 128 words, rax = what to add afterward
void write_big_times_ten(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label next_word = out.new_label();
    out.place(runtime.big_times_ten);
    out.bytes({0x49, 0x89, 0xC3});  // mov r11, rax
    out.bytes({0x31, 0xC9});  // xor ecx, ecx
    out.place(next_word);
    out.bytes({0x48, 0x8B, 0x04, 0xCF});  // mov rax, qword ptr [rdi+rcx*8]
    out.bytes({0xBA, 0x0A, 0x00, 0x00, 0x00});  // mov edx, 10
    out.bytes({0x48, 0xF7, 0xE2});  // mul rdx
    out.bytes({0x4C, 0x01, 0xD8});  // add rax, r11
    out.bytes({0x48, 0x83, 0xD2, 0x00});  // adc rdx, 0
    out.bytes({0x48, 0x89, 0x04, 0xCF});  // mov qword ptr [rdi+rcx*8], rax
    out.bytes({0x49, 0x89, 0xD3});  // mov r11, rdx
    out.bytes({0x48, 0xFF, 0xC1});  // inc rcx
    out.bytes({0x48, 0x81, 0xF9, 0x80, 0x00, 0x00, 0x00});  // cmp rcx, 128
    out.jump_if_below(next_word);
    out.bytes({0xC3});  // ret
}

// rdi = a whole number of 128 words
void write_big_double(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label next_word = out.new_label();
    out.place(runtime.big_double);
    out.bytes({0xB9, 0x80, 0x00, 0x00, 0x00});  // mov ecx, 128
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.place(next_word);
    out.bytes({0x48, 0xD1, 0x17});  // rcl qword ptr [rdi], 1
    out.bytes({0x48, 0x8D, 0x7F, 0x08});  // lea rdi, [rdi+8]
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.jump_if_not_equal(next_word);
    out.bytes({0xC3});  // ret
}

// rdi and rsi = two whole numbers of 128 words. Answers with less than, equal to or more than 0 in rax
void write_big_compare(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label next_word = out.new_label();
    const Label different = out.new_label();
    out.place(runtime.big_compare);
    out.bytes({0xB9, 0x7F, 0x00, 0x00, 0x00});  // mov ecx, 127
    out.place(next_word);
    out.bytes({0x48, 0x8B, 0x04, 0xCF});  // mov rax, qword ptr [rdi+rcx*8]
    out.bytes({0x48, 0x3B, 0x04, 0xCE});  // cmp rax, qword ptr [rsi+rcx*8]
    out.jump_if_not_equal(different);
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.jump_if_not_negative(next_word);
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.bytes({0xC3});  // ret
    out.place(different);
    out.bytes({0x48, 0x19, 0xC0});  // sbb rax, rax
    out.bytes({0x48, 0x83, 0xC8, 0x01});  // or rax, 1
    out.bytes({0xC3});  // ret
}

// rdi = a whole number of 128 words, which rsi is taken away from
void write_big_subtract(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label next_word = out.new_label();
    out.place(runtime.big_subtract);
    out.bytes({0xB9, 0x80, 0x00, 0x00, 0x00});  // mov ecx, 128
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.place(next_word);
    out.bytes({0x48, 0x8B, 0x04, 0xD6});  // mov rax, qword ptr [rsi+rdx*8]
    out.bytes({0x48, 0x19, 0x04, 0xD7});  // sbb qword ptr [rdi+rdx*8], rax
    out.bytes({0x48, 0xFF, 0xC2});  // inc rdx
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.jump_if_not_equal(next_word);
    out.bytes({0xC3});  // ret
}

// Keeps r12 to r15, where Jamo code may hold variables, around string_to_float_body
void write_string_to_float(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.string_to_float);
    out.bytes({0x41, 0x54});  // push r12
    out.bytes({0x41, 0x55});  // push r13
    out.bytes({0x41, 0x56});  // push r14
    out.bytes({0x41, 0x57});  // push r15
    out.call(runtime.string_to_float_body);
    out.bytes({0x41, 0x5F});  // pop r15
    out.bytes({0x41, 0x5E});  // pop r14
    out.bytes({0x41, 0x5D});  // pop r13
    out.bytes({0x41, 0x5C});  // pop r12
    out.bytes({0xC3});  // ret
}

// rax = the text. Rounds exactly, using whole numbers only. [rsp] holds the top of a fraction, and [rsp+1024] the bottom
void write_string_to_float_body(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label next_char = out.new_label();
    const Label is_significant = out.new_label();
    const Label digit_counted = out.new_label();
    const Label too_many_digits = out.new_label();
    const Label char_done = out.new_label();
    const Label found_dot = out.new_label();
    const Label all_read = out.new_label();
    const Label scale_top = out.new_label();
    const Label scale_bottom = out.new_label();
    const Label scaled = out.new_label();
    const Label shift_bottom = out.new_label();
    const Label bottom_too_small = out.new_label();
    const Label top_too_small = out.new_label();
    const Label in_range = out.new_label();
    const Label has_precision = out.new_label();
    const Label next_bit = out.new_label();
    const Label bit_done = out.new_label();
    const Label check_rest = out.new_label();
    const Label round_up = out.new_label();
    const Label rounded = out.new_label();
    const Label has_answer = out.new_label();
    const Label bad_number = out.new_label();
    out.place(runtime.string_to_float_body);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x81, 0xEC, 0x00, 0x08, 0x00, 0x00});  // sub rsp, 2048
    out.bytes({0x48, 0x83, 0x0C, 0x24, 0x00});  // or qword ptr [rsp], 0
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.bytes({0x48, 0x8D, 0x3C, 0x24});  // lea rdi, [rsp]
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.bytes({0xB9, 0x00, 0x01, 0x00, 0x00});  // mov ecx, 256
    out.bytes({0xF3, 0x48, 0xAB});  // rep stosq
    out.bytes({0x48, 0x8D, 0x73, 0x08});  // lea rsi, [rbx+8]
    out.bytes({0x4C, 0x8B, 0x03});  // mov r8, qword ptr [rbx]
    out.bytes({0x4D, 0x85, 0xC0});  // test r8, r8
    out.jump_if_equal(bad_number);
    out.bytes({0x45, 0x31, 0xFF});  // xor r15d, r15d
    out.bytes({0x45, 0x31, 0xE4});  // xor r12d, r12d
    out.bytes({0x45, 0x31, 0xED});  // xor r13d, r13d
    out.bytes({0x45, 0x31, 0xF6});  // xor r14d, r14d
    out.bytes({0x45, 0x31, 0xC9});  // xor r9d, r9d
    out.bytes({0x45, 0x31, 0xD2});  // xor r10d, r10d
    out.bytes({0x80, 0x3E, 0x2D});  // cmp byte ptr [rsi], 45
    out.jump_if_not_equal(next_char);
    out.bytes({0x41, 0xBF, 0x01, 0x00, 0x00, 0x00});  // mov r15d, 1
    out.bytes({0x48, 0xFF, 0xC6});  // inc rsi
    out.bytes({0x49, 0xFF, 0xC8});  // dec r8
    out.jump_if_equal(bad_number);
    out.place(next_char);
    out.bytes({0x4D, 0x85, 0xC0});  // test r8, r8
    out.jump_if_equal(all_read);
    out.bytes({0x0F, 0xB6, 0x06});  // movzx eax, byte ptr [rsi]
    out.bytes({0x3C, 0x2E});  // cmp al, 46
    out.jump_if_equal(found_dot);
    out.bytes({0x83, 0xE8, 0x30});  // sub eax, 48
    out.bytes({0x83, 0xF8, 0x09});  // cmp eax, 9
    out.jump_if_above(bad_number);
    out.bytes({0x49, 0xFF, 0xC1});  // inc r9
    out.bytes({0x4D, 0x85, 0xED});  // test r13, r13
    out.jump_if_not_equal(is_significant);
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_equal(digit_counted);
    out.place(is_significant);
    out.bytes({0x49, 0x81, 0xFD, 0x20, 0x03, 0x00, 0x00});  // cmp r13, 800
    out.jump_if_at_least(too_many_digits);
    out.bytes({0x48, 0x8D, 0x3C, 0x24});  // lea rdi, [rsp]
    out.call(runtime.big_times_ten);
    out.bytes({0x49, 0xFF, 0xC5});  // inc r13
    out.place(digit_counted);
    out.bytes({0x4D, 0x85, 0xD2});  // test r10, r10
    out.jump_if_equal(char_done);
    out.bytes({0x49, 0xFF, 0xCC});  // dec r12
    out.jump(char_done);
    out.place(too_many_digits);
    out.bytes({0x49, 0x09, 0xC6});  // or r14, rax
    out.bytes({0x4D, 0x85, 0xD2});  // test r10, r10
    out.jump_if_not_equal(char_done);
    out.bytes({0x49, 0xFF, 0xC4});  // inc r12
    out.place(char_done);
    out.bytes({0x48, 0xFF, 0xC6});  // inc rsi
    out.bytes({0x49, 0xFF, 0xC8});  // dec r8
    out.jump(next_char);
    out.place(found_dot);
    out.bytes({0x4D, 0x85, 0xD2});  // test r10, r10
    out.jump_if_not_equal(bad_number);
    out.bytes({0x4D, 0x85, 0xC9});  // test r9, r9
    out.jump_if_equal(bad_number);
    out.bytes({0x41, 0xBA, 0x01, 0x00, 0x00, 0x00});  // mov r10d, 1
    out.bytes({0x48, 0xFF, 0xC6});  // inc rsi
    out.bytes({0x49, 0xFF, 0xC8});  // dec r8
    out.jump_if_equal(bad_number);
    out.jump(next_char);
    out.place(all_read);
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.bytes({0x4D, 0x85, 0xED});  // test r13, r13
    out.jump_if_equal(has_answer);
    out.bytes({0x4B, 0x8D, 0x04, 0x2C});  // lea rax, [r12+r13]
    out.bytes({0x48, 0x3D, 0x36, 0x01, 0x00, 0x00});  // cmp rax, 310
    out.jump_if_greater(bad_number);
    out.bytes({0x48, 0x3D, 0xB6, 0xFE, 0xFF, 0xFF});  // cmp rax, -330
    out.jump_if_less(bad_number);
    out.bytes({0x48, 0xC7, 0x84, 0x24, 0x00, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+1024], 1
    out.place(scale_top);
    out.bytes({0x4D, 0x85, 0xE4});  // test r12, r12
    out.jump_if_less_equal(scale_bottom);
    out.bytes({0x48, 0x8D, 0x3C, 0x24});  // lea rdi, [rsp]
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.call(runtime.big_times_ten);
    out.bytes({0x49, 0xFF, 0xCC});  // dec r12
    out.jump(scale_top);
    out.place(scale_bottom);
    out.bytes({0x4D, 0x85, 0xE4});  // test r12, r12
    out.jump_if_greater_equal(scaled);
    out.bytes({0x48, 0x8D, 0xBC, 0x24, 0x00, 0x04, 0x00, 0x00});  // lea rdi, [rsp+1024]
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.call(runtime.big_times_ten);
    out.bytes({0x49, 0xFF, 0xC4});  // inc r12
    out.jump(scale_bottom);
    out.place(scaled);
    out.bytes({0x41, 0xBD, 0x35, 0x00, 0x00, 0x00});  // mov r13d, 53
    out.bytes({0x41, 0xBC, 0x35, 0x00, 0x00, 0x00});  // mov r12d, 53
    out.place(shift_bottom);
    out.bytes({0x48, 0x8D, 0xBC, 0x24, 0x00, 0x04, 0x00, 0x00});  // lea rdi, [rsp+1024]
    out.call(runtime.big_double);
    out.bytes({0x49, 0xFF, 0xCC});  // dec r12
    out.jump_if_not_equal(shift_bottom);
    out.place(bottom_too_small);
    out.bytes({0x48, 0x8D, 0x3C, 0x24});  // lea rdi, [rsp]
    out.bytes({0x48, 0x8D, 0xB4, 0x24, 0x00, 0x04, 0x00, 0x00});  // lea rsi, [rsp+1024]
    out.call(runtime.big_compare);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_negative(top_too_small);
    out.bytes({0x48, 0x8D, 0xBC, 0x24, 0x00, 0x04, 0x00, 0x00});  // lea rdi, [rsp+1024]
    out.call(runtime.big_double);
    out.bytes({0x49, 0xFF, 0xC5});  // inc r13
    out.jump(bottom_too_small);
    out.place(top_too_small);
    out.bytes({0x48, 0x8D, 0x3C, 0x24});  // lea rdi, [rsp]
    out.bytes({0x48, 0x8D, 0xB4, 0x24, 0x00, 0x04, 0x00, 0x00});  // lea rsi, [rsp+1024]
    out.call(runtime.big_compare);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_not_negative(in_range);
    out.bytes({0x48, 0x8D, 0x3C, 0x24});  // lea rdi, [rsp]
    out.call(runtime.big_double);
    out.bytes({0x49, 0xFF, 0xCD});  // dec r13
    out.jump(top_too_small);
    out.place(in_range);
    out.bytes({0x49, 0x81, 0xFD, 0xFF, 0x03, 0x00, 0x00});  // cmp r13, 1023
    out.jump_if_greater(bad_number);
    out.bytes({0x41, 0xB9, 0x35, 0x00, 0x00, 0x00});  // mov r9d, 53
    out.bytes({0x49, 0x81, 0xFD, 0x02, 0xFC, 0xFF, 0xFF});  // cmp r13, -1022
    out.jump_if_greater_equal(has_precision);
    out.bytes({0x4D, 0x8D, 0x8D, 0x33, 0x04, 0x00, 0x00});  // lea r9, [r13+1075]
    out.bytes({0x4D, 0x85, 0xC9});  // test r9, r9
    out.jump_if_negative(bad_number);
    out.place(has_precision);
    out.bytes({0x4D, 0x8D, 0x61, 0x01});  // lea r12, [r9+1]
    out.bytes({0x45, 0x31, 0xD2});  // xor r10d, r10d
    out.place(next_bit);
    out.bytes({0x49, 0xD1, 0xE2});  // shl r10, 1
    out.bytes({0x48, 0x8D, 0x3C, 0x24});  // lea rdi, [rsp]
    out.bytes({0x48, 0x8D, 0xB4, 0x24, 0x00, 0x04, 0x00, 0x00});  // lea rsi, [rsp+1024]
    out.call(runtime.big_compare);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_negative(bit_done);
    out.bytes({0x48, 0x8D, 0x3C, 0x24});  // lea rdi, [rsp]
    out.bytes({0x48, 0x8D, 0xB4, 0x24, 0x00, 0x04, 0x00, 0x00});  // lea rsi, [rsp+1024]
    out.bytes({0xF8});  // clc
    out.call(runtime.big_subtract_clear);
    out.bytes({0x49, 0x83, 0xCA, 0x01});  // or r10, 1
    out.place(bit_done);
    out.bytes({0x48, 0x8D, 0x3C, 0x24});  // lea rdi, [rsp]
    out.call(runtime.big_double);
    out.bytes({0x49, 0xFF, 0xCC});  // dec r12
    out.jump_if_not_equal(next_bit);
    out.bytes({0xB9, 0x7F, 0x00, 0x00, 0x00});  // mov ecx, 127
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.place(check_rest);
    out.bytes({0x48, 0x0B, 0x04, 0xCC});  // or rax, qword ptr [rsp+rcx*8]
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.jump_if_not_negative(check_rest);
    out.bytes({0x49, 0x09, 0xC6});  // or r14, rax
    out.bytes({0x4D, 0x89, 0xD3});  // mov r11, r10
    out.bytes({0x49, 0x83, 0xE3, 0x01});  // and r11, 1
    out.bytes({0x49, 0xD1, 0xEA});  // shr r10, 1
    out.bytes({0x4D, 0x85, 0xDB});  // test r11, r11
    out.jump_if_equal(rounded);
    out.bytes({0x4D, 0x85, 0xF6});  // test r14, r14
    out.jump_if_not_equal(round_up);
    out.bytes({0x49, 0xF7, 0xC2, 0x01, 0x00, 0x00, 0x00});  // test r10, 1
    out.jump_if_equal(rounded);
    out.place(round_up);
    out.bytes({0x49, 0xFF, 0xC2});  // inc r10
    out.place(rounded);
    out.bytes({0x4C, 0x89, 0xD0});  // mov rax, r10
    out.bytes({0x49, 0x81, 0xFD, 0x02, 0xFC, 0xFF, 0xFF});  // cmp r13, -1022
    out.jump_if_less(has_answer);
    out.bytes({0x49, 0x8D, 0x8D, 0xFE, 0x03, 0x00, 0x00});  // lea rcx, [r13+1022]
    out.bytes({0x48, 0xC1, 0xE1, 0x34});  // shl rcx, 52
    out.bytes({0x48, 0x01, 0xC8});  // add rax, rcx
    out.bytes({0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x7F});  // movabs rcx, 0x7FF0000000000000
    out.bytes({0x48, 0x39, 0xC8});  // cmp rax, rcx
    out.jump_if_at_least(bad_number);
    out.place(has_answer);
    out.bytes({0x49, 0xC1, 0xE7, 0x3F});  // shl r15, 63
    out.bytes({0x4C, 0x09, 0xF8});  // or rax, r15
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
    out.place(bad_number);
    out.bytes({0x48, 0x89, 0xD8});  // mov rax, rbx
    out.lea_string(RDX, text.cant_turn);
    out.lea_string(R8, text.into_float);
    out.jump(runtime.text_error);
}

// The same as big_subtract, starting with nothing borrowed
void write_big_subtract_clear(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label next_word = out.new_label();
    out.place(runtime.big_subtract_clear);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0xB9, 0x80, 0x00, 0x00, 0x00});  // mov ecx, 128
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.place(next_word);
    out.bytes({0x48, 0x8B, 0x04, 0xD6});  // mov rax, qword ptr [rsi+rdx*8]
    out.bytes({0x48, 0x19, 0x04, 0xD7});  // sbb qword ptr [rdi+rdx*8], rax
    out.bytes({0x48, 0xFF, 0xC2});  // inc rdx
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.jump_if_not_equal(next_word);
    out.bytes({0xC3});  // ret
}

// Keeps r12 to r15, where Jamo code may hold variables, around to_wide_body
void write_to_wide(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.to_wide);
    out.bytes({0x41, 0x54});  // push r12
    out.bytes({0x41, 0x55});  // push r13
    out.bytes({0x41, 0x56});  // push r14
    out.bytes({0x41, 0x57});  // push r15
    out.call(runtime.to_wide_body);
    out.bytes({0x41, 0x5F});  // pop r15
    out.bytes({0x41, 0x5E});  // pop r14
    out.bytes({0x41, 0x5D});  // pop r13
    out.bytes({0x41, 0x5C});  // pop r12
    out.bytes({0xC3});  // ret
}

// rax = a string. Answers with its text the way Windows' file functions want it. Keeps rbx and rsi
void write_to_wide_body(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.to_wide_body);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x53});  // push rbx
    out.bytes({0x56});  // push rsi
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x30});  // sub rsp, 48
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.bytes({0xB9, 0xE9, 0xFD, 0x00, 0x00});  // mov ecx, 65001
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x4C, 0x8D, 0x43, 0x08});  // lea r8, [rbx+8]
    out.bytes({0x4C, 0x8B, 0x0B});  // mov r9, qword ptr [rbx]
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+32], 0
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x28, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+40], 0
    out.call_windows(DataRef::MultiByteToWideChar);
    out.bytes({0x89, 0xC6});  // mov esi, eax
    out.bytes({0x48, 0x8D, 0x4C, 0x00, 0x02});  // lea rcx, [rax+rax+2]
    out.call(runtime.alloc_text);
    out.bytes({0x49, 0x89, 0xC4});  // mov r12, rax
    out.bytes({0xB9, 0xE9, 0xFD, 0x00, 0x00});  // mov ecx, 65001
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x4C, 0x8D, 0x43, 0x08});  // lea r8, [rbx+8]
    out.bytes({0x4C, 0x8B, 0x0B});  // mov r9, qword ptr [rbx]
    out.bytes({0x4C, 0x89, 0x64, 0x24, 0x20});  // mov qword ptr [rsp+32], r12
    out.bytes({0x48, 0x89, 0x74, 0x24, 0x28});  // mov qword ptr [rsp+40], rsi
    out.call_windows(DataRef::MultiByteToWideChar);
    out.bytes({0x66, 0x41, 0xC7, 0x04, 0x74, 0x00, 0x00});  // mov word ptr [r12+rsi*2], 0
    out.bytes({0x4C, 0x89, 0xE0});  // mov rax, r12
    out.bytes({0x48, 0x8D, 0x65, 0xF0});  // lea rsp, [rbp-16]
    out.bytes({0x5E});  // pop rsi
    out.bytes({0x5B});  // pop rbx
    out.bytes({0x5D});  // pop rbp
    out.bytes({0xC3});  // ret
}

// Keeps r12 to r15, where Jamo code may hold variables, around read_whole_file_body
void write_read_whole_file(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.read_whole_file);
    out.bytes({0x41, 0x54});  // push r12
    out.bytes({0x41, 0x55});  // push r13
    out.bytes({0x41, 0x56});  // push r14
    out.bytes({0x41, 0x57});  // push r15
    out.call(runtime.read_whole_file_body);
    out.bytes({0x41, 0x5F});  // pop r15
    out.bytes({0x41, 0x5E});  // pop r14
    out.bytes({0x41, 0x5D});  // pop r13
    out.bytes({0x41, 0x5C});  // pop r12
    out.bytes({0xC3});  // ret
}

// rax = the path
void write_read_whole_file_body(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label next_chunk = out.new_label();
    const Label finished = out.new_label();
    const Label failed = out.new_label();
    out.place(runtime.read_whole_file_body);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x81, 0xEC, 0xE0, 0x0F, 0x00, 0x00});  // sub rsp, 4064
    out.bytes({0x48, 0x83, 0x0C, 0x24, 0x00});  // or qword ptr [rsp], 0
    out.bytes({0x49, 0x89, 0xC5});  // mov r13, rax
    out.call(runtime.to_wide);
    out.bytes({0x48, 0x89, 0xC1});  // mov rcx, rax
    out.bytes({0xBA, 0x00, 0x00, 0x00, 0x80});  // mov edx, 0x80000000
    out.bytes({0x41, 0xB8, 0x03, 0x00, 0x00, 0x00});  // mov r8d, 3
    out.bytes({0x45, 0x31, 0xC9});  // xor r9d, r9d
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x20, 0x03, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+32], 3
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x28, 0x80, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+40], 0x80
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+48], 0
    out.call_windows(DataRef::CreateFileW);
    out.bytes({0x48, 0x83, 0xF8, 0xFF});  // cmp rax, -1
    out.jump_if_equal(failed);
    out.bytes({0x49, 0x89, 0xC4});  // mov r12, rax
    out.call(runtime.builder_start);
    out.place(next_chunk);
    out.bytes({0x4C, 0x89, 0xE1});  // mov rcx, r12
    out.bytes({0x48, 0x8D, 0x54, 0x24, 0x40});  // lea rdx, [rsp+64]
    out.bytes({0x41, 0xB8, 0xA0, 0x0F, 0x00, 0x00});  // mov r8d, 4000
    out.bytes({0x4C, 0x8D, 0x4C, 0x24, 0x38});  // lea r9, [rsp+56]
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+32], 0
    out.call_windows(DataRef::ReadFile);
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_equal(failed);
    out.bytes({0x8B, 0x44, 0x24, 0x38});  // mov eax, dword ptr [rsp+56]
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(finished);
    out.bytes({0x48, 0x8D, 0x54, 0x24, 0x40});  // lea rdx, [rsp+64]
    out.bytes({0x49, 0x89, 0xC0});  // mov r8, rax
    out.call(runtime.builder_add);
    out.jump(next_chunk);
    out.place(finished);
    out.bytes({0x4C, 0x89, 0xE1});  // mov rcx, r12
    out.call_windows(DataRef::CloseHandle);
    out.call(runtime.builder_to_string);
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
    out.place(failed);
    out.bytes({0x4C, 0x89, 0xE8});  // mov rax, r13
    out.lea_string(RDX, text.cant_read);
    out.lea_string(R8, text.period);
    out.jump(runtime.text_error);
}

// Keeps r12 to r15, where Jamo code may hold variables, around write_whole_file_body
void write_write_whole_file(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.write_whole_file);
    out.bytes({0x41, 0x54});  // push r12
    out.bytes({0x41, 0x55});  // push r13
    out.bytes({0x41, 0x56});  // push r14
    out.bytes({0x41, 0x57});  // push r15
    out.call(runtime.write_whole_file_body);
    out.bytes({0x41, 0x5F});  // pop r15
    out.bytes({0x41, 0x5E});  // pop r14
    out.bytes({0x41, 0x5D});  // pop r13
    out.bytes({0x41, 0x5C});  // pop r12
    out.bytes({0xC3});  // ret
}

// rax = the path, rcx = the text. Replaces the file
void write_write_whole_file_body(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label next_part = out.new_label();
    const Label finished = out.new_label();
    const Label failed = out.new_label();
    out.place(runtime.write_whole_file_body);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x40});  // sub rsp, 64
    out.bytes({0x49, 0x89, 0xC5});  // mov r13, rax
    out.bytes({0x49, 0x89, 0xCE});  // mov r14, rcx
    out.call(runtime.to_wide);
    out.bytes({0x48, 0x89, 0xC1});  // mov rcx, rax
    out.bytes({0xBA, 0x00, 0x00, 0x00, 0x40});  // mov edx, 0x40000000
    out.bytes({0x41, 0xB8, 0x01, 0x00, 0x00, 0x00});  // mov r8d, 1
    out.bytes({0x45, 0x31, 0xC9});  // xor r9d, r9d
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x20, 0x02, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+32], 2
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x28, 0x80, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+40], 0x80
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+48], 0
    out.call_windows(DataRef::CreateFileW);
    out.bytes({0x48, 0x83, 0xF8, 0xFF});  // cmp rax, -1
    out.jump_if_equal(failed);
    out.bytes({0x49, 0x89, 0xC4});  // mov r12, rax
    out.bytes({0x49, 0x8D, 0x5E, 0x08});  // lea rbx, [r14+8]
    out.bytes({0x49, 0x8B, 0x36});  // mov rsi, qword ptr [r14]
    out.place(next_part);
    out.bytes({0x48, 0x85, 0xF6});  // test rsi, rsi
    out.jump_if_equal(finished);
    out.bytes({0x49, 0x89, 0xF0});  // mov r8, rsi
    out.bytes({0xB8, 0x00, 0x00, 0x00, 0x40});  // mov eax, 0x40000000
    out.bytes({0x49, 0x39, 0xC0});  // cmp r8, rax
    out.bytes({0x4C, 0x0F, 0x47, 0xC0});  // cmova r8, rax
    out.bytes({0x4C, 0x89, 0xE1});  // mov rcx, r12
    out.bytes({0x48, 0x89, 0xDA});  // mov rdx, rbx
    out.bytes({0x4C, 0x8D, 0x4C, 0x24, 0x38});  // lea r9, [rsp+56]
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+32], 0
    out.call_windows(DataRef::WriteFile);
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_equal(failed);
    out.bytes({0x8B, 0x44, 0x24, 0x38});  // mov eax, dword ptr [rsp+56]
    out.bytes({0x48, 0x01, 0xC3});  // add rbx, rax
    out.bytes({0x48, 0x29, 0xC6});  // sub rsi, rax
    out.jump(next_part);
    out.place(finished);
    out.bytes({0x4C, 0x89, 0xE1});  // mov rcx, r12
    out.call_windows(DataRef::CloseHandle);
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
    out.place(failed);
    out.bytes({0x4C, 0x89, 0xE8});  // mov rax, r13
    out.lea_string(RDX, text.cant_write);
    out.lea_string(R8, text.period);
    out.jump(runtime.text_error);
}

// rax = the path. Answers with a bool
void write_file_exists(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.file_exists);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x20});  // sub rsp, 32
    out.call(runtime.to_wide);
    out.bytes({0x48, 0x89, 0xC1});  // mov rcx, rax
    out.call_windows(DataRef::GetFileAttributesW);
    out.bytes({0x83, 0xF8, 0xFF});  // cmp eax, -1
    out.bytes({0x0F, 0x95, 0xC0});  // setne al
    out.bytes({0x0F, 0xB6, 0xC0});  // movzx eax, al
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
}

// Reads more typed or piped input. Answers with how much came. Keeps rbx
void write_stdin_refill(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label has_buffer = out.new_label();
    const Label no_input = out.new_label();
    const Label finished = out.new_label();
    const Label console_input = out.new_label();
    out.place(runtime.stdin_refill);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x53});  // push rbx
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x40});  // sub rsp, 64
    out.lea_data(RBX, DataRef::Stdin);
    out.bytes({0x48, 0x83, 0x3B, 0x00});  // cmp qword ptr [rbx], 0
    out.jump_if_not_equal(has_buffer);
    out.bytes({0xB9, 0x00, 0x10, 0x00, 0x00});  // mov ecx, 4096
    out.call(runtime.raw_alloc);
    out.bytes({0x48, 0x89, 0x03});  // mov qword ptr [rbx], rax
    out.place(has_buffer);
    out.bytes({0x48, 0xC7, 0xC1, 0xF6, 0xFF, 0xFF, 0xFF});  // mov rcx, -10
    out.call_windows(DataRef::GetStdHandle);
    out.bytes({0x48, 0x89, 0x44, 0x24, 0x30});  // mov qword ptr [rsp+48], rax
    out.bytes({0x48, 0x89, 0xC1});  // mov rcx, rax
    out.bytes({0x48, 0x8D, 0x54, 0x24, 0x28});  // lea rdx, [rsp+40]
    out.call_windows(DataRef::GetConsoleMode);
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_not_equal(console_input);
    out.bytes({0x48, 0x8B, 0x4C, 0x24, 0x30});  // mov rcx, qword ptr [rsp+48]
    out.bytes({0x48, 0x8B, 0x13});  // mov rdx, qword ptr [rbx]
    out.bytes({0x41, 0xB8, 0x00, 0x10, 0x00, 0x00});  // mov r8d, 4096
    out.bytes({0x4C, 0x8D, 0x4C, 0x24, 0x38});  // lea r9, [rsp+56]
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x38, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+56], 0
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+32], 0
    out.call_windows(DataRef::ReadFile);
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_equal(no_input);
    out.bytes({0x8B, 0x44, 0x24, 0x38});  // mov eax, dword ptr [rsp+56]
    out.bytes({0x48, 0xC7, 0x43, 0x08, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+8], 0
    out.bytes({0x48, 0x89, 0x43, 0x10});  // mov qword ptr [rbx+16], rax
    out.jump(finished);
    out.place(no_input);
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.place(finished);
    out.bytes({0x48, 0x8D, 0x65, 0xF8});  // lea rsp, [rbp-8]
    out.bytes({0x5B});  // pop rbx
    out.bytes({0x5D});  // pop rbp
    out.bytes({0xC3});  // ret
    out.place(console_input);
    out.bytes({0x48, 0x81, 0xEC, 0x40, 0x0A, 0x00, 0x00});  // sub rsp, 2624
    out.bytes({0x48, 0x8B, 0x8C, 0x24, 0x70, 0x0A, 0x00, 0x00});  // mov rcx, qword ptr [rsp+2672]
    out.bytes({0x48, 0x8D, 0x54, 0x24, 0x40});  // lea rdx, [rsp+64]
    out.bytes({0x41, 0xB8, 0x14, 0x05, 0x00, 0x00});  // mov r8d, 1300
    out.bytes({0x4C, 0x8D, 0x4C, 0x24, 0x38});  // lea r9, [rsp+56]
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x38, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+56], 0
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+32], 0
    out.call_windows(DataRef::ReadConsoleW);
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_equal(no_input);
    out.bytes({0xB9, 0xE9, 0xFD, 0x00, 0x00});  // mov ecx, 65001
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x4C, 0x8D, 0x44, 0x24, 0x40});  // lea r8, [rsp+64]
    out.bytes({0x44, 0x8B, 0x4C, 0x24, 0x38});  // mov r9d, dword ptr [rsp+56]
    out.bytes({0x48, 0x8B, 0x03});  // mov rax, qword ptr [rbx]
    out.bytes({0x48, 0x89, 0x44, 0x24, 0x20});  // mov qword ptr [rsp+32], rax
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x28, 0x00, 0x10, 0x00, 0x00});  // mov qword ptr [rsp+40], 4096
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+48], 0
    out.bytes({0x48, 0xC7, 0x44, 0x24, 0x38, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rsp+56], 0
    out.call_windows(DataRef::WideCharToMultiByte);
    out.bytes({0x89, 0xC0});  // mov eax, eax
    out.bytes({0x48, 0xC7, 0x43, 0x08, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+8], 0
    out.bytes({0x48, 0x89, 0x43, 0x10});  // mov qword ptr [rbx+16], rax
    out.jump(finished);
}

// Answers with the next line of input, without its line ending, or "" when there's no more
void write_read_line(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label next_char = out.new_label();
    const Label has_char = out.new_label();
    const Label line_done = out.new_label();
    const Label no_return = out.new_label();
    out.place(runtime.read_line);
    out.lea_data(RBX, DataRef::Stdin);
    out.call(runtime.builder_start);
    out.place(next_char);
    out.bytes({0x48, 0x8B, 0x43, 0x08});  // mov rax, qword ptr [rbx+8]
    out.bytes({0x48, 0x3B, 0x43, 0x10});  // cmp rax, qword ptr [rbx+16]
    out.jump_if_below(has_char);
    out.call(runtime.stdin_refill);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(line_done);
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.place(has_char);
    out.bytes({0x48, 0x8B, 0x13});  // mov rdx, qword ptr [rbx]
    out.bytes({0x48, 0x01, 0xC2});  // add rdx, rax
    out.bytes({0x48, 0xFF, 0x43, 0x08});  // inc qword ptr [rbx+8]
    out.bytes({0x80, 0x3A, 0x0A});  // cmp byte ptr [rdx], 10
    out.jump_if_equal(line_done);
    out.bytes({0x41, 0xB8, 0x01, 0x00, 0x00, 0x00});  // mov r8d, 1
    out.call(runtime.builder_add);
    out.jump(next_char);
    out.place(line_done);
    out.lea_data(RAX, DataRef::Builder);
    out.bytes({0x48, 0x8B, 0x48, 0x08});  // mov rcx, qword ptr [rax+8]
    out.bytes({0x48, 0x85, 0xC9});  // test rcx, rcx
    out.jump_if_equal(no_return);
    out.bytes({0x48, 0x8B, 0x10});  // mov rdx, qword ptr [rax]
    out.bytes({0x80, 0x7C, 0x0A, 0xFF, 0x0D});  // cmp byte ptr [rdx+rcx-1], 13
    out.jump_if_not_equal(no_return);
    out.bytes({0x48, 0xFF, 0x48, 0x08});  // dec qword ptr [rax+8]
    out.place(no_return);
    out.jump(runtime.builder_to_string);
}

// rax = one byte. Keeps rbx
void write_builder_add_byte(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.builder_add_byte);
    out.bytes({0x50});  // push rax
    out.bytes({0x48, 0x89, 0xE2});  // mov rdx, rsp
    out.bytes({0x41, 0xB8, 0x01, 0x00, 0x00, 0x00});  // mov r8d, 1
    out.call(runtime.builder_add);
    out.bytes({0x58});  // pop rax
    out.bytes({0xC3});  // ret
}

// rax = a letter's number in Unicode, added as UTF-8. Keeps r12, r13, r14 and r15
void write_builder_add_letter(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label two_bytes = out.new_label();
    const Label three_bytes = out.new_label();
    const Label four_bytes = out.new_label();
    const Label middle_byte = out.new_label();
    const Label last_byte = out.new_label();
    out.place(runtime.builder_add_letter);
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.bytes({0x48, 0x81, 0xFB, 0x80, 0x00, 0x00, 0x00});  // cmp rbx, 0x80
    out.jump_if_at_least(two_bytes);
    out.jump(runtime.builder_add_byte);
    out.place(two_bytes);
    out.bytes({0x48, 0x81, 0xFB, 0x00, 0x08, 0x00, 0x00});  // cmp rbx, 0x800
    out.jump_if_at_least(three_bytes);
    out.bytes({0x48, 0xC1, 0xE8, 0x06});  // shr rax, 6
    out.bytes({0x48, 0x0D, 0xC0, 0x00, 0x00, 0x00});  // or rax, 0xC0
    out.call(runtime.builder_add_byte);
    out.jump(last_byte);
    out.place(three_bytes);
    out.bytes({0x48, 0x81, 0xFB, 0x00, 0x00, 0x01, 0x00});  // cmp rbx, 0x10000
    out.jump_if_at_least(four_bytes);
    out.bytes({0x48, 0xC1, 0xE8, 0x0C});  // shr rax, 12
    out.bytes({0x48, 0x0D, 0xE0, 0x00, 0x00, 0x00});  // or rax, 0xE0
    out.call(runtime.builder_add_byte);
    out.jump(middle_byte);
    out.place(four_bytes);
    out.bytes({0x48, 0xC1, 0xE8, 0x12});  // shr rax, 18
    out.bytes({0x48, 0x0D, 0xF0, 0x00, 0x00, 0x00});  // or rax, 0xF0
    out.call(runtime.builder_add_byte);
    out.bytes({0x48, 0x89, 0xD8});  // mov rax, rbx
    out.bytes({0x48, 0xC1, 0xE8, 0x0C});  // shr rax, 12
    out.bytes({0x48, 0x83, 0xE0, 0x3F});  // and rax, 0x3F
    out.bytes({0x48, 0x0D, 0x80, 0x00, 0x00, 0x00});  // or rax, 0x80
    out.call(runtime.builder_add_byte);
    out.place(middle_byte);
    out.bytes({0x48, 0x89, 0xD8});  // mov rax, rbx
    out.bytes({0x48, 0xC1, 0xE8, 0x06});  // shr rax, 6
    out.bytes({0x48, 0x83, 0xE0, 0x3F});  // and rax, 0x3F
    out.bytes({0x48, 0x0D, 0x80, 0x00, 0x00, 0x00});  // or rax, 0x80
    out.call(runtime.builder_add_byte);
    out.place(last_byte);
    out.bytes({0x48, 0x89, 0xD8});  // mov rax, rbx
    out.bytes({0x48, 0x83, 0xE0, 0x3F});  // and rax, 0x3F
    out.bytes({0x48, 0x0D, 0x80, 0x00, 0x00, 0x00});  // or rax, 0x80
    out.jump(runtime.builder_add_byte);
}

// Keeps r12 to r15, where Jamo code may hold variables, around get_args_body
void write_get_args(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.get_args);
    out.bytes({0x41, 0x54});  // push r12
    out.bytes({0x41, 0x55});  // push r13
    out.bytes({0x41, 0x56});  // push r14
    out.bytes({0x41, 0x57});  // push r15
    out.call(runtime.get_args_body);
    out.bytes({0x41, 0x5F});  // pop r15
    out.bytes({0x41, 0x5E});  // pop r14
    out.bytes({0x41, 0x5D});  // pop r13
    out.bytes({0x41, 0x5C});  // pop r12
    out.bytes({0xC3});  // ret
}

// Answers with the command line's inputs as an array of strings, read by Windows' usual rules
void write_get_args_body(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label skip_quoted_name = out.new_label();
    const Label skip_plain_name = out.new_label();
    const Label next_input = out.new_label();
    const Label skip_space = out.new_label();
    const Label found_input = out.new_label();
    const Label next_letter = out.new_label();
    const Label plain_letter = out.new_label();
    const Label whole_letter = out.new_label();
    const Label quote_mark = out.new_label();
    const Label flip_quotes = out.new_label();
    const Label backslashes = out.new_label();
    const Label count_backslash = out.new_label();
    const Label counted = out.new_label();
    const Label add_backslashes = out.new_label();
    const Label input_done = out.new_label();
    const Label all_done = out.new_label();
    out.place(runtime.get_args_body);
    out.bytes({0xB9, 0x00, 0x00, 0x00, 0x00});  // mov ecx, 0
    out.call(runtime.new_array);
    out.bytes({0x49, 0x89, 0xC4});  // mov r12, rax
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x20});  // sub rsp, 32
    out.call_windows(DataRef::GetCommandLineW);
    out.bytes({0x49, 0x89, 0xC5});  // mov r13, rax
    out.bytes({0x66, 0x41, 0x83, 0x7D, 0x00, 0x22});  // cmp word ptr [r13], 34
    out.jump_if_not_equal(skip_plain_name);
    out.place(skip_quoted_name);
    out.bytes({0x49, 0x83, 0xC5, 0x02});  // add r13, 2
    out.bytes({0x41, 0x0F, 0xB7, 0x45, 0x00});  // movzx eax, word ptr [r13]
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_equal(all_done);
    out.bytes({0x83, 0xF8, 0x22});  // cmp eax, 34
    out.jump_if_not_equal(skip_quoted_name);
    out.bytes({0x49, 0x83, 0xC5, 0x02});  // add r13, 2
    out.jump(next_input);
    out.place(skip_plain_name);
    out.bytes({0x41, 0x0F, 0xB7, 0x45, 0x00});  // movzx eax, word ptr [r13]
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_equal(all_done);
    out.bytes({0x83, 0xF8, 0x20});  // cmp eax, 32
    out.jump_if_equal(next_input);
    out.bytes({0x83, 0xF8, 0x09});  // cmp eax, 9
    out.jump_if_equal(next_input);
    out.bytes({0x49, 0x83, 0xC5, 0x02});  // add r13, 2
    out.jump(skip_plain_name);
    out.place(next_input);
    out.bytes({0x41, 0x0F, 0xB7, 0x45, 0x00});  // movzx eax, word ptr [r13]
    out.bytes({0x83, 0xF8, 0x20});  // cmp eax, 32
    out.jump_if_equal(skip_space);
    out.bytes({0x83, 0xF8, 0x09});  // cmp eax, 9
    out.jump_if_not_equal(found_input);
    out.place(skip_space);
    out.bytes({0x49, 0x83, 0xC5, 0x02});  // add r13, 2
    out.jump(next_input);
    out.place(found_input);
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_equal(all_done);
    out.call(runtime.builder_start);
    out.bytes({0x45, 0x31, 0xF6});  // xor r14d, r14d
    out.place(next_letter);
    out.bytes({0x41, 0x0F, 0xB7, 0x45, 0x00});  // movzx eax, word ptr [r13]
    out.bytes({0x85, 0xC0});  // test eax, eax
    out.jump_if_equal(input_done);
    out.bytes({0x83, 0xF8, 0x5C});  // cmp eax, 92
    out.jump_if_equal(backslashes);
    out.bytes({0x83, 0xF8, 0x22});  // cmp eax, 34
    out.jump_if_equal(quote_mark);
    out.bytes({0x4D, 0x85, 0xF6});  // test r14, r14
    out.jump_if_not_equal(plain_letter);
    out.bytes({0x83, 0xF8, 0x20});  // cmp eax, 32
    out.jump_if_equal(input_done);
    out.bytes({0x83, 0xF8, 0x09});  // cmp eax, 9
    out.jump_if_equal(input_done);
    out.place(plain_letter);
    out.bytes({0x49, 0x83, 0xC5, 0x02});  // add r13, 2
    out.bytes({0x89, 0xC1});  // mov ecx, eax
    out.bytes({0x81, 0xE1, 0x00, 0xFC, 0x00, 0x00});  // and ecx, 0xFC00
    out.bytes({0x81, 0xF9, 0x00, 0xD8, 0x00, 0x00});  // cmp ecx, 0xD800
    out.jump_if_not_equal(whole_letter);
    out.bytes({0x41, 0x0F, 0xB7, 0x4D, 0x00});  // movzx ecx, word ptr [r13]
    out.bytes({0x89, 0xCA});  // mov edx, ecx
    out.bytes({0x81, 0xE2, 0x00, 0xFC, 0x00, 0x00});  // and edx, 0xFC00
    out.bytes({0x81, 0xFA, 0x00, 0xDC, 0x00, 0x00});  // cmp edx, 0xDC00
    out.jump_if_not_equal(whole_letter);
    out.bytes({0x49, 0x83, 0xC5, 0x02});  // add r13, 2
    out.bytes({0x2D, 0x00, 0xD8, 0x00, 0x00});  // sub eax, 0xD800
    out.bytes({0xC1, 0xE0, 0x0A});  // shl eax, 10
    out.bytes({0x81, 0xE9, 0x00, 0xDC, 0x00, 0x00});  // sub ecx, 0xDC00
    out.bytes({0x01, 0xC8});  // add eax, ecx
    out.bytes({0x05, 0x00, 0x00, 0x01, 0x00});  // add eax, 0x10000
    out.place(whole_letter);
    out.call(runtime.builder_add_letter);
    out.jump(next_letter);
    out.place(quote_mark);
    out.bytes({0x49, 0x83, 0xC5, 0x02});  // add r13, 2
    out.bytes({0x4D, 0x85, 0xF6});  // test r14, r14
    out.jump_if_equal(flip_quotes);
    out.bytes({0x66, 0x41, 0x83, 0x7D, 0x00, 0x22});  // cmp word ptr [r13], 34
    out.jump_if_not_equal(flip_quotes);
    out.bytes({0x49, 0x83, 0xC5, 0x02});  // add r13, 2
    out.bytes({0xB8, 0x22, 0x00, 0x00, 0x00});  // mov eax, 34
    out.call(runtime.builder_add_byte);
    out.jump(next_letter);
    out.place(flip_quotes);
    out.bytes({0x49, 0x83, 0xF6, 0x01});  // xor r14, 1
    out.jump(next_letter);
    out.place(backslashes);
    out.bytes({0x45, 0x31, 0xFF});  // xor r15d, r15d
    out.place(count_backslash);
    out.bytes({0x66, 0x41, 0x83, 0x7D, 0x00, 0x5C});  // cmp word ptr [r13], 92
    out.jump_if_not_equal(counted);
    out.bytes({0x49, 0xFF, 0xC7});  // inc r15
    out.bytes({0x49, 0x83, 0xC5, 0x02});  // add r13, 2
    out.jump(count_backslash);
    out.place(counted);
    out.bytes({0x66, 0x41, 0x83, 0x7D, 0x00, 0x22});  // cmp word ptr [r13], 34
    out.jump_if_not_equal(add_backslashes);
    out.bytes({0x4C, 0x89, 0xF8});  // mov rax, r15
    out.bytes({0x48, 0x83, 0xE0, 0x01});  // and rax, 1
    out.bytes({0x49, 0xD1, 0xEF});  // shr r15, 1
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(add_backslashes);
    out.bytes({0x49, 0x83, 0xC5, 0x02});  // add r13, 2
    out.bytes({0x4C, 0x89, 0xFB});  // mov rbx, r15
    out.call(runtime.add_backslash_run);
    out.bytes({0xB8, 0x22, 0x00, 0x00, 0x00});  // mov eax, 34
    out.call(runtime.builder_add_byte);
    out.jump(next_letter);
    out.place(add_backslashes);
    out.bytes({0x4C, 0x89, 0xFB});  // mov rbx, r15
    out.call(runtime.add_backslash_run);
    out.jump(next_letter);
    out.place(input_done);
    out.call(runtime.builder_to_string);
    out.bytes({0x48, 0x89, 0xC1});  // mov rcx, rax
    out.bytes({0x4C, 0x89, 0xE0});  // mov rax, r12
    out.call(runtime.array_add);
    out.jump(next_input);
    out.place(all_done);
    out.bytes({0x4C, 0x89, 0xE0});  // mov rax, r12
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
}

// rbx = how many backslashes to add
void write_add_backslash_run(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label none_left = out.new_label();
    out.place(runtime.add_backslash_run);
    out.bytes({0x48, 0x85, 0xDB});  // test rbx, rbx
    out.jump_if_equal(none_left);
    out.bytes({0xB8, 0x5C, 0x00, 0x00, 0x00});  // mov eax, 92
    out.call(runtime.builder_add_byte);
    out.bytes({0x48, 0xFF, 0xCB});  // dec rbx
    out.jump(runtime.add_backslash_run);
    out.place(none_left);
    out.bytes({0xC3});  // ret
}

// rcx = what raw_alloc gave, or 0. Keeps rbx, rsi and rdi
void write_raw_free(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label nothing = out.new_label();
    out.place(runtime.raw_free);
    out.bytes({0x48, 0x85, 0xC9});  // test rcx, rcx
    out.jump_if_equal(nothing);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x20});  // sub rsp, 32
    out.bytes({0x49, 0x89, 0xC8});  // mov r8, rcx
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.load_rcx_data(DataRef::HeapHandle);
    out.call_windows(DataRef::HeapFree);
    out.bytes({0xC9});  // leave
    out.place(nothing);
    out.bytes({0xC3});  // ret
}

// rcx = bytes wanted, for a block that can hold references. Answers in rax with the memory zeroed, and keeps every other register
void write_alloc(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.alloc);
    out.bytes({0x41, 0xBB, 0x01, 0x00, 0x00, 0x00});  // mov r11d, 1
    out.jump(runtime.alloc_block);
}

// rcx = bytes wanted, for a block that holds no references, like a string
void write_alloc_text(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.alloc_text);
    out.bytes({0x45, 0x31, 0xDB});  // xor r11d, r11d
    out.jump(runtime.alloc_block);
}

// Each block starts with a 32-byte header: the next block, its size, its type at +16, its mark at +17, where it sits in its chunk at +20 or 0 if it has no chunk, and a check value. Blocks of up to 512 bytes come from chunks, and this quick path saves few registers because it can't clean or call Windows
void write_alloc_block(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label slow = out.new_label();
    out.place(runtime.alloc_block);
    out.bytes({0x50});  // push rax
    out.bytes({0x51});  // push rcx
    out.bytes({0x52});  // push rdx
    out.bytes({0x53});  // push rbx
    out.bytes({0x57});  // push rdi
    out.lea_data(RBX, DataRef::Cleaner);
    out.bytes({0x48, 0x8D, 0x51, 0xFF});  // lea rdx, [rcx-1]
    out.bytes({0x48, 0x81, 0xFA, 0x00, 0x02, 0x00, 0x00});  // cmp rdx, 512
    out.jump_if_at_least(slow);
    out.bytes({0x48, 0x83, 0xE2, 0xF0});  // and rdx, -16
    out.bytes({0x48, 0x8D, 0x42, 0x30});  // lea rax, [rdx+48]
    out.bytes({0x48, 0x03, 0x43, 0x10});  // add rax, qword ptr [rbx+16]
    out.bytes({0x48, 0x3B, 0x43, 0x18});  // cmp rax, qword ptr [rbx+24]
    out.jump_if_at_least(slow);
    out.bytes({0xF6, 0x43, 0x20, 0x01});  // test byte ptr [rbx+32], 1
    out.jump_if_not_equal(slow);
    out.jump(runtime.alloc_take);
    out.place(slow);
    out.bytes({0x5F});  // pop rdi
    out.bytes({0x5B});  // pop rbx
    out.bytes({0x5A});  // pop rdx
    out.bytes({0x59});  // pop rcx
    out.bytes({0x58});  // pop rax
    out.jump(runtime.alloc_slow);
}

// Comes back from alloc_slow, which has made sure a small block can be had
void write_alloc_again(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    out.place(runtime.alloc_again);
    out.bytes({0x50});  // push rax
    out.bytes({0x51});  // push rcx
    out.bytes({0x52});  // push rdx
    out.bytes({0x53});  // push rbx
    out.bytes({0x57});  // push rdi
    out.lea_data(RBX, DataRef::Cleaner);
    out.bytes({0x48, 0x8D, 0x51, 0xFF});  // lea rdx, [rcx-1]
    out.bytes({0x48, 0x83, 0xE2, 0xF0});  // and rdx, -16
    out.jump(runtime.alloc_take);
}

// rdx = 16 x the size class. Takes a block from its free list, or else from the current chunk, then sets it up and zeroes it. Five registers are on the stack
void write_alloc_take(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label set_up = out.new_label();
    const Label next_zero = out.new_label();
    const Label from_chunk = out.new_label();
    const Label slow = out.new_label();
    const Label not_free = out.new_label();
    out.place(runtime.alloc_take);
    out.bytes({0x48, 0x89, 0xD1});  // mov rcx, rdx
    out.bytes({0x48, 0xD1, 0xE9});  // shr rcx, 1
    out.bytes({0x48, 0x8B, 0x84, 0x0B, 0x90, 0x00, 0x00, 0x00});  // mov rax, qword ptr [rbx+rcx+144]
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(from_chunk);
    out.bytes({0x80, 0x78, 0x10, 0x03});  // cmp byte ptr [rax+16], 3
    out.jump_if_not_equal(not_free);
    out.bytes({0x48, 0x8B, 0x38});  // mov rdi, qword ptr [rax]
    out.bytes({0x48, 0x89, 0xBC, 0x0B, 0x90, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+rcx+144], rdi
    out.place(set_up);
    out.bytes({0x48, 0x8D, 0x7A, 0x30});  // lea rdi, [rdx+48]
    out.bytes({0x48, 0x01, 0x7B, 0x10});  // add qword ptr [rbx+16], rdi
    out.bytes({0x48, 0x8B, 0x3B});  // mov rdi, qword ptr [rbx]
    out.bytes({0x48, 0x89, 0x38});  // mov qword ptr [rax], rdi
    out.bytes({0x48, 0x89, 0x03});  // mov qword ptr [rbx], rax
    out.bytes({0x48, 0xFF, 0x43, 0x08});  // inc qword ptr [rbx+8]
    out.bytes({0x48, 0x8B, 0x4C, 0x24, 0x18});  // mov rcx, qword ptr [rsp+24]
    out.bytes({0x48, 0x89, 0x48, 0x08});  // mov qword ptr [rax+8], rcx
    out.bytes({0x44, 0x88, 0x58, 0x10});  // mov byte ptr [rax+16], r11b
    out.bytes({0xC6, 0x40, 0x11, 0x00});  // mov byte ptr [rax+17], 0
    out.bytes({0x48, 0x31, 0xC1});  // xor rcx, rax
    out.bytes({0x48, 0xBF, 0x41, 0x45, 0x4C, 0x43, 0x4F, 0x4D, 0x41, 0x4A});  // movabs rdi, 0x4A414D4F434C4541
    out.bytes({0x48, 0x31, 0xF9});  // xor rcx, rdi
    out.bytes({0x48, 0x89, 0x48, 0x18});  // mov qword ptr [rax+24], rcx
    out.bytes({0x48, 0x83, 0xC0, 0x20});  // add rax, 32
    out.place(next_zero);
    out.bytes({0x48, 0xC7, 0x04, 0x10, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rax+rdx], 0
    out.bytes({0x48, 0xC7, 0x44, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rax+rdx+8], 0
    out.bytes({0x48, 0x83, 0xEA, 0x10});  // sub rdx, 16
    out.jump_if_not_negative(next_zero);
    out.bytes({0x5F});  // pop rdi
    out.bytes({0x5B});  // pop rbx
    out.bytes({0x5A});  // pop rdx
    out.bytes({0x59});  // pop rcx
    out.bytes({0x48, 0x83, 0xC4, 0x08});  // add rsp, 8
    out.bytes({0xC3});  // ret
    out.place(from_chunk);
    out.bytes({0x48, 0x8B, 0x43, 0x68});  // mov rax, qword ptr [rbx+104]
    out.bytes({0x48, 0x8D, 0x7C, 0x10, 0x30});  // lea rdi, [rax+rdx+48]
    out.bytes({0x48, 0x3B, 0x7B, 0x70});  // cmp rdi, qword ptr [rbx+112]
    out.jump_if_above(slow);
    out.bytes({0x48, 0x89, 0x7B, 0x68});  // mov qword ptr [rbx+104], rdi
    out.bytes({0x48, 0x89, 0xC7});  // mov rdi, rax
    out.bytes({0x48, 0x2B, 0x7B, 0x60});  // sub rdi, qword ptr [rbx+96]
    out.bytes({0x89, 0x78, 0x14});  // mov dword ptr [rax+20], edi
    out.jump(set_up);
    out.place(slow);
    out.bytes({0x5F});  // pop rdi
    out.bytes({0x5B});  // pop rbx
    out.bytes({0x5A});  // pop rdx
    out.bytes({0x59});  // pop rcx
    out.bytes({0x58});  // pop rax
    out.jump(runtime.alloc_slow);
    out.place(not_free);
    out.lea_string(RAX, text.cleaner_bad_header);
    out.jump(runtime.run_error);
}

// Everything the quick path can't do: cleaning, getting a chunk, and large blocks. Every register goes on the stack first, so cleaning can find what's in them
void write_alloc_slow(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label clean_first = out.new_label();
    const Label no_clean = out.new_label();
    const Label new_chunk = out.new_label();
    const Label get_chunk = out.new_label();
    const Label use_chunk = out.new_label();
    const Label small_ready = out.new_label();
    const Label large = out.new_label();
    const Label no_memory = out.new_label();
    const Label got_memory = out.new_label();
    out.place(runtime.alloc_slow);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x50});  // push rax
    out.bytes({0x51});  // push rcx
    out.bytes({0x52});  // push rdx
    out.bytes({0x53});  // push rbx
    out.bytes({0x56});  // push rsi
    out.bytes({0x57});  // push rdi
    out.bytes({0x41, 0x50});  // push r8
    out.bytes({0x41, 0x51});  // push r9
    out.bytes({0x41, 0x52});  // push r10
    out.bytes({0x41, 0x53});  // push r11
    out.bytes({0x41, 0x54});  // push r12
    out.bytes({0x41, 0x55});  // push r13
    out.bytes({0x41, 0x56});  // push r14
    out.bytes({0x41, 0x57});  // push r15
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x20});  // sub rsp, 32
    out.lea_data(RBX, DataRef::Cleaner);
    out.bytes({0x48, 0x8B, 0x45, 0xF0});  // mov rax, qword ptr [rbp-16]
    out.bytes({0x48, 0x83, 0xC0, 0x2F});  // add rax, 47
    out.bytes({0x48, 0x83, 0xE0, 0xF0});  // and rax, -16
    out.bytes({0x48, 0x01, 0x43, 0x10});  // add qword ptr [rbx+16], rax
    out.bytes({0x45, 0x31, 0xE4});  // xor r12d, r12d
    out.bytes({0xF6, 0x43, 0x20, 0x04});  // test byte ptr [rbx+32], 4
    out.jump_if_not_equal(no_clean);
    out.bytes({0xF6, 0x43, 0x20, 0x01});  // test byte ptr [rbx+32], 1
    out.jump_if_not_equal(clean_first);
    out.bytes({0x48, 0x8B, 0x43, 0x10});  // mov rax, qword ptr [rbx+16]
    out.bytes({0x48, 0x3B, 0x43, 0x18});  // cmp rax, qword ptr [rbx+24]
    out.jump_if_below(no_clean);
    out.place(clean_first);
    out.call(runtime.cleaner_clean);
    out.bytes({0x41, 0xBC, 0x01, 0x00, 0x00, 0x00});  // mov r12d, 1
    out.place(no_clean);
    out.bytes({0x48, 0x8B, 0x4D, 0xF0});  // mov rcx, qword ptr [rbp-16]
    out.bytes({0x48, 0xFF, 0xC9});  // dec rcx
    out.bytes({0x48, 0x81, 0xF9, 0x00, 0x02, 0x00, 0x00});  // cmp rcx, 512
    out.jump_if_at_least(large);
    out.bytes({0x48, 0x83, 0xE1, 0xF0});  // and rcx, -16
    out.bytes({0x48, 0x89, 0xC8});  // mov rax, rcx
    out.bytes({0x48, 0xD1, 0xE8});  // shr rax, 1
    out.bytes({0x48, 0x83, 0xBC, 0x03, 0x90, 0x00, 0x00, 0x00, 0x00});  // cmp qword ptr [rbx+rax+144], 0
    out.jump_if_not_equal(small_ready);
    out.bytes({0x48, 0x8B, 0x43, 0x68});  // mov rax, qword ptr [rbx+104]
    out.bytes({0x48, 0x8D, 0x44, 0x08, 0x30});  // lea rax, [rax+rcx+48]
    out.bytes({0x48, 0x3B, 0x43, 0x70});  // cmp rax, qword ptr [rbx+112]
    out.jump_if_at_most(small_ready);
    out.bytes({0x48, 0x8B, 0x83, 0x80, 0x00, 0x00, 0x00});  // mov rax, qword ptr [rbx+128]
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(new_chunk);
    out.bytes({0x48, 0x8B, 0x08});  // mov rcx, qword ptr [rax]
    out.bytes({0x48, 0x89, 0x8B, 0x80, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+128], rcx
    out.bytes({0x48, 0xFF, 0x8B, 0x88, 0x00, 0x00, 0x00});  // dec qword ptr [rbx+136]
    out.jump(use_chunk);
    out.place(new_chunk);
    out.bytes({0x4D, 0x85, 0xE4});  // test r12, r12
    out.jump_if_not_equal(get_chunk);
    out.bytes({0xF6, 0x43, 0x20, 0x04});  // test byte ptr [rbx+32], 4
    out.jump_if_not_equal(get_chunk);
    out.bytes({0x48, 0x8B, 0x43, 0x08});  // mov rax, qword ptr [rbx+8]
    out.bytes({0x48, 0x03, 0x43, 0x50});  // add rax, qword ptr [rbx+80]
    out.bytes({0x4C, 0x8D, 0x04, 0x80});  // lea r8, [rax+rax*4]
    out.bytes({0x49, 0xC1, 0xE0, 0x03});  // shl r8, 3
    out.bytes({0x49, 0x81, 0xC0, 0x00, 0x00, 0x04, 0x00});  // add r8, 0x40000
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.call_windows(DataRef::HeapAlloc);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(clean_first);
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x49, 0x89, 0xC0});  // mov r8, rax
    out.call_windows(DataRef::HeapFree);
    out.place(get_chunk);
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x41, 0xB8, 0x00, 0x00, 0x04, 0x00});  // mov r8d, 0x40000
    out.call_windows(DataRef::HeapAlloc);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(no_memory);
    out.place(use_chunk);
    out.bytes({0x48, 0x8B, 0x4B, 0x78});  // mov rcx, qword ptr [rbx+120]
    out.bytes({0x48, 0x89, 0x08});  // mov qword ptr [rax], rcx
    out.bytes({0x48, 0x89, 0x43, 0x78});  // mov qword ptr [rbx+120], rax
    out.bytes({0x48, 0xC7, 0x40, 0x08, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rax+8], 0
    out.bytes({0x48, 0x89, 0x43, 0x60});  // mov qword ptr [rbx+96], rax
    out.bytes({0x48, 0x8D, 0x48, 0x10});  // lea rcx, [rax+16]
    out.bytes({0x48, 0x89, 0x4B, 0x68});  // mov qword ptr [rbx+104], rcx
    out.bytes({0x48, 0x8D, 0x88, 0x00, 0x00, 0x04, 0x00});  // lea rcx, [rax+0x40000]
    out.bytes({0x48, 0x89, 0x4B, 0x70});  // mov qword ptr [rbx+112], rcx
    out.place(small_ready);
    out.bytes({0x48, 0x8D, 0x65, 0x90});  // lea rsp, [rbp-112]
    out.bytes({0x41, 0x5F});  // pop r15
    out.bytes({0x41, 0x5E});  // pop r14
    out.bytes({0x41, 0x5D});  // pop r13
    out.bytes({0x41, 0x5C});  // pop r12
    out.bytes({0x41, 0x5B});  // pop r11
    out.bytes({0x41, 0x5A});  // pop r10
    out.bytes({0x41, 0x59});  // pop r9
    out.bytes({0x41, 0x58});  // pop r8
    out.bytes({0x5F});  // pop rdi
    out.bytes({0x5E});  // pop rsi
    out.bytes({0x5B});  // pop rbx
    out.bytes({0x5A});  // pop rdx
    out.bytes({0x59});  // pop rcx
    out.bytes({0x58});  // pop rax
    out.bytes({0x5D});  // pop rbp
    out.jump(runtime.alloc_again);
    out.place(large);
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0xBA, 0x08, 0x00, 0x00, 0x00});  // mov edx, 8
    out.bytes({0x4C, 0x8B, 0x45, 0xF0});  // mov r8, qword ptr [rbp-16]
    out.bytes({0x49, 0x83, 0xC0, 0x20});  // add r8, 32
    out.call_windows(DataRef::HeapAlloc);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_not_equal(got_memory);
    out.place(no_memory);
    out.bytes({0x4D, 0x85, 0xE4});  // test r12, r12
    out.jump_if_not_equal(runtime.out_of_memory);
    out.bytes({0xF6, 0x43, 0x20, 0x04});  // test byte ptr [rbx+32], 4
    out.jump_if_not_equal(runtime.out_of_memory);
    out.jump(clean_first);
    out.place(got_memory);
    out.bytes({0x48, 0x8B, 0x0B});  // mov rcx, qword ptr [rbx]
    out.bytes({0x48, 0x89, 0x08});  // mov qword ptr [rax], rcx
    out.bytes({0x48, 0x89, 0x03});  // mov qword ptr [rbx], rax
    out.bytes({0x48, 0xFF, 0x43, 0x08});  // inc qword ptr [rbx+8]
    out.bytes({0x48, 0x8B, 0x4D, 0xF0});  // mov rcx, qword ptr [rbp-16]
    out.bytes({0x48, 0x89, 0x48, 0x08});  // mov qword ptr [rax+8], rcx
    out.bytes({0x48, 0x8B, 0x55, 0xB0});  // mov rdx, qword ptr [rbp-80]
    out.bytes({0x88, 0x50, 0x10});  // mov byte ptr [rax+16], dl
    out.bytes({0x48, 0x31, 0xC1});  // xor rcx, rax
    out.bytes({0x48, 0xBA, 0x41, 0x45, 0x4C, 0x43, 0x4F, 0x4D, 0x41, 0x4A});  // movabs rdx, 0x4A414D4F434C4541
    out.bytes({0x48, 0x31, 0xD1});  // xor rcx, rdx
    out.bytes({0x48, 0x89, 0x48, 0x18});  // mov qword ptr [rax+24], rcx
    out.bytes({0x48, 0x83, 0xC0, 0x20});  // add rax, 32
    out.bytes({0x48, 0x89, 0x45, 0xF8});  // mov qword ptr [rbp-8], rax
    out.bytes({0x48, 0x8D, 0x65, 0x90});  // lea rsp, [rbp-112]
    out.bytes({0x41, 0x5F});  // pop r15
    out.bytes({0x41, 0x5E});  // pop r14
    out.bytes({0x41, 0x5D});  // pop r13
    out.bytes({0x41, 0x5C});  // pop r12
    out.bytes({0x41, 0x5B});  // pop r11
    out.bytes({0x41, 0x5A});  // pop r10
    out.bytes({0x41, 0x59});  // pop r9
    out.bytes({0x41, 0x58});  // pop r8
    out.bytes({0x5F});  // pop rdi
    out.bytes({0x5E});  // pop rsi
    out.bytes({0x5B});  // pop rbx
    out.bytes({0x5A});  // pop rdx
    out.bytes({0x59});  // pop rcx
    out.bytes({0x58});  // pop rax
    out.bytes({0x5D});  // pop rbp
    out.bytes({0xC3});  // ret
}

// rax = the mode: 1 cleans at every allocation, 2 adds extra checks, 4 turns cleaning off, 16 is a window program. Also notes where the stack starts. The cleaner's values: the first block +0, how many +8, bytes since a clean +16, when to clean +24, mode +32, stack start +40, freed list +56, how many freed +80, how many cleans +88, current chunk +96, its next free spot +104, its end +112, chunks in use +120, empty chunks +128, how many empty +136, the free list of each size class +144, then the table of blocks +400 and how many entries it has +408, and the list to look through +416 and its entries +424, kept from clean to clean while each is 2 MB or less, so a program that cleans often doesn't ask Windows for them every time. A chunk starts with the next chunk and how many blocks in it are in use
void write_cleaner_start(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label will_clean = out.new_label();
    out.place(runtime.cleaner_start);
    out.lea_data(RDX, DataRef::Cleaner);
    out.bytes({0x48, 0x89, 0x42, 0x20});  // mov qword ptr [rdx+32], rax
    out.bytes({0x48, 0x89, 0x62, 0x28});  // mov qword ptr [rdx+40], rsp
    out.bytes({0x48, 0xC7, 0x42, 0x18, 0x00, 0x00, 0x40, 0x00});  // mov qword ptr [rdx+24], 0x400000
    out.bytes({0xA8, 0x04});  // test al, 4
    out.jump_if_equal(will_clean);
    out.bytes({0x48, 0xC7, 0x42, 0x18, 0xFF, 0xFF, 0xFF, 0xFF});  // mov qword ptr [rdx+24], -1
    out.place(will_clean);
    out.bytes({0xC3});  // ret
}

// The extra checks: every block's header, and that freed memory still holds only its marker. rbx = the cleaner's values
void write_cleaner_verify(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label next_live = out.new_label();
    const Label check_freed = out.new_label();
    const Label next_freed = out.new_label();
    const Label all_good = out.new_label();
    const Label bad_header = out.new_label();
    const Label freed_changed = out.new_label();
    out.place(runtime.cleaner_verify);
    out.bytes({0x48, 0x8B, 0x13});  // mov rdx, qword ptr [rbx]
    out.place(next_live);
    out.bytes({0x48, 0x85, 0xD2});  // test rdx, rdx
    out.jump_if_equal(check_freed);
    out.bytes({0x48, 0x8B, 0x42, 0x08});  // mov rax, qword ptr [rdx+8]
    out.bytes({0x48, 0x31, 0xD0});  // xor rax, rdx
    out.bytes({0x48, 0xB9, 0x41, 0x45, 0x4C, 0x43, 0x4F, 0x4D, 0x41, 0x4A});  // movabs rcx, 0x4A414D4F434C4541
    out.bytes({0x48, 0x31, 0xC8});  // xor rax, rcx
    out.bytes({0x48, 0x3B, 0x42, 0x18});  // cmp rax, qword ptr [rdx+24]
    out.jump_if_not_equal(bad_header);
    out.bytes({0x80, 0x7A, 0x10, 0x01});  // cmp byte ptr [rdx+16], 1
    out.jump_if_above(bad_header);
    out.bytes({0x80, 0x7A, 0x11, 0x00});  // cmp byte ptr [rdx+17], 0
    out.jump_if_not_equal(bad_header);
    out.bytes({0x48, 0x8B, 0x12});  // mov rdx, qword ptr [rdx]
    out.jump(next_live);
    out.place(check_freed);
    out.bytes({0x48, 0x8B, 0x53, 0x38});  // mov rdx, qword ptr [rbx+56]
    out.place(next_freed);
    out.bytes({0x48, 0x85, 0xD2});  // test rdx, rdx
    out.jump_if_equal(all_good);
    out.bytes({0x48, 0x8B, 0x42, 0x08});  // mov rax, qword ptr [rdx+8]
    out.bytes({0x48, 0x31, 0xD0});  // xor rax, rdx
    out.bytes({0x48, 0xB9, 0x41, 0x45, 0x4C, 0x43, 0x4F, 0x4D, 0x41, 0x4A});  // movabs rcx, 0x4A414D4F434C4541
    out.bytes({0x48, 0x31, 0xC8});  // xor rax, rcx
    out.bytes({0x48, 0x3B, 0x42, 0x18});  // cmp rax, qword ptr [rdx+24]
    out.jump_if_not_equal(bad_header);
    out.bytes({0x80, 0x7A, 0x10, 0x02});  // cmp byte ptr [rdx+16], 2
    out.jump_if_not_equal(bad_header);
    out.bytes({0x48, 0x8D, 0x7A, 0x20});  // lea rdi, [rdx+32]
    out.bytes({0x48, 0x8B, 0x4A, 0x08});  // mov rcx, qword ptr [rdx+8]
    out.bytes({0x48, 0xC1, 0xE9, 0x03});  // shr rcx, 3
    out.bytes({0x48, 0xB8, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD});  // movabs rax, 0xDDDDDDDDDDDDDDDD
    out.bytes({0x48, 0x39, 0xC0});  // cmp rax, rax
    out.bytes({0xF3, 0x48, 0xAF});  // repe scasq
    out.jump_if_not_equal(freed_changed);
    out.bytes({0x48, 0x8B, 0x4A, 0x08});  // mov rcx, qword ptr [rdx+8]
    out.bytes({0x48, 0x83, 0xE1, 0x07});  // and rcx, 7
    out.bytes({0x48, 0x39, 0xC0});  // cmp rax, rax
    out.bytes({0xF3, 0xAE});  // repe scasb
    out.jump_if_not_equal(freed_changed);
    out.bytes({0x48, 0x8B, 0x12});  // mov rdx, qword ptr [rdx]
    out.jump(next_freed);
    out.place(all_good);
    out.bytes({0xC3});  // ret
    out.place(bad_header);
    out.lea_string(RAX, text.cleaner_bad_header);
    out.jump(runtime.run_error);
    out.place(freed_changed);
    out.lea_string(RAX, text.cleaner_freed_changed);
    out.jump(runtime.run_error);
}

// rdx = a list of blocks to add to the table of known blocks. r12 = the table, r13 = its size minus 1
void write_cleaner_add_to_table(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    (void)text;
    const Label next_block = out.new_label();
    const Label next_place = out.new_label();
    const Label found_room = out.new_label();
    const Label finished = out.new_label();
    out.place(runtime.cleaner_add_to_table);
    out.bytes({0x49, 0xB9, 0x15, 0x7C, 0x4A, 0x7F, 0xB9, 0x79, 0x37, 0x9E});  // movabs r9, 0x9E3779B97F4A7C15
    out.place(next_block);
    out.bytes({0x48, 0x85, 0xD2});  // test rdx, rdx
    out.jump_if_equal(finished);
    out.bytes({0x48, 0x8D, 0x42, 0x20});  // lea rax, [rdx+32]
    out.bytes({0x48, 0x89, 0xC1});  // mov rcx, rax
    out.bytes({0x48, 0xC1, 0xE9, 0x04});  // shr rcx, 4
    out.bytes({0x49, 0x0F, 0xAF, 0xC9});  // imul rcx, r9
    out.bytes({0x48, 0xC1, 0xE9, 0x20});  // shr rcx, 32
    out.place(next_place);
    out.bytes({0x4C, 0x21, 0xE9});  // and rcx, r13
    out.bytes({0x49, 0x83, 0x3C, 0xCC, 0x00});  // cmp qword ptr [r12+rcx*8], 0
    out.jump_if_equal(found_room);
    out.bytes({0x48, 0xFF, 0xC1});  // inc rcx
    out.jump(next_place);
    out.place(found_room);
    out.bytes({0x49, 0x89, 0x04, 0xCC});  // mov qword ptr [r12+rcx*8], rax
    out.bytes({0x48, 0x8B, 0x12});  // mov rdx, qword ptr [rdx]
    out.jump(next_block);
    out.place(finished);
    out.bytes({0xC3});  // ret
}

// rax = anything at all. If it's exactly where a known block's memory starts, that block is in use. r14 = the top of the list still to look through, r15 = 1 when the word came from a block. With checks on, a block's word that points at a block freed in the last 2 cleans is a bug: when the cleaner frees something still in use, that's when it shows. Older ones are left alone, since a plain number can equal an old block's address
void write_cleaner_mark_word(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label next_place = out.new_label();
    const Label found = out.new_label();
    const Label nothing = out.new_label();
    const Label is_freed = out.new_label();
    out.place(runtime.cleaner_mark_word);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(nothing);
    out.bytes({0xA8, 0x0F});  // test al, 15
    out.jump_if_not_equal(nothing);
    out.bytes({0x49, 0xB9, 0x15, 0x7C, 0x4A, 0x7F, 0xB9, 0x79, 0x37, 0x9E});  // movabs r9, 0x9E3779B97F4A7C15
    out.bytes({0x48, 0x89, 0xC1});  // mov rcx, rax
    out.bytes({0x48, 0xC1, 0xE9, 0x04});  // shr rcx, 4
    out.bytes({0x49, 0x0F, 0xAF, 0xC9});  // imul rcx, r9
    out.bytes({0x48, 0xC1, 0xE9, 0x20});  // shr rcx, 32
    out.place(next_place);
    out.bytes({0x4C, 0x21, 0xE9});  // and rcx, r13
    out.bytes({0x49, 0x8B, 0x14, 0xCC});  // mov rdx, qword ptr [r12+rcx*8]
    out.bytes({0x48, 0x85, 0xD2});  // test rdx, rdx
    out.jump_if_equal(nothing);
    out.bytes({0x48, 0x39, 0xC2});  // cmp rdx, rax
    out.jump_if_equal(found);
    out.bytes({0x48, 0xFF, 0xC1});  // inc rcx
    out.jump(next_place);
    out.place(found);
    out.bytes({0x80, 0x78, 0xF0, 0x02});  // cmp byte ptr [rax-16], 2
    out.jump_if_equal(is_freed);
    out.bytes({0x80, 0x78, 0xF1, 0x00});  // cmp byte ptr [rax-15], 0
    out.jump_if_not_equal(nothing);
    out.bytes({0xC6, 0x40, 0xF1, 0x01});  // mov byte ptr [rax-15], 1
    out.bytes({0x80, 0x78, 0xF0, 0x01});  // cmp byte ptr [rax-16], 1
    out.jump_if_not_equal(nothing);
    out.bytes({0x49, 0x89, 0x06});  // mov qword ptr [r14], rax
    out.bytes({0x49, 0x83, 0xC6, 0x08});  // add r14, 8
    out.place(nothing);
    out.bytes({0xC3});  // ret
    out.place(is_freed);
    out.bytes({0x4D, 0x85, 0xFF});  // test r15, r15
    out.jump_if_equal(nothing);
    out.bytes({0x0F, 0xB7, 0x48, 0xF2});  // movzx ecx, word ptr [rax-14]
    out.bytes({0x8B, 0x53, 0x58});  // mov edx, dword ptr [rbx+88]
    out.bytes({0x29, 0xCA});  // sub edx, ecx
    out.bytes({0x81, 0xE2, 0xFF, 0xFF, 0x00, 0x00});  // and edx, 0xFFFF
    out.bytes({0x83, 0xFA, 0x02});  // cmp edx, 2
    out.jump_if_above(nothing);
    out.lea_string(RAX, text.cleaner_freed_reference);
    out.jump(runtime.run_error);
}

// Marks everything that's in use, starting from the whole stack, which holds every saved register. Uses a list instead of recursion, so long chains can't overflow the stack
void write_cleaner_clean(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label no_checks = out.new_label();
    const Label size_table = out.new_label();
    const Label table_sized = out.new_label();
    const Label table_freed = out.new_label();
    const Label reuse_table = out.new_label();
    const Label table_ready = out.new_label();
    const Label list_freed = out.new_label();
    const Label reuse_list = out.new_label();
    const Label next_root = out.new_label();
    const Label roots_done = out.new_label();
    const Label next_to_look_at = out.new_label();
    const Label next_word = out.new_label();
    const Label all_marked = out.new_label();
    const Label next_count = out.new_label();
    const Label counts_cleared = out.new_label();
    const Label next_block = out.new_label();
    const Label not_in_chunk = out.new_label();
    const Label free_block = out.new_label();
    const Label free_small = out.new_label();
    const Label keep_freed = out.new_label();
    const Label block_freed = out.new_label();
    const Label freed_twice = out.new_label();
    const Label sweep_done = out.new_label();
    const Label next_counted = out.new_label();
    const Label has_live = out.new_label();
    const Label all_counted = out.new_label();
    const Label next_list = out.new_label();
    const Label next_free = out.new_label();
    const Label drop_free = out.new_label();
    const Label list_done = out.new_label();
    const Label next_chunk = out.new_label();
    const Label empty_chunk = out.new_label();
    const Label not_current = out.new_label();
    const Label trim_chunks = out.new_label();
    const Label chunks_done = out.new_label();
    const Label give_up = out.new_label();
    const Label keep_table = out.new_label();
    const Label keep_list = out.new_label();
    out.place(runtime.cleaner_clean);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16
    out.bytes({0x48, 0x83, 0xEC, 0x30});  // sub rsp, 48
    out.lea_data(RBX, DataRef::Cleaner);
    out.bytes({0x48, 0xFF, 0x43, 0x58});  // inc qword ptr [rbx+88]
    out.bytes({0xF6, 0x43, 0x20, 0x02});  // test byte ptr [rbx+32], 2
    out.jump_if_equal(no_checks);
    out.call(runtime.cleaner_verify);
    out.place(no_checks);
    out.bytes({0x48, 0x8B, 0x43, 0x08});  // mov rax, qword ptr [rbx+8]
    out.bytes({0x48, 0x03, 0x43, 0x50});  // add rax, qword ptr [rbx+80]
    out.bytes({0x48, 0x8D, 0x54, 0x00, 0x01});  // lea rdx, [rax+rax+1]
    out.bytes({0x41, 0xBD, 0x10, 0x00, 0x00, 0x00});  // mov r13d, 16
    out.place(size_table);
    out.bytes({0x49, 0x39, 0xD5});  // cmp r13, rdx
    out.jump_if_at_least(table_sized);
    out.bytes({0x49, 0xD1, 0xE5});  // shl r13, 1
    out.jump(size_table);
    out.place(table_sized);
    out.bytes({0x4C, 0x3B, 0xAB, 0x98, 0x01, 0x00, 0x00});  // cmp r13, qword ptr [rbx+408]
    out.jump_if_at_most(reuse_table);
    out.bytes({0x4C, 0x8B, 0x83, 0x90, 0x01, 0x00, 0x00});  // mov r8, qword ptr [rbx+400]
    out.bytes({0x4D, 0x85, 0xC0});  // test r8, r8
    out.jump_if_equal(table_freed);
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.call_windows(DataRef::HeapFree);
    out.place(table_freed);
    out.bytes({0x48, 0xC7, 0x83, 0x90, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+400], 0
    out.bytes({0x48, 0xC7, 0x83, 0x98, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+408], 0
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0xBA, 0x08, 0x00, 0x00, 0x00});  // mov edx, 8
    out.bytes({0x4E, 0x8D, 0x04, 0xED, 0x00, 0x00, 0x00, 0x00});  // lea r8, [r13*8]
    out.call_windows(DataRef::HeapAlloc);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(give_up);
    out.bytes({0x48, 0x89, 0x83, 0x90, 0x01, 0x00, 0x00});  // mov qword ptr [rbx+400], rax
    out.bytes({0x4C, 0x89, 0xAB, 0x98, 0x01, 0x00, 0x00});  // mov qword ptr [rbx+408], r13
    out.jump(table_ready);
    out.place(reuse_table);
    out.bytes({0x4C, 0x8B, 0xAB, 0x98, 0x01, 0x00, 0x00});  // mov r13, qword ptr [rbx+408]
    out.bytes({0x48, 0x8B, 0xBB, 0x90, 0x01, 0x00, 0x00});  // mov rdi, qword ptr [rbx+400]
    out.bytes({0x4C, 0x89, 0xE9});  // mov rcx, r13
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.bytes({0xF3, 0x48, 0xAB});  // rep stosq
    out.place(table_ready);
    out.bytes({0x4C, 0x8B, 0xA3, 0x90, 0x01, 0x00, 0x00});  // mov r12, qword ptr [rbx+400]
    out.bytes({0x4C, 0x8B, 0x7B, 0x08});  // mov r15, qword ptr [rbx+8]
    out.bytes({0x49, 0xFF, 0xC7});  // inc r15
    out.bytes({0x4C, 0x3B, 0xBB, 0xA8, 0x01, 0x00, 0x00});  // cmp r15, qword ptr [rbx+424]
    out.jump_if_at_most(reuse_list);
    out.bytes({0x4C, 0x8B, 0x83, 0xA0, 0x01, 0x00, 0x00});  // mov r8, qword ptr [rbx+416]
    out.bytes({0x4D, 0x85, 0xC0});  // test r8, r8
    out.jump_if_equal(list_freed);
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.call_windows(DataRef::HeapFree);
    out.place(list_freed);
    out.bytes({0x48, 0xC7, 0x83, 0xA0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+416], 0
    out.bytes({0x48, 0xC7, 0x83, 0xA8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+424], 0
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x4E, 0x8D, 0x04, 0xFD, 0x00, 0x00, 0x00, 0x00});  // lea r8, [r15*8]
    out.call_windows(DataRef::HeapAlloc);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(give_up);
    out.bytes({0x48, 0x89, 0x83, 0xA0, 0x01, 0x00, 0x00});  // mov qword ptr [rbx+416], rax
    out.bytes({0x4C, 0x89, 0xBB, 0xA8, 0x01, 0x00, 0x00});  // mov qword ptr [rbx+424], r15
    out.place(reuse_list);
    out.bytes({0x4C, 0x8B, 0xB3, 0xA0, 0x01, 0x00, 0x00});  // mov r14, qword ptr [rbx+416]
    out.bytes({0x4C, 0x89, 0x74, 0x24, 0x28});  // mov qword ptr [rsp+40], r14
    out.bytes({0x49, 0xFF, 0xCD});  // dec r13
    out.bytes({0x48, 0x8B, 0x13});  // mov rdx, qword ptr [rbx]
    out.call(runtime.cleaner_add_to_table);
    out.bytes({0x48, 0x8B, 0x53, 0x38});  // mov rdx, qword ptr [rbx+56]
    out.call(runtime.cleaner_add_to_table);
    out.bytes({0x45, 0x31, 0xFF});  // xor r15d, r15d
    out.bytes({0x48, 0x89, 0xE6});  // mov rsi, rsp
    out.place(next_root);
    out.bytes({0x48, 0x3B, 0x73, 0x28});  // cmp rsi, qword ptr [rbx+40]
    out.jump_if_at_least(roots_done);
    out.bytes({0x48, 0x8B, 0x06});  // mov rax, qword ptr [rsi]
    out.call(runtime.cleaner_mark_word);
    out.bytes({0x48, 0x83, 0xC6, 0x08});  // add rsi, 8
    out.jump(next_root);
    out.place(roots_done);
    out.bytes({0x41, 0xBF, 0x01, 0x00, 0x00, 0x00});  // mov r15d, 1
    out.place(next_to_look_at);
    out.bytes({0x4C, 0x3B, 0x74, 0x24, 0x28});  // cmp r14, qword ptr [rsp+40]
    out.jump_if_equal(all_marked);
    out.bytes({0x49, 0x83, 0xEE, 0x08});  // sub r14, 8
    out.bytes({0x49, 0x8B, 0x36});  // mov rsi, qword ptr [r14]
    out.bytes({0x48, 0x8B, 0x7E, 0xE8});  // mov rdi, qword ptr [rsi-24]
    out.bytes({0x48, 0xC1, 0xEF, 0x03});  // shr rdi, 3
    out.place(next_word);
    out.bytes({0x48, 0x85, 0xFF});  // test rdi, rdi
    out.jump_if_equal(next_to_look_at);
    out.bytes({0x48, 0x8B, 0x06});  // mov rax, qword ptr [rsi]
    out.call(runtime.cleaner_mark_word);
    out.bytes({0x48, 0x83, 0xC6, 0x08});  // add rsi, 8
    out.bytes({0x48, 0xFF, 0xCF});  // dec rdi
    out.jump(next_word);
    out.place(all_marked);
    out.bytes({0x48, 0x8B, 0x43, 0x78});  // mov rax, qword ptr [rbx+120]
    out.place(next_count);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(counts_cleared);
    out.bytes({0x48, 0xC7, 0x40, 0x08, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rax+8], 0
    out.bytes({0x48, 0x8B, 0x00});  // mov rax, qword ptr [rax]
    out.jump(next_count);
    out.place(counts_cleared);
    out.bytes({0x48, 0x89, 0xDE});  // mov rsi, rbx
    out.bytes({0x48, 0x8B, 0x3B});  // mov rdi, qword ptr [rbx]
    out.bytes({0x45, 0x31, 0xF6});  // xor r14d, r14d
    out.place(next_block);
    out.bytes({0x48, 0x85, 0xFF});  // test rdi, rdi
    out.jump_if_equal(sweep_done);
    out.bytes({0x80, 0x7F, 0x11, 0x00});  // cmp byte ptr [rdi+17], 0
    out.jump_if_equal(free_block);
    out.bytes({0xC6, 0x47, 0x11, 0x00});  // mov byte ptr [rdi+17], 0
    out.bytes({0x48, 0x8B, 0x47, 0x08});  // mov rax, qword ptr [rdi+8]
    out.bytes({0x48, 0x83, 0xC0, 0x2F});  // add rax, 47
    out.bytes({0x48, 0x83, 0xE0, 0xF0});  // and rax, -16
    out.bytes({0x49, 0x01, 0xC6});  // add r14, rax
    out.bytes({0x8B, 0x57, 0x14});  // mov edx, dword ptr [rdi+20]
    out.bytes({0x85, 0xD2});  // test edx, edx
    out.jump_if_equal(not_in_chunk);
    out.bytes({0x48, 0x89, 0xF8});  // mov rax, rdi
    out.bytes({0x48, 0x29, 0xD0});  // sub rax, rdx
    out.bytes({0x48, 0xFF, 0x40, 0x08});  // inc qword ptr [rax+8]
    out.place(not_in_chunk);
    out.bytes({0x48, 0x89, 0xFE});  // mov rsi, rdi
    out.bytes({0x48, 0x8B, 0x3F});  // mov rdi, qword ptr [rdi]
    out.jump(next_block);
    out.place(free_block);
    out.bytes({0x80, 0x7F, 0x10, 0x02});  // cmp byte ptr [rdi+16], 2
    out.jump_if_equal(freed_twice);
    out.bytes({0x4C, 0x8B, 0x3F});  // mov r15, qword ptr [rdi]
    out.bytes({0x4C, 0x89, 0x3E});  // mov qword ptr [rsi], r15
    out.bytes({0x48, 0xFF, 0x4B, 0x08});  // dec qword ptr [rbx+8]
    out.bytes({0xF6, 0x43, 0x20, 0x02});  // test byte ptr [rbx+32], 2
    out.jump_if_not_equal(keep_freed);
    out.bytes({0x83, 0x7F, 0x14, 0x00});  // cmp dword ptr [rdi+20], 0
    out.jump_if_not_equal(free_small);
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x49, 0x89, 0xF8});  // mov r8, rdi
    out.call_windows(DataRef::HeapFree);
    out.jump(block_freed);
    out.place(free_small);
    out.bytes({0xC6, 0x47, 0x10, 0x03});  // mov byte ptr [rdi+16], 3
    out.bytes({0x48, 0x8B, 0x47, 0x08});  // mov rax, qword ptr [rdi+8]
    out.bytes({0x48, 0xFF, 0xC8});  // dec rax
    out.bytes({0x48, 0xC1, 0xE8, 0x04});  // shr rax, 4
    out.bytes({0x48, 0x8B, 0x8C, 0xC3, 0x90, 0x00, 0x00, 0x00});  // mov rcx, qword ptr [rbx+rax*8+144]
    out.bytes({0x48, 0x89, 0x0F});  // mov qword ptr [rdi], rcx
    out.bytes({0x48, 0x89, 0xBC, 0xC3, 0x90, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+rax*8+144], rdi
    out.jump(block_freed);
    out.place(keep_freed);
    out.bytes({0xC6, 0x47, 0x10, 0x02});  // mov byte ptr [rdi+16], 2
    out.bytes({0x8B, 0x43, 0x58});  // mov eax, dword ptr [rbx+88]
    out.bytes({0x66, 0x89, 0x47, 0x12});  // mov word ptr [rdi+18], ax
    out.bytes({0x48, 0x8B, 0x43, 0x38});  // mov rax, qword ptr [rbx+56]
    out.bytes({0x48, 0x89, 0x07});  // mov qword ptr [rdi], rax
    out.bytes({0x48, 0x89, 0x7B, 0x38});  // mov qword ptr [rbx+56], rdi
    out.bytes({0x48, 0xFF, 0x43, 0x50});  // inc qword ptr [rbx+80]
    out.bytes({0x48, 0x8B, 0x4F, 0x08});  // mov rcx, qword ptr [rdi+8]
    out.bytes({0x48, 0x83, 0xC7, 0x20});  // add rdi, 32
    out.bytes({0xB0, 0xDD});  // mov al, 0xDD
    out.bytes({0xF3, 0xAA});  // rep stosb
    out.place(block_freed);
    out.bytes({0x4C, 0x89, 0xFF});  // mov rdi, r15
    out.jump(next_block);
    out.place(freed_twice);
    out.lea_string(RAX, text.cleaner_double_free);
    out.jump(runtime.run_error);
    out.place(sweep_done);
    out.bytes({0x4D, 0x01, 0xF6});  // add r14, r14
    out.bytes({0xB8, 0x00, 0x00, 0x40, 0x00});  // mov eax, 0x400000
    out.bytes({0x49, 0x39, 0xC6});  // cmp r14, rax
    out.bytes({0x49, 0x0F, 0x47, 0xC6});  // cmova rax, r14
    out.bytes({0x48, 0x89, 0x43, 0x18});  // mov qword ptr [rbx+24], rax
    out.bytes({0xF6, 0x43, 0x20, 0x02});  // test byte ptr [rbx+32], 2
    out.jump_if_not_equal(chunks_done);
    out.bytes({0x31, 0xC9});  // xor ecx, ecx
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x48, 0x8B, 0x43, 0x78});  // mov rax, qword ptr [rbx+120]
    out.place(next_counted);
    out.bytes({0x48, 0x85, 0xC0});  // test rax, rax
    out.jump_if_equal(all_counted);
    out.bytes({0x48, 0xFF, 0xC1});  // inc rcx
    out.bytes({0x48, 0x83, 0x78, 0x08, 0x00});  // cmp qword ptr [rax+8], 0
    out.jump_if_not_equal(has_live);
    out.bytes({0x48, 0xFF, 0xC2});  // inc rdx
    out.place(has_live);
    out.bytes({0x48, 0x8B, 0x00});  // mov rax, qword ptr [rax]
    out.jump(next_counted);
    out.place(all_counted);
    out.bytes({0x48, 0xC1, 0xE2, 0x03});  // shl rdx, 3
    out.bytes({0x48, 0x39, 0xCA});  // cmp rdx, rcx
    out.jump_if_below(trim_chunks);
    out.bytes({0x45, 0x31, 0xED});  // xor r13d, r13d
    out.place(next_list);
    out.bytes({0x4A, 0x8D, 0xB4, 0x2B, 0x90, 0x00, 0x00, 0x00});  // lea rsi, [rbx+r13+144]
    out.place(next_free);
    out.bytes({0x48, 0x8B, 0x3E});  // mov rdi, qword ptr [rsi]
    out.bytes({0x48, 0x85, 0xFF});  // test rdi, rdi
    out.jump_if_equal(list_done);
    out.bytes({0x8B, 0x57, 0x14});  // mov edx, dword ptr [rdi+20]
    out.bytes({0x48, 0x89, 0xF8});  // mov rax, rdi
    out.bytes({0x48, 0x29, 0xD0});  // sub rax, rdx
    out.bytes({0x48, 0x83, 0x78, 0x08, 0x00});  // cmp qword ptr [rax+8], 0
    out.jump_if_equal(drop_free);
    out.bytes({0x48, 0x89, 0xFE});  // mov rsi, rdi
    out.jump(next_free);
    out.place(drop_free);
    out.bytes({0x48, 0x8B, 0x07});  // mov rax, qword ptr [rdi]
    out.bytes({0x48, 0x89, 0x06});  // mov qword ptr [rsi], rax
    out.jump(next_free);
    out.place(list_done);
    out.bytes({0x49, 0x83, 0xC5, 0x08});  // add r13, 8
    out.bytes({0x49, 0x81, 0xFD, 0x00, 0x01, 0x00, 0x00});  // cmp r13, 256
    out.jump_if_below(next_list);
    out.bytes({0x48, 0x8D, 0x73, 0x78});  // lea rsi, [rbx+120]
    out.place(next_chunk);
    out.bytes({0x48, 0x8B, 0x3E});  // mov rdi, qword ptr [rsi]
    out.bytes({0x48, 0x85, 0xFF});  // test rdi, rdi
    out.jump_if_equal(trim_chunks);
    out.bytes({0x48, 0x83, 0x7F, 0x08, 0x00});  // cmp qword ptr [rdi+8], 0
    out.jump_if_equal(empty_chunk);
    out.bytes({0x48, 0x89, 0xFE});  // mov rsi, rdi
    out.jump(next_chunk);
    out.place(empty_chunk);
    out.bytes({0x48, 0x8B, 0x07});  // mov rax, qword ptr [rdi]
    out.bytes({0x48, 0x89, 0x06});  // mov qword ptr [rsi], rax
    out.bytes({0x48, 0x3B, 0x7B, 0x60});  // cmp rdi, qword ptr [rbx+96]
    out.jump_if_not_equal(not_current);
    out.bytes({0x31, 0xC0});  // xor eax, eax
    out.bytes({0x48, 0x89, 0x43, 0x60});  // mov qword ptr [rbx+96], rax
    out.bytes({0x48, 0x89, 0x43, 0x68});  // mov qword ptr [rbx+104], rax
    out.bytes({0x48, 0x89, 0x43, 0x70});  // mov qword ptr [rbx+112], rax
    out.place(not_current);
    out.bytes({0x48, 0x8B, 0x83, 0x80, 0x00, 0x00, 0x00});  // mov rax, qword ptr [rbx+128]
    out.bytes({0x48, 0x89, 0x07});  // mov qword ptr [rdi], rax
    out.bytes({0x48, 0x89, 0xBB, 0x80, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+128], rdi
    out.bytes({0x48, 0xFF, 0x83, 0x88, 0x00, 0x00, 0x00});  // inc qword ptr [rbx+136]
    out.jump(next_chunk);
    out.place(trim_chunks);
    out.bytes({0x48, 0x8B, 0x83, 0x88, 0x00, 0x00, 0x00});  // mov rax, qword ptr [rbx+136]
    out.bytes({0x48, 0xC1, 0xE0, 0x12});  // shl rax, 18
    out.bytes({0x48, 0x8B, 0x4B, 0x18});  // mov rcx, qword ptr [rbx+24]
    out.bytes({0x48, 0xD1, 0xE9});  // shr rcx, 1
    out.bytes({0x48, 0x81, 0xC1, 0x00, 0x00, 0x08, 0x00});  // add rcx, 0x80000
    out.bytes({0x48, 0x39, 0xC8});  // cmp rax, rcx
    out.jump_if_at_most(chunks_done);
    out.bytes({0x48, 0x8B, 0xBB, 0x80, 0x00, 0x00, 0x00});  // mov rdi, qword ptr [rbx+128]
    out.bytes({0x48, 0x8B, 0x07});  // mov rax, qword ptr [rdi]
    out.bytes({0x48, 0x89, 0x83, 0x80, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+128], rax
    out.bytes({0x48, 0xFF, 0x8B, 0x88, 0x00, 0x00, 0x00});  // dec qword ptr [rbx+136]
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x49, 0x89, 0xF8});  // mov r8, rdi
    out.call_windows(DataRef::HeapFree);
    out.jump(trim_chunks);
    out.place(chunks_done);
    out.place(give_up);
    out.bytes({0x48, 0x81, 0xBB, 0x98, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00});  // cmp qword ptr [rbx+408], 262144
    out.jump_if_at_most(keep_table);
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x4C, 0x8B, 0x83, 0x90, 0x01, 0x00, 0x00});  // mov r8, qword ptr [rbx+400]
    out.call_windows(DataRef::HeapFree);
    out.bytes({0x48, 0xC7, 0x83, 0x90, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+400], 0
    out.bytes({0x48, 0xC7, 0x83, 0x98, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+408], 0
    out.place(keep_table);
    out.bytes({0x48, 0x81, 0xBB, 0xA8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00});  // cmp qword ptr [rbx+424], 262144
    out.jump_if_at_most(keep_list);
    out.load_rcx_data(DataRef::HeapHandle);
    out.bytes({0x31, 0xD2});  // xor edx, edx
    out.bytes({0x4C, 0x8B, 0x83, 0xA0, 0x01, 0x00, 0x00});  // mov r8, qword ptr [rbx+416]
    out.call_windows(DataRef::HeapFree);
    out.bytes({0x48, 0xC7, 0x83, 0xA0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+416], 0
    out.bytes({0x48, 0xC7, 0x83, 0xA8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+424], 0
    out.place(keep_list);
    out.bytes({0x48, 0xC7, 0x43, 0x10, 0x00, 0x00, 0x00, 0x00});  // mov qword ptr [rbx+16], 0
    out.bytes({0xC9});  // leave
    out.bytes({0xC3});  // ret
}

// Walks up the stack, showing each function call that led here. The table holds pairs of 4-byte places: where a call returns to, and the words for that call
void write_show_calls(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label next_frame = out.new_label();
    const Label next_entry = out.new_label();
    const Label found_call = out.new_label();
    const Label not_a_call = out.new_label();
    const Label too_many = out.new_label();
    const Label finished = out.new_label();
    out.place(runtime.show_calls);
    out.bytes({0x49, 0x89, 0xED});  // mov r13, rbp
    out.bytes({0x45, 0x31, 0xF6});  // xor r14d, r14d
    out.lea_data(RAX, DataRef::Cleaner);
    out.bytes({0x4C, 0x8B, 0x78, 0x28});  // mov r15, qword ptr [rax+40]
    out.place(next_frame);
    out.bytes({0x4D, 0x85, 0xED});  // test r13, r13
    out.jump_if_equal(finished);
    out.bytes({0x4D, 0x39, 0xFD});  // cmp r13, r15
    out.jump_if_at_least(finished);
    out.bytes({0x49, 0x8B, 0x45, 0x08});  // mov rax, qword ptr [r13+8]
    out.bytes({0x48, 0xB9, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00});  // movabs rcx, 0x140000000
    out.bytes({0x48, 0x29, 0xC8});  // sub rax, rcx
    out.lea_data(RDX, DataRef::CallTable);
    out.bytes({0x8B, 0x0A});  // mov ecx, dword ptr [rdx]
    out.bytes({0x48, 0x83, 0xC2, 0x08});  // add rdx, 8
    out.place(next_entry);
    out.bytes({0x85, 0xC9});  // test ecx, ecx
    out.jump_if_equal(not_a_call);
    out.bytes({0x3B, 0x02});  // cmp eax, dword ptr [rdx]
    out.jump_if_equal(found_call);
    out.bytes({0x48, 0x83, 0xC2, 0x08});  // add rdx, 8
    out.bytes({0xFF, 0xC9});  // dec ecx
    out.jump(next_entry);
    out.place(found_call);
    out.bytes({0x49, 0x83, 0xFE, 0x0A});  // cmp r14, 10
    out.jump_if_at_least(too_many);
    out.bytes({0x49, 0xFF, 0xC6});  // inc r14
    out.bytes({0x8B, 0x42, 0x04});  // mov eax, dword ptr [rdx+4]
    out.bytes({0x48, 0xB9, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00});  // movabs rcx, 0x140000000
    out.bytes({0x48, 0x01, 0xC8});  // add rax, rcx
    out.call(runtime.error_text);
    out.place(not_a_call);
    out.bytes({0x49, 0x8B, 0x45, 0x00});  // mov rax, qword ptr [r13]
    out.bytes({0x4C, 0x39, 0xE8});  // cmp rax, r13
    out.jump_if_at_most(finished);
    out.bytes({0x49, 0x89, 0xC5});  // mov r13, rax
    out.jump(next_frame);
    out.place(too_many);
    out.lea_string(RAX, text.more_calls);
    out.jump(runtime.error_text);
    out.place(finished);
    out.bytes({0xC3});  // ret
}

// rax = a function's name. For --trace: writes it to the error stream on its own line, two spaces in for each Jamo call under it. Keeps rbx, rsi and rdi
void write_trace_call(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    const Label count_frames = out.new_label();
    const Label counted = out.new_label();
    const Label indent = out.new_label();
    const Label write_name = out.new_label();
    out.place(runtime.trace_call);
    out.bytes({0x55});  // push rbp
    out.bytes({0x48, 0x89, 0xE5});  // mov rbp, rsp
    out.bytes({0x53});  // push rbx
    out.bytes({0x56});  // push rsi
    out.bytes({0x57});  // push rdi
    out.bytes({0x48, 0x83, 0xEC, 0x08});  // sub rsp, 8
    out.bytes({0x48, 0x89, 0xC3});  // mov rbx, rax
    out.bytes({0x48, 0x8B, 0x75, 0x00});  // mov rsi, qword ptr [rbp]
    out.bytes({0x31, 0xFF});  // xor edi, edi
    out.lea_data(RAX, DataRef::Cleaner);
    out.bytes({0x4C, 0x8B, 0x40, 0x28});  // mov r8, qword ptr [rax+40]
    out.place(count_frames);
    out.bytes({0x48, 0x85, 0xF6});  // test rsi, rsi
    out.jump_if_equal(counted);
    out.bytes({0x4C, 0x39, 0xC6});  // cmp rsi, r8
    out.jump_if_at_least(counted);
    out.bytes({0x48, 0xFF, 0xC7});  // inc rdi
    out.bytes({0x48, 0x8B, 0x06});  // mov rax, qword ptr [rsi]
    out.bytes({0x48, 0x39, 0xF0});  // cmp rax, rsi
    out.jump_if_at_most(counted);
    out.bytes({0x48, 0x89, 0xC6});  // mov rsi, rax
    out.jump(count_frames);
    out.place(counted);
    out.bytes({0x48, 0xFF, 0xCF});  // dec rdi
    out.bytes({0x48, 0x83, 0xFF, 0x28});  // cmp rdi, 40
    out.jump_if_at_most(indent);
    out.bytes({0xBF, 0x28, 0x00, 0x00, 0x00});  // mov edi, 40
    out.place(indent);
    out.bytes({0x48, 0x85, 0xFF});  // test rdi, rdi
    out.jump_if_less_equal(write_name);
    out.lea_data(RAX, DataRef::Letters);
    out.bytes({0x48, 0x05, 0x00, 0x02, 0x00, 0x00});  // add rax, 512
    out.call(runtime.error_text);
    out.lea_data(RAX, DataRef::Letters);
    out.bytes({0x48, 0x05, 0x00, 0x02, 0x00, 0x00});  // add rax, 512
    out.call(runtime.error_text);
    out.bytes({0x48, 0xFF, 0xCF});  // dec rdi
    out.jump(indent);
    out.place(write_name);
    out.bytes({0x48, 0x89, 0xD8});  // mov rax, rbx
    out.call(runtime.error_text);
    out.lea_string(RAX, text.new_line);
    out.call(runtime.error_text);
    out.bytes({0x48, 0x8D, 0x65, 0xE8});  // lea rsp, [rbp-24]
    out.bytes({0x5F});  // pop rdi
    out.bytes({0x5E});  // pop rsi
    out.bytes({0x5B});  // pop rbx
    out.bytes({0x5D});  // pop rbp
    out.bytes({0xC3});  // ret
}

}  // namespace

Runtime make_runtime_labels(Assembler& out) {
    Runtime runtime;
    for (Label* label : {&runtime.exit, &runtime.write, &runtime.raw_alloc, &runtime.format_int, &runtime.make_string, &runtime.print_int, &runtime.int_to_string, &runtime.bool_to_string, &runtime.print_bool, &runtime.print_string, &runtime.join_strings, &runtime.join_many, &runtime.strings_equal, &runtime.error_text, &runtime.file_name_of, &runtime.run_error, &runtime.stop_with_error, &runtime.divide_by_zero, &runtime.out_of_memory, &runtime.format_float, &runtime.print_float, &runtime.float_to_string, &runtime.float_to_int, &runtime.float_mod, &runtime.builder_start, &runtime.builder_add, &runtime.builder_add_string, &runtime.builder_to_string, &runtime.builder_add_int, &runtime.index_error, &runtime.string_at, &runtime.new_array, &runtime.array_add, &runtime.array_pop, &runtime.array_address, &runtime.array_at, &runtime.builder_add_float, &runtime.builder_add_bool, &runtime.builder_add_quoted, &runtime.builder_print, &runtime.show_enter, &runtime.show_leave, &runtime.text_error, &runtime.string_to_int, &runtime.big_times_ten, &runtime.big_double, &runtime.big_compare, &runtime.big_subtract, &runtime.string_to_float, &runtime.string_to_float_body, &runtime.big_subtract_clear, &runtime.to_wide, &runtime.to_wide_body, &runtime.read_whole_file, &runtime.read_whole_file_body, &runtime.write_whole_file, &runtime.write_whole_file_body, &runtime.file_exists, &runtime.stdin_refill, &runtime.read_line, &runtime.builder_add_byte, &runtime.builder_add_letter, &runtime.get_args, &runtime.get_args_body, &runtime.add_backslash_run, &runtime.raw_free, &runtime.alloc, &runtime.alloc_text, &runtime.alloc_block, &runtime.alloc_again, &runtime.alloc_take, &runtime.alloc_slow, &runtime.cleaner_start, &runtime.cleaner_verify, &runtime.cleaner_add_to_table, &runtime.cleaner_mark_word, &runtime.cleaner_clean, &runtime.show_calls, &runtime.trace_call}) *label = out.new_label();
    return runtime;
}

void write_runtime(Assembler& out, const Runtime& runtime, const RuntimeText& text) {
    write_exit(out, runtime, text);
    write_write(out, runtime, text);
    write_raw_alloc(out, runtime, text);
    write_format_int(out, runtime, text);
    write_make_string(out, runtime, text);
    write_print_int(out, runtime, text);
    write_int_to_string(out, runtime, text);
    write_bool_to_string(out, runtime, text);
    write_print_bool(out, runtime, text);
    write_print_string(out, runtime, text);
    write_join_strings(out, runtime, text);
    write_join_many(out, runtime, text);
    write_strings_equal(out, runtime, text);
    write_error_text(out, runtime, text);
    write_file_name_of(out, runtime, text);
    write_run_error(out, runtime, text);
    write_stop_with_error(out, runtime, text);
    write_divide_by_zero(out, runtime, text);
    write_out_of_memory(out, runtime, text);
    write_format_float(out, runtime, text);
    write_print_float(out, runtime, text);
    write_float_to_string(out, runtime, text);
    write_float_to_int(out, runtime, text);
    write_float_mod(out, runtime, text);
    write_builder_start(out, runtime, text);
    write_builder_add(out, runtime, text);
    write_builder_add_string(out, runtime, text);
    write_builder_to_string(out, runtime, text);
    write_builder_add_int(out, runtime, text);
    write_index_error(out, runtime, text);
    write_string_at(out, runtime, text);
    write_new_array(out, runtime, text);
    write_array_add(out, runtime, text);
    write_array_pop(out, runtime, text);
    write_array_address(out, runtime, text);
    write_array_at(out, runtime, text);
    write_builder_add_float(out, runtime, text);
    write_builder_add_bool(out, runtime, text);
    write_builder_add_quoted(out, runtime, text);
    write_builder_print(out, runtime, text);
    write_show_enter(out, runtime, text);
    write_show_leave(out, runtime, text);
    write_text_error(out, runtime, text);
    write_string_to_int(out, runtime, text);
    write_big_times_ten(out, runtime, text);
    write_big_double(out, runtime, text);
    write_big_compare(out, runtime, text);
    write_big_subtract(out, runtime, text);
    write_string_to_float(out, runtime, text);
    write_string_to_float_body(out, runtime, text);
    write_big_subtract_clear(out, runtime, text);
    write_to_wide(out, runtime, text);
    write_to_wide_body(out, runtime, text);
    write_read_whole_file(out, runtime, text);
    write_read_whole_file_body(out, runtime, text);
    write_write_whole_file(out, runtime, text);
    write_write_whole_file_body(out, runtime, text);
    write_file_exists(out, runtime, text);
    write_stdin_refill(out, runtime, text);
    write_read_line(out, runtime, text);
    write_builder_add_byte(out, runtime, text);
    write_builder_add_letter(out, runtime, text);
    write_get_args(out, runtime, text);
    write_get_args_body(out, runtime, text);
    write_add_backslash_run(out, runtime, text);
    write_raw_free(out, runtime, text);
    write_alloc(out, runtime, text);
    write_alloc_text(out, runtime, text);
    write_alloc_block(out, runtime, text);
    write_alloc_again(out, runtime, text);
    write_alloc_take(out, runtime, text);
    write_alloc_slow(out, runtime, text);
    write_cleaner_start(out, runtime, text);
    write_cleaner_verify(out, runtime, text);
    write_cleaner_add_to_table(out, runtime, text);
    write_cleaner_mark_word(out, runtime, text);
    write_cleaner_clean(out, runtime, text);
    write_show_calls(out, runtime, text);
    write_trace_call(out, runtime, text);
}
