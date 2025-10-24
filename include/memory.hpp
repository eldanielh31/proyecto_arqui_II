#pragma once
#include "config.hpp"
#include "types.hpp"
#include <vector>
#include <mutex>

namespace sim {

/**
 * Memoria simple de palabras de 64b. 
 * - read64/write64: acceso alineado a cfg::kWordBytes (8B).
 * - read_aligned/write_aligned: API genérica con alineamiento configurable.
 *   Útil para el requisito de “definir alineamiento”. Devuelve true/false.
 */
class Memory {
public:
  Memory();

  // Accesos a palabra de 64 bits (alineados a cfg::kWordBytes)
  Word read64(Addr addr) const;
  void write64(Addr addr, Word value);

  // --- API genérica con alineamiento definible (bytes) ---
  // Nota: retorna false si (addr o size) no respetan el alineamiento o hay OOB.
  bool read_aligned(Addr addr, void* dst, std::size_t bytes, std::size_t align) const;
  bool write_aligned(Addr addr, const void* src, std::size_t bytes, std::size_t align);

private:
  std::vector<Word> mem_;          // backing store
  mutable std::mutex mtx_;         // permite lockear en métodos const
};

} // namespace sim
