#pragma once
/**
 * Simulator: orquesta Bus, Memoria, Caches y PEs.
 * 
 * Cambios relevantes:
 * - Stepping interactivo:
 *     - run_stepping(): bucle que lee comandos desde stdin.
 *       ENTER=step, c=continuar, r=regs, b=bus, q=salir
 *     - step_one(): ejecuta 1 paso (cada PE 1 instrucción si puede) + 1 avance de bus,
 *                   imprime: BEFORE/AFTER de registros por PE (con diffs) y dump de caché.
 *     - all_done(): true si todos los PEs finalizaron.
 *     - dump_cache()/dump_regs(): utilidades para debug.
 * 
 * Nota importante (unique_ptr con tipos incompletos):
 *   Adelantamos (forward-declare) Bus/Cache/Processor y guardamos unique_ptr a ellas.
 *   Para evitar el error de “incomplete type” con unique_ptr, declaramos ~Simulator()
 *   aquí y lo definimos en el .cpp, donde las clases ya están completamente definidas.
 */

#include <array>
#include <memory>
#include <optional>
#include <cstddef>
#include <string>

#include "config.hpp"
#include "types.hpp"
#include "memory.hpp"   // 'mem_' necesita tipo completo

namespace sim {

class Cache;
class Bus;
class Processor;
struct Program;

class Simulator {
public:
  Simulator();
  ~Simulator();  // Definición en el .cpp

  // ---- Inicialización / carga de programas
  void init_dot_problem(std::size_t N, Addr baseA, Addr baseB, Addr basePS);
  void load_demo_traces();
  void load_program_all(const Program &p);
  void load_program_all_from_file(const std::string &path);

  // ---- Ejecución "normal"
  void run_cycles(std::size_t cycles);
  void run_until_done(std::size_t safety_max = 100000);

  // ---- Stepping interactivo
  // ENTER: step | c: continuar | r: regs | b: bus | q: salir
  void run_stepping();
  void step_one();          // 1 paso: PEs (<=1 instr), bus (1 step), BEFORE/AFTER + dump de caches
  bool all_done() const;    // ¿todos los PEs finalizaron?

  // ---- Utilidades de dump
  void dump_cache(std::size_t pe, std::optional<std::size_t> only_set = std::nullopt) const;
  void dump_regs(std::size_t pe) const;

private:
  std::unique_ptr<Bus> bus_;
  std::array<std::unique_ptr<Cache>,     cfg::kNumPEs> caches_;
  std::array<std::unique_ptr<Processor>, cfg::kNumPEs> pes_;
  Memory mem_;
};

} // namespace sim
