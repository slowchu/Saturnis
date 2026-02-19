#include "cpu/sh2_decode.hpp"

namespace saturnis::cpu::decode {
namespace {

constexpr std::array<OpcodePattern, 22> kPatterns{{
    // Branch/control-flow families.
    {0xF000U, 0xA000U, "BRA disp12"},
    {0xF000U, 0xB000U, "BSR disp12"},
    {0xF0FFU, 0x400BU, "JSR @Rn"},
    {0xF0FFU, 0x402BU, "JMP @Rn"},
    {0xFFFFU, 0x000BU, "RTS"},
    {0xFF00U, 0x8900U, "BT"},
    {0xFF00U, 0x8B00U, "BF"},
    {0xFF00U, 0x8D00U, "BT/S"},
    {0xFF00U, 0x8F00U, "BF/S"},

    // Exception/system forms currently modeled.
    {0xFF00U, 0xC300U, "TRAPA #imm"},
    {0xFFFFU, 0x002BU, "RTE"},
    {0xF0FFU, 0x400EU, "LDC Rm,SR"},
    {0xF0FFU, 0x0002U, "STC SR,Rn"},
    {0xF0FFU, 0x4022U, "STS.L PR,@-Rn"},
    {0xF0FFU, 0x4026U, "LDS.L @Rm+,PR"},

    // Representative ALU/data movement forms used by current subset.
    {0xF000U, 0xE000U, "MOV #imm,Rn"},
    {0xF000U, 0x7000U, "ADD #imm,Rn"},
    {0xF00FU, 0x300CU, "ADD Rm,Rn"},
    {0xF00FU, 0x6003U, "MOV Rm,Rn"},
    {0xF00FU, 0x2000U, "MOV.B Rm,@Rn"},
    {0xF00FU, 0x2002U, "MOV.L Rm,@Rn"},
    {0xFFFFU, 0x0009U, "NOP"},
}};

} // namespace

const std::array<OpcodePattern, 22> &patterns() { return kPatterns; }

std::optional<std::string_view> decode_family(std::uint16_t instr) {
  for (const auto &p : kPatterns) {
    if ((instr & p.mask) == p.value) {
      return p.family;
    }
  }
  return std::nullopt;
}

std::size_t decode_match_count(std::uint16_t instr) {
  std::size_t matches = 0;
  for (const auto &p : kPatterns) {
    if ((instr & p.mask) == p.value) {
      ++matches;
    }
  }
  return matches;
}

} // namespace saturnis::cpu::decode
