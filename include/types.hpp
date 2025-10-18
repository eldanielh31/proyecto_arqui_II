#pragma once
// Tipos base del simulador: direcciones, palabra de 64 bits, enums MESI/Bus,
// y structs para requests del bus y accesos de traza. Todo simple y directo.
#include <cstdint>
#include <optional>
#include <string>

namespace sim {

using Addr  = std::uint64_t;  // dirección en bytes
using Word  = std::uint64_t;  // dato de 64 bits (puede reinterpretarse como double)
using PEId  = std::uint32_t;

enum class MESI : std::uint8_t { I, S, E, M };

enum class BusCmd : std::uint8_t {
  None,
  BusRd,    // lectura compartible (trae línea)
  BusRdX,   // lectura con intención de escribir (exclusiva)
  BusUpgr,  // upgrade a M desde S/E sin recargar datos
  Flush     // respuesta con datos (intervención)
};

struct BusRequest {
  BusCmd       cmd{BusCmd::None};
  PEId         source{0};
  Addr         addr{0};
  std::size_t  size{0};   // bytes (normalmente tamaño de línea)
  std::uint64_t tid{0};   // id de transacción (lo asigna el bus)
};

struct BusResponse {
  bool       has_data{false};
  Addr       addr{0};
  // Se puede ampliar con latencia, fuente, etc.
};

enum class AccessType : std::uint8_t { Load, Store };

// Entrada de traza (acceso simple)
struct Access {
  AccessType type;
  Addr       addr;
  std::size_t size;
};

// Helpers para logs
inline std::string to_string(MESI s) {
  switch (s) {
    case MESI::I: return "I";
    case MESI::S: return "S";
    case MESI::E: return "E";
    case MESI::M: return "M";
  }
  return "?";
}

inline const char* cmd_str(BusCmd c) {
  switch (c) {
    case BusCmd::None:   return "None";
    case BusCmd::BusRd:  return "BusRd";
    case BusCmd::BusRdX: return "BusRdX";
    case BusCmd::BusUpgr:return "BusUpgr";
    case BusCmd::Flush:  return "Flush";
  }
  return "?";
}

} // namespace sim
