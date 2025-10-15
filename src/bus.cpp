#include "bus.hpp"
#include "cache.hpp"
#include "config.hpp"
#include "types.hpp"
#include <algorithm>
#include <iomanip>

namespace sim {

static const char* cmd_str(BusCmd c) {
  switch (c) {
    case BusCmd::None:   return "None";
    case BusCmd::BusRd:  return "BusRd";
    case BusCmd::BusRdX: return "BusRdX";
    case BusCmd::BusUpgr:return "BusUpgr";
    case BusCmd::Flush:  return "Flush";
  }
  return "?";
}

Bus::Bus(std::vector<Cache*>& caches) : caches_(caches) {}

void Bus::push_request(const BusRequest& req) {
  {
    std::scoped_lock lk(mtx_);
    q_.push(req);
  }
  LOG_IF(cfg::kLogBus, "[BUS] push_request src=PE" << req.source
        << " cmd=" << cmd_str(req.cmd)
        << " addr=0x" << std::hex << req.addr << std::dec
        << " size=" << req.size);
}

void Bus::broadcast(const BusRequest& req) {
  LOG_IF(cfg::kLogBus, "[BUS] broadcast cmd=" << cmd_str(req.cmd)
        << " addr=0x" << std::hex << req.addr << std::dec
        << " src=PE" << req.source);

  // Contar comando
  cmd_counts_[static_cast<std::size_t>(req.cmd)]++;

  std::optional<Word> data;
  for (auto* c : caches_) {
    if (!c) continue;
    if (c->owner() == req.source) continue; // evitar self-snoop

    std::optional<Word> local_data;
    bool acted = c->snoop(req, local_data);
    if (acted) {
      LOG_IF(cfg::kLogBus, "  -> cache de PE" << c->owner() << " reaccionó"
            << (local_data.has_value() ? " (Flush)" : ""));
      if (local_data.has_value() && !data.has_value()) {
        data = local_data; // primera fuente de datos
      }
    }
  }

  if (data.has_value()) {
    // Hubo intervención con datos (Flush)
    bus_bytes_ += cfg::kLineBytes;
    flushes_++;
  } else {
    bus_bytes_ += req.size;
  }
  LOG_IF(cfg::kLogBus, "[BUS] bytes acumulados=" << bus_bytes_
        << " | flushes=" << flushes_);
}

void Bus::step() {
  std::size_t processed = 0;
  while (processed < cfg::kBusOpsPerCycle) {
    BusRequest req;
    {
      std::scoped_lock lk(mtx_);
      if (q_.empty()) {
        if (processed == 0)
          LOG_IF(cfg::kLogBus, "[BUS] step: cola vacía");
        break;
      }
      req = q_.front(); q_.pop();
    }
    LOG_IF(cfg::kLogBus, "[BUS] step: procesando cmd=" << cmd_str(req.cmd)
          << " addr=0x" << std::hex << req.addr << std::dec
          << " src=PE" << req.source);
    broadcast(req);
    processed++;
  }
}

} // namespace sim
