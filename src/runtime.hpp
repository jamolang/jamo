// Runtime: the small routines every built program carries. The only code that talks to Windows.
//
// A Jamo string is a pointer to: its length (8 bytes), its bytes, then a hidden new line for print.
#pragma once

#include <string>
#include <vector>

#include "assembler.hpp"

struct Runtime {
    Label exit, write, raw_alloc, format_int, make_string, print_int, int_to_string, bool_to_string, print_bool, print_string, join_strings, join_many, strings_equal, error_text, file_name_of, run_error, stop_with_error, divide_by_zero, out_of_memory, format_float, print_float, float_to_string, float_to_int, float_mod, builder_start, builder_add, builder_add_string, builder_to_string, builder_add_int, index_error, string_at, new_array, array_add, array_pop, array_address, array_at, builder_add_float, builder_add_bool, builder_add_quoted, builder_print, show_enter, show_leave, text_error, string_to_int, big_times_ten, big_double, big_compare, big_subtract, string_to_float, string_to_float_body, big_subtract_clear, to_wide, to_wide_body, read_whole_file, read_whole_file_body, write_whole_file, write_whole_file_body, file_exists, stdin_refill, read_line, builder_add_byte, builder_add_letter, get_args, get_args_body, add_backslash_run, raw_free, alloc, alloc_text, alloc_block, alloc_again, alloc_take, alloc_slow, cleaner_start, cleaner_verify, cleaner_add_to_table, cleaner_mark_word, cleaner_clean, show_calls, trace_call;
};

// Which entry in the program's string table holds each fixed text
struct RuntimeText {
    std::vector<int> file_names;
    int true_word = 0, false_word = 0, line_word = 0, error_word = 0, divide_by_zero = 0, out_of_memory = 0, float_too_large = 0, index_word = 0, outside_string = 0, bytes_word = 0, pop_empty = 0, outside_array = 0, items_word = 0, item_word = 0, byte_word = 0, quote = 0, new_line = 0, cant_turn = 0, into_int = 0, into_float = 0, cant_read = 0, cant_write = 0, period = 0, cleaner_bad_header = 0, cleaner_freed_changed = 0, cleaner_freed_reference = 0, cleaner_double_free = 0, more_calls = 0, user32_dll = 0, message_box = 0, error_title = 0;
};

Runtime make_runtime_labels(Assembler& out);
void write_runtime(Assembler& out, const Runtime& runtime, const RuntimeText& text);
