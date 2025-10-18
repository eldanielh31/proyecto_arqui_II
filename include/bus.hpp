#pragma once
#include "types.hpp"
#include <queue>
#include <vector>
#include <mutex>
#include <optional>
#include <cstdint>
#include <array>
#include <string>

namespace sim {

class Cache;

// Bus compartido (rollo MESI) bien simple:
// - Las cachés empujan requests (BusRequest en types.hpp)
// - El bus las difunde a todas (broadcast)
// - step() procesa 1 request por tick (FIFO)
// - Se llevan métricas básicas (bytes y conteos por comando)
class Bus {
public:
  explicit Bus(std::vector<Cache*>& caches); // conectar cachés al crear el bus

  // Permite reconectar/actualizar el set de cachés (útil en tests)
  void set_caches(const std::vector<Cache*>& caches);

  // Encola una solicitud del bus (thread-safe con mtx_)
  void push_request(const BusRequest& req);

  // Procesa una entrada de la cola y la difunde (llamar en el loop de sim)
  void step();

  // Métricas rápidas:
  std::uint64_t bytes() const;                 // bytes totales movidos por el bus
  std::uint64_t count_cmd(BusCmd cmd) const;   // cuántas veces vimos ese comando
  std::uint64_t flushes() const;               // intervenciones con datos (flush/write-back)

private:
  std::vector<Cache*> caches_;         // cachés conectadas
  std::queue<BusRequest> q_;           // cola FIFO de requests
  std::mutex mtx_;                     // para push_request

  // Estado para logs/debug
  bool bus_was_empty_{true};
  std::uint64_t next_tid_{1};          // id simple para requests (si se usa)

  // Métricas
  std::uint64_t bus_bytes_{0};         // acumulado de bytes transferidos
  std::array<std::uint64_t, 5> cmd_counts_{}; // contadores por BusCmd (0..4)
  std::uint64_t flushes_{0};           // número de flush/intervenciones con datos

  // Difunde la request a todas las cachés conectadas
  void broadcast(const BusRequest& req);
};

} // namespace sim
