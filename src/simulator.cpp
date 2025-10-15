#include "simulator.hpp"
#include "config.hpp"
#include "assembler.hpp"
#include "types.hpp"
#include "memory.hpp"
#include "bus.hpp"
#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>

namespace sim
{

  Simulator::Simulator()
  {
    // Construye bus con vector (vacío) temporal
    std::vector<Cache *> tmp;
    bus_ = std::make_unique<Bus>(tmp);

    // Construye cachés
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
    {
      caches_[i] = std::make_unique<Cache>(static_cast<PEId>(i), *bus_, mem_);
    }

    // Conectar cachés al bus (snoops MESI)
    std::vector<Cache *> ptrs;
    ptrs.reserve(cfg::kNumPEs);
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
      ptrs.push_back(caches_[i].get());
    bus_->set_caches(ptrs);

    // Instanciar procesadores
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
    {
      pes_[i] = std::make_unique<Processor>(static_cast<PEId>(i), *caches_[i]);
    }
  }

  void Simulator::init_dot_problem(std::size_t N, Addr baseA, Addr baseB, Addr basePS)
  {
    auto to_u64 = [](double d)
    {
      std::uint64_t v;
      std::memcpy(&v, &d, sizeof(double));
      return v;
    };

    // --- DRAM: A y B contiguos como double (8B) ---
    for (std::size_t i = 0; i < N; ++i)
    {
      mem_.write64(baseA + i * 8, to_u64(static_cast<double>(i + 1))); // A[i] = i+1
      mem_.write64(baseB + i * 8, to_u64(1.0));                        // B[i] = 1.0
    }

    // --- DRAM: partial_sums[PE] = 0.0 ---
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe)
    {
      mem_.write64(basePS + pe * 8, to_u64(0.0));
    }

    if (N % cfg::kNumPEs != 0)
    {
      throw std::runtime_error("N debe ser múltiplo de cfg::kNumPEs");
    }
    const std::size_t seg = N / cfg::kNumPEs;

    // Reg-map que respeta tu JNZ (usa REG0 como contador):
    //   REG0 = seg
    //   REG1 = &A[pe*seg]
    //   REG2 = &B[pe*seg]
    //   REG3 = &partial_sums[pe]
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe)
    {
      pes_[pe]->set_reg(0, seg);
      pes_[pe]->set_reg(1, baseA + pe * seg * 8);
      pes_[pe]->set_reg(2, baseB + pe * seg * 8);
      pes_[pe]->set_reg(3, basePS + pe * 8);
    }

    LOG_IF(cfg::kLogSim,
           "[InitDot] N=" << N
                          << " baseA=0x" << std::hex << baseA
                          << " baseB=0x" << baseB
                          << " basePS=0x" << basePS << std::dec
                          << " seg=" << seg << " (INC avanza " << cfg::kWordBytes << "B)");
  }

  void Simulator::load_demo_traces()
  {
    auto p = Assembler::assemble_from_file("examples/demo.asm");
    load_program_all(p);
  }

  void Simulator::load_program_all(const Program &p)
  {
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
    {
      pes_[i]->load_program(p);
    }
  }

  void Simulator::load_program_all_from_file(const std::string &path)
  {
    auto p = Assembler::assemble_from_file(path);
    load_program_all(p);
  }

  void Simulator::run_cycles(std::size_t cycles)
  {
    for (std::size_t c = 0; c < cycles; ++c)
    {
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

  void Simulator::run_until_done(std::size_t safety_max)
  {
    std::size_t after_done_bus_steps = 0;

    for (std::size_t c = 0; c < safety_max; ++c)
    {
      std::size_t done = 0;

      for (auto &pe : pes_)
      {
        if (pe->is_done())
        {
          ++done;
        }
        else
        {
          pe->step();
        }
      }

      // Avanza el bus (procesa una petición si existe)
      bus_->step();

      if (done == cfg::kNumPEs)
      {
        // Dar unos pasos extra al bus para "drenar" cualquier pendiente.
        if (++after_done_bus_steps >= 16)
          break;
      }
      else
      {
        after_done_bus_steps = 0;
      }
    }

    // ---- Reporte estándar ----
    std::cout << std::endl;
    std::cout << "[Simulacion MESI] Ejecución completada.\n\n";
    std::cout << "--------------------------- Métricas de desempeño ------------------------------\n";
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
    std::cout << "-----------------------------------------------------------------------------------\n";
    std::cout << "Bus bytes: " << bus_->bytes()
              << " | BusRd=" << bus_->count_cmd(BusCmd::BusRd)
              << " | BusRdX=" << bus_->count_cmd(BusCmd::BusRdX)
              << " | Upgr=" << bus_->count_cmd(BusCmd::BusUpgr)
              << " | Flushes=" << bus_->flushes()
              << "\n";
  }

} // namespace sim
