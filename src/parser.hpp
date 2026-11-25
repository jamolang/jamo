// Parser: turns tokens into the tree in ast.hpp. After a mistake in the structure, it carries on from the next line.
#pragma once

#include "ast.hpp"
#include "lexer.hpp"

Program parse_program(const std::vector<Token>& tokens, Errors& errors);

// Adds one more file's code to a program
void parse_into(const std::vector<Token>& tokens, Errors& errors, Program& program);
