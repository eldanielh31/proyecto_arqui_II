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

// Modo de ejecución: por traza o ejecutando ISA
enum class ExecMode { Trace, ISA };

class Processor {
public:
  // Construcción: id del PE y su caché
  Processor(PEId id, Cache& cache);

  // Carga de trabajo
  void load_trace(const std::vector<Access>& trace); // lista de accesos (addr, load/store)
  void load_program(const Program& p);               // programa ya ensamblado
  void load_program_from_string(const std::string& asm_source); // asm en texto
  void load_program_from_file(const std::string& path);         // asm desde archivo

  // Un paso de CPU (avanza una instrucción o un acceso)
  void step();

  // ¿Ya terminó? (pc fuera de rango o traza consumida)
  bool is_done() const;

  // Registros (8 de 64 bits)
  void          set_reg(int idx, std::uint64_t val);
  std::uint64_t get_reg(int idx) const;

  PEId id() const { return id_; }

private:
  // Helpers para ISA
  static double        as_double(std::uint64_t v);   // reinterpretar u64 como f64
  static std::uint64_t from_double(double d);        // f64 -> u64
  void                 exec_one();                   // ejecuta prog_[pc_]

  // Acceso a memoria vía caché (64 bits)
  std::uint64_t mem_load64(std::uint64_t addr);
  void          mem_store64(std::uint64_t addr, std::uint64_t val);

  // Labels del ensamblador (para JNZ)
  static const std::unordered_map<std::string,int>& labels_map();

  // Estado
  PEId        id_;
  Cache&      cache_;
  ExecMode    mode_ = ExecMode::ISA;

  // ISA
  Program     prog_{};
  std::size_t pc_ = 0;
  std::uint64_t reg_[8] = {0};

  // Traza
  std::vector<Access> trace_{};
  std::size_t pc_trace_ = 0;
};

} // namespace sim
