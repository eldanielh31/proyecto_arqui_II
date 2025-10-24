#pragma once
#include <cstdint>

namespace sim {

/**
 * Métricas por caché/PE.
 * - loads/stores/hits/misses/invalidations/flushes: ya estaban.
 * - bus_bytes: tráfico de bus *atribuido a este PE* (nuevo: lo actualiza el Bus).
 * - transiciones MESI: contadores simples para análisis.
 */
struct Metrics {
  std::uint64_t loads = 0;
  std::uint64_t stores = 0;
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;

  std::uint64_t invalidations = 0;
  std::uint64_t flushes = 0;       // veces que esta caché hizo Flush (M->S / RdX Upgr)

  std::uint64_t bus_bytes = 0;     // tráfico de bus atribuido a este PE (req y/o flush)

  // ---- Transiciones MESI (nuevo) ----
  std::uint64_t trans_e_to_s = 0;
  std::uint64_t trans_s_to_m = 0;
  std::uint64_t trans_e_to_m = 0;
  std::uint64_t trans_m_to_s = 0;
  std::uint64_t trans_x_to_i = 0;  // cualquier {S,E,M} -> I por inval

  void reset() { *this = {}; }
};

} // namespace sim
