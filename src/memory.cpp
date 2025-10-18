#include "memory.hpp"
#include <cassert>

namespace sim {

// Memoria compartida súper simple (tipo DRAM):
// - Arreglo de Word (64 bits) con tamaño cfg::kMemWords.
// - Accesos alineados a 8 bytes.
// - Mutex para serializar lecturas/escrituras (sin latencias).
Memory::Memory() : mem_(cfg::kMemWords, 0) {}

Word Memory::read64(Addr addr) {
  std::scoped_lock lk(mtx_);
  assert(addr % cfg::kWordBytes == 0);     // debe venir alineada
  std::size_t idx = addr / cfg::kWordBytes;
  if (idx >= mem_.size()) return 0;        // fuera de rango -> 0
  return mem_[idx];
}

void Memory::write64(Addr addr, Word value) {
  std::scoped_lock lk(mtx_);
  assert(addr % cfg::kWordBytes == 0);     // debe venir alineada
  std::size_t idx = addr / cfg::kWordBytes;
  if (idx < mem_.size()) mem_[idx] = value;
}

} // namespace sim
