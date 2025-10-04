#include "simulator.hpp"
#include "config.hpp"
#include <iostream>

namespace sim {

Simulator::Simulator() {
  std::vector<Cache*> cache_ptrs;
  bus_ = std::make_unique<Bus>(cache_ptrs);

  for (std::size_t i = 0; i < cfg::kNumPEs; ++i) {
    caches_[i] = std::make_unique<Cache>(static_cast<PEId>(i), *bus_, mem_);
  }

  cache_ptrs.clear();
  for (auto& up : caches_) cache_ptrs.push_back(up.get());
  bus_->set_caches(cache_ptrs);

  for (std::size_t i = 0; i < cfg::kNumPEs; ++i) {
    pes_[i] = std::make_unique<Processor>(static_cast<PEId>(i), *caches_[i]);
  }

  std::cout << "[Init] Sistema configurado con " << cfg::kNumPEs
            << " PEs, cachés privadas y bus compartido.\n";
}

void Simulator::load_demo_traces() {
  // ======== DEMO POR DEFECTO (direcciones disjuntas, SIN contención) ========
  // Comportamiento: cada PE hace LOAD(Exclusivo) -> STORE(M) -> LOAD(hit),
  // sin invalidaciones porque no comparten líneas.
  // {
  //   const Addr baseA = 0;
  //   const Addr baseB = 8 * 64;
  //   const Addr stride = 64;

  //   for (std::size_t i = 0; i < cfg::kNumPEs; ++i) {
  //     std::vector<Access> t;
  //     t.push_back({AccessType::Load,  baseA + i * stride, 8});
  //     t.push_back({AccessType::Store, baseB + i * stride, 8});
  //     t.push_back({AccessType::Load,  baseA + i * stride, 8});
  //     pes_[i]->load_trace(t);
  //   }

  //   std::cout << "[Init] Trazas de demostración cargadas (" << cfg::kNumPEs
  //             << " PEs).\n";
  // }

  // ========================================================================
  // ============= OPCIÓN 1: CONTENCIÓN REAL (INVALIDACIONES) ===============
  // Descomentar este bloque para que PE0 y PE1 peleen por la MISMA dirección.
  // Esperado:
  //  - PE0: LOAD -> E
  //  - PE1: LOAD -> E (degrada PE0 a S) o S (si PE0 ya en E; según orden)
  //  - PE0: STORE -> BusUpgr -> pasa a M; PE1 invalida (S->I)
  //  - PE1: STORE posterior -> BusRdX -> invalida a PE0 si estaba en S/E, o interviene si M

  {
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i) {
      std::vector<Access> t;
      if (i == 0) {
        // PE0 y PE1 usan la MISMA línea 0x100
        t.push_back({AccessType::Load,  0x100, 8}); // Miss -> E
        t.push_back({AccessType::Store, 0x100, 8}); // Upgr -> M, invalida a PE1
        t.push_back({AccessType::Load,  0x100, 8}); // Hit en M
      } else if (i == 1) {
        t.push_back({AccessType::Load,  0x100, 8}); // Puede degradar al otro a S, según timing
        t.push_back({AccessType::Store, 0x100, 8}); // BusRdX -> invalida al otro si lo tenía
        t.push_back({AccessType::Load,  0x100, 8}); // Hit
      } else {
        // Otros PEs acceden a direcciones disjuntas para no mezclar señales
        t.push_back({AccessType::Load,  0x300 + i * 0x40, 8});
        t.push_back({AccessType::Store, 0x500 + i * 0x40, 8});
        t.push_back({AccessType::Load,  0x300 + i * 0x40, 8});
      }
      pes_[i]->load_trace(t);
    }
    std::cout << "[Init] (OPC1) Contención PE0<->PE1 en 0x100 LISTA (bloque comentado).\n";
  }

  // ========================================================================
  // ======= OPCIÓN 2: FLUSH (M->S por lectura remota y write-back) =========
  // Descomentar este bloque para forzar que PE0 lleve la línea a M y luego
  // PE1 haga BusRd que causa intervención (Flush) y downgrade M->S en PE0.
  /*
  {
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i) {
      std::vector<Access> t;
      if (i == 0) {
        t.push_back({AccessType::Load,  0x180, 8});  // Miss -> E
        t.push_back({AccessType::Store, 0x180, 8});  // Upgr -> M
        t.push_back({AccessType::Load,  0x180, 8});  // Hit M
      } else if (i == 1) {
        t.push_back({AccessType::Load,  0x180, 8});  // BusRd -> PE0 Flush + downgrade a S
        t.push_back({AccessType::Load,  0x180, 8});  // Ahora S (compartida)
        t.push_back({AccessType::Store, 0x180, 8});  // BusUpgr/BusRdX -> invalidar al otro
      } else {
        t.push_back({AccessType::Load,  0x700 + i * 0x40, 8});
        t.push_back({AccessType::Store, 0x900 + i * 0x40, 8});
        t.push_back({AccessType::Load,  0x700 + i * 0x40, 8});
      }
      pes_[i]->load_trace(t);
    }
    std::cout << "[Init] (OPC2) Secuencia con Flush y downgrade M->S LISTA (bloque comentado).\n";
  }
  */

  // ========================================================================
  // ======= OPCIÓN 3: CONFLICTO DE SETS (reemplazos + write-back) ==========
  // Direcciones que caen en el mismo set para provocar reemplazos y WB.
  /*
  {
    auto same_set = [](Addr base, int k) {
      // Mantén el mismo índice de conjunto: base + k * (num_sets * line_bytes)
      const std::size_t line_bytes = cfg::kLineBytes;
      const std::size_t num_sets   = cfg::kCacheLines / cfg::kCacheWays;
      return base + static_cast<Addr>(k) * (num_sets * line_bytes);
    };

    for (std::size_t i = 0; i < cfg::kNumPEs; ++i) {
      std::vector<Access> t;
      // Tres accesos que mapean al MISMO set, forzando reemplazo
      Addr a0 = same_set(0x200 + i*8, 0);
      Addr a1 = same_set(0x200 + i*8, 1);
      Addr a2 = same_set(0x200 + i*8, 2);
      t.push_back({AccessType::Load,  a0, 8});
      t.push_back({AccessType::Load,  a1, 8});
      t.push_back({AccessType::Store, a2, 8});
      pes_[i]->load_trace(t);
    }
    std::cout << "[Init] (OPC3) Conflicto de sets preparado (bloque comentado).\n";
  }
  */
}

void Simulator::run_cycles(std::size_t cycles) {
  std::cout << "[Sim] Ejecutando " << cycles << " ciclos...\n";
  for (std::size_t c = 0; c < cycles; ++c) {
    LOG_IF(cfg::kLogSim, "== Ciclo " << c << " ==");
    for (auto& pe : pes_) pe->step();
    bus_->step();
  }

  std::cout << "[Sim] Ejecución completada.\n\n";
  std::cout << "----- Métricas de desempeño -----\n";
  for (std::size_t i = 0; i < cfg::kNumPEs; ++i) {
    const auto& m = caches_[i]->metrics();
    std::cout << "PE" << i
              << " | Loads: " << m.loads
              << " | Stores: " << m.stores
              << " | Hits: " << m.hits
              << " | Misses: " << m.misses
              << " | Invalidations: " << m.invalidations
              << "\n";
  }
  std::cout << "---------------------------------\n";
  std::cout << "Bus total bytes transferidos: " << bus_->bus_bytes() << "\n";
}

} // namespace sim
