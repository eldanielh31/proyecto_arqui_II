#pragma once
#include "isa.hpp"
#include <string>
#include <unordered_map>

namespace sim {

// Ensamblador mínimo con labels (formato por línea):
//   LABEL:
//   LOAD  REGd, [REGs]
//   STORE REGs, [REGd]
//   FMUL  REGd, REGa, REGb
//   FADD  REGd, REGa, REGb
//   INC   REGx
//   DEC   REGx
//   MOVI  REGx, IMM64    ; inmediato en decimal o 0xHEX
//   JNZ   LABEL          ; usa REG0 como contador implícito

class Assembler {
public:
  // Lanza std::runtime_error si hay error de parseo.
  static Program assemble_from_string(const std::string& src);
  static Program assemble_from_file(const std::string& path);

private:
  static int         parse_reg(const std::string& token); // REG0..REG7
  static std::string trim(const std::string& s);
  static bool        starts_with(const std::string& s, const std::string& p);
};

// Acceso al mapa global de labels generado por el ensamblador
std::unordered_map<std::string,int>& get_labels_singleton();

} // namespace sim
