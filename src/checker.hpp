// Checker: finds wrong types, unknown names and other mistakes, and fills in what the code generator needs.
#pragma once

#include "ast.hpp"

void check_program(Program& program, Errors& errors);
