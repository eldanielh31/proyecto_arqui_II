#pragma once
#include "types.hpp"
#include "isa.hpp"
#include <vector>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace sim {

class Cache;

// Modo de ejecución: traza o ISA
enum class ExecMode { Trace, ISA };

class Processor {
public:
  // Construcción
  Processor(PEId id, Cache& cache);

  // Carga/mode
  void load_trace(const std::vector<Access>& trace);
  void load_program(const Program& p);
  void load_program_from_string(const std::string& asm_source);
  void load_program_from_file(const std::string& path);

  // Paso simple de ejecución
  void step();

  // ¿Terminó?
  bool is_done() const;

  // Registros (8 regs de 64 bits)
  void          set_reg(int idx, std::uint64_t val);
  std::uint64_t get_reg(int idx) const;

  PEId id() const { return id_; }

private:
  // Helpers ISA
  static double        as_double(std::uint64_t v);
  static std::uint64_t from_double(double d);
  void                 exec_one(); // ejecuta una instrucción de prog_

  // Acceso a memoria vía caché (64-bit)
  std::uint64_t mem_load64(std::uint64_t addr);
  void          mem_store64(std::uint64_t addr, std::uint64_t val);

  // Resolver labels (JNZ) – getter interno
  static const std::unordered_map<std::string,int>& labels_map();

  // Estado
  PEId        id_;
  Cache&      cache_;
  ExecMode    mode_ = ExecMode::ISA;

  // ISA
  Program     prog_{};
  std::size_t pc_ = 0;
  std::uint64_t reg_[8] = {0};

  // Trazas
  std::vector<Access> trace_{};
  std::size_t pc_trace_ = 0;
};

} // namespace sim
