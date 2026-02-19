#include "cpu/sh2_decode.hpp"

namespace saturnis::cpu::decode {
namespace {

constexpr std::array<OpcodePattern, 30> kPatterns{{
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

    // PC-relative/indexed/GBR addressing forms.
    {0xF000U, 0x9000U, "MOV.W @(disp,PC),Rn"},
    {0xF000U, 0xD000U, "MOV.L @(disp,PC),Rn"},
    {0xFF00U, 0xC700U, "MOVA @(disp,PC),R0"},
    {0xF00FU, 0x000CU, "MOV.B @(R0,Rm),Rn"},
    {0xF00FU, 0x000DU, "MOV.W @(R0,Rm),Rn"},
    {0xF00FU, 0x000EU, "MOV.L @(R0,Rm),Rn"},
    {0xF00FU, 0x0004U, "MOV.B Rm,@(R0,Rn)"},
    {0xF00FU, 0x0005U, "MOV.W Rm,@(R0,Rn)"},
    {0xF00FU, 0x0006U, "MOV.L Rm,@(R0,Rn)"},
    {0xFF00U, 0xC400U, "MOV.B @(disp,GBR),R0"},
    {0xFF00U, 0xC500U, "MOV.W @(disp,GBR),R0"},
    {0xFF00U, 0xC600U, "MOV.L @(disp,GBR),R0"},
    {0xFF00U, 0xC000U, "MOV.B R0,@(disp,GBR)"},
    {0xFF00U, 0xC100U, "MOV.W R0,@(disp,GBR)"},
    {0xFF00U, 0xC200U, "MOV.L R0,@(disp,GBR)"},
}};

} // namespace

const std::array<OpcodePattern, 30> &patterns() { return kPatterns; }

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
