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

  // Conectar (o reconectar) cachés luego de construir el bus
  void set_caches(const std::vector<Cache*>& caches);

  // Cola del bus
  void push_request(const BusRequest& req);
  void step();

  // Métricas
  std::uint64_t bytes() const;
  std::uint64_t count_cmd(BusCmd cmd) const;
  std::uint64_t flushes() const;

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
