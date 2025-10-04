#pragma once
#include <cstddef>
#include <iostream>  // para logs

namespace cfg {
// Arquitectura base del proyecto
inline constexpr std::size_t kNumPEs        = 4;

// Memoria principal: 512 posiciones de 64 bits (double)
inline constexpr std::size_t kMemWords      = 512;

// Caché privada por PE: 2-way set associative, 16 bloques totales, línea de 32 bytes
inline constexpr std::size_t kCacheWays     = 2;
inline constexpr std::size_t kCacheLines    = 16;
inline constexpr std::size_t kLineBytes     = 32;

// Tamaño de palabra de datos (double, 8 bytes)
inline constexpr std::size_t kWordBytes     = 8;

// Parámetros de simulación
inline constexpr bool kCycleDriven          = true;

// ====== Flags de LOG (actívalos/desactívalos según necesites) ======
inline constexpr bool kLogSim    = true;   // logs del simulador/ciclos
inline constexpr bool kLogPE     = true;   // logs de cada PE (accesos)
inline constexpr bool kLogCache  = true;   // logs de caché (hits/misses/wb)
inline constexpr bool kLogSnoop  = true;   // logs de snoops/invalidaciones
inline constexpr bool kLogBus    = true;   // logs del bus (cola/broadcast)

// Macro simple de logging condicional
#define LOG_IF(flag, msg) do { if (flag) { std::cerr << msg << std::endl; } } while (0)
} // namespace cfg
