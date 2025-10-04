#include "memory.hpp"
#include <cassert>

namespace sim {

Memory::Memory() : mem_(cfg::kMemWords, 0) {}

Word Memory::read64(Addr addr) {
  std::scoped_lock lk(mtx_);
  // Direccionamiento por palabras de 8 bytes (alineado por simplicidad)
  assert(addr % cfg::kWordBytes == 0);
  std::size_t idx = addr / cfg::kWordBytes;
  if (idx >= mem_.size()) return 0;
  return mem_[idx];
}

void Memory::write64(Addr addr, Word value) {
  std::scoped_lock lk(mtx_);
  assert(addr % cfg::kWordBytes == 0);
  std::size_t idx = addr / cfg::kWordBytes;
  if (idx < mem_.size()) mem_[idx] = value;
}

} // namespace sim
