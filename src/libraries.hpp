// Libraries: the .jamo libraries built into jamo.exe, like window.jamo.
#pragma once

#include <string>

// The library's source, or nullptr if there's no built-in library with that name
const char* builtin_library(const std::string& name);
