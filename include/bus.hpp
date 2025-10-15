#pragma once
#include "types.hpp"
#include <queue>
#include <vector>
#include <mutex>
#include <optional>
#include <cstdint>
#include <array>

namespace sim {

class Cache;

class Bus {
public:
  explicit Bus(std::vector<Cache*>& caches);

  // Permite actualizar las cachés registradas sin reconstruir el Bus
  void set_caches(const std::vector<Cache*>& caches) { caches_ = caches; }

  // PEs/Cachés publican peticiones aquí
  void push_request(const BusRequest& req);

  // Avance por ciclo: procesa hasta K transacciones pendientes
  void step();

  // Métricas del bus
  std::uint64_t bytes() const { return bus_bytes_; }
  std::uint64_t count_cmd(BusCmd c) const { return cmd_counts_[static_cast<std::size_t>(c)]; }
  std::uint64_t flushes() const { return flushes_; }

private:
  std::vector<Cache*> caches_;
  std::queue<BusRequest> q_;
  std::mutex mtx_;

  // Métricas
  std::uint64_t bus_bytes_{0};
  std::array<std::uint64_t, 6> cmd_counts_{}; // por cada BusCmd (0..5)
  std::uint64_t flushes_{0};                  // número de veces que hubo intervención con datos

  void broadcast(const BusRequest& req);
};

} // namespace sim
