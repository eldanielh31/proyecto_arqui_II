#pragma once
#include "config.hpp"
#include "types.hpp"
#include "cache_line.hpp"
#include "metrics.hpp"
#include "memory.hpp"   // Asegura que Memory esté declarado
#include <vector>
#include <optional>
#include <utility>
#include <cstdint>
#include <ostream>      // std::ostream

namespace sim {

class Bus;

// Caché set-asociativa con MESI, en corto:
// - LOAD miss  -> BusRd, trae línea. Queda E (o S si otro la tiene).
// - STORE hit  -> si S/E pide BusUpgr y pasa a M; si ya está M, escribe local.
// - STORE miss -> write-allocate + BusRdX, queda en M.
// Nota: simulamos write-through (se escribe a DRAM en cada store) y la línea se
// mantiene limpia (dirty=false). El bus acumula bytes y “flushes” por intervención.
class Cache {
public:
  Cache(PEId owner, Bus& bus, Memory& mem);

  // Accesos del PE (true = hit)
  bool load(Addr addr, std::size_t size, Word& out);
  bool store(Addr addr, std::size_t size, Word value);

  // Snoop desde el bus (true si invalidó/compartió/proveyó datos)
  bool snoop(const BusRequest& req, std::optional<Word>& data_out);

  // Métricas
  const Metrics& metrics() const { return metrics_; }
  void clear_metrics() { metrics_.reset(); }

  // Identificador del PE (para evitar self-snoop)
  PEId owner() const { return pe_; }

  // Dump rápido para debug (puede resaltar una dirección y volcar datos)
  void debug_dump(std::ostream& os,
                  std::optional<Addr> highlight_addr = std::nullopt,
                  bool dump_data = false) const;

private:
  struct Set {
    std::vector<CacheLine> ways; // cfg::kCacheWays
  };

  // Orden importa
  PEId      pe_;
  Bus&      bus_;
  Memory&   mem_;
  Metrics   metrics_;

  // Parámetros (antes de armar sets_)
  std::size_t      line_bytes_ = cfg::kLineBytes;
  std::size_t      num_lines_  = cfg::kCacheLines;
  std::size_t      num_sets_   = cfg::kCacheLines / cfg::kCacheWays;

  // Datos
  std::vector<Set> sets_;

  // Mapeo: devuelve (índice, tag)
  std::pair<std::size_t, std::uint64_t> index_tag(Addr addr) const;
  static inline Addr line_base(Addr addr) { return (addr / cfg::kLineBytes) * cfg::kLineBytes; }
  static inline std::size_t line_offset(Addr addr) { return static_cast<std::size_t>(addr % cfg::kLineBytes); }

  // Búsqueda y reemplazo
  int  find_way(std::size_t set_idx, std::uint64_t tag) const;
  int  select_victim(std::size_t set_idx) const; // FIFO simple

  // Operaciones internas (respetan offset/tamaño)
  bool read_hit (std::size_t set_idx, int way, Addr addr, std::size_t size, Word& out);
  bool write_hit(std::size_t set_idx, int way, Addr addr, std::size_t size, Word value);

  // Miss handling
  bool handle_load_miss (Addr addr, std::size_t size, Word& out);
  bool handle_store_miss(Addr addr, std::size_t size, Word value);
};

} // namespace sim
