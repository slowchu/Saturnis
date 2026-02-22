#pragma once

#include <cstdint>

namespace busarb {

std::uint32_t ymir_access_cycles(void *ctx, std::uint32_t addr, bool is_write, std::uint8_t size_bytes);

} // namespace busarb
