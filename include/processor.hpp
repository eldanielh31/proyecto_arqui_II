#pragma once
#include "types.hpp"
#include <vector>

namespace sim {

class Cache;

class Processor {
public:
  Processor(PEId id, Cache& cache);

  // Ejecución por ciclo (programa mínimo de prueba)
  void step();

  // Carga un “programa” de accesos (para pruebas tempranas)
  void load_trace(const std::vector<Access>& trace);

private:
  PEId      id_;
  Cache&    cache_;
  std::size_t pc_{0};
  std::vector<Access> trace_;
};

} // namespace sim
