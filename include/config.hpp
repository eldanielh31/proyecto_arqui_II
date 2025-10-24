#pragma once
#include <cstddef>
#include <iostream> // logs
#include <syncstream>

namespace cfg
{
    // Stdout/stderr sincronizados para logs en paralelo
    #define SOUT  std::osyncstream(std::cout)
    #define SERR  std::osyncstream(std::cerr)

    // --- Topología básica ---
    inline constexpr std::size_t kNumPEs = 4;     // 4 PEs
    inline constexpr std::size_t kMemWords = 512; // 512 palabras de 64 bits

    // --- Caché privada por PE: 2-way, 16 líneas totales, línea de 32B ---
    inline constexpr std::size_t kCacheWays  = 2;
    inline constexpr std::size_t kCacheLines = 16;
    inline constexpr std::size_t kLineBytes  = 32;

    // Tamaño de palabra (double = 8B)
    inline constexpr std::size_t kWordBytes = 8;

    // Modo de simulación
    inline constexpr bool kCycleDriven = true;

    // --- Flags de log rápidos ---
    inline constexpr bool kLogSim   = true; // ciclos del simulador
    inline constexpr bool kLogPE    = true; // accesos de cada PE
    inline constexpr bool kLogCache = true; // hits/misses/writeback
    inline constexpr bool kLogSnoop = true; // snoops/invalidaciones
    inline constexpr bool kLogBus   = true; // cola/broadcast del bus

    inline constexpr bool kDemoContention = true; // fuerza contención para ver coherencia

    // --- Trabajo del bus por ciclo ---
    // 1 operación por ciclo para ver claramente Upgr/Rd/Flush en orden.
    inline constexpr std::size_t kBusOpsPerCycle = 1; // (antes: 2)

    // Macro simple de logging condicional
    #define LOG_IF(flag, msg)        \
        do {                         \
            if (flag) {              \
                SERR << msg << '\n'; \
            }                        \
        } while (0)
} // namespace cfg
