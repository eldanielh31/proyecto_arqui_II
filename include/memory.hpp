#pragma once
#include "config.hpp"
#include "types.hpp"
#include <vector>
#include <mutex>

namespace sim {

class Memory {
public:
  Memory();

  // Accesos a palabra de 64 bits
  Word read64(Addr addr);
  void write64(Addr addr, Word value);

private:
  std::vector<Word> mem_;
  std::mutex mtx_; // simple exclusión; el modelo de latencia puede añadirse después
};

} // namespace sim
