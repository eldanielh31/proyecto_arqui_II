#include "assembler.hpp"
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <unordered_map>
#include <fstream>
#include <algorithm>

namespace sim
{
// Ensamblador sencillo: 2 pasadas.
// 1) Junta labels -> PC. 2) Parsea instrucciones a Program.

// --- Tabla global de labels (compartida entre TUs) ---
static std::unordered_map<std::string, int>& labels_storage() {
  static std::unordered_map<std::string, int> L;
  return L;
}

// Quita comentarios que empiecen con ';' o '#'
static std::string strip_comment(const std::string& line) {
  auto pos = line.find_first_of(";#");
  if (pos == std::string::npos) return line;
  return line.substr(0, pos);
}

// ===== Helpers privados de Assembler =====
std::string Assembler::trim(const std::string& s) {
  std::size_t i = 0, j = s.size();
  while (i < j && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1]))) --j;
  return s.substr(i, j - i);
}

bool Assembler::starts_with(const std::string& s, const std::string& p) {
  if (s.size() < p.size()) return false;
  // case-insensitive simple
  for (std::size_t i = 0; i < p.size(); ++i) {
    if (std::toupper(static_cast<unsigned char>(s[i])) !=
        std::toupper(static_cast<unsigned char>(p[i])))
      return false;
  }
  return true;
}

// Acepta "REG0..REG7" y "[REGx]"
int Assembler::parse_reg(const std::string& token) {
  std::string t = token;
  if (!t.empty() && t.front() == '[' && t.back() == ']')
    t = t.substr(1, t.size() - 2);

  if (t.size() < 4 ||
      !(t[0] == 'R' || t[0] == 'r') ||
      !(t[1] == 'E' || t[1] == 'e') ||
      !(t[2] == 'G' || t[2] == 'g'))
    throw std::runtime_error("Registro inválido: " + token);

  int idx = std::stoi(t.substr(3));
  if (idx < 0 || idx > 7)
    throw std::runtime_error("Índice fuera de rango (REG0..REG7): " + token);
  return idx;
}

// ===== Utilidades locales =====

// Split por espacios/comas, manteniendo "[REGx]" entero
static std::vector<std::string> split_tokens(const std::string& line) {
  std::vector<std::string> t;
  std::string cur;
  auto flush = [&]{
    if (!cur.empty()) {
      while (!cur.empty() && cur.back() == ',') cur.pop_back();
      if (!cur.empty()) t.push_back(cur);
      cur.clear();
    }
  };

  bool in_brackets = false;
  for (char c : line) {
    if (c == '[') { in_brackets = true;  cur.push_back(c); continue; }
    if (c == ']') { in_brackets = false; cur.push_back(c); continue; }
    if (!in_brackets && (std::isspace(static_cast<unsigned char>(c)) || c == ',')) {
      flush();
    } else {
      cur.push_back(c);
    }
  }
  flush();
  return t;
}

// trim local rápido
static std::string normalize_line(const std::string& line) {
  std::size_t i = 0, j = line.size();
  while (i < j && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
  while (j > i && std::isspace(static_cast<unsigned char>(line[j - 1]))) --j;
  return line.substr(i, j - i);
}

// ===== Ensamblado =====

Program Assembler::assemble_from_string(const std::string& src) {
  // 1) Limpia líneas (sin comentarios y sin espacios)
  std::vector<std::string> lines;
  lines.reserve(256);
  {
    std::istringstream is(src);
    std::string line;
    while (std::getline(is, line)) {
      auto s = trim(strip_comment(line));
      if (!s.empty()) lines.push_back(s);
    }
  }

  // 2) Pasada 1: labels -> PC, y lista de código sin labels
  std::unordered_map<std::string, int> label_to_pc;
  std::vector<std::string> code_only;
  code_only.reserve(lines.size());
  int pc = 0;
  for (auto& l : lines) {
    if (!l.empty() && l.back() == ':') {
      auto lab = trim(l.substr(0, l.size() - 1));
      if (lab.empty())           throw std::runtime_error("Label vacío");
      if (label_to_pc.count(lab)) throw std::runtime_error("Label duplicado: " + lab);
      label_to_pc[lab] = pc;
    } else {
      code_only.push_back(l);
      ++pc;
    }
  }
  labels_storage() = label_to_pc;

  // 3) Pasada 2: parseo de instrucciones
  Program p;
  p.code.reserve(code_only.size());

  for (std::size_t i = 0; i < code_only.size(); ++i) {
    auto line = normalize_line(code_only[i]);
    auto tok  = split_tokens(line);
    if (tok.empty()) continue;

    Instr ins{};

    if (starts_with(tok[0], "LOAD")) {
      if (tok.size() != 3) throw std::runtime_error("Sintaxis LOAD: LOAD Rd, [Rs]");
      ins.op = OpCode::LOAD;
      ins.rd = parse_reg(tok[1]);
      ins.ra = parse_reg(tok[2]);
    }
    else if (starts_with(tok[0], "STORE")) {
      if (tok.size() != 3) throw std::runtime_error("Sintaxis STORE: STORE Rs, [Rd]");
      ins.op = OpCode::STORE;
      ins.ra = parse_reg(tok[1]);
      ins.rd = parse_reg(tok[2]);
    }
    else if (starts_with(tok[0], "FMUL")) {
      if (tok.size() != 4) throw std::runtime_error("Sintaxis FMUL: FMUL Rd, Ra, Rb");
      ins.op = OpCode::FMUL;
      ins.rd = parse_reg(tok[1]);
      ins.ra = parse_reg(tok[2]);
      ins.rb = parse_reg(tok[3]);
    }
    else if (starts_with(tok[0], "FADD")) {
      if (tok.size() != 4) throw std::runtime_error("Sintaxis FADD: FADD Rd, Ra, Rb");
      ins.op = OpCode::FADD;
      ins.rd = parse_reg(tok[1]);
      ins.ra = parse_reg(tok[2]);
      ins.rb = parse_reg(tok[3]);
    }
    else if (starts_with(tok[0], "REDUCE")) {
      if (tok.size() != 4) throw std::runtime_error("Sintaxis REDUCE: REDUCE Rd, Ra, Rb");
      ins.op = OpCode::REDUCE;
      ins.rd = parse_reg(tok[1]);
      ins.ra = parse_reg(tok[2]); // base
      ins.rb = parse_reg(tok[3]); // count
    }
    else if (starts_with(tok[0], "INC")) {
      if (tok.size() != 2) throw std::runtime_error("Sintaxis INC: INC Reg");
      ins.op = OpCode::INC;
      ins.rd = parse_reg(tok[1]);
    }
    else if (starts_with(tok[0], "DEC")) {
      if (tok.size() != 2) throw std::runtime_error("Sintaxis DEC: DEC Reg");
      ins.op = OpCode::DEC;
      ins.rd = parse_reg(tok[1]);
    }
    else if (starts_with(tok[0], "MOVI")) {
      if (tok.size() != 3) throw std::runtime_error("Sintaxis MOVI: MOVI Rd, Imm64");
      ins.op = OpCode::MOVI;
      ins.rd = parse_reg(tok[1]);
      // Inmediato decimal o 0xHEX
      std::string imm = tok[2];
      unsigned long long val = 0;
      try {
        if (imm.size() > 2 && imm[0] == '0' && (imm[1] == 'x' || imm[1] == 'X'))
          val = std::stoull(imm, nullptr, 16);
        else
          val = std::stoull(imm, nullptr, 10);
      } catch (...) {
        throw std::runtime_error("Inmediato inválido en MOVI: " + imm);
      }
      ins.imm = static_cast<std::uint64_t>(val);
    }
    else if (starts_with(tok[0], "JNZ")) {
      if (tok.size() != 2) throw std::runtime_error("Sintaxis JNZ: JNZ label (REG0 implícito)");
      ins.op   = OpCode::JNZ;
      ins.label = tok[1]; // PC real se resuelve en ejecución con la tabla de labels
    }
    else {
      throw std::runtime_error("Instrucción no soportada: " + tok[0]);
    }

    p.code.push_back(ins);
  }

  return p;
}

// Ensambla desde archivo (lee todo y delega)
Program Assembler::assemble_from_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("No se puede abrir ASM: " + path);
  std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return assemble_from_string(src);
}

// Exporta el mapa de labels (singleton)
std::unordered_map<std::string, int>& get_labels_singleton() {
  return labels_storage();
}

} // namespace sim
