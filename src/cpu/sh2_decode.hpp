#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace saturnis::cpu::decode {

[[nodiscard]] constexpr std::uint32_t field_n(std::uint16_t instr) { return (instr >> 8U) & 0x0FU; }
[[nodiscard]] constexpr std::uint32_t field_m(std::uint16_t instr) { return (instr >> 4U) & 0x0FU; }
[[nodiscard]] constexpr std::uint32_t field_imm8(std::uint16_t instr) { return instr & 0x00FFU; }
[[nodiscard]] constexpr std::uint32_t field_disp4(std::uint16_t instr) { return instr & 0x000FU; }
[[nodiscard]] constexpr std::uint32_t field_disp12(std::uint16_t instr) { return instr & 0x0FFFU; }

struct OpcodePattern {
  std::uint16_t mask;
  std::uint16_t value;
  std::string_view family;
};

[[nodiscard]] std::optional<std::string_view> decode_family(std::uint16_t instr);
[[nodiscard]] std::size_t decode_match_count(std::uint16_t instr);
[[nodiscard]] const std::array<OpcodePattern, 30> &patterns();

} // namespace saturnis::cpu::decode
