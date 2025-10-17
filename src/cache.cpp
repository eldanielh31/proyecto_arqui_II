#include "cache.hpp"
#include "bus.hpp"
#include "memory.hpp"
#include "config.hpp"
#include <cassert>
#include <cstring>
#include <iomanip>

namespace sim
{

  Cache::Cache(PEId owner, Bus &bus, Memory &mem)
      : pe_(owner), bus_(bus), mem_(mem)
  {
    sets_.resize(num_sets_);
    for (auto &set : sets_)
    {
      set.ways = std::vector<CacheLine>(cfg::kCacheWays, CacheLine(line_bytes_));
    }
  }

  std::pair<std::size_t, std::uint64_t> Cache::index_tag(Addr addr) const
  {
    std::size_t line_idx = (addr / line_bytes_);
    std::size_t set_idx  = line_idx % num_sets_;
    std::uint64_t tag    = line_idx / num_sets_;
    return {set_idx, tag};
  }

  int Cache::find_way(std::size_t set_idx, std::uint64_t tag) const
  {
    const auto &set = sets_[set_idx];
    for (int w = 0; w < static_cast<int>(set.ways.size()); ++w)
    {
      if (set.ways[w].valid && set.ways[w].tag == tag)
        return w;
    }
    return -1;
  }

  int Cache::select_victim(std::size_t set_idx) const
  {
    const auto &set = sets_[set_idx];
    for (int w = 0; w < static_cast<int>(set.ways.size()); ++w)
    {
      if (!set.ways[w].valid)
        return w;
    }
    return 0; // FIFO simplificado
  }

  bool Cache::read_hit(std::size_t set_idx, int way, Addr addr, std::size_t size, Word &out)
  {
    auto &line = sets_[set_idx].ways[way];
    if (!line.valid || line.state == MESI::I)
      return false;

    const std::size_t off = line_offset(addr);
    assert(off + size <= line_bytes_ && "Lectura cruza límite de línea");
    std::memcpy(&out, line.data.data() + off, size);

    metrics_.hits++;
    metrics_.loads++;
    LOG_IF(cfg::kLogCache, "[CACHE PE" << pe_ << "] READ HIT set=" << set_idx
                                       << " way=" << way << " state=" << to_string(line.state));
    return true;
  }

  bool Cache::write_hit(std::size_t set_idx, int way, Addr addr, std::size_t size, Word value)
  {
    auto &line = sets_[set_idx].ways[way];
    if (!line.valid || line.state == MESI::I) return false;

    if (line.state == MESI::S) {
      LOG_IF(cfg::kLogCache, "[CACHE PE" << pe_ << "] WRITE HIT necesita BusUpgr en addr=0x"
                                        << std::hex << addr << std::dec << " (state=S)");
      BusRequest req{BusCmd::BusUpgr, pe_, line_base(addr), line_bytes_};
      bus_.push_request(req);
    }
    if (line.state == MESI::S || line.state == MESI::E) {
      line.state = MESI::M;
    }

    const std::size_t off = line_offset(addr);
    assert(off + size <= line_bytes_);
    std::memcpy(line.data.data() + off, &value, size);

    // write-through: memoria actualizada, línea limpia
    mem_.write64(addr, value);
    line.dirty = false;

    metrics_.hits++;
    metrics_.stores++;
    LOG_IF(cfg::kLogCache, "[CACHE PE" << pe_ << "] WRITE HIT set=" << set_idx
                                      << " way=" << way << " -> state=" << to_string(line.state)
                                      << " dirty=0 (write-through)");
    return true;
  }


  bool Cache::handle_load_miss(Addr addr, std::size_t size, Word &out)
  {
    auto [set_idx, tag] = index_tag(addr);
    int victim = select_victim(set_idx);
    auto &line = sets_[set_idx].ways[victim];

    if (line.valid && line.dirty) {
      Addr victim_addr = ((line.tag * num_sets_) + set_idx) * line_bytes_;
      for (std::size_t off = 0; off < line_bytes_; off += sizeof(Word)) {
        Word w; std::memcpy(&w, line.data.data() + off, sizeof(Word));
        mem_.write64(victim_addr + off, w);
      }
      line.dirty = false;
      LOG_IF(cfg::kLogCache, "[CACHE PE" << pe_ << "] WB (LOAD miss) addr=0x"
                                        << std::hex << victim_addr << std::dec);
    }

    LOG_IF(cfg::kLogCache, "[CACHE PE" << pe_ << "] LOAD MISS addr=0x"
                                      << std::hex << addr << std::dec << " -> BusRd");
    BusRequest req{BusCmd::BusRd, pe_, line_base(addr), line_bytes_};
    bus_.push_request(req);

    Addr base = line_base(addr);
    for (std::size_t off = 0; off < line_bytes_; off += sizeof(Word)) {
      Word w = mem_.read64(base + off);
      std::memcpy(line.data.data() + off, &w, sizeof(Word));
    }

    line.valid = true;
    line.tag   = tag;
    line.state = MESI::S;    // seguro si no detectamos “shared”
    line.dirty = false;

    const std::size_t off = line_offset(addr);
    std::memcpy(&out, line.data.data() + off, size);

    metrics_.misses++;
    metrics_.loads++;
    return true;
  }

  bool Cache::handle_store_miss(Addr addr, std::size_t size, Word value)
  {
    auto [set_idx, tag] = index_tag(addr);
    int victim = select_victim(set_idx);
    auto &line = sets_[set_idx].ways[victim];

    if (line.valid && line.dirty) {
      Addr victim_addr = ((line.tag * num_sets_) + set_idx) * line_bytes_;
      for (std::size_t off = 0; off < line_bytes_; off += sizeof(Word)) {
        Word w; std::memcpy(&w, line.data.data() + off, sizeof(Word));
        mem_.write64(victim_addr + off, w);
      }
      line.dirty = false;
      LOG_IF(cfg::kLogCache, "[CACHE PE" << pe_ << "] WB (STORE miss) addr=0x"
                                        << std::hex << victim_addr << std::dec);
    }

    LOG_IF(cfg::kLogCache, "[CACHE PE" << pe_ << "] STORE MISS addr=0x"
                                      << std::hex << addr << std::dec << " -> BusRdX");
    BusRequest req{BusCmd::BusRdX, pe_, line_base(addr), line_bytes_};
    bus_.push_request(req);

    // Traigo la línea y escribo
    Addr base = line_base(addr);
    for (std::size_t off = 0; off < line_bytes_; off += sizeof(Word)) {
      Word w = mem_.read64(base + off);
      std::memcpy(line.data.data() + off, &w, sizeof(Word));
    }

    const std::size_t off = line_offset(addr);
    assert(off + size <= line_bytes_);
    std::memcpy(line.data.data() + off, &value, size);
    mem_.write64(addr, value);     // write-through

    line.valid = true;
    line.tag   = tag;
    line.state = MESI::M;
    line.dirty = false;

    metrics_.misses++;
    metrics_.stores++;
    return true;
  }


  bool Cache::load(Addr addr, std::size_t size, Word &out)
  {
    auto [set_idx, tag] = index_tag(addr);
    int way = find_way(set_idx, tag);
    LOG_IF(cfg::kLogCache, "[CACHE PE" << pe_ << "] LOAD addr=0x"
                                       << std::hex << addr << std::dec << " set=" << set_idx
                                       << " tag=" << tag << (way >= 0 ? " (hit)" : " (miss)"));
    if (way >= 0)
      return read_hit(set_idx, way, addr, size, out);
    return handle_load_miss(addr, size, out);
  }

  bool Cache::store(Addr addr, std::size_t size, Word value)
  {
    auto [set_idx, tag] = index_tag(addr);
    int way = find_way(set_idx, tag);
    LOG_IF(cfg::kLogCache, "[CACHE PE" << pe_ << "] STORE addr=0x"
                                       << std::hex << addr << std::dec << " set=" << set_idx
                                       << " tag=" << tag << (way >= 0 ? " (hit)" : " (miss)"));
    if (way >= 0)
      return write_hit(set_idx, way, addr, size, value);
    return handle_store_miss(addr, size, value);
  }

  bool Cache::snoop(const BusRequest &req, std::optional<Word> &data_out)
  {
    (void)data_out; // no usamos data_out con write-through; silencia -Wunused-parameter
    if (req.cmd == BusCmd::None)
      return false;

    auto [set_idx, tag] = index_tag(req.addr);
    int way = find_way(set_idx, tag);
    if (way < 0)
    {
      LOG_IF(cfg::kLogSnoop, "[SNOOP PE" << pe_ << "] cmd=" << (int)req.cmd
                                         << " addr=0x" << std::hex << req.addr << std::dec
                                         << " -> línea no presente");
      return false;
    }

    auto &line = sets_[set_idx].ways[way];
    LOG_IF(cfg::kLogSnoop, "[SNOOP PE" << pe_ << "] cmd=" << (int)req.cmd
                                       << " addr=0x" << std::hex << req.addr << std::dec
                                       << " estado=" << to_string(line.state));

    auto flush_full_line = [&](bool count_flush_metric){
      Addr base = line_base(req.addr);
      for (std::size_t off = 0; off < line_bytes_; off += sizeof(Word)) {
        Word w;
        std::memcpy(&w, line.data.data() + off, sizeof(Word));
        mem_.write64(base + off, w);
      }
      if (count_flush_metric) metrics_.flushes++;
    };

    switch (req.cmd)
    {
    case BusCmd::BusRd:
      if (line.state == MESI::M) {
        // Sólo si realmente está sucia (en este diseño no debería suceder)
        flush_full_line(true);
        line.state = MESI::S;
        line.dirty = false;
        LOG_IF(cfg::kLogSnoop, "  -> Flush + degradar a S");
      } else if (line.state == MESI::E) {
        line.state = MESI::S;
        LOG_IF(cfg::kLogSnoop, "  -> degradar E->S");
      }
      return true;

    case BusCmd::BusRdX:
    case BusCmd::BusUpgr:
      if (line.state == MESI::M && line.dirty) {
        flush_full_line(true);
        LOG_IF(cfg::kLogSnoop, "  -> Flush por RdX/Upgr (dirty)");
      }
      if (line.state != MESI::I) {
        line.state = MESI::I;
        line.valid = false;
        line.dirty = false;
        metrics_.invalidations++;
        LOG_IF(cfg::kLogSnoop, "  -> Invalidate línea (I)");
        return true;
      }
      return false;

    default:
      return false;
    }
  }

} // namespace sim
