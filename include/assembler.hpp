#pragma once
#include "isa.hpp"
#include <string>
#include <unordered_map>

//
// Ensamblador mini del simulador.
// Toma texto (string o archivo) y lo convierte en un Program.
//
// Sintaxis por línea (simple y sin vueltas):
//   LABEL:
//   LOAD  REGd, [REGs]
//   STORE REGs, [REGd]
//   FMUL  REGd, REGa, REGb
//   FADD  REGd, REGa, REGb
//   INC   REGx
//   DEC   REGx
//   MOVI  REGx, IMM64    // inmediato decimal o 0xHEX
//   JNZ   LABEL          // usa REG0 como contador implícito
//
// Notas rápidas:
// - Registros válidos: REG0..REG7
// - Los comentarios empiezan con ';'
// - Los separadores son comas y espacios opcionales
// - Si hay un error de parseo, se lanza std::runtime_error
//

namespace sim {

class Assembler {
public:
  // Ensambla directamente desde una cadena completa.
  static Program assemble_from_string(const std::string& src);

  // Lee el archivo y ensambla su contenido.
  static Program assemble_from_file(const std::string& path);

private:
  // Convierte "REG0".."REG7" a su índice 0..7.
  static int         parse_reg(const std::string& token);

  // Quita espacios al inicio y al final.
  static std::string trim(const std::string& s);

  // ¿s empieza con p?
  static bool        starts_with(const std::string& s, const std::string& p);
};

// Acceso a la tabla global de labels del último ensamblado (nombre -> índice).
std::unordered_map<std::string,int>& get_labels_singleton();

} // namespace sim
