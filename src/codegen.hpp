// Code generator: writes real x86-64 machine code for a checked Jamo program, with no assembler.
#pragma once

#include <string>

#include "assembler.hpp"
#include "ast.hpp"

// file_name appears in run errors, like game.jamo line 8: Error: division by zero
// test_flags are for testing: 1 cleans at every allocation, 2 adds cleaner checks, 4 turns the cleaner off, 8 turns off optimizing
MachineCode generate_code(const Program& program, const std::string& file_name, int test_flags = 0);
