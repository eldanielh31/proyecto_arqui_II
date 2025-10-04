#pragma once
#include "config.hpp"
#include "processor.hpp"
#include "cache.hpp"
#include "bus.hpp"
#include "memory.hpp"
#include <memory>
#include <array>

namespace sim {

class Simulator {
public:
  Simulator();

  // Carga un conjunto de trazas simples para validar lectura/escritura y coherencia
  void load_demo_traces();

  // Avanza N ciclos (PEs -> Bus)
  void run_cycles(std::size_t cycles);

private:
  Memory mem_;
  std::array<std::unique_ptr<Cache>, cfg::kNumPEs> caches_;
  std::unique_ptr<Bus> bus_;
  std::array<std::unique_ptr<Processor>, cfg::kNumPEs> pes_;
};

} // namespace sim
