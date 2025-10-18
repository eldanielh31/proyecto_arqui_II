#pragma once
#include <cstdint>

namespace sim {

// Métricas rápidas de caché/PE (contadores simples).
struct Metrics {
  std::uint64_t loads = 0;          // lecturas solicitadas por el PE
  std::uint64_t stores = 0;         // escrituras solicitadas por el PE
  std::uint64_t hits = 0;           // aciertos en caché
  std::uint64_t misses = 0;         // fallos en caché
  std::uint64_t invalidations = 0;  // líneas invalidadas por snoop
  std::uint64_t bus_bytes = 0;      // bytes movidos por el bus (si se usa local)
  std::uint64_t flushes = 0;        // veces que esta caché hizo Flush (M->S)

  // Deja todo en cero.
  void reset() { *this = {}; }
};

} // namespace sim
