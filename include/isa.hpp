#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sim {

// ISA mini: opcodes y formatos básicos (versión light).
enum class OpCode {
  LOAD,   // LOAD  Rd, [Rs]
  STORE,  // STORE Rs, [Rd]
  FMUL,   // FMUL  Rd, Ra, Rb
  FADD,   // FADD  Rd, Ra, Rb
  REDUCE, // REDUCE Rd, Ra, Rb  (Ra=base, Rb=count)  // agregado
  INC,    // INC   R
  DEC,    // DEC   R
  MOVI,   // MOVI  Rd, IMM64
  JNZ     // JNZ   label  (usa REG0 como contador)
};

// Instrucción cruda (sin microdetalles).
struct Instr {
  OpCode op{};        // operación
  int rd = 0;         // destino (en STORE: registro con dirección destino)
  int ra = 0;         // operando A (en LOAD/STORE: registro fuente)
  int rb = 0;         // operando B (FMUL/FADD/REDUCE)
  std::string label;  // para JNZ
  std::uint64_t imm=0;// para MOVI (decimal o 0xHEX)
};

// Programa = lista plana de instrucciones.
struct Program {
  std::vector<Instr> code;
};

} // namespace sim
