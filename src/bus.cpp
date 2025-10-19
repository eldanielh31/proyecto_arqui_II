#include "bus.hpp"
#include "cache.hpp"
#include "config.hpp"
#include "types.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace sim {

Bus::Bus(std::vector<Cache*>& caches) : caches_(caches) {}

void Bus::set_caches(const std::vector<Cache*>& caches) {
  std::scoped_lock lk(mtx_);
  caches_ = caches;
}

void Bus::push_request(const BusRequest& req_in) {
  BusRequest req = req_in;
  if (req.tid == 0) req.tid = next_tid_++;

  {
    std::scoped_lock lk(mtx_);
    q_.push(req);
  }
  Addr line_base = (req.addr / cfg::kLineBytes) * cfg::kLineBytes;
  LOG_IF(cfg::kLogBus, "[BUS] push T#" << req.tid
        << " src=PE" << req.source
        << " " << cmd_str(req.cmd)
        << " line=0x" << std::hex << line_base << std::dec
        << " size=" << req.size);
}

void Bus::broadcast(const BusRequest& req) {
  Addr line_base = (req.addr / cfg::kLineBytes) * cfg::kLineBytes;
  LOG_IF(cfg::kLogBus, "[BUS] proc T#" << req.tid
        << " PE" << req.source
        << " " << cmd_str(req.cmd)
        << " line=0x" << std::hex << line_base << std::dec);

  // Contar comando
  cmd_counts_[static_cast<std::size_t>(req.cmd)]++;

  // Recorremos cachés (snoop). Si alguna devuelve datos (Flush), lo registramos.
  std::optional<Word> data_from_peer;
  std::vector<int> acted_pes;
  int provider_id = -1; // PE que proveyó datos (Flush), si aplica

  for (auto* c : caches_) {
    if (!c) continue;
    if (c->owner() == req.source) continue; // evitar self-snoop

    std::optional<Word> local;
    bool acted = c->snoop(req, local);
    if (acted) acted_pes.push_back(static_cast<int>(c->owner()));
    if (local.has_value() && provider_id < 0) {
      data_from_peer = local; 
      provider_id = static_cast<int>(c->owner());
    }
  }

  // Contabilización de tráfico en el bus:
  std::uint64_t add_bytes = 0;
  if (data_from_peer.has_value()) {
    // Intervención: transferencia de una línea completa
    add_bytes = cfg::kLineBytes;
    bus_bytes_ += add_bytes;
    flushes_++;
  } else {
    // Tráfico reportado por la petición (p.e. Upgr sin datos)
    add_bytes = req.size;
    bus_bytes_ += add_bytes;
  }

  // --- NUEVO: Acreditar tráfico por-PE ---
  // 1) Al emisor de la transacción
  for (auto* c : caches_) {
    if (!c) continue;
    if (c->owner() == req.source) {
      c->account_bus_bytes(add_bytes); // el requester pagó/recibió este tráfico
      break;
    }
  }
  // 2) Al proveedor del Flush (si hubo)
  if (provider_id >= 0) {
    for (auto* c : caches_) {
      if (!c) continue;
      if (static_cast<int>(c->owner()) == provider_id) {
        c->account_bus_bytes(cfg::kLineBytes); // el que flushea también participa
        break;
      }
    }
  }

  // Resumen de snoops
  std::ostringstream oss;
  if (acted_pes.empty()) {
    oss << "none";
  } else {
    for (std::size_t i = 0; i < acted_pes.size(); ++i) {
      if (i) oss << ",";
      oss << "PE" << acted_pes[i];
    }
  }

  LOG_IF(cfg::kLogBus, "[BUS] T#" << req.tid
        << " snoops:" << (acted_pes.empty() ? " none" : (" " + oss.str()))
        << " | bytes+=" << add_bytes
        << " | total=" << bus_bytes_
        << " | flushes=" << flushes_);
}

void Bus::step() {
  std::size_t processed = 0;
  while (processed < cfg::kBusOpsPerCycle) {
    BusRequest req;
    {
      std::scoped_lock lk(mtx_);
      if (q_.empty()) {
        if (!bus_was_empty_) {
          LOG_IF(cfg::kLogBus, "[BUS] step: cola vacía");
          bus_was_empty_ = true;
        }
        break;
      }
      req = q_.front(); q_.pop();
    }
    bus_was_empty_ = false;
    broadcast(req);
    processed++;
  }
}

std::uint64_t Bus::bytes() const {
  return bus_bytes_;
}

std::uint64_t Bus::count_cmd(BusCmd cmd) const {
  return cmd_counts_[static_cast<std::size_t>(cmd)];
}

std::uint64_t Bus::flushes() const {
  return flushes_;
}

} // namespace sim
