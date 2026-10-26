#pragma once

#include <cstddef>
#include <cstdint>

namespace br::disasm {

// Компактный Read-Only декодер базовых инструкций AArch64 для встроенного просмотрщика кода (.text)
void FormatArm64Instruction(uint32_t insn, uintptr_t pc, char* out_buf, size_t out_size) noexcept;

} // namespace br::disasm
