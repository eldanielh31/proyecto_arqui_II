#include "assembler.hpp"
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <unordered_map>
#include <fstream>

namespace sim {

// Almacén único de labels accesible desde cualquier TU
static std::unordered_map<std::string,int>& labels_storage() {
  static std::unordered_map<std::string,int> L;
  return L;
}

static std::string strip_comment(const std::string& line) {
  auto pos = line.find_first_of(";#");
  if (pos == std::string::npos) return line;
  return line.substr(0, pos);
}

std::string Assembler::trim(const std::string& s) {
  size_t i=0, j=s.size();
  while (i<j && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  while (j>i && std::isspace(static_cast<unsigned char>(s[j-1]))) --j;
  return s.substr(i, j-i);
}

bool Assembler::starts_with(const std::string& s, const std::string& p) {
  return s.size()>=p.size() && std::equal(p.begin(), p.end(), s.begin(),
         [](char a, char b){ return std::toupper(a)==std::toupper(b);});
}

int Assembler::parse_reg(const std::string& token) {
  // Espera "REG0".."REG7"
  if (token.size()<4 || !(token[0]=='R' || token[0]=='r') ||
      !(token[1]=='E' || token[1]=='e') ||
      !(token[2]=='G' || token[2]=='g')) {
    throw std::runtime_error("Registro inválido: " + token);
  }
  int idx = std::stoi(token.substr(3));
  if (idx < 0 || idx > 7) throw std::runtime_error("Registro fuera de rango: " + token);
  return idx;
}

Program Assembler::assemble_from_string(const std::string& src) {
  // 1) Normalizar líneas, quitar comentarios y espacios
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

  // 2) Primera pasada: resolver labels y construir lista de "código" sin labels
  std::unordered_map<std::string,int> label_to_pc;
  std::vector<std::string> code_only;
  code_only.reserve(lines.size());
  int pc=0;
  for (auto& l : lines) {
    if (!l.empty() && l.back()==':') {
      auto lab = trim(l.substr(0, l.size()-1));
      if (lab.empty()) throw std::runtime_error("Label vacío");
      if (label_to_pc.count(lab)) throw std::runtime_error("Label duplicado: " + lab);
      label_to_pc[lab] = pc;
    } else {
      code_only.push_back(l);
      pc++;
    }
  }

  // 3) Segunda pasada: parsear instrucciones
  Program p;
  p.code.reserve(code_only.size());
  for (auto& l : code_only) {
    // Tokenizar por comas/espacios/corchetes
    std::vector<std::string> tok;
    tok.reserve(8);
    std::string cur;
    auto flush_cur = [&](){
      if (!cur.empty()) { tok.push_back(trim(cur)); cur.clear(); }
    };
    for (char c : l) {
      if (c=='[' || c==']' || c==',' || std::isspace(static_cast<unsigned char>(c))) {
        flush_cur();
      } else {
        cur.push_back(c);
      }
    }
    flush_cur();
    if (tok.empty()) continue;

    Instr ins{};
    if (starts_with(tok[0],"LOAD")) {
      if (tok.size()!=3) throw std::runtime_error("Sintaxis LOAD: LOAD Rd, [Rs]");
      ins.op = OpCode::LOAD;
      ins.rd = parse_reg(tok[1]);
      ins.ra = parse_reg(tok[2]);
    } else if (starts_with(tok[0],"STORE")) {
      if (tok.size()!=3) throw std::runtime_error("Sintaxis STORE: STORE Rs, [Rd]");
      ins.op = OpCode::STORE;
      ins.ra = parse_reg(tok[1]);
      ins.rd = parse_reg(tok[2]);
    } else if (starts_with(tok[0],"FMUL")) {
      if (tok.size()!=4) throw std::runtime_error("Sintaxis FMUL: FMUL Rd, Ra, Rb");
      ins.op = OpCode::FMUL;
      ins.rd = parse_reg(tok[1]);
      ins.ra = parse_reg(tok[2]);
      ins.rb = parse_reg(tok[3]);
    } else if (starts_with(tok[0],"FADD")) {
      if (tok.size()!=4) throw std::runtime_error("Sintaxis FADD: FADD Rd, Ra, Rb");
      ins.op = OpCode::FADD;
      ins.rd = parse_reg(tok[1]);
      ins.ra = parse_reg(tok[2]);
      ins.rb = parse_reg(tok[3]);
    } else if (starts_with(tok[0],"INC")) {
      if (tok.size()!=2) throw std::runtime_error("Sintaxis INC: INC Reg");
      ins.op = OpCode::INC;
      ins.rd = parse_reg(tok[1]);
    } else if (starts_with(tok[0],"DEC")) {
      if (tok.size()!=2) throw std::runtime_error("Sintaxis DEC: DEC Reg");
      ins.op = OpCode::DEC;
      ins.rd = parse_reg(tok[1]);
    } else if (starts_with(tok[0],"MOVI")) {
      if (tok.size()!=3) throw std::runtime_error("Sintaxis MOVI: MOVI Reg, Inm");
      ins.op = OpCode::MOVI;
      ins.rd = parse_reg(tok[1]);
      // inmediato: decimal o 0xHEX
      std::string imm = tok[2];
      unsigned long long val = 0;
      try {
        if (imm.size()>2 && (imm[0]=='0') && (imm[1]=='x' || imm[1]=='X')) {
          val = std::stoull(imm, nullptr, 16);
        } else {
          val = std::stoull(imm, nullptr, 10);
        }
      } catch (...) { throw std::runtime_error("Inmediato inválido en MOVI: " + imm); }
      ins.imm = static_cast<std::uint64_t>(val);
    } else if (starts_with(tok[0],"JNZ")) {
      if (tok.size()!=2) throw std::runtime_error("Sintaxis JNZ: JNZ label (usa REG0 implícito)");
      ins.op    = OpCode::JNZ;
      ins.label = tok[1];
      // El PC real se resuelve en ejecución, usando la tabla de labels
    } else {
      throw std::runtime_error("Instrucción no soportada: " + tok[0]);
    }

    p.code.push_back(ins);
  }

  // 4) Guardar el mapa de labels en el singleton compartido
  labels_storage() = std::move(label_to_pc);
  return p;
}

Program Assembler::assemble_from_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("No se puede abrir archivo ASM: " + path);
  std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return assemble_from_string(src);
}

// Export del singleton (enlace externo)
std::unordered_map<std::string,int>& get_labels_singleton() {
  return labels_storage();
}

} // namespace sim
