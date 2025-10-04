#include "processor.hpp"
#include "cache.hpp"
#include "config.hpp"
#include "types.hpp"
#include <iostream>
#include <iomanip>

namespace sim {

Processor::Processor(PEId id, Cache& cache) : id_(id), cache_(cache) {}

void Processor::load_trace(const std::vector<Access>& trace) {
  trace_ = trace;
  pc_ = 0;
}

void Processor::step() {
  if (pc_ >= trace_.size()) return;
  const auto& a = trace_[pc_++];

  Word w = 0;
  if (a.type == AccessType::Load) {
    LOG_IF(cfg::kLogPE, "[PE" << id_ << "] LOAD  addr=0x" << std::hex << a.addr << std::dec);
    cache_.load(a.addr, a.size, w);
  } else {
    LOG_IF(cfg::kLogPE, "[PE" << id_ << "] STORE addr=0x" << std::hex << a.addr << std::dec);
    cache_.store(a.addr, a.size, a.addr /*valor demostrativo*/);
  }
}

} // namespace sim
