#pragma once
#include "config.hpp"
#include "processor.hpp"
#include "cache.hpp"
#include "bus.hpp"
#include "memory.hpp"
#include "isa.hpp"
#include <memory>
#include <array>
#include <string>

namespace sim {

class Simulator {
public:
  Simulator();

  // Carga un conjunto de trazas simples (demo original)
  void load_demo_traces();

  // Carga un programa .asm y lo asigna a todos los PEs
  void load_program_all_from_file(const std::string& path);

  // Alternativa: cargar el mismo Program ya ensamblado
  void load_program_all(const Program& p);

  // Avanza N ciclos (compatibilidad)
  void run_cycles(std::size_t cycles);

  // Corre hasta que TODOS los PEs terminen (con tope de seguridad)
  void run_until_done(std::size_t safety_max = 1000000);

  // Inicializa el problema de producto punto (double):
  // - Carga A[], B[] en DRAM (N elementos cada uno)
  // - Inicializa partial_sums[PE] = 0.0
  // - Setea registros por PE:
  //     REG0 = N/kNumPEs   (contador de iteraciones para JNZ)
  //     REG1 = base del segmento A de este PE
  //     REG2 = base del segmento B de este PE
  //     REG3 = dirección de partial_sums[PE]
  void init_dot_problem(std::size_t N,
                        Addr baseA = 0x000,
                        Addr baseB = 0x100,
                        Addr basePS = 0x200);

private:
  Memory mem_;
  std::array<std::unique_ptr<Cache>, cfg::kNumPEs> caches_;
  std::unique_ptr<Bus> bus_;
  std::array<std::unique_ptr<Processor>, cfg::kNumPEs> pes_;
};

} // namespace sim
