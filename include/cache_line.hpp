#pragma once
#include "types.hpp"
#include <cstdint>
#include <vector>

namespace sim {

// Línea de caché (MESI) versión light.
// Campos básicos: valid, dirty, estado MESI, tag y los bytes de la línea.
struct CacheLine {
  bool  valid{false};            // la tenemos en caché
  bool  dirty{false};            // cambios locales sin escribir
  MESI  state{MESI::I};          // M/E/S/I
  std::uint64_t tag{0};          // etiqueta (conjunto/índice)
  std::vector<std::uint8_t> data; // contenido (solo lógico en el sim)

  // Crea la línea con 'line_bytes' bytes en cero.
  CacheLine(std::size_t line_bytes = 0) : data(line_bytes, 0) {}
};

} // namespace sim
