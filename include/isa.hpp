#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sim {

enum class OpCode {
  LOAD,   // LOAD Rd, [Rs]
  STORE,  // STORE Rs, [Rd]
  FMUL,   // FMUL Rd, Ra, Rb
  FADD,   // FADD Rd, Ra, Rb
  INC,    // INC R
  DEC,    // DEC R
  MOVI,   // MOVI Rd, IMM64
  JNZ     // JNZ label  (usa REG0 implícito como contador)
};

struct Instr {
  OpCode op{};
  int rd = 0;           // destino (o registro de dirección en STORE)
  int ra = 0;           // operando A (o src en LOAD/STORE)
  int rb = 0;           // operando B (FMUL/FADD)
  std::string label;    // para JNZ
  std::uint64_t imm=0;  // para MOVI
};

struct Program {
  std::vector<Instr> code;
};

} // namespace sim
