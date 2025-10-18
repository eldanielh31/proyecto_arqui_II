#pragma once
#include "config.hpp"
#include "types.hpp"
#include "cache_line.hpp"
#include "metrics.hpp"
#include <vector>
#include <optional>
#include <utility>
#include <cstdint>

namespace sim {

class Bus;
class Memory;

class Cache {
public:
  Cache(PEId owner, Bus& bus, Memory& mem);

  // Accesos locales (desde PE)
  bool load(Addr addr, std::size_t size, Word& out);   // devuelve hit/miss
  bool store(Addr addr, std::size_t size, Word value); // idem

  // Reacciones a snoop (invocado por Bus)
  // Retorna true si actuó (invalida/compartió/proveyó datos)
  bool snoop(const BusRequest& req, std::optional<Word>& data_out);

  // Consultas
  const Metrics& metrics() const { return metrics_; }
  void clear_metrics() { metrics_.reset(); }

  // Identificador del propietario (PE) para que el bus pueda evitar self-snoop
  PEId owner() const { return pe_; }

private:
  struct Set {
    std::vector<CacheLine> ways; // size = cfg::kCacheWays
  };

  // --- Orden IMPORTA: primero dependencias y parámetros, luego 'sets_' ---
  PEId      pe_;
  Bus&      bus_;
  Memory&   mem_;
  Metrics   metrics_;

  // Parámetros de la caché (deben inicializarse ANTES de construir 'sets_')
  std::size_t      line_bytes_ = cfg::kLineBytes;
  std::size_t      num_lines_  = cfg::kCacheLines;
  std::size_t      num_sets_   = cfg::kCacheLines / cfg::kCacheWays;

  // Estructura de datos (se inicializa en el constructor, ya con params listos)
  std::vector<Set> sets_;

  // Helpers de mapeo
  std::pair<std::size_t, std::uint64_t> index_tag(Addr addr) const;
  static inline Addr line_base(Addr addr) { return (addr / cfg::kLineBytes) * cfg::kLineBytes; }
  static inline std::size_t line_offset(Addr addr) { return static_cast<std::size_t>(addr % cfg::kLineBytes); }

  int  find_way(std::size_t set_idx, std::uint64_t tag) const;
  int  select_victim(std::size_t set_idx) const; // FIFO simple

  // Operaciones internas (ahora con addr/size para respetar offset)
  bool read_hit(std::size_t set_idx, int way, Addr addr, std::size_t size, Word& out);
  bool write_hit(std::size_t set_idx, int way, Addr addr, std::size_t size, Word value);

  // Miss handling
  bool handle_load_miss(Addr addr, std::size_t size, Word& out);
  bool handle_store_miss(Addr addr, std::size_t size, Word value);
};

} // namespace sim
