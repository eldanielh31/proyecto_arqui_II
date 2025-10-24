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

/**
 * @brief Caché set-asociativa con coherencia MESI.
 *
 * Políticas resumidas:
 * - LOAD miss: BusRd, trae línea completa. Estado final: E (o S si otro la tiene).
 * - STORE hit:
 *     - Si S/E: se pide BusUpgr (upgrade de permisos) y pasa a M.
 *     - Si M: se escribe localmente.
 *   Es *write-through* (se escribe DRAM en cada store) y mantenemos la línea limpia (dirty=false).
 * - STORE miss: write-allocate + BusRdX, escribimos y la línea queda en M (limpia).
 *
 * El bus modela contabilidad de bytes (size) y flushes por intervención.
 */
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

  // --- Nuevo: el Bus acredita tráfico al PE que origina la operación o provee Flush ---
  void account_bus_bytes(std::uint64_t b) { metrics_.bus_bytes += b; }

  /**
   * @brief Dump legible del contenido de la caché (para stepping/debug).
   * @param os              flujo de salida
   * @param highlight_addr  dirección a resaltar (opcional); si pertenece a una línea, se marca con '*'
   * @param dump_data       si true, también vuelca los 64b de cada palabra en la línea
   */
  void debug_dump(std::ostream& os,
                  std::optional<Addr> highlight_addr = std::nullopt,
                  bool dump_data = false) const;

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

  // Operaciones internas (respetan offset/tamaño dentro de la línea)
  bool read_hit (std::size_t set_idx, int way, Addr addr, std::size_t size, Word& out);
  bool write_hit(std::size_t set_idx, int way, Addr addr, std::size_t size, Word value);

  // Miss handling (write-allocate, write-through)
  bool handle_load_miss (Addr addr, std::size_t size, Word& out);
  bool handle_store_miss(Addr addr, std::size_t size, Word value);
};

} // namespace sim
