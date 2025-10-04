#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace sim {

using Addr  = std::uint64_t;  // Dirección en bytes
using Word  = std::uint64_t;  // Dato de 64 bits (double reinterpretado si se desea)
using PEId  = std::uint32_t;

enum class MESI : std::uint8_t { I, S, E, M };

enum class BusCmd : std::uint8_t {
  None,
  BusRd,    // lectura compartible
  BusRdX,   // lectura con intención de escribir (exclusiva)
  BusUpgr,  // upgrade a M desde S/E sin recargar datos
  Flush     // respuesta con datos (intervención)
};

struct BusRequest {
  BusCmd     cmd{BusCmd::None};
  PEId       source{0};
  Addr       addr{0};
  std::size_t size{0}; // bytes
};

struct BusResponse {
  bool       has_data{false};
  Addr       addr{0};
  // Puede ampliarse con latencia, fuente, etc.
};

enum class AccessType : std::uint8_t { Load, Store };

// Métrica de acceso (simple; ampliable)
struct Access {
  AccessType type;
  Addr       addr;
  std::size_t size;
};

inline std::string to_string(MESI s) {
  switch (s) {
    case MESI::I: return "I";
    case MESI::S: return "S";
    case MESI::E: return "E";
    case MESI::M: return "M";
  }
  return "?";
}

} // namespace sim
