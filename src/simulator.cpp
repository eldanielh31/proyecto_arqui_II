#include "simulator.hpp"
#include "config.hpp"
#include "assembler.hpp"
#include "types.hpp"

#include "memory.hpp"
#include "bus.hpp"
#include "cache.hpp"
#include "processor.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <optional>
#include <thread>

namespace sim
{

  // Helpers rápidos: bitcast u64<->f64 y prints de registros para diffs.
  static inline std::uint64_t to_u64(double d) {
    std::uint64_t v; std::memcpy(&v, &d, sizeof(double)); return v;
  }
  static inline double to_f64(std::uint64_t u) {
    double d; std::memcpy(&d, &u, sizeof(double)); return d;
  }

  static void print_reg_compact(std::ostream& os, int r, std::uint64_t v) {
    os << "R" << r << "=";
    if (r >= 4) {
      os << "0x" << std::hex << v << std::dec
         << " (f64=" << std::fixed << std::setprecision(6) << to_f64(v) << ")";
    } else if (r == 1 || r == 2 || r == 3) {
      os << "0x" << std::hex << v << std::dec << " (addr-dec=" << v << ")";
    } else {
      os << "0x" << std::hex << v << std::dec;
    }
  }

  static void print_reg_diff(std::ostream& os, int r, std::uint64_t before, std::uint64_t after) {
    if (before == after) return;
    os << "  R" << r << ": ";
    if (r >= 4) {
      os << "0x" << std::hex << before << std::dec << " (" << to_f64(before) << ")"
         << " -> "
         << "0x" << std::hex << after  << std::dec << " (" << to_f64(after) << ")";
    } else if (r == 1 || r == 2 || r == 3) {
      os << "0x" << std::hex << before << std::dec << " [" << before << "]"
         << " -> "
         << "0x" << std::hex << after  << std::dec << " [" << after  << "]";
    } else {
      os << "0x" << std::hex << before << std::dec
         << " -> "
         << "0x" << std::hex << after  << std::dec;
    }
    os << "\n";
  }

  // Config global para dumps finales
  static std::size_t gN = 0;
  static std::size_t gSeg = 0;
  static Addr gBaseA = 0;
  static Addr gBaseB = 0;
  static Addr gBasePS = 0;

  // Ciclo de vida: lanza hilos en ctor, los cierra en dtor.
  Simulator::~Simulator() {
    stop_threads();  // detener hilos ANTES de destruir Bus/PEs/Caches
  }

  Simulator::Simulator()
  {
    // 1) Crear Bus (sin cachés aún)
    std::vector<Cache *> tmp;
    bus_ = std::make_unique<Bus>(tmp);

    // 2) Crear cachés y conectarlas al bus
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
      caches_[i] = std::make_unique<Cache>(static_cast<PEId>(i), *bus_, mem_);
    std::vector<Cache*> ptrs;
    for (auto &c : caches_) ptrs.push_back(c.get());
    bus_->set_caches(ptrs);

    // 3) Crear PEs (cada uno con su caché)
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
      pes_[i] = std::make_unique<Processor>(static_cast<PEId>(i), *caches_[i]);

    // 4) Lanzar hilos (quedan en Idle)
    start_threads();
  }

  // --- Multihilo: barrera por tick (PEs -> Bus) ---
  void Simulator::start_threads() {
    std::lock_guard<std::mutex> lk(m_);
    if (threads_started_) return;

    tick_ = 0;
    pe_done_count_ = 0;
    bus_done_ = false;
    pe_last_tick_.fill(0);
    bus_last_tick_ = 0;
    phase_ = Phase::Idle;

    // Hilos de PEs
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i) {
      pe_threads_[i] = std::thread(&Simulator::worker_pe, this, i);
    }
    // Hilo del Bus
    bus_thread_ = std::thread(&Simulator::worker_bus, this);

    threads_started_ = true;
  }

  void Simulator::stop_threads() {
    std::unique_lock<std::mutex> lk(m_);
    if (!threads_started_) return;

    phase_ = Phase::Halt;
    cv_.notify_all();
    lk.unlock();

    for (auto& t : pe_threads_) if (t.joinable()) t.join();
    if (bus_thread_.joinable()) bus_thread_.join();

    lk.lock();
    threads_started_ = false;
    phase_ = Phase::Idle;
  }

  void Simulator::worker_pe(std::size_t pe_idx) {
    std::unique_lock<std::mutex> lk(m_);
    while (true) {
      cv_.wait(lk, [&]{ return phase_ == Phase::RunPE || phase_ == Phase::Halt; });
      if (phase_ == Phase::Halt) break;

      const std::size_t mytick = tick_;
      if (pe_last_tick_[pe_idx] == mytick) {
        cv_.wait(lk, [&]{ return tick_ != mytick || phase_ == Phase::Halt; });
        if (phase_ == Phase::Halt) break;
        continue;
      }

      lk.unlock();
      if (!pes_[pe_idx]->is_done()) {
        pes_[pe_idx]->step(); // ejecuta 1 instr máx.
      }
      lk.lock();

      pe_last_tick_[pe_idx] = mytick;
      ++pe_done_count_;
      if (pe_done_count_ == cfg::kNumPEs) cv_.notify_all();

      cv_.wait(lk, [&]{ return tick_ != mytick || phase_ == Phase::Halt; });
      if (phase_ == Phase::Halt) break;
    }
  }

  void Simulator::worker_bus() {
    std::unique_lock<std::mutex> lk(m_);
    while (true) {
      cv_.wait(lk, [&]{ return phase_ == Phase::RunBus || phase_ == Phase::Halt; });
      if (phase_ == Phase::Halt) break;

      const std::size_t mytick = tick_;
      if (bus_last_tick_ == mytick) {
        cv_.wait(lk, [&]{ return tick_ != mytick || phase_ == Phase::Halt; });
        if (phase_ == Phase::Halt) break;
        continue;
      }

      lk.unlock();
      bus_->step();  // procesa hasta kBusOpsPerCycle
      lk.lock();

      bus_last_tick_ = mytick;
      bus_done_ = true;
      cv_.notify_all();

      cv_.wait(lk, [&]{ return tick_ != mytick || phase_ == Phase::Halt; });
      if (phase_ == Phase::Halt) break;
    }
  }

  void Simulator::advance_one_tick_blocking() {
    std::unique_lock<std::mutex> lk(m_);

    // Nuevo tick
    ++tick_;
    pe_done_count_ = 0;
    bus_done_ = false;

    // 1) PEs
    phase_ = Phase::RunPE;
    cv_.notify_all();
    cv_.wait(lk, [&]{ return pe_done_count_ == cfg::kNumPEs || phase_ == Phase::Halt; });
    if (phase_ == Phase::Halt) return;

    // 2) Bus
    phase_ = Phase::RunBus;
    cv_.notify_all();
    cv_.wait(lk, [&]{ return bus_done_ || phase_ == Phase::Halt; });
    if (phase_ == Phase::Halt) return;

    // Idle
    phase_ = Phase::Idle;
    cv_.notify_all();
  }

  // --- Setup de memoria / programa demo dot product ---
  void Simulator::init_dot_problem(std::size_t N, Addr baseA, Addr baseB, Addr basePS)
  {
    gN = N; gBaseA = baseA; gBaseB = baseB; gBasePS = basePS;

    // Carga A y B desde input.txt (si existe) o genera por defecto.
    {
      std::ifstream fin("input.txt");
      bool loaded = false;

      if (fin) {
        std::string lineA, lineB;
        auto nonempty = [](const std::string& s){
          return s.find_first_not_of(" \t\r\n") != std::string::npos;
        };
        while (std::getline(fin, lineA) && !nonempty(lineA)) {}
        while (std::getline(fin, lineB) && !nonempty(lineB)) {}

        if (!lineA.empty() && !lineB.empty()) {
          auto parse_line = [](const std::string &s) {
            std::vector<double> v;
            std::istringstream is(s);
            double d;
            while (is >> d) v.push_back(d);
            return v;
          };
          std::vector<double> va = parse_line(lineA);
          std::vector<double> vb = parse_line(lineB);

          if (!va.empty() && !vb.empty()) {
            std::size_t M = std::min<std::size_t>({va.size(), vb.size(), N});
            for (std::size_t i = 0; i < M; ++i) {
              mem_.write64(baseA + i*8, to_u64(va[i]));
              mem_.write64(baseB + i*8, to_u64(vb[i]));
            }
            for (std::size_t i = M; i < N; ++i) {
              mem_.write64(baseA + i*8, to_u64(0.0));
              mem_.write64(baseB + i*8, to_u64(0.0));
            }
            LOG_IF(cfg::kLogSim, "[InitDot] input.txt cargado: M=" << M << " (N solicitado=" << N << ")");
            loaded = true;
          }
        }
      }

      if (!loaded) {
        for (std::size_t i = 0; i < N; ++i) {
          mem_.write64(baseA + i*8, to_u64(static_cast<double>(i + 1)));
          mem_.write64(baseB + i*8, to_u64(1.0));
        }
        LOG_IF(cfg::kLogSim, "[InitDot] input.txt no encontrado/incompleto, usando por defecto");
      }
    }

    // partial_sums[PE] = 0.0
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe)
      mem_.write64(basePS + pe*8, to_u64(0.0));

    // Partición: REG0=seg, REG1/2 punteros A/B, REG3 destino PS
    gSeg = N / cfg::kNumPEs;
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) {
      pes_[pe]->set_reg(0, gSeg);
      pes_[pe]->set_reg(1, baseA + pe*gSeg*8);
      pes_[pe]->set_reg(2, baseB + pe*gSeg*8);
      pes_[pe]->set_reg(3, basePS + pe*8);
    }

    LOG_IF(cfg::kLogSim,
      "[InitDot] N=" << N
      << " baseA=0x" << std::hex << baseA
      << " baseB=0x" << baseB
      << " basePS=0x" << basePS << std::dec
      << " seg=" << gSeg << " (INC avanza " << cfg::kWordBytes << "B)");

    // Dump de memoria inicial (para ver qué se cargó)
    SOUT << "\n========== CONTENIDO DE MEMORIA (inicial) ==========\n";
    for (std::size_t addr = 0; addr < cfg::kMemWords * cfg::kWordBytes; addr += 8) {
      std::uint64_t v = mem_.read64(addr);
      double d; std::memcpy(&d, &v, sizeof(double));
      SOUT << "0x" << std::hex << std::setw(4) << addr << std::dec
           << " : " << std::fixed << std::setprecision(6) << d << "\n";
    }
    SOUT << "====================================================\n";
  }

  void Simulator::load_demo_traces() {
    auto p = Assembler::assemble_from_file("examples/demo.asm");
    load_program_all(p);
  }

  void Simulator::load_program_all(const Program &p) {
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i) pes_[i]->load_program(p);
  }

  void Simulator::load_program_all_from_file(const std::string &path) {
    auto p = Assembler::assemble_from_file(path);
    load_program_all(p);
  }

  // --- Ejecución fija por N ciclos + reducción final en PE0 ---
  void Simulator::run_cycles(std::size_t cycles)
  {
    for (std::size_t c = 0; c < cycles; ++c) {
      advance_one_tick_blocking();
    }

    SOUT << "[Sim] Ejecución completada.\n\n";

    // Reducción final en PE0 (suma de partial_sums)
    {
      Program p;
      Instr warm1; warm1.op = OpCode::MOVI;  warm1.rd = 1; warm1.imm = gBasePS + 0x8;
      Instr warm2; warm2.op = OpCode::LOAD;  warm2.rd = 7; warm2.ra  = 1;

      Instr m1;   m1.op   = OpCode::MOVI;    m1.rd = 1; m1.imm = gBasePS;
      Instr m2;   m2.op   = OpCode::MOVI;    m2.rd = 2; m2.imm = cfg::kNumPEs;
      Instr r;    r.op    = OpCode::REDUCE;  r.rd = 4; r.ra = 1; r.rb = 2;
      Instr st;   st.op   = OpCode::STORE;   st.ra = 4; st.rd = 3;

      p.code = { warm1, warm2, m1, m2, r, st };
      pes_[0]->load_program(p);

      std::size_t k = 0;
      while (!pes_[0]->is_done() && k++ < 2000) {
        advance_one_tick_blocking();
      }

      std::uint64_t bits = pes_[0]->get_reg(4);
      double final; std::memcpy(&final, &bits, sizeof(double));
      SERR << "\n[Resultado final en PE0] Producto punto = "
           << std::fixed << std::setprecision(6) << final << "\n";
    }

    // Métricas
    SOUT << "----- Métricas de desempeño -----\n";
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i) {
      const auto &m = caches_[i]->metrics();
      SOUT << "PE" << i
           << " | Loads: " << m.loads
           << " | Stores: " << m.stores
           << " | Hits: " << m.hits
           << " | Misses: " << m.misses
           << " | Invalidations: " << m.invalidations
           << " | Flushes: " << m.flushes
           << "\n";
    }
    SOUT << "-----------------------------------------------------------------------------------\n";
    SOUT << "Bus bytes: " << bus_->bytes()
         << " | BusRd=" << bus_->count_cmd(BusCmd::BusRd)
         << " | BusRdX=" << bus_->count_cmd(BusCmd::BusRdX)
         << " | Upgr=" << bus_->count_cmd(BusCmd::BusUpgr)
         << " | Flushes=" << bus_->flushes()
         << "\n";

    // Dumps útiles por PE (regs y segmentos)
    SOUT << "\n================= DEBUG POR PE (REGISTROS + MEMORIA) =================\n";
    SOUT << std::fixed << std::setprecision(6);

    double ref_dot = 0.0;
    for (std::size_t i = 0; i < gN; ++i) {
      double a = to_f64(mem_.read64(gBaseA + i*8));
      double b = to_f64(mem_.read64(gBaseB + i*8));
      ref_dot += a * b;
    }
    SOUT << "[Referencia CPU] dot(A,B) con N=" << gN << " -> " << ref_dot << "\n\n";

    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) {
      SOUT << "---- PE" << pe << " -------------------------------------------------\n";

      SOUT << "REGISTROS:\n";
      for (int r = 0; r < 8; ++r) {
        std::uint64_t u = pes_[pe]->get_reg(r);
        SOUT << "  R" << r << " = 0x" << std::hex << u << std::dec;
        if (r >= 4) {SOUT << "  (f64=" << to_f64(u) << ")"; }
        if (r == 1 || r == 2 || r == 3) {SOUT << "  (addr-dec=" << u << ")"; }
        SOUT << "\n";
      }

      const std::size_t baseIdx = pe * gSeg;
      SOUT << "Segmento A[ " << baseIdx << " .. " << (baseIdx + gSeg - 1) << " ]\n";
      for (std::size_t k = 0; k < gSeg; ++k) {
        Addr addr = gBaseA + (baseIdx + k) * 8;
        double v = to_f64(mem_.read64(addr));
        SOUT << "  A[" << (baseIdx + k) << "] @0x" << std::hex << addr << std::dec
             << " = " << v << "\n";
      }

      SOUT << "Segmento B[ " << baseIdx << " .. " << (baseIdx + gSeg - 1) << " ]\n";
      for (std::size_t k = 0; k < gSeg; ++k) {
        Addr addr = gBaseB + (baseIdx + k) * 8;
        double v = to_f64(mem_.read64(addr));
        SOUT << "  B[" << (baseIdx + k) << "] @0x" << std::hex << addr << std::dec
             << " = " << v << "\n";
      }

      Addr ps = gBasePS + pe * 8;
      double vps = to_f64(mem_.read64(ps));
      SOUT << "partial_sums[" << pe << "] @0x" << std::hex << ps << std::dec
           << " = " << vps << "\n";

      double ref_partial = 0.0;
      for (std::size_t k = 0; k < gSeg; ++k) {
        double a = to_f64(mem_.read64(gBaseA + (baseIdx + k)*8));
        double b = to_f64(mem_.read64(gBaseB + (baseIdx + k)*8));
        ref_partial += a * b;
      }
      SOUT << "Parcial esperado PE" << pe << " = " << ref_partial << "\n";
      SOUT << "-----------------------------------------------------------------------\n\n";
    }
    SOUT << "=======================================================================\n";
  }

  void Simulator::run_until_done(std::size_t safety_max)
  {
    std::size_t after_done_bus_steps = 0;

    for (std::size_t c = 0; c < safety_max; ++c)
    {
      const bool already_done = all_done();
      advance_one_tick_blocking();

      if (already_done) {
        if (++after_done_bus_steps >= 2) break;
      } else {
        after_done_bus_steps = 0;
      }
    }

    // Reducción final igual que en run_cycles()
    {
      Program p;
      Instr warm1; warm1.op = OpCode::MOVI;  warm1.rd = 1; warm1.imm = gBasePS + 0x8;
      Instr warm2; warm2.op = OpCode::LOAD;  warm2.rd = 7; warm2.ra  = 1;

      Instr m1;   m1.op   = OpCode::MOVI;    m1.rd = 1; m1.imm = gBasePS;
      Instr m2;   m2.op   = OpCode::MOVI;    m2.rd = 2; m2.imm = cfg::kNumPEs;
      Instr r;    r.op    = OpCode::REDUCE;  r.rd = 4; r.ra = 1; r.rb = 2;
      Instr st;   st.op   = OpCode::STORE;   st.ra = 4; st.rd = 3;

      p.code = { warm1, warm2, m1, m2, r, st };
      pes_[0]->load_program(p);

      std::size_t k = 0;
      while (!pes_[0]->is_done() && k++ < 2000) {
        advance_one_tick_blocking();
      }

      std::uint64_t bits = pes_[0]->get_reg(4);
      double final; std::memcpy(&final, &bits, sizeof(double));
      SERR << "\n[Resultado final en PE0] Producto punto = "
           << std::fixed << std::setprecision(6) << final << "\n";
    }

    // Reutilizamos los mismos dumps por PE
    SOUT << "\n================= DEBUG POR PE (REGISTROS + MEMORIA) =================\n";
    SOUT << std::fixed << std::setprecision(6);

    double ref_dot = 0.0;
    for (std::size_t i = 0; i < gN; ++i) {
      double a = to_f64(mem_.read64(gBaseA + i*8));
      double b = to_f64(mem_.read64(gBaseB + i*8));
      ref_dot += a * b;
    }
    SOUT << "[Referencia CPU] dot(A,B) con N=" << gN << " -> " << ref_dot << "\n\n";

    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe)
    {
      SOUT << "---- PE" << pe << " -------------------------------------------------\n";
      SOUT << "REGISTROS:\n";
      for (int r = 0; r < 8; ++r) {
        std::uint64_t u = pes_[pe]->get_reg(r);
        SOUT << "  R" << r << " = 0x" << std::hex << u << std::dec;
        if (r >= 4) {SOUT << "  (f64=" << to_f64(u) << ")"; }
        if (r == 1 || r == 2 || r == 3) {SOUT << "  (addr-dec=" << u << ")"; }
        SOUT << "\n";
      }

      const std::size_t baseIdx = pe * gSeg;

      SOUT << "Segmento A[ " << baseIdx << " .. " << (baseIdx + gSeg - 1) << " ]\n";
      for (std::size_t k = 0; k < gSeg; ++k) {
        Addr addr = gBaseA + (baseIdx + k) * 8;
        double v = to_f64(mem_.read64(addr));
        SOUT << "  A[" << (baseIdx + k) << "] @0x" << std::hex << addr << std::dec
             << " = " << v << "\n";
      }

      SOUT << "Segmento B[ " << baseIdx << " .. " << (baseIdx + gSeg - 1) << " ]\n";
      for (std::size_t k = 0; k < gSeg; ++k) {
        Addr addr = gBaseB + (baseIdx + k) * 8;
        double v = to_f64(mem_.read64(addr));
        SOUT << "  B[" << (baseIdx + k) << "] @0x" << std::hex << addr << std::dec
             << " = " << v << "\n";
      }

      Addr ps = gBasePS + pe * 8;
      double vps = to_f64(mem_.read64(ps));
      SOUT << "partial_sums[" << pe << "] @0x" << std::hex << ps << std::dec
           << " = " << vps << "\n";

      double ref_partial = 0.0;
      for (std::size_t k = 0; k < gSeg; ++k) {
        double a = to_f64(mem_.read64(gBaseA + (baseIdx + k)*8));
        double b = to_f64(mem_.read64(gBaseB + (baseIdx + k)*8));
        ref_partial += a * b;
      }
      SOUT << "Parcial esperado PE" << pe << " = " << ref_partial << "\n";
      SOUT << "-----------------------------------------------------------------------\n\n";
    }
    SOUT << "=======================================================================\n";
  }

  // --- Stepping interactivo: ENTER step, 'c' continúa, 'r' regs, 'b' bus, 'q' sale ---
  void Simulator::run_stepping() {
    SOUT << "\n===================== STEPPING INTERACTIVO =====================\n"
         << "ENTER=step | c=continuar | r=regs | b=bus | q=salir\n";

    bool auto_run = false;
    std::size_t step = 0;

    while (!all_done()) {
      if (!auto_run) {
        SOUT << "\n[step " << step << "] > ";
        std::string line;
        if (!std::getline(std::cin, line)) {SOUT << "\n[Stepping] stdin cerrado. Saliendo.\n"; break; }
        if (line == "q" || line == "Q") {SOUT << "[Stepping] Salir.\n"; break; }
        if (line == "c" || line == "C") { auto_run = true; SOUT << "[Stepping] Continuación automática habilitada.\n"; }
        else if (line == "r" || line == "R") { for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) dump_regs(pe); continue; }
        else if (line == "b" || line == "B") {
          SOUT << "[Bus] bytes=" << bus_->bytes()
               << " | BusRd="  << bus_->count_cmd(BusCmd::BusRd)
               << " | BusRdX=" << bus_->count_cmd(BusCmd::BusRdX)
               << " | Upgr="   << bus_->count_cmd(BusCmd::BusUpgr)
               << " | Flushes="<< bus_->flushes() << "\n";
          continue;
        }
      }

      SOUT << "\n===== STEP " << step << " =====\n";
      step_one();
      ++step;
    }

    SOUT << "\n[Stepping] Terminado (auto_run=" << (auto_run ? "true" : "false") << ").\n";
  }

  void Simulator::step_one() {
    // Snapshot BEFORE
    std::array<std::array<std::uint64_t, 8>, cfg::kNumPEs> before{};
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe)
      for (int r = 0; r < 8; ++r) before[pe][r] = pes_[pe]->get_reg(r);

    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) {
      if (pes_[pe]->is_done()) {SOUT << "[PE" << pe << "] DONE (no ejecuta)\n"; continue; }
      SOUT << "[PE" << pe << "] BEFORE: ";
      print_reg_compact(std::cout, 0, before[pe][0]); SOUT << " | ";
      print_reg_compact(std::cout, 1, before[pe][1]); SOUT << " | ";
      print_reg_compact(std::cout, 2, before[pe][2]); SOUT << " | ";
      print_reg_compact(std::cout, 3, before[pe][3]); SOUT << " | ";
      print_reg_compact(std::cout, 4, before[pe][4]); SOUT << "\n";
    }

    // 1 tick completo
    advance_one_tick_blocking();

    // Diffs AFTER
    SOUT << "\n--- REG DIFFS (AFTER) ---\n";
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) {
      std::array<std::uint64_t, 8> after{};
      for (int r = 0; r < 8; ++r) after[r] = pes_[pe]->get_reg(r);

      bool any = false;
      std::ostringstream oss;
      for (int r = 0; r < 8; ++r) {
        std::ostringstream line;
        print_reg_diff(line, r, before[pe][r], after[r]);
        if (!line.str().empty()) { any = true; oss << line.str(); }
      }
      if (any) SOUT << "[PE" << pe << "]\n" << oss.str();
      else     SOUT << "[PE" << pe << "] (sin cambios en registros)\n";
    }

    SOUT << "[BUS] bytes=" << bus_->bytes()
         << " | BusRd="  << bus_->count_cmd(BusCmd::BusRd)
         << " | BusRdX=" << bus_->count_cmd(BusCmd::BusRdX)
         << " | Upgr="   << bus_->count_cmd(BusCmd::BusUpgr)
         << " | Flushes="<< bus_->flushes()
         << "\n";

    // Dump de caché por PE
    SOUT << "\n----------------------- CACHE DUMP (por paso) -----------------------\n";
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) {
      SOUT << "[PE" << pe << "]\n";
      caches_[pe]->debug_dump(std::cout, std::nullopt, /*with_data=*/true);
      SOUT << "------------------------------------------------------------------\n";
    }
  }

  bool Simulator::all_done() const {
    for (const auto& pe : pes_) if (!pe->is_done()) return false;
    return true;
  }

  void Simulator::dump_cache(std::size_t pe, std::optional<std::size_t> only_set) const {
    if (pe >= cfg::kNumPEs) return;
    caches_[pe]->debug_dump(std::cout, only_set, /*with_data=*/true);
  }

  void Simulator::dump_regs(std::size_t pe) const {
    if (pe >= cfg::kNumPEs) return;
    SOUT << "REGISTROS PE" << pe << ":\n";
    for (int r = 0; r < 8; ++r) {
      std::uint64_t u = pes_[pe]->get_reg(r);
      SOUT << "  R" << r << " = 0x" << std::hex << u << std::dec;
      if (r >= 4) { double d; std::memcpy(&d, &u, sizeof(double)); SOUT << "  (f64=" << d << ")"; }
      if (r == 1 || r == 2 || r == 3) { SOUT << "  (addr-dec=" << u << ")"; }
      SOUT << "\n";
    }
  }

} // namespace sim
