@ exit rcx = exit code. A window program that collected a run error shows it in a message box first, since it has no console
    ! out.lea_data(RAX, DataRef::Cleaner);
    test byte ptr [rax+32], 16
    je plain_exit
    ! out.lea_data(RAX, DataRef::ErrorText);
    cmp qword ptr [rax], 0
    je plain_exit
    mov rbx, rcx
    and rsp, -16
    sub rsp, 32
    ! out.lea_string(RCX, text.user32_dll);
    add rcx, 8
    ! out.call_windows(DataRef::LoadLibraryA);
    test rax, rax
    je box_done
    mov rcx, rax
    ! out.lea_string(RDX, text.message_box);
    add rdx, 8
    ! out.call_windows(DataRef::GetProcAddress);
    test rax, rax
    je box_done
    mov r11, rax
    xor ecx, ecx
    ! out.lea_data(RDX, DataRef::ErrorText);
    add rdx, 8
    ! out.lea_string(R8, text.error_title);
    add r8, 8
    mov r9d, 16
    ! out.call_r11();
box_done:
    mov rcx, rbx
plain_exit:
    and rsp, -16
    sub rsp, 32
    ! out.call_windows(DataRef::ExitProcess);

@ write rcx = which stream, rdx = the text, r8 = its length. Keeps rbx, rsi and rdi for its callers. A window program has no console, so what goes to the error stream is kept for exit's message box, up to 4095 bytes
    cmp rcx, -12
    jne to_stream
    ! out.lea_data(R9, DataRef::Cleaner);
    test byte ptr [r9+32], 16
    je to_stream
    push rsi
    push rdi
    ! out.lea_data(R9, DataRef::ErrorText);
    mov rdi, qword ptr [r9]
    mov rcx, 4095
    sub rcx, rdi
    cmp rcx, r8
    cmova rcx, r8
    add qword ptr [r9], rcx
    lea rdi, [r9+rdi+8]
    mov rsi, rdx
    rep movsb
    pop rdi
    pop rsi
    ret
to_stream:
    push rbp
    mov rbp, rsp
    push rbx
    push rsi
    push rdi
    mov rbx, rdx
    mov rsi, r8
    and rsp, -16
    sub rsp, 48
    ! out.call_windows(DataRef::GetStdHandle);
    mov rdi, rax
    mov rcx, rdi
    lea rdx, [rsp+40]
    ! out.call_windows(DataRef::GetConsoleMode);
    test eax, eax
    jne console
next_part:
    test rsi, rsi
    je finished
    mov r8, rsi
    mov eax, 0x40000000
    cmp r8, rax
    cmova r8, rax
    mov rcx, rdi
    mov rdx, rbx
    lea r9, [rsp+40]
    mov qword ptr [rsp+32], 0
    ! out.call_windows(DataRef::WriteFile);
    test eax, eax
    je finished
    mov eax, dword ptr [rsp+40]
    test rax, rax
    je finished
    add rbx, rax
    sub rsi, rax
    jmp next_part
console:
    sub rsp, 2064
console_part:
    test rsi, rsi
    je finished
    mov r8, rsi
    mov eax, 1000
    cmp r8, rax
    jbe whole_letters
    mov r8, rax
back_off:
    movzx eax, byte ptr [rbx+r8]
    and eax, 192
    cmp eax, 128
    jne whole_letters
    dec r8
    jne back_off
    mov r8d, 1000
whole_letters:
    mov qword ptr [rsp+2056], r8
    mov ecx, 65001
    xor edx, edx
    mov r9, r8
    mov r8, rbx
    lea rax, [rsp+48]
    mov qword ptr [rsp+32], rax
    mov qword ptr [rsp+40], 1000
    ! out.call_windows(DataRef::MultiByteToWideChar);
    mov rcx, rdi
    lea rdx, [rsp+48]
    mov r8d, eax
    lea r9, [rsp+2048]
    mov qword ptr [rsp+32], 0
    ! out.call_windows(DataRef::WriteConsoleW);
    mov rax, qword ptr [rsp+2056]
    add rbx, rax
    sub rsi, rax
    jmp console_part
finished:
    lea rsp, [rbp-24]
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret

@ raw_alloc rcx = bytes wanted, for the text builder and the like, which the cleaner never touches. Keeps rbx, rsi and rdi
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 32
    mov r8, rcx
    xor edx, edx
    ! out.load_rcx_data(DataRef::HeapHandle);
    ! out.call_windows(DataRef::HeapAlloc);
    test rax, rax
    je out_of_memory
    leave
    ret

@ format_int rax = the int, rdi = where the text must end. Answers with its start in rdx, building it backward
    mov r10, rdi
    xor r11d, r11d
    test rax, rax
    jns positive
    mov r11d, 1
    neg rax
positive:
    mov ecx, 10
next_digit:
    xor edx, edx
    div rcx
    add dl, 48
    dec r10
    mov byte ptr [r10], dl
    test rax, rax
    jne next_digit
    test r11, r11
    je no_sign
    dec r10
    mov byte ptr [r10], 45
no_sign:
    mov rdx, r10
    ret

@ make_string rdx = the text, r8 = its length. Answers with a new string in rax
    mov rbx, rdx
    mov rsi, r8
    lea rcx, [r8+9]
    call alloc_text
    mov r9, rax
    mov qword ptr [r9], rsi
    lea rdi, [r9+8]
    mov rcx, rsi
    mov rsi, rbx
    rep movsb
    mov byte ptr [rdi], 10
    mov rax, r9
    ret

@ print_int rax = the int
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov byte ptr [rbp-1], 10
    lea rdi, [rbp-1]
    call format_int
    mov r8, rbp
    sub r8, rdx
    mov rcx, -11
    call write
    leave
    ret

@ int_to_string rax = the int
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov rdi, rbp
    call format_int
    mov r8, rbp
    sub r8, rdx
    call make_string
    leave
    ret

@ bool_to_string rax = the bool. Answers with the shared true or false string
    test rax, rax
    je is_false
    ! out.lea_rax_string(text.true_word);
    ret
is_false:
    ! out.lea_rax_string(text.false_word);
    ret

@ print_bool rax = the bool
    call bool_to_string
    jmp print_string

@ print_string rax = the string. Every string keeps a hidden new line just past its end
    lea rdx, [rax+8]
    mov r8, qword ptr [rax]
    inc r8
    mov rcx, -11
    jmp write

@ join_strings rax = the left string, rcx = the right string
    mov rbx, rax
    mov rsi, rcx
    mov rcx, qword ptr [rbx]
    add rcx, qword ptr [rsi]
    add rcx, 9
    call alloc_text
    mov r8, rax
    mov rcx, qword ptr [rbx]
    add rcx, qword ptr [rsi]
    mov qword ptr [r8], rcx
    lea rdi, [r8+8]
    mov rdx, rsi
    lea rsi, [rbx+8]
    mov rcx, qword ptr [rbx]
    rep movsb
    lea rsi, [rdx+8]
    mov rcx, qword ptr [rdx]
    rep movsb
    mov byte ptr [rdi], 10
    mov rax, r8
    ret

@ join_many rcx = how many strings, 2 or more, pushed in order before the call. Answers with them joined, made once. The caller takes them off the stack
    mov rbx, rcx
    xor ecx, ecx
    lea rsi, [rsp+8]
    mov rdx, rbx
add_length:
    mov rax, qword ptr [rsi]
    add rcx, qword ptr [rax]
    add rsi, 8
    dec rdx
    jne add_length
    mov r8, rcx
    add rcx, 9
    call alloc_text
    mov qword ptr [rax], r8
    lea rdi, [rax+8]
    lea rdx, [rsp+rbx*8]
copy_part:
    mov rsi, qword ptr [rdx]
    mov rcx, qword ptr [rsi]
    add rsi, 8
    rep movsb
    sub rdx, 8
    dec rbx
    jne copy_part
    mov byte ptr [rdi], 10
    ret

@ strings_equal rax = one string, rcx = the other. Answers with a bool. The same string is equal to itself without looking
    cmp rax, rcx
    je same
    mov rsi, rax
    mov rdi, rcx
    mov rcx, qword ptr [rsi]
    cmp rcx, qword ptr [rdi]
    jne different
    add rsi, 8
    add rdi, 8
    xor eax, eax
    repe cmpsb
    sete al
    ret
same:
    mov eax, 1
    ret
different:
    xor eax, eax
    ret

@ error_text rax = the string, written to the error stream with no new line
    lea rdx, [rax+8]
    mov r8, qword ptr [rax]
    mov rcx, -12
    jmp write

@ file_name_of rax = which file. Answers with its name
    ! for (size_t i = 1; i < text.file_names.size(); i++) {
    !     const Label next = out.new_label();
    !     out.mov_rcx_number(static_cast<int>(i));
    !     out.cmp_rax_rcx();
    !     out.jump_if_not_equal(next);
    !     out.lea_rax_string(text.file_names[i]);
    !     out.bytes({0xC3});
    !     out.place(next);
    ! }
    ! out.lea_rax_string(text.file_names[0]);
    ret

@ run_error rax = the message. With a try open, goes back to that try's rbp and rsp and jumps to its catch with the message still in rax, making the try around it the newest. Otherwise shows the file and line, then stops with exit code 1
    ! out.lea_data(RCX, DataRef::TryFrame);
    mov rdx, qword ptr [rcx]
    test rdx, rdx
    je not_caught
    mov r8, qword ptr [rdx]
    mov qword ptr [rcx], r8
    mov rbp, qword ptr [rdx+8]
    mov rsp, qword ptr [rdx+16]
    push qword ptr [rdx+24]
    ret
not_caught:
    jmp stop_with_error

@ stop_with_error rax = the message. Shows the file and line, then stops with exit code 1, even with a try open
    mov rbx, rax
    ! out.load_rax_data(DataRef::CurrentLine);
    mov r12, rax
    shr rax, 32
    call file_name_of
    call error_text
    ! out.lea_rax_string(text.line_word);
    call error_text
    sub rsp, 48
    mov eax, r12d
    lea rdi, [rsp+48]
    call format_int
    lea r8, [rsp+48]
    sub r8, rdx
    mov rcx, -12
    call write
    ! out.lea_rax_string(text.error_word);
    call error_text
    lea rdx, [rbx+8]
    mov r8, qword ptr [rbx]
    inc r8
    mov rcx, -12
    call write
    call show_calls
    mov ecx, 1
    jmp exit

@ divide_by_zero
    ! out.lea_rax_string(text.divide_by_zero);
    jmp run_error

@ out_of_memory Can't be caught, since it can happen half way through the memory cleaner's work
    ! out.lea_rax_string(text.out_of_memory);
    jmp stop_with_error

@ format_float rax = the float, rdi = where the text must end. Answers with its start in rdx, building it backward. Rounds exactly, like %.6f, using whole numbers only
    mov r8, rax
    mov rbx, rax
    btr rbx, 63
    movabs rax, 0x7FF0000000000000
    cmp rbx, rax
    jae not_finite
    movabs rax, 0x4341C37937E08000
    cmp rbx, rax
    jae scientific
    xor r9d, r9d
    xor esi, esi
    mov rcx, rbx
    shr rcx, 52
    cmp rcx, 1002
    jb write_fixed
    mov rax, rbx
    movabs rdx, 0x000FFFFFFFFFFFFF
    and rax, rdx
    bts rax, 52
    sub rcx, 1075
    jl has_fraction
    shl rax, cl
    mov r9, rax
    jmp write_fixed
has_fraction:
    neg rcx
    cmp rcx, 64
    jae only_fraction
    mov r9, rax
    shr r9, cl
    mov edx, 1
    shl rdx, cl
    dec rdx
    and rax, rdx
only_fraction:
    mov r10, rcx
    mov edx, 1000000
    mul rdx
    cmp r10, 64
    jae long_shift
    lea rcx, [r10-1]
    mov r11d, 1
    shl r11, cl
    add rax, r11
    adc rdx, 0
    mov rcx, r10
    mov rsi, rax
    shrd rsi, rdx, cl
    mov r11d, 1
    shl r11, cl
    dec r11
    test rax, r11
    jne carry_check
    and rsi, -2
    jmp carry_check
long_shift:
    lea rcx, [r10-64]
    test rcx, rcx
    jne half_is_high
    movabs r11, 0x8000000000000000
    add rax, r11
    adc rdx, 0
    jmp half_added
half_is_high:
    lea rcx, [r10-65]
    mov r11d, 1
    shl r11, cl
    add rdx, r11
half_added:
    lea rcx, [r10-64]
    mov rsi, rdx
    shr rsi, cl
    test rax, rax
    jne carry_check
    mov r11d, 1
    shl r11, cl
    dec r11
    test rdx, r11
    jne carry_check
    and rsi, -2
carry_check:
    cmp rsi, 1000000
    jne write_fixed
    inc r9
    xor esi, esi
write_fixed:
    mov r10, rdi
    test rsi, rsi
    jne has_decimals
    dec r10
    mov byte ptr [r10], 48
    jmp write_dot
has_decimals:
    mov r11d, 6
    mov ecx, 10
next_trim:
    mov rax, rsi
    xor edx, edx
    div rcx
    test rdx, rdx
    jne next_place
    mov rsi, rax
    dec r11
    jmp next_trim
next_place:
    mov rax, rsi
    xor edx, edx
    div rcx
    mov rsi, rax
    add dl, 48
    dec r10
    mov byte ptr [r10], dl
    dec r11
    jne next_place
write_dot:
    dec r10
    mov byte ptr [r10], 46
    mov rdi, r10
    mov rax, r9
    call format_int
add_sign:
    test r8, r8
    jns all_done
    dec rdx
    mov byte ptr [rdx], 45
all_done:
    ret
not_finite:
    lea rdx, [rdi-3]
    movabs rax, 0x7FF0000000000000
    cmp rbx, rax
    je is_infinite
    mov byte ptr [rdx], 110
    mov byte ptr [rdx+1], 97
    mov byte ptr [rdx+2], 110
    ret
is_infinite:
    mov byte ptr [rdx], 105
    mov byte ptr [rdx+1], 110
    mov byte ptr [rdx+2], 102
    jmp add_sign
scientific:
    push rbp
    mov rbp, rsp
    sub rsp, 512
    mov r9, rdi
    lea rdi, [rsp]
    xor eax, eax
    mov ecx, 16
    rep stosq
    mov rcx, rbx
    shr rcx, 52
    sub rcx, 1075
    mov rax, rbx
    movabs rdx, 0x000FFFFFFFFFFFFF
    and rax, rdx
    bts rax, 52
    mov rdx, rcx
    shr rdx, 6
    and ecx, 63
    xor r10d, r10d
    shld r10, rax, cl
    shl rax, cl
    mov qword ptr [rsp+rdx*8], rax
    mov qword ptr [rsp+rdx*8+8], r10
    lea rdi, [rsp+512]
    mov r10d, 10
next_big_digit:
    mov ecx, 15
    xor edx, edx
    xor r11d, r11d
next_word:
    mov rax, qword ptr [rsp+rcx*8]
    div r10
    mov qword ptr [rsp+rcx*8], rax
    or r11, rax
    dec rcx
    jns next_word
    add dl, 48
    dec rdi
    mov byte ptr [rdi], dl
    test r11, r11
    jne next_big_digit
    mov rsi, rdi
    lea r10, [rsp+511]
    sub r10, rsi
    mov al, byte ptr [rsi+7]
    cmp al, 53
    ja round_up
    jb rounded
    lea rdi, [rsi+8]
    lea rcx, [rsp+512]
check_rest:
    cmp rdi, rcx
    jae is_tie
    cmp byte ptr [rdi], 48
    jne round_up
    inc rdi
    jmp check_rest
is_tie:
    test byte ptr [rsi+6], 1
    je rounded
round_up:
    mov ecx, 6
next_carry:
    cmp byte ptr [rsi+rcx], 57
    jne plain_increase
    mov byte ptr [rsi+rcx], 48
    dec rcx
    jns next_carry
    mov byte ptr [rsi], 49
    inc r10
    jmp rounded
plain_increase:
    inc byte ptr [rsi+rcx]
rounded:
    mov rdi, r9
    mov rax, r10
    call format_int
    dec rdx
    mov byte ptr [rdx], 43
    dec rdx
    mov byte ptr [rdx], 101
    mov ecx, 6
trim_zero:
    cmp rcx, 1
    je copy_digit
    cmp byte ptr [rsi+rcx], 48
    jne copy_digit
    dec rcx
    jmp trim_zero
copy_digit:
    mov al, byte ptr [rsi+rcx]
    dec rdx
    mov byte ptr [rdx], al
    dec rcx
    jne copy_digit
    dec rdx
    mov byte ptr [rdx], 46
    mov al, byte ptr [rsi]
    dec rdx
    mov byte ptr [rdx], al
    leave
    jmp add_sign

@ print_float rax = the float
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov byte ptr [rbp-1], 10
    lea rdi, [rbp-1]
    call format_float
    mov r8, rbp
    sub r8, rdx
    mov rcx, -11
    call write
    leave
    ret

@ float_to_string rax = the float
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov rdi, rbp
    call format_float
    mov r8, rbp
    sub r8, rdx
    call make_string
    leave
    ret

@ float_to_int rax = the float. The processor answers with the smallest int when a float doesn't fit
    movq xmm0, rax
    cvttsd2si rax, xmm0
    movabs rcx, 0x8000000000000000
    cmp rax, rcx
    jne fits
    movabs rcx, 0xC3E0000000000000
    movq xmm1, rcx
    ucomisd xmm0, xmm1
    jp too_large
    je fits
too_large:
    ! out.lea_rax_string(text.float_too_large);
    jmp run_error
fits:
    ret

@ float_mod rax = the left float, rcx = the right float. The remainder keeps the left side's sign, like % on ints
    push rcx
    fld qword ptr [rsp]
    mov qword ptr [rsp], rax
    fld qword ptr [rsp]
next_step:
    fprem
    fnstsw ax
    test ah, 4
    jne next_step
    fstp st(1)
    fstp qword ptr [rsp]
    pop rax
    ret

@ builder_start The one text builder: its pointer, length and room. Clears it for new text
    ! out.lea_data(RAX, DataRef::Builder);
    mov qword ptr [rax+8], 0
    ret

@ builder_add rdx = the text, r8 = its length. Keeps rbx, rsi and rdi
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    ! out.lea_data(RBX, DataRef::Builder);
    mov rsi, rdx
    mov r12, r8
    mov rax, qword ptr [rbx+8]
    add rax, r12
    cmp rax, qword ptr [rbx+16]
    jbe has_room
    lea rcx, [rax+rax]
    cmp rcx, 256
    jae big_enough
    mov ecx, 256
big_enough:
    mov qword ptr [rbx+16], rcx
    call raw_alloc
    mov rdi, rax
    mov r8, rsi
    mov rsi, qword ptr [rbx]
    mov r13, rsi
    mov qword ptr [rbx], rdi
    mov rcx, qword ptr [rbx+8]
    rep movsb
    mov rsi, r8
    mov rcx, r13
    call raw_free
has_room:
    mov rdi, qword ptr [rbx]
    add rdi, qword ptr [rbx+8]
    mov rcx, r12
    rep movsb
    add qword ptr [rbx+8], r12
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

@ builder_add_string rax = the string
    lea rdx, [rax+8]
    mov r8, qword ptr [rax]
    jmp builder_add

@ builder_to_string Answers with the built text as a new string
    ! out.lea_data(RAX, DataRef::Builder);
    mov rdx, qword ptr [rax]
    mov r8, qword ptr [rax+8]
    jmp make_string

@ builder_add_int rax = the int. Keeps rbx, rsi, r12 and r13
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov rdi, rbp
    call format_int
    mov r8, rbp
    sub r8, rdx
    call builder_add
    leave
    ret

@ index_error rax = the index, rcx = the size, rdx = the words after the index, r8 = the words after the size
    cmp rcx, 1
    jne words_chosen
    ! out.lea_string(R9, text.items_word);
    cmp r8, r9
    jne not_items
    ! out.lea_string(R8, text.item_word);
    jmp words_chosen
not_items:
    ! out.lea_string(R9, text.bytes_word);
    cmp r8, r9
    jne words_chosen
    ! out.lea_string(R8, text.byte_word);
words_chosen:
    mov rbx, rax
    mov rsi, rcx
    mov r12, rdx
    mov r13, r8
    call builder_start
    ! out.lea_string(RAX, text.index_word);
    call builder_add_string
    mov rax, rbx
    call builder_add_int
    mov rax, r12
    call builder_add_string
    mov rax, rsi
    call builder_add_int
    mov rax, r13
    call builder_add_string
    call builder_to_string
    jmp run_error

@ string_at rax = the string, rcx = the index. Answers with that byte's one-letter string, which is shared, since text never changes
    cmp rcx, qword ptr [rax]
    jae outside
    movzx eax, byte ptr [rax+rcx+8]
    shl eax, 4
    ! out.lea_data(RDX, DataRef::Letters);
    add rax, rdx
    ret
outside:
    mov rdx, rcx
    mov rcx, qword ptr [rax]
    mov rax, rdx
    ! out.lea_string(RDX, text.outside_string);
    ! out.lea_string(R8, text.bytes_word);
    jmp index_error

@ new_array rcx = how many items. An array is its length, its room and a pointer to its items, so it can grow and stay shared. An empty one gets its items on its first add, since most never get any
    mov rbx, rcx
    mov ecx, 24
    call alloc
    mov rsi, rax
    mov rcx, rbx
    test rcx, rcx
    je stays_empty
    cmp rcx, 4
    jae enough_room
    mov ecx, 4
enough_room:
    mov rdi, rcx
    shl rcx, 3
    call alloc
    mov qword ptr [rsi+16], rax
    mov qword ptr [rsi+8], rdi
    mov qword ptr [rsi], rbx
    mov rax, rsi
    ret
stays_empty:
    mov rax, rsi
    ret

@ array_add rax = the array, rcx = the new item
    mov rbx, rax
    mov rsi, rcx
    mov rax, qword ptr [rbx]
    cmp rax, qword ptr [rbx+8]
    jb has_room
    mov rcx, qword ptr [rbx+8]
    test rcx, rcx
    jne has_items
    mov qword ptr [rbx+8], 2
    mov ecx, 2
has_items:
    shl rcx, 4
    call alloc
    mov rdi, rax
    shl qword ptr [rbx+8], 1
    mov r8, rsi
    mov rsi, qword ptr [rbx+16]
    mov qword ptr [rbx+16], rdi
    mov rcx, qword ptr [rbx]
    rep movsq
    mov rsi, r8
has_room:
    mov rax, qword ptr [rbx]
    mov rdx, qword ptr [rbx+16]
    mov qword ptr [rdx+rax*8], rsi
    inc qword ptr [rbx]
    ret

@ array_pop rax = the array. Answers with its last item
    mov rcx, qword ptr [rax]
    test rcx, rcx
    je is_empty
    dec rcx
    mov qword ptr [rax], rcx
    mov rdx, qword ptr [rax+16]
    mov rax, qword ptr [rdx+rcx*8]
    mov qword ptr [rdx+rcx*8], 0
    ret
is_empty:
    ! out.lea_string(RAX, text.pop_empty);
    jmp run_error

@ array_address rax = the array, rcx = the index. Answers with where that item lives, and keeps r9
    cmp rcx, qword ptr [rax]
    jae outside
    mov rax, qword ptr [rax+16]
    lea rax, [rax+rcx*8]
    ret
outside:
    mov rdx, rcx
    mov rcx, qword ptr [rax]
    mov rax, rdx
    ! out.lea_string(RDX, text.outside_array);
    ! out.lea_string(R8, text.items_word);
    jmp index_error

@ array_at rax = the array, rcx = the index. Answers with that item
    call array_address
    mov rax, qword ptr [rax]
    ret

@ builder_add_float rax = the float
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov rdi, rbp
    call format_float
    mov r8, rbp
    sub r8, rdx
    call builder_add
    leave
    ret

@ builder_add_bool rax = the bool
    call bool_to_string
    jmp builder_add_string

@ builder_add_quoted rax = the string, shown inside an array or struct
    mov rbx, rax
    ! out.lea_string(RAX, text.quote);
    call builder_add_string
    mov rax, rbx
    call builder_add_string
    ! out.lea_string(RAX, text.quote);
    jmp builder_add_string

@ builder_print Writes the built text and a new line
    ! out.lea_string(RAX, text.new_line);
    call builder_add_string
    ! out.lea_data(RAX, DataRef::Builder);
    mov rdx, qword ptr [rax]
    mov r8, qword ptr [rax+8]
    mov rcx, -11
    jmp write

@ show_enter rax = the array or struct being shown. Answers with 1 if it's already being shown, so a loop of items can't go on forever. Otherwise links it in, using [rbp-16] and [rbp-24] of its caller
    ! out.lea_data(RDX, DataRef::VisitHead);
    mov rcx, qword ptr [rdx]
next_visit:
    test rcx, rcx
    je not_found
    cmp qword ptr [rcx+8], rax
    je found
    mov rcx, qword ptr [rcx]
    jmp next_visit
found:
    mov eax, 1
    ret
not_found:
    mov qword ptr [rbp-16], rax
    mov rcx, qword ptr [rdx]
    mov qword ptr [rbp-24], rcx
    lea rcx, [rbp-24]
    mov qword ptr [rdx], rcx
    xor eax, eax
    ret

@ show_leave Unlinks what show_enter linked in
    ! out.lea_data(RDX, DataRef::VisitHead);
    mov rax, qword ptr [rbp-24]
    mov qword ptr [rdx], rax
    ret

@ text_error rax = some text from the program, rdx = the words before it, r8 = the words after it
    mov rbx, rax
    mov r12, rdx
    mov r13, r8
    call builder_start
    mov rax, r12
    call builder_add_string
    mov rax, rbx
    call builder_add_string
    mov rax, r13
    call builder_add_string
    call builder_to_string
    jmp run_error

@ string_to_int rax = the text. Takes digits with an optional minus sign, and nothing else
    mov rbx, rax
    lea rsi, [rax+8]
    mov rcx, qword ptr [rax]
    test rcx, rcx
    je bad_number
    xor edi, edi
    xor r8d, r8d
    mov r9d, 10
    cmp byte ptr [rsi], 45
    jne next_digit
    mov edi, 1
    inc rsi
    dec rcx
    je bad_number
next_digit:
    movzx eax, byte ptr [rsi]
    sub eax, 48
    cmp eax, 9
    ja bad_number
    mov r10, rax
    mov rax, r8
    mul r9
    jb bad_number
    add rax, r10
    jb bad_number
    mov r8, rax
    inc rsi
    dec rcx
    jne next_digit
    movabs rax, 0x8000000000000000
    cmp r8, rax
    ja bad_number
    jne fits
    test edi, edi
    je bad_number
fits:
    mov rax, r8
    test edi, edi
    je positive
    neg rax
positive:
    ret
bad_number:
    mov rax, rbx
    ! out.lea_string(RDX, text.cant_turn);
    ! out.lea_string(R8, text.into_int);
    jmp text_error

@ big_times_ten rdi = a whole number of 128 words, rax = what to add afterward
    mov r11, rax
    xor ecx, ecx
next_word:
    mov rax, qword ptr [rdi+rcx*8]
    mov edx, 10
    mul rdx
    add rax, r11
    adc rdx, 0
    mov qword ptr [rdi+rcx*8], rax
    mov r11, rdx
    inc rcx
    cmp rcx, 128
    jb next_word
    ret

@ big_double rdi = a whole number of 128 words
    mov ecx, 128
    xor eax, eax
next_word:
    rcl qword ptr [rdi], 1
    lea rdi, [rdi+8]
    dec rcx
    jne next_word
    ret

@ big_compare rdi and rsi = two whole numbers of 128 words. Answers with less than, equal to or more than 0 in rax
    mov ecx, 127
next_word:
    mov rax, qword ptr [rdi+rcx*8]
    cmp rax, qword ptr [rsi+rcx*8]
    jne different
    dec rcx
    jns next_word
    xor eax, eax
    ret
different:
    sbb rax, rax
    or rax, 1
    ret

@ big_subtract rdi = a whole number of 128 words, which rsi is taken away from
    mov ecx, 128
    xor edx, edx
next_word:
    mov rax, qword ptr [rsi+rdx*8]
    sbb qword ptr [rdi+rdx*8], rax
    inc rdx
    dec rcx
    jne next_word
    ret

@ string_to_float Keeps r12 to r15, where Jamo code may hold variables, around string_to_float_body
    push r12
    push r13
    push r14
    push r15
    call string_to_float_body
    pop r15
    pop r14
    pop r13
    pop r12
    ret

@ string_to_float_body rax = the text. Rounds exactly, using whole numbers only. [rsp] holds the top of a fraction, and [rsp+1024] the bottom
    push rbp
    mov rbp, rsp
    sub rsp, 2048
    or qword ptr [rsp], 0
    mov rbx, rax
    lea rdi, [rsp]
    xor eax, eax
    mov ecx, 256
    rep stosq
    lea rsi, [rbx+8]
    mov r8, qword ptr [rbx]
    test r8, r8
    je bad_number
    xor r15d, r15d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r9d, r9d
    xor r10d, r10d
    cmp byte ptr [rsi], 45
    jne next_char
    mov r15d, 1
    inc rsi
    dec r8
    je bad_number
next_char:
    test r8, r8
    je all_read
    movzx eax, byte ptr [rsi]
    cmp al, 46
    je found_dot
    sub eax, 48
    cmp eax, 9
    ja bad_number
    inc r9
    test r13, r13
    jne is_significant
    test eax, eax
    je digit_counted
is_significant:
    cmp r13, 800
    jae too_many_digits
    lea rdi, [rsp]
    call big_times_ten
    inc r13
digit_counted:
    test r10, r10
    je char_done
    dec r12
    jmp char_done
too_many_digits:
    or r14, rax
    test r10, r10
    jne char_done
    inc r12
char_done:
    inc rsi
    dec r8
    jmp next_char
found_dot:
    test r10, r10
    jne bad_number
    test r9, r9
    je bad_number
    mov r10d, 1
    inc rsi
    dec r8
    je bad_number
    jmp next_char
all_read:
    xor eax, eax
    test r13, r13
    je has_answer
    lea rax, [r12+r13]
    cmp rax, 310
    jg bad_number
    cmp rax, -330
    jl bad_number
    mov qword ptr [rsp+1024], 1
scale_top:
    test r12, r12
    jle scale_bottom
    lea rdi, [rsp]
    xor eax, eax
    call big_times_ten
    dec r12
    jmp scale_top
scale_bottom:
    test r12, r12
    jge scaled
    lea rdi, [rsp+1024]
    xor eax, eax
    call big_times_ten
    inc r12
    jmp scale_bottom
scaled:
    mov r13d, 53
    mov r12d, 53
shift_bottom:
    lea rdi, [rsp+1024]
    call big_double
    dec r12
    jne shift_bottom
bottom_too_small:
    lea rdi, [rsp]
    lea rsi, [rsp+1024]
    call big_compare
    test rax, rax
    js top_too_small
    lea rdi, [rsp+1024]
    call big_double
    inc r13
    jmp bottom_too_small
top_too_small:
    lea rdi, [rsp]
    lea rsi, [rsp+1024]
    call big_compare
    test rax, rax
    jns in_range
    lea rdi, [rsp]
    call big_double
    dec r13
    jmp top_too_small
in_range:
    cmp r13, 1023
    jg bad_number
    mov r9d, 53
    cmp r13, -1022
    jge has_precision
    lea r9, [r13+1075]
    test r9, r9
    js bad_number
has_precision:
    lea r12, [r9+1]
    xor r10d, r10d
next_bit:
    shl r10, 1
    lea rdi, [rsp]
    lea rsi, [rsp+1024]
    call big_compare
    test rax, rax
    js bit_done
    lea rdi, [rsp]
    lea rsi, [rsp+1024]
    clc
    call big_subtract_clear
    or r10, 1
bit_done:
    lea rdi, [rsp]
    call big_double
    dec r12
    jne next_bit
    mov ecx, 127
    xor eax, eax
check_rest:
    or rax, qword ptr [rsp+rcx*8]
    dec rcx
    jns check_rest
    or r14, rax
    mov r11, r10
    and r11, 1
    shr r10, 1
    test r11, r11
    je rounded
    test r14, r14
    jne round_up
    test r10, 1
    je rounded
round_up:
    inc r10
rounded:
    mov rax, r10
    cmp r13, -1022
    jl has_answer
    lea rcx, [r13+1022]
    shl rcx, 52
    add rax, rcx
    movabs rcx, 0x7FF0000000000000
    cmp rax, rcx
    jae bad_number
has_answer:
    shl r15, 63
    or rax, r15
    leave
    ret
bad_number:
    mov rax, rbx
    ! out.lea_string(RDX, text.cant_turn);
    ! out.lea_string(R8, text.into_float);
    jmp text_error

@ big_subtract_clear The same as big_subtract, starting with nothing borrowed
    xor edx, edx
    mov ecx, 128
    xor eax, eax
next_word:
    mov rax, qword ptr [rsi+rdx*8]
    sbb qword ptr [rdi+rdx*8], rax
    inc rdx
    dec rcx
    jne next_word
    ret

@ to_wide Keeps r12 to r15, where Jamo code may hold variables, around to_wide_body
    push r12
    push r13
    push r14
    push r15
    call to_wide_body
    pop r15
    pop r14
    pop r13
    pop r12
    ret

@ to_wide_body rax = a string. Answers with its text the way Windows' file functions want it. Keeps rbx and rsi
    push rbp
    mov rbp, rsp
    push rbx
    push rsi
    and rsp, -16
    sub rsp, 48
    mov rbx, rax
    mov ecx, 65001
    xor edx, edx
    lea r8, [rbx+8]
    mov r9, qword ptr [rbx]
    mov qword ptr [rsp+32], 0
    mov qword ptr [rsp+40], 0
    ! out.call_windows(DataRef::MultiByteToWideChar);
    mov esi, eax
    lea rcx, [rax+rax+2]
    call alloc_text
    mov r12, rax
    mov ecx, 65001
    xor edx, edx
    lea r8, [rbx+8]
    mov r9, qword ptr [rbx]
    mov qword ptr [rsp+32], r12
    mov qword ptr [rsp+40], rsi
    ! out.call_windows(DataRef::MultiByteToWideChar);
    mov word ptr [r12+rsi*2], 0
    mov rax, r12
    lea rsp, [rbp-16]
    pop rsi
    pop rbx
    pop rbp
    ret

@ read_whole_file Keeps r12 to r15, where Jamo code may hold variables, around read_whole_file_body
    push r12
    push r13
    push r14
    push r15
    call read_whole_file_body
    pop r15
    pop r14
    pop r13
    pop r12
    ret

@ read_whole_file_body rax = the path
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 4064
    or qword ptr [rsp], 0
    mov r13, rax
    call to_wide
    mov rcx, rax
    mov edx, 0x80000000
    mov r8d, 3
    xor r9d, r9d
    mov qword ptr [rsp+32], 3
    mov qword ptr [rsp+40], 0x80
    mov qword ptr [rsp+48], 0
    ! out.call_windows(DataRef::CreateFileW);
    cmp rax, -1
    je failed
    mov r12, rax
    call builder_start
next_chunk:
    mov rcx, r12
    lea rdx, [rsp+64]
    mov r8d, 4000
    lea r9, [rsp+56]
    mov qword ptr [rsp+32], 0
    ! out.call_windows(DataRef::ReadFile);
    test eax, eax
    je failed
    mov eax, dword ptr [rsp+56]
    test rax, rax
    je finished
    lea rdx, [rsp+64]
    mov r8, rax
    call builder_add
    jmp next_chunk
finished:
    mov rcx, r12
    ! out.call_windows(DataRef::CloseHandle);
    call builder_to_string
    leave
    ret
failed:
    mov rax, r13
    ! out.lea_string(RDX, text.cant_read);
    ! out.lea_string(R8, text.period);
    jmp text_error

@ write_whole_file Keeps r12 to r15, where Jamo code may hold variables, around write_whole_file_body
    push r12
    push r13
    push r14
    push r15
    call write_whole_file_body
    pop r15
    pop r14
    pop r13
    pop r12
    ret

@ write_whole_file_body rax = the path, rcx = the text. Replaces the file
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 64
    mov r13, rax
    mov r14, rcx
    call to_wide
    mov rcx, rax
    mov edx, 0x40000000
    mov r8d, 1
    xor r9d, r9d
    mov qword ptr [rsp+32], 2
    mov qword ptr [rsp+40], 0x80
    mov qword ptr [rsp+48], 0
    ! out.call_windows(DataRef::CreateFileW);
    cmp rax, -1
    je failed
    mov r12, rax
    lea rbx, [r14+8]
    mov rsi, qword ptr [r14]
next_part:
    test rsi, rsi
    je finished
    mov r8, rsi
    mov eax, 0x40000000
    cmp r8, rax
    cmova r8, rax
    mov rcx, r12
    mov rdx, rbx
    lea r9, [rsp+56]
    mov qword ptr [rsp+32], 0
    ! out.call_windows(DataRef::WriteFile);
    test eax, eax
    je failed
    mov eax, dword ptr [rsp+56]
    add rbx, rax
    sub rsi, rax
    jmp next_part
finished:
    mov rcx, r12
    ! out.call_windows(DataRef::CloseHandle);
    leave
    ret
failed:
    mov rax, r13
    ! out.lea_string(RDX, text.cant_write);
    ! out.lea_string(R8, text.period);
    jmp text_error

@ file_exists rax = the path. Answers with a bool
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 32
    call to_wide
    mov rcx, rax
    ! out.call_windows(DataRef::GetFileAttributesW);
    cmp eax, -1
    setne al
    movzx eax, al
    leave
    ret

@ stdin_refill Reads more typed or piped input. Answers with how much came. Keeps rbx
    push rbp
    mov rbp, rsp
    push rbx
    and rsp, -16
    sub rsp, 64
    ! out.lea_data(RBX, DataRef::Stdin);
    cmp qword ptr [rbx], 0
    jne has_buffer
    mov ecx, 4096
    call raw_alloc
    mov qword ptr [rbx], rax
has_buffer:
    mov rcx, -10
    ! out.call_windows(DataRef::GetStdHandle);
    mov qword ptr [rsp+48], rax
    mov rcx, rax
    lea rdx, [rsp+40]
    ! out.call_windows(DataRef::GetConsoleMode);
    test eax, eax
    jne console_input
    mov rcx, qword ptr [rsp+48]
    mov rdx, qword ptr [rbx]
    mov r8d, 4096
    lea r9, [rsp+56]
    mov qword ptr [rsp+56], 0
    mov qword ptr [rsp+32], 0
    ! out.call_windows(DataRef::ReadFile);
    test eax, eax
    je no_input
    mov eax, dword ptr [rsp+56]
    mov qword ptr [rbx+8], 0
    mov qword ptr [rbx+16], rax
    jmp finished
no_input:
    xor eax, eax
finished:
    lea rsp, [rbp-8]
    pop rbx
    pop rbp
    ret
console_input:
    sub rsp, 2624
    mov rcx, qword ptr [rsp+2672]
    lea rdx, [rsp+64]
    mov r8d, 1300
    lea r9, [rsp+56]
    mov qword ptr [rsp+56], 0
    mov qword ptr [rsp+32], 0
    ! out.call_windows(DataRef::ReadConsoleW);
    test eax, eax
    je no_input
    mov ecx, 65001
    xor edx, edx
    lea r8, [rsp+64]
    mov r9d, dword ptr [rsp+56]
    mov rax, qword ptr [rbx]
    mov qword ptr [rsp+32], rax
    mov qword ptr [rsp+40], 4096
    mov qword ptr [rsp+48], 0
    mov qword ptr [rsp+56], 0
    ! out.call_windows(DataRef::WideCharToMultiByte);
    mov eax, eax
    mov qword ptr [rbx+8], 0
    mov qword ptr [rbx+16], rax
    jmp finished

@ read_line Answers with the next line of input, without its line ending, or "" when there's no more
    ! out.lea_data(RBX, DataRef::Stdin);
    call builder_start
next_char:
    mov rax, qword ptr [rbx+8]
    cmp rax, qword ptr [rbx+16]
    jb has_char
    call stdin_refill
    test rax, rax
    je line_done
    xor eax, eax
has_char:
    mov rdx, qword ptr [rbx]
    add rdx, rax
    inc qword ptr [rbx+8]
    cmp byte ptr [rdx], 10
    je line_done
    mov r8d, 1
    call builder_add
    jmp next_char
line_done:
    ! out.lea_data(RAX, DataRef::Builder);
    mov rcx, qword ptr [rax+8]
    test rcx, rcx
    je no_return
    mov rdx, qword ptr [rax]
    cmp byte ptr [rdx+rcx-1], 13
    jne no_return
    dec qword ptr [rax+8]
no_return:
    jmp builder_to_string

@ builder_add_byte rax = one byte. Keeps rbx
    push rax
    mov rdx, rsp
    mov r8d, 1
    call builder_add
    pop rax
    ret

@ builder_add_letter rax = a letter's number in Unicode, added as UTF-8. Keeps r12, r13, r14 and r15
    mov rbx, rax
    cmp rbx, 0x80
    jae two_bytes
    jmp builder_add_byte
two_bytes:
    cmp rbx, 0x800
    jae three_bytes
    shr rax, 6
    or rax, 0xC0
    call builder_add_byte
    jmp last_byte
three_bytes:
    cmp rbx, 0x10000
    jae four_bytes
    shr rax, 12
    or rax, 0xE0
    call builder_add_byte
    jmp middle_byte
four_bytes:
    shr rax, 18
    or rax, 0xF0
    call builder_add_byte
    mov rax, rbx
    shr rax, 12
    and rax, 0x3F
    or rax, 0x80
    call builder_add_byte
middle_byte:
    mov rax, rbx
    shr rax, 6
    and rax, 0x3F
    or rax, 0x80
    call builder_add_byte
last_byte:
    mov rax, rbx
    and rax, 0x3F
    or rax, 0x80
    jmp builder_add_byte

@ get_args Keeps r12 to r15, where Jamo code may hold variables, around get_args_body
    push r12
    push r13
    push r14
    push r15
    call get_args_body
    pop r15
    pop r14
    pop r13
    pop r12
    ret

@ get_args_body Answers with the command line's inputs as an array of strings, read by Windows' usual rules
    mov ecx, 0
    call new_array
    mov r12, rax
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 32
    ! out.call_windows(DataRef::GetCommandLineW);
    mov r13, rax
    cmp word ptr [r13], 34
    jne skip_plain_name
skip_quoted_name:
    add r13, 2
    movzx eax, word ptr [r13]
    test eax, eax
    je all_done
    cmp eax, 34
    jne skip_quoted_name
    add r13, 2
    jmp next_input
skip_plain_name:
    movzx eax, word ptr [r13]
    test eax, eax
    je all_done
    cmp eax, 32
    je next_input
    cmp eax, 9
    je next_input
    add r13, 2
    jmp skip_plain_name
next_input:
    movzx eax, word ptr [r13]
    cmp eax, 32
    je skip_space
    cmp eax, 9
    jne found_input
skip_space:
    add r13, 2
    jmp next_input
found_input:
    test eax, eax
    je all_done
    call builder_start
    xor r14d, r14d
next_letter:
    movzx eax, word ptr [r13]
    test eax, eax
    je input_done
    cmp eax, 92
    je backslashes
    cmp eax, 34
    je quote_mark
    test r14, r14
    jne plain_letter
    cmp eax, 32
    je input_done
    cmp eax, 9
    je input_done
plain_letter:
    add r13, 2
    mov ecx, eax
    and ecx, 0xFC00
    cmp ecx, 0xD800
    jne whole_letter
    movzx ecx, word ptr [r13]
    mov edx, ecx
    and edx, 0xFC00
    cmp edx, 0xDC00
    jne whole_letter
    add r13, 2
    sub eax, 0xD800
    shl eax, 10
    sub ecx, 0xDC00
    add eax, ecx
    add eax, 0x10000
whole_letter:
    call builder_add_letter
    jmp next_letter
quote_mark:
    add r13, 2
    test r14, r14
    je flip_quotes
    cmp word ptr [r13], 34
    jne flip_quotes
    add r13, 2
    mov eax, 34
    call builder_add_byte
    jmp next_letter
flip_quotes:
    xor r14, 1
    jmp next_letter
backslashes:
    xor r15d, r15d
count_backslash:
    cmp word ptr [r13], 92
    jne counted
    inc r15
    add r13, 2
    jmp count_backslash
counted:
    cmp word ptr [r13], 34
    jne add_backslashes
    mov rax, r15
    and rax, 1
    shr r15, 1
    test rax, rax
    je add_backslashes
    add r13, 2
    mov rbx, r15
    call add_backslash_run
    mov eax, 34
    call builder_add_byte
    jmp next_letter
add_backslashes:
    mov rbx, r15
    call add_backslash_run
    jmp next_letter
input_done:
    call builder_to_string
    mov rcx, rax
    mov rax, r12
    call array_add
    jmp next_input
all_done:
    mov rax, r12
    leave
    ret

@ add_backslash_run rbx = how many backslashes to add
    test rbx, rbx
    je none_left
    mov eax, 92
    call builder_add_byte
    dec rbx
    jmp add_backslash_run
none_left:
    ret

@ raw_free rcx = what raw_alloc gave, or 0. Keeps rbx, rsi and rdi
    test rcx, rcx
    je nothing
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 32
    mov r8, rcx
    xor edx, edx
    ! out.load_rcx_data(DataRef::HeapHandle);
    ! out.call_windows(DataRef::HeapFree);
    leave
nothing:
    ret

@ alloc rcx = bytes wanted, for a block that can hold references. Answers in rax with the memory zeroed, and keeps every other register
    mov r11d, 1
    jmp alloc_block

@ alloc_text rcx = bytes wanted, for a block that holds no references, like a string
    xor r11d, r11d
    jmp alloc_block

@ alloc_block Each block starts with a 32-byte header: the next block, its size, its type at +16, its mark at +17, where it sits in its chunk at +20 or 0 if it has no chunk, and a check value. Blocks of up to 512 bytes come from chunks, and this quick path saves few registers because it can't clean or call Windows
    push rax
    push rcx
    push rdx
    push rbx
    push rdi
    ! out.lea_data(RBX, DataRef::Cleaner);
    lea rdx, [rcx-1]
    cmp rdx, 512
    jae slow
    and rdx, -16
    lea rax, [rdx+48]
    add rax, qword ptr [rbx+16]
    cmp rax, qword ptr [rbx+24]
    jae slow
    test byte ptr [rbx+32], 1
    jne slow
    jmp alloc_take
slow:
    pop rdi
    pop rbx
    pop rdx
    pop rcx
    pop rax
    jmp alloc_slow

@ alloc_again Comes back from alloc_slow, which has made sure a small block can be had
    push rax
    push rcx
    push rdx
    push rbx
    push rdi
    ! out.lea_data(RBX, DataRef::Cleaner);
    lea rdx, [rcx-1]
    and rdx, -16
    jmp alloc_take

@ alloc_take rdx = 16 x the size class. Takes a block from its free list, or else from the current chunk, then sets it up and zeroes it. Five registers are on the stack
    mov rcx, rdx
    shr rcx, 1
    mov rax, qword ptr [rbx+rcx+144]
    test rax, rax
    je from_chunk
    cmp byte ptr [rax+16], 3
    jne not_free
    mov rdi, qword ptr [rax]
    mov qword ptr [rbx+rcx+144], rdi
set_up:
    lea rdi, [rdx+48]
    add qword ptr [rbx+16], rdi
    mov rdi, qword ptr [rbx]
    mov qword ptr [rax], rdi
    mov qword ptr [rbx], rax
    inc qword ptr [rbx+8]
    mov rcx, qword ptr [rsp+24]
    mov qword ptr [rax+8], rcx
    mov byte ptr [rax+16], r11b
    mov byte ptr [rax+17], 0
    xor rcx, rax
    movabs rdi, 0x4A414D4F434C4541
    xor rcx, rdi
    mov qword ptr [rax+24], rcx
    add rax, 32
next_zero:
    mov qword ptr [rax+rdx], 0
    mov qword ptr [rax+rdx+8], 0
    sub rdx, 16
    jns next_zero
    pop rdi
    pop rbx
    pop rdx
    pop rcx
    add rsp, 8
    ret
from_chunk:
    mov rax, qword ptr [rbx+104]
    lea rdi, [rax+rdx+48]
    cmp rdi, qword ptr [rbx+112]
    ja slow
    mov qword ptr [rbx+104], rdi
    mov rdi, rax
    sub rdi, qword ptr [rbx+96]
    mov dword ptr [rax+20], edi
    jmp set_up
slow:
    pop rdi
    pop rbx
    pop rdx
    pop rcx
    pop rax
    jmp alloc_slow
not_free:
    ! out.lea_string(RAX, text.cleaner_bad_header);
    jmp run_error

@ alloc_slow Everything the quick path can't do: cleaning, getting a chunk, and large blocks. Every register goes on the stack first, so cleaning can find what's in them
    push rbp
    mov rbp, rsp
    push rax
    push rcx
    push rdx
    push rbx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    and rsp, -16
    sub rsp, 32
    ! out.lea_data(RBX, DataRef::Cleaner);
    mov rax, qword ptr [rbp-16]
    add rax, 47
    and rax, -16
    add qword ptr [rbx+16], rax
    xor r12d, r12d
    test byte ptr [rbx+32], 4
    jne no_clean
    test byte ptr [rbx+32], 1
    jne clean_first
    mov rax, qword ptr [rbx+16]
    cmp rax, qword ptr [rbx+24]
    jb no_clean
clean_first:
    call cleaner_clean
    mov r12d, 1
no_clean:
    mov rcx, qword ptr [rbp-16]
    dec rcx
    cmp rcx, 512
    jae large
    and rcx, -16
    mov rax, rcx
    shr rax, 1
    cmp qword ptr [rbx+rax+144], 0
    jne small_ready
    mov rax, qword ptr [rbx+104]
    lea rax, [rax+rcx+48]
    cmp rax, qword ptr [rbx+112]
    jbe small_ready
    mov rax, qword ptr [rbx+128]
    test rax, rax
    je new_chunk
    mov rcx, qword ptr [rax]
    mov qword ptr [rbx+128], rcx
    dec qword ptr [rbx+136]
    jmp use_chunk
new_chunk:
    test r12, r12
    jne get_chunk
    test byte ptr [rbx+32], 4
    jne get_chunk
    mov rax, qword ptr [rbx+8]
    add rax, qword ptr [rbx+80]
    lea r8, [rax+rax*4]
    shl r8, 3
    add r8, 0x40000
    ! out.load_rcx_data(DataRef::HeapHandle);
    xor edx, edx
    ! out.call_windows(DataRef::HeapAlloc);
    test rax, rax
    je clean_first
    ! out.load_rcx_data(DataRef::HeapHandle);
    xor edx, edx
    mov r8, rax
    ! out.call_windows(DataRef::HeapFree);
get_chunk:
    ! out.load_rcx_data(DataRef::HeapHandle);
    xor edx, edx
    mov r8d, 0x40000
    ! out.call_windows(DataRef::HeapAlloc);
    test rax, rax
    je no_memory
use_chunk:
    mov rcx, qword ptr [rbx+120]
    mov qword ptr [rax], rcx
    mov qword ptr [rbx+120], rax
    mov qword ptr [rax+8], 0
    mov qword ptr [rbx+96], rax
    lea rcx, [rax+16]
    mov qword ptr [rbx+104], rcx
    lea rcx, [rax+0x40000]
    mov qword ptr [rbx+112], rcx
small_ready:
    lea rsp, [rbp-112]
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbx
    pop rdx
    pop rcx
    pop rax
    pop rbp
    jmp alloc_again
large:
    ! out.load_rcx_data(DataRef::HeapHandle);
    mov edx, 8
    mov r8, qword ptr [rbp-16]
    add r8, 32
    ! out.call_windows(DataRef::HeapAlloc);
    test rax, rax
    jne got_memory
no_memory:
    test r12, r12
    jne out_of_memory
    test byte ptr [rbx+32], 4
    jne out_of_memory
    jmp clean_first
got_memory:
    mov rcx, qword ptr [rbx]
    mov qword ptr [rax], rcx
    mov qword ptr [rbx], rax
    inc qword ptr [rbx+8]
    mov rcx, qword ptr [rbp-16]
    mov qword ptr [rax+8], rcx
    mov rdx, qword ptr [rbp-80]
    mov byte ptr [rax+16], dl
    xor rcx, rax
    movabs rdx, 0x4A414D4F434C4541
    xor rcx, rdx
    mov qword ptr [rax+24], rcx
    add rax, 32
    mov qword ptr [rbp-8], rax
    lea rsp, [rbp-112]
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbx
    pop rdx
    pop rcx
    pop rax
    pop rbp
    ret

@ cleaner_start rax = the mode: 1 cleans at every allocation, 2 adds extra checks, 4 turns cleaning off, 16 is a window program. Also notes where the stack starts. The cleaner's values: the first block +0, how many +8, bytes since a clean +16, when to clean +24, mode +32, stack start +40, freed list +56, how many freed +80, how many cleans +88, current chunk +96, its next free spot +104, its end +112, chunks in use +120, empty chunks +128, how many empty +136, the free list of each size class +144, then the table of blocks +400 and how many entries it has +408, and the list to look through +416 and its entries +424, kept from clean to clean while each is 2 MB or less, so a program that cleans often doesn't ask Windows for them every time. A chunk starts with the next chunk and how many blocks in it are in use
    ! out.lea_data(RDX, DataRef::Cleaner);
    mov qword ptr [rdx+32], rax
    mov qword ptr [rdx+40], rsp
    mov qword ptr [rdx+24], 0x400000
    test al, 4
    je will_clean
    mov qword ptr [rdx+24], -1
will_clean:
    ret

@ cleaner_verify The extra checks: every block's header, and that freed memory still holds only its marker. rbx = the cleaner's values
    mov rdx, qword ptr [rbx]
next_live:
    test rdx, rdx
    je check_freed
    mov rax, qword ptr [rdx+8]
    xor rax, rdx
    movabs rcx, 0x4A414D4F434C4541
    xor rax, rcx
    cmp rax, qword ptr [rdx+24]
    jne bad_header
    cmp byte ptr [rdx+16], 1
    ja bad_header
    cmp byte ptr [rdx+17], 0
    jne bad_header
    mov rdx, qword ptr [rdx]
    jmp next_live
check_freed:
    mov rdx, qword ptr [rbx+56]
next_freed:
    test rdx, rdx
    je all_good
    mov rax, qword ptr [rdx+8]
    xor rax, rdx
    movabs rcx, 0x4A414D4F434C4541
    xor rax, rcx
    cmp rax, qword ptr [rdx+24]
    jne bad_header
    cmp byte ptr [rdx+16], 2
    jne bad_header
    lea rdi, [rdx+32]
    mov rcx, qword ptr [rdx+8]
    shr rcx, 3
    movabs rax, 0xDDDDDDDDDDDDDDDD
    cmp rax, rax
    repe scasq
    jne freed_changed
    mov rcx, qword ptr [rdx+8]
    and rcx, 7
    cmp rax, rax
    repe scasb
    jne freed_changed
    mov rdx, qword ptr [rdx]
    jmp next_freed
all_good:
    ret
bad_header:
    ! out.lea_string(RAX, text.cleaner_bad_header);
    jmp run_error
freed_changed:
    ! out.lea_string(RAX, text.cleaner_freed_changed);
    jmp run_error

@ cleaner_add_to_table rdx = a list of blocks to add to the table of known blocks. r12 = the table, r13 = its size minus 1
    movabs r9, 0x9E3779B97F4A7C15
next_block:
    test rdx, rdx
    je finished
    lea rax, [rdx+32]
    mov rcx, rax
    shr rcx, 4
    imul rcx, r9
    shr rcx, 32
next_place:
    and rcx, r13
    cmp qword ptr [r12+rcx*8], 0
    je found_room
    inc rcx
    jmp next_place
found_room:
    mov qword ptr [r12+rcx*8], rax
    mov rdx, qword ptr [rdx]
    jmp next_block
finished:
    ret

@ cleaner_mark_word rax = anything at all. If it's exactly where a known block's memory starts, that block is in use. r14 = the top of the list still to look through, r15 = 1 when the word came from a block. With checks on, a block's word that points at a block freed in the last 2 cleans is a bug: when the cleaner frees something still in use, that's when it shows. Older ones are left alone, since a plain number can equal an old block's address
    test rax, rax
    je nothing
    test al, 15
    jne nothing
    movabs r9, 0x9E3779B97F4A7C15
    mov rcx, rax
    shr rcx, 4
    imul rcx, r9
    shr rcx, 32
next_place:
    and rcx, r13
    mov rdx, qword ptr [r12+rcx*8]
    test rdx, rdx
    je nothing
    cmp rdx, rax
    je found
    inc rcx
    jmp next_place
found:
    cmp byte ptr [rax-16], 2
    je is_freed
    cmp byte ptr [rax-15], 0
    jne nothing
    mov byte ptr [rax-15], 1
    cmp byte ptr [rax-16], 1
    jne nothing
    mov qword ptr [r14], rax
    add r14, 8
nothing:
    ret
is_freed:
    test r15, r15
    je nothing
    movzx ecx, word ptr [rax-14]
    mov edx, dword ptr [rbx+88]
    sub edx, ecx
    and edx, 0xFFFF
    cmp edx, 2
    ja nothing
    ! out.lea_string(RAX, text.cleaner_freed_reference);
    jmp run_error

@ cleaner_clean Marks everything that's in use, starting from the whole stack, which holds every saved register. Uses a list instead of recursion, so long chains can't overflow the stack
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 48
    ! out.lea_data(RBX, DataRef::Cleaner);
    inc qword ptr [rbx+88]
    test byte ptr [rbx+32], 2
    je no_checks
    call cleaner_verify
no_checks:
    mov rax, qword ptr [rbx+8]
    add rax, qword ptr [rbx+80]
    lea rdx, [rax+rax+1]
    mov r13d, 16
size_table:
    cmp r13, rdx
    jae table_sized
    shl r13, 1
    jmp size_table
table_sized:
    cmp r13, qword ptr [rbx+408]
    jbe reuse_table
    mov r8, qword ptr [rbx+400]
    test r8, r8
    je table_freed
    ! out.load_rcx_data(DataRef::HeapHandle);
    xor edx, edx
    ! out.call_windows(DataRef::HeapFree);
table_freed:
    mov qword ptr [rbx+400], 0
    mov qword ptr [rbx+408], 0
    ! out.load_rcx_data(DataRef::HeapHandle);
    mov edx, 8
    lea r8, [r13*8]
    ! out.call_windows(DataRef::HeapAlloc);
    test rax, rax
    je give_up
    mov qword ptr [rbx+400], rax
    mov qword ptr [rbx+408], r13
    jmp table_ready
reuse_table:
    mov r13, qword ptr [rbx+408]
    mov rdi, qword ptr [rbx+400]
    mov rcx, r13
    xor eax, eax
    rep stosq
table_ready:
    mov r12, qword ptr [rbx+400]
    mov r15, qword ptr [rbx+8]
    inc r15
    cmp r15, qword ptr [rbx+424]
    jbe reuse_list
    mov r8, qword ptr [rbx+416]
    test r8, r8
    je list_freed
    ! out.load_rcx_data(DataRef::HeapHandle);
    xor edx, edx
    ! out.call_windows(DataRef::HeapFree);
list_freed:
    mov qword ptr [rbx+416], 0
    mov qword ptr [rbx+424], 0
    ! out.load_rcx_data(DataRef::HeapHandle);
    xor edx, edx
    lea r8, [r15*8]
    ! out.call_windows(DataRef::HeapAlloc);
    test rax, rax
    je give_up
    mov qword ptr [rbx+416], rax
    mov qword ptr [rbx+424], r15
reuse_list:
    mov r14, qword ptr [rbx+416]
    mov qword ptr [rsp+40], r14
    dec r13
    mov rdx, qword ptr [rbx]
    call cleaner_add_to_table
    mov rdx, qword ptr [rbx+56]
    call cleaner_add_to_table
    xor r15d, r15d
    mov rsi, rsp
next_root:
    cmp rsi, qword ptr [rbx+40]
    jae roots_done
    mov rax, qword ptr [rsi]
    call cleaner_mark_word
    add rsi, 8
    jmp next_root
roots_done:
    mov r15d, 1
next_to_look_at:
    cmp r14, qword ptr [rsp+40]
    je all_marked
    sub r14, 8
    mov rsi, qword ptr [r14]
    mov rdi, qword ptr [rsi-24]
    shr rdi, 3
next_word:
    test rdi, rdi
    je next_to_look_at
    mov rax, qword ptr [rsi]
    call cleaner_mark_word
    add rsi, 8
    dec rdi
    jmp next_word
all_marked:
    mov rax, qword ptr [rbx+120]
next_count:
    test rax, rax
    je counts_cleared
    mov qword ptr [rax+8], 0
    mov rax, qword ptr [rax]
    jmp next_count
counts_cleared:
    mov rsi, rbx
    mov rdi, qword ptr [rbx]
    xor r14d, r14d
next_block:
    test rdi, rdi
    je sweep_done
    cmp byte ptr [rdi+17], 0
    je free_block
    mov byte ptr [rdi+17], 0
    mov rax, qword ptr [rdi+8]
    add rax, 47
    and rax, -16
    add r14, rax
    mov edx, dword ptr [rdi+20]
    test edx, edx
    je not_in_chunk
    mov rax, rdi
    sub rax, rdx
    inc qword ptr [rax+8]
not_in_chunk:
    mov rsi, rdi
    mov rdi, qword ptr [rdi]
    jmp next_block
free_block:
    cmp byte ptr [rdi+16], 2
    je freed_twice
    mov r15, qword ptr [rdi]
    mov qword ptr [rsi], r15
    dec qword ptr [rbx+8]
    test byte ptr [rbx+32], 2
    jne keep_freed
    cmp dword ptr [rdi+20], 0
    jne free_small
    ! out.load_rcx_data(DataRef::HeapHandle);
    xor edx, edx
    mov r8, rdi
    ! out.call_windows(DataRef::HeapFree);
    jmp block_freed
free_small:
    mov byte ptr [rdi+16], 3
    mov rax, qword ptr [rdi+8]
    dec rax
    shr rax, 4
    mov rcx, qword ptr [rbx+rax*8+144]
    mov qword ptr [rdi], rcx
    mov qword ptr [rbx+rax*8+144], rdi
    jmp block_freed
keep_freed:
    mov byte ptr [rdi+16], 2
    mov eax, dword ptr [rbx+88]
    mov word ptr [rdi+18], ax
    mov rax, qword ptr [rbx+56]
    mov qword ptr [rdi], rax
    mov qword ptr [rbx+56], rdi
    inc qword ptr [rbx+80]
    mov rcx, qword ptr [rdi+8]
    add rdi, 32
    mov al, 0xDD
    rep stosb
block_freed:
    mov rdi, r15
    jmp next_block
freed_twice:
    ! out.lea_string(RAX, text.cleaner_double_free);
    jmp run_error
sweep_done:
    add r14, r14
    mov eax, 0x400000
    cmp r14, rax
    cmova rax, r14
    mov qword ptr [rbx+24], rax
    test byte ptr [rbx+32], 2
    jne chunks_done
    xor ecx, ecx
    xor edx, edx
    mov rax, qword ptr [rbx+120]
next_counted:
    test rax, rax
    je all_counted
    inc rcx
    cmp qword ptr [rax+8], 0
    jne has_live
    inc rdx
has_live:
    mov rax, qword ptr [rax]
    jmp next_counted
all_counted:
    shl rdx, 3
    cmp rdx, rcx
    jb trim_chunks
    xor r13d, r13d
next_list:
    lea rsi, [rbx+r13+144]
next_free:
    mov rdi, qword ptr [rsi]
    test rdi, rdi
    je list_done
    mov edx, dword ptr [rdi+20]
    mov rax, rdi
    sub rax, rdx
    cmp qword ptr [rax+8], 0
    je drop_free
    mov rsi, rdi
    jmp next_free
drop_free:
    mov rax, qword ptr [rdi]
    mov qword ptr [rsi], rax
    jmp next_free
list_done:
    add r13, 8
    cmp r13, 256
    jb next_list
    lea rsi, [rbx+120]
next_chunk:
    mov rdi, qword ptr [rsi]
    test rdi, rdi
    je trim_chunks
    cmp qword ptr [rdi+8], 0
    je empty_chunk
    mov rsi, rdi
    jmp next_chunk
empty_chunk:
    mov rax, qword ptr [rdi]
    mov qword ptr [rsi], rax
    cmp rdi, qword ptr [rbx+96]
    jne not_current
    xor eax, eax
    mov qword ptr [rbx+96], rax
    mov qword ptr [rbx+104], rax
    mov qword ptr [rbx+112], rax
not_current:
    mov rax, qword ptr [rbx+128]
    mov qword ptr [rdi], rax
    mov qword ptr [rbx+128], rdi
    inc qword ptr [rbx+136]
    jmp next_chunk
trim_chunks:
    mov rax, qword ptr [rbx+136]
    shl rax, 18
    mov rcx, qword ptr [rbx+24]
    shr rcx, 1
    add rcx, 0x80000
    cmp rax, rcx
    jbe chunks_done
    mov rdi, qword ptr [rbx+128]
    mov rax, qword ptr [rdi]
    mov qword ptr [rbx+128], rax
    dec qword ptr [rbx+136]
    ! out.load_rcx_data(DataRef::HeapHandle);
    xor edx, edx
    mov r8, rdi
    ! out.call_windows(DataRef::HeapFree);
    jmp trim_chunks
chunks_done:
give_up:
    cmp qword ptr [rbx+408], 262144
    jbe keep_table
    ! out.load_rcx_data(DataRef::HeapHandle);
    xor edx, edx
    mov r8, qword ptr [rbx+400]
    ! out.call_windows(DataRef::HeapFree);
    mov qword ptr [rbx+400], 0
    mov qword ptr [rbx+408], 0
keep_table:
    cmp qword ptr [rbx+424], 262144
    jbe keep_list
    ! out.load_rcx_data(DataRef::HeapHandle);
    xor edx, edx
    mov r8, qword ptr [rbx+416]
    ! out.call_windows(DataRef::HeapFree);
    mov qword ptr [rbx+416], 0
    mov qword ptr [rbx+424], 0
keep_list:
    mov qword ptr [rbx+16], 0
    leave
    ret

@ show_calls Walks up the stack, showing each function call that led here. The table holds pairs of 4-byte places: where a call returns to, and the words for that call
    mov r13, rbp
    xor r14d, r14d
    ! out.lea_data(RAX, DataRef::Cleaner);
    mov r15, qword ptr [rax+40]
next_frame:
    test r13, r13
    je finished
    cmp r13, r15
    jae finished
    mov rax, qword ptr [r13+8]
    movabs rcx, 0x140000000
    sub rax, rcx
    ! out.lea_data(RDX, DataRef::CallTable);
    mov ecx, dword ptr [rdx]
    add rdx, 8
next_entry:
    test ecx, ecx
    je not_a_call
    cmp eax, dword ptr [rdx]
    je found_call
    add rdx, 8
    dec ecx
    jmp next_entry
found_call:
    cmp r14, 10
    jae too_many
    inc r14
    mov eax, dword ptr [rdx+4]
    movabs rcx, 0x140000000
    add rax, rcx
    call error_text
not_a_call:
    mov rax, qword ptr [r13]
    cmp rax, r13
    jbe finished
    mov r13, rax
    jmp next_frame
too_many:
    ! out.lea_string(RAX, text.more_calls);
    jmp error_text
finished:
    ret
