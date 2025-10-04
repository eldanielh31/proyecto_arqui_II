#pragma once
#include <cstdint>

namespace sim {

struct Metrics {
  std::uint64_t loads = 0;
  std::uint64_t stores = 0;
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;
  std::uint64_t invalidations = 0;
  std::uint64_t bus_bytes = 0;

  void reset() { *this = {}; }
};

} // namespace sim
