#pragma once
#include "config.hpp"
#include "types.hpp"
#include <vector>
#include <mutex>

namespace sim {

// Memoria compartida “tipo DRAM” bien simple.
// - Tamaño: cfg::kMemWords palabras de 64 bits.
// - read64 / write64: dirección en bytes (se alinea a 8B internamente).
// - Mutex para serializar accesos (sin modelar latencias todavía).
class Memory {
public:
  Memory();

  // Acceso a palabra de 64 bits (addr en bytes, alineado a 8).
  Word read64(Addr addr);
  void write64(Addr addr, Word value);

private:
  std::vector<Word> mem_; // almacenamiento plano (uint64_t)
  std::mutex mtx_;        // exclusión básica para concurrencia
};

} // namespace sim
