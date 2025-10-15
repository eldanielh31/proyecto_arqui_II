#include "simulator.hpp"
#include "config.hpp"
#include <iostream>

namespace sim
{

  Simulator::Simulator()
  {
    std::vector<Cache *> cache_ptrs;
    bus_ = std::make_unique<Bus>(cache_ptrs);

    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
    {
      caches_[i] = std::make_unique<Cache>(static_cast<PEId>(i), *bus_, mem_);
    }

    cache_ptrs.clear();
    for (auto &up : caches_)
      cache_ptrs.push_back(up.get());
    bus_->set_caches(cache_ptrs);

    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
    {
      pes_[i] = std::make_unique<Processor>(static_cast<PEId>(i), *caches_[i]);
    }

    std::cout << "[Init] Sistema configurado con " << cfg::kNumPEs
              << " PEs, cachés privadas y bus compartido.\n";
  }

  void Simulator::load_demo_traces()
  {
    if (cfg::kDemoContention)
    {
      //
      // Objetivo:
      //  - PE0 lleva línea 0x100 a M (LOAD -> STORE).
      //  - Luego PE1 hace LOAD a 0x100: BusRd contra una línea en M -> Flush (M->S).
      //
      // Para asegurar el orden temporal:
      //  - kBusOpsPerCycle = 1 (ver config.hpp).
      //  - PE1 hace dos accesos "dummy" primero para darle tiempo a PE0 de alcanzar M.
      //
      // Línea conflictiva
      const Addr X = 0x100;

      for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
      {
        std::vector<Access> t;
        if (i == 0)
        {
          // PE0: llega a M en la línea X antes de que PE1 lea X
          t.push_back({AccessType::Load, X, 8});  // Ciclo 0: BusRd -> E
          t.push_back({AccessType::Store, X, 8}); // Ciclo 1: BusUpgr -> M
          t.push_back({AccessType::Load, X, 8});  // Ciclo 2: hit M
        }
        else if (i == 1)
        {
          // PE1: retrasa el acceso a X para que ocurra DESPUÉS del Upgr de PE0
          t.push_back({AccessType::Load, 0x700, 8}); // Ciclo 0: dummy (disjunto)
          t.push_back({AccessType::Load, 0x740, 8}); // Ciclo 1: dummy (disjunto)
          t.push_back({AccessType::Load, X, 8});     // Ciclo 2: BusRd -> Flush en PE0 (M->S)
          t.push_back({AccessType::Store, X, 8});    // Ciclo 3: Upgr/RdX -> invalida a PE0
          t.push_back({AccessType::Load, X, 8});     // Ciclo 4: hit
        }
        else
        {
          // Otros PEs en direcciones disjuntas para no interferir
          const Addr base0 = 0x800 + static_cast<Addr>(i) * 0x80;
          const Addr base1 = 0x900 + static_cast<Addr>(i) * 0x80;
          t.push_back({AccessType::Load, base0, 8});
          t.push_back({AccessType::Store, base1, 8});
          t.push_back({AccessType::Load, base0, 8});
        }
        pes_[i]->load_trace(t);
      }
      std::cout << "[Init] Trazas de contención (Flush garantizado) cargadas.\n";
      return;
    }

    // ===== Demo por defecto (disjunto) =====
    const Addr baseA = 0;
    const Addr baseB = 8 * 64;
    const Addr stride = 64;

    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
    {
      std::vector<Access> t;
      t.push_back({AccessType::Load, baseA + i * stride, 8});
      t.push_back({AccessType::Store, baseB + i * stride, 8});
      t.push_back({AccessType::Load, baseA + i * stride, 8});
      pes_[i]->load_trace(t);
    }

    std::cout << "[Init] Trazas de demostración cargadas (" << cfg::kNumPEs << " PEs).\n";
  }

  void Simulator::run_cycles(std::size_t cycles)
  {
    std::cout << "[Sim] Ejecutando " << cycles << " ciclos...\n";
    for (std::size_t c = 0; c < cycles; ++c)
    {
      LOG_IF(cfg::kLogSim, "== Ciclo " << c << " ==");
      for (auto &pe : pes_)
        pe->step();
      bus_->step();
    }

    std::cout << "[Sim] Ejecución completada.\n\n";
    std::cout << "----- Métricas de desempeño -----\n";
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
    {
      const auto &m = caches_[i]->metrics();
      std::cout << "PE" << i
                << " | Loads: " << m.loads
                << " | Stores: " << m.stores
                << " | Hits: " << m.hits
                << " | Misses: " << m.misses
                << " | Invalidations: " << m.invalidations
                << " | Flushes: " << m.flushes
                << "\n";
    }
    std::cout << "---------------------------------\n";
    std::cout << "Bus bytes: " << bus_->bytes()
              << " | BusRd=" << bus_->count_cmd(BusCmd::BusRd)
              << " | BusRdX=" << bus_->count_cmd(BusCmd::BusRdX)
              << " | Upgr=" << bus_->count_cmd(BusCmd::BusUpgr)
              << " | Flushes=" << bus_->flushes()
              << "\n";
  }

} // namespace sim
