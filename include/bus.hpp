#pragma once
#include "types.hpp"
#include <queue>
#include <vector>
#include <mutex>
#include <optional>

namespace sim {

class Cache;

class Bus {
public:
  explicit Bus(std::vector<Cache*>& caches);

  // permite actualizar las cachés registradas sin reconstruir el Bus
  void set_caches(const std::vector<Cache*>& caches) { caches_ = caches; }

  void push_request(const BusRequest& req);
  void step();
  std::uint64_t& bus_bytes() { return bus_bytes_; }

private:
  std::vector<Cache*> caches_;
  std::queue<BusRequest> q_;
  std::mutex mtx_;
  std::uint64_t bus_bytes_{0};

  void broadcast(const BusRequest& req);
};

} // namespace sim
