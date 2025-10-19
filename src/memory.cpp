#include "memory.hpp"
#include <cassert>
#include <cstring> // std::memcpy
#include <cstdint>

namespace sim {

Memory::Memory() : mem_(cfg::kMemWords, 0) {}

// Helper interno: rango válido (en bytes) sobre el backing store
static inline std::size_t mem_size_bytes(const std::vector<Word>& v) {
  return v.size() * cfg::kWordBytes;
}

Word Memory::read64(Addr addr) const {
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

// --- API genérica de alineamiento ---
// Permite leer cualquier tamaño "bytes" siempre que addr y bytes respeten "align".
bool Memory::read_aligned(Addr addr, void* dst, std::size_t bytes, std::size_t align) const {
  if (align == 0) return false;
  if ((addr % align) != 0 || (bytes % align) != 0) return false;

  std::scoped_lock lk(mtx_);
  const std::size_t total = mem_size_bytes(mem_);
  if (addr + bytes > total) return false;

  const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(mem_.data());
  std::memcpy(dst, base + addr, bytes);
  return true;
}

bool Memory::write_aligned(Addr addr, const void* src, std::size_t bytes, std::size_t align) {
  if (align == 0) return false;
  if ((addr % align) != 0 || (bytes % align) != 0) return false;

  std::scoped_lock lk(mtx_);
  const std::size_t total = mem_size_bytes(mem_);
  if (addr + bytes > total) return false;

  std::uint8_t* base = reinterpret_cast<std::uint8_t*>(mem_.data());
  std::memcpy(base + addr, src, bytes);
  return true;
}

} // namespace sim
