// Modules: loads a program's main file and every file it uses, each one once.
#pragma once

#include <filesystem>

#include "ast.hpp"
#include "errors.hpp"

// Answers false only if the main file can't be read. Everything else ends up in errors
bool load_program(const std::filesystem::path& main_file, Errors& errors, Program& program);

// Reads one more file from text, like the main that jamo test makes. It uses nothing
void load_extra_file(const std::string& name, const std::string& source, Errors& errors, Program& program);
