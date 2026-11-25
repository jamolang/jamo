// PE writer: packs machine code into a real Windows .exe, with no linker.
#pragma once

#include <cstdint>
#include <vector>

#include "codegen.hpp"

std::vector<uint8_t> build_exe(const MachineCode& code);
