#pragma once
#include "types.hpp"
#include <cstdint>
#include <vector>

namespace sim {

struct CacheLine {
  bool  valid{false};
  bool  dirty{false};
  MESI  state{MESI::I};
  std::uint64_t tag{0};
  // Contenido de línea (opcional en simulador lógico); aquí solo tamaño lógico
  std::vector<std::uint8_t> data;

  CacheLine(std::size_t line_bytes = 0) : data(line_bytes, 0) {}
};

} // namespace sim
