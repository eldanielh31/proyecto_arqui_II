/* 
 * Notas rápidas:
 * - Se agregó validación PASS/FAIL del dot product (comparación contra ref CPU).
 * - Se imprime tráfico de bus por PE y transiciones MESI en dump_metrics().
 * - No se eliminó nada; sólo se sumaron comentarios y lógica mínima.
 */

#include "simulator.hpp"
#include "config.hpp"
#include "assembler.hpp"
#include "types.hpp"

#include "memory.hpp"
#include "bus.hpp"
#include "cache.hpp"
#include "processor.hpp"
#include "debug_io.hpp"

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
#include <cmath>   // fabs para validación

namespace sim {

using dbg::to_u64;
using dbg::to_f64;

// ---------- Ciclo de vida ----------
Simulator::~Simulator() {
  stop_threads();  // detener hilos ANTES de destruir Bus/PEs/Caches
}

Simulator::Simulator() {
  // Bus primero (sin cachés)
  std::vector<Cache *> tmp;
  bus_ = std::make_unique<Bus>(tmp);

  // Cachés y registro en bus
  for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
    caches_[i] = std::make_unique<Cache>(static_cast<PEId>(i), *bus_, mem_);
  std::vector<Cache*> ptrs;
  for (auto &c : caches_) ptrs.push_back(c.get());
  bus_->set_caches(ptrs);

  // PEs
  for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
    pes_[i] = std::make_unique<Processor>(static_cast<PEId>(i), *caches_[i]);

  // Lanzar hilos (quedan en Idle)
  start_threads();
}

// ---------- Multihilo ----------
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
  // Hilo de BUS
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
    // Esperar a fase de ejecución de PEs o fin
    cv_.wait(lk, [&]{ return phase_ == Phase::RunPE || phase_ == Phase::Halt; });
    if (phase_ == Phase::Halt) break;

    // Capturar el tick que debo procesar
    const std::size_t mytick = tick_;
    // Evitar doble proceso del mismo tick (p.ej. despertar espurio)
    if (pe_last_tick_[pe_idx] == mytick) {
      // Esperar al siguiente tick
      cv_.wait(lk, [&]{ return tick_ != mytick || phase_ == Phase::Halt; });
      if (phase_ == Phase::Halt) break;
      continue;
    }

    lk.unlock();
    // --- Trabajo del PE en este tick (1 instrucción máx.) ---
    if (!pes_[pe_idx]->is_done()) {
      pes_[pe_idx]->step(); // logs dentro
    }
    lk.lock();

    // Marcar completado para este tick
    pe_last_tick_[pe_idx] = mytick;
    ++pe_done_count_;
    if (pe_done_count_ == cfg::kNumPEs) cv_.notify_all();

    // Esperar al SIGUIENTE TICK (no sólo cambio de fase)
    cv_.wait(lk, [&]{ return tick_ != mytick || phase_ == Phase::Halt; });
    if (phase_ == Phase::Halt) break;
  }
}

void Simulator::worker_bus() {
  std::unique_lock<std::mutex> lk(m_);
  while (true) {
    // Esperar fase de bus o fin
    cv_.wait(lk, [&]{ return phase_ == Phase::RunBus || phase_ == Phase::Halt; });
    if (phase_ == Phase::Halt) break;

    const std::size_t mytick = tick_;
    if (bus_last_tick_ == mytick) {
      // Ya procesé este tick: espero el siguiente
      cv_.wait(lk, [&]{ return tick_ != mytick || phase_ == Phase::Halt; });
      if (phase_ == Phase::Halt) break;
      continue;
    }

    lk.unlock();
    bus_->step();  // logs dentro
    lk.lock();

    bus_last_tick_ = mytick;
    bus_done_ = true;
    cv_.notify_all();

    // Esperar al SIGUIENTE TICK
    cv_.wait(lk, [&]{ return tick_ != mytick || phase_ == Phase::Halt; });
    if (phase_ == Phase::Halt) break;
  }
}

void Simulator::advance_one_tick_blocking() {
  std::unique_lock<std::mutex> lk(m_);

  // Preparar nuevo tick
  ++tick_;
  pe_done_count_ = 0;
  bus_done_ = false;

  // Fase 1: PEs
  phase_ = Phase::RunPE;
  cv_.notify_all();
  cv_.wait(lk, [&]{ return pe_done_count_ == cfg::kNumPEs || phase_ == Phase::Halt; });
  if (phase_ == Phase::Halt) return;

  // Fase 2: BUS
  phase_ = Phase::RunBus;
  cv_.notify_all();
  cv_.wait(lk, [&]{ return bus_done_ || phase_ == Phase::Halt; });
  if (phase_ == Phase::Halt) return;

  // Vuelta a Idle (y notificar por si algún hilo espera cambio)
  phase_ = Phase::Idle;
  cv_.notify_all();
}

// ---------- Inicialización de memoria y programas ----------
bool Simulator::init_vectors_from_file(Addr baseA, Addr baseB, std::size_t N) {
  std::ifstream fin("input.txt");
  if (!fin) return false;

  auto nonempty = [](const std::string& s){
    return s.find_first_not_of(" \t\r\n") != std::string::npos;
  };
  std::string lineA, lineB;
  while (std::getline(fin, lineA) && !nonempty(lineA)) {}
  while (std::getline(fin, lineB) && !nonempty(lineB)) {}
  if (lineA.empty() || lineB.empty()) return false;

  auto parse_line = [](const std::string &s) {
    std::vector<double> v;
    std::istringstream is(s);
    for (double d; is >> d; ) v.push_back(d);
    return v;
  };
  const auto va = parse_line(lineA);
  const auto vb = parse_line(lineB);
  if (va.empty() || vb.empty()) return false;

  const std::size_t M = std::min<std::size_t>({va.size(), vb.size(), N});
  for (std::size_t i = 0; i < M; ++i) {
    mem_.write64(baseA + i*8, to_u64(va[i]));
    mem_.write64(baseB + i*8, to_u64(vb[i]));
  }
  for (std::size_t i = M; i < N; ++i) {
    mem_.write64(baseA + i*8, to_u64(0.0));
    mem_.write64(baseB + i*8, to_u64(0.0));
  }
  LOG_IF(cfg::kLogSim, "[InitDot] input.txt cargado: M=" << M << " (N solicitado=" << N << ")");
  return true;
}

void Simulator::dump_initial_memory() const {
  SOUT << "\n========== CONTENIDO DE MEMORIA (inicial) ==========\n";
  for (std::size_t addr = 0; addr < cfg::kMemWords * cfg::kWordBytes; addr += 8) {
    std::uint64_t v = mem_.read64(addr);
    double d; std::memcpy(&d, &v, sizeof(double));
    SOUT << "0x" << std::hex << std::setw(4) << addr << std::dec
         << " : " << std::fixed << std::setprecision(6) << d << "\n";
  }
  SOUT << "====================================================\n";
}

void Simulator::init_dot_problem(std::size_t N, Addr baseA, Addr baseB, Addr basePS) {
  dot_.N = N; dot_.baseA = baseA; dot_.baseB = baseB; dot_.basePS = basePS;

  // DRAM: A y B como double (8B)
  if (!init_vectors_from_file(baseA, baseB, N)) {
    for (std::size_t i = 0; i < N; ++i) {
      mem_.write64(baseA + i*8, to_u64(static_cast<double>(i + 1)));
      mem_.write64(baseB + i*8, to_u64(1.0));
    }
    LOG_IF(cfg::kLogSim, "[InitDot] input.txt no encontrado/incompleto, usando por defecto");
  }

  // partial_sums[PE] = 0.0
  for_each_pe([&](std::size_t pe){
    mem_.write64(basePS + pe*8, to_u64(0.0));
  });

  // Partición por PE
  dot_.seg = N / cfg::kNumPEs;
  for_each_pe([&](std::size_t pe){
    pes_[pe]->set_reg(0, dot_.seg);
    pes_[pe]->set_reg(1, baseA + pe*dot_.seg*8);
    pes_[pe]->set_reg(2, baseB + pe*dot_.seg*8);
    pes_[pe]->set_reg(3, basePS + pe*8);
  });

  LOG_IF(cfg::kLogSim,
    "[InitDot] N=" << N
    << " baseA=0x" << std::hex << baseA
    << " baseB=0x" << baseB
    << " basePS=0x" << basePS << std::dec
    << " seg=" << dot_.seg << " (INC avanza " << cfg::kWordBytes << "B)");

  dump_initial_memory();
}

void Simulator::load_demo_traces() {
  auto p = Assembler::assemble_from_file("examples/demo.asm");
  load_program_all(p);
}

void Simulator::load_program_all(const Program &p) {
  for_each_pe([&](std::size_t i){ pes_[i]->load_program(p); });
}

void Simulator::load_program_all_from_file(const std::string &path) {
  auto p = Assembler::assemble_from_file(path);
  load_program_all(p);
}

// ---------- Factorización: helpers de finalización ----------
void Simulator::do_final_reduction_and_print() {
  Program p;
  Instr warm1; warm1.op = OpCode::MOVI;  warm1.rd = 1; warm1.imm = dot_.basePS + 0x8;
  Instr warm2; warm2.op = OpCode::LOAD;  warm2.rd = 7; warm2.ra  = 1;

  Instr m1;   m1.op   = OpCode::MOVI;    m1.rd = 1; m1.imm = dot_.basePS;
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

  // --- Validación PASS/FAIL contra referencia CPU ---
  double ref = ref_dot_cpu();
  const double eps = 1e-9;
  bool pass = std::fabs(final - ref) < eps;

  SERR << "\n[Resultado final en PE0] Producto punto = "
       << std::fixed << std::setprecision(12) << final
       << " | ref=" << ref
       << " | " << (pass ? "PASS" : "FAIL")
       << " (eps=" << eps << ")\n";
}

void Simulator::dump_metrics() const {
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
         << " | BusBytes: " << m.bus_bytes
         << " | MESI{ E->S:" << m.trans_e_to_s
         << " S->M:" << m.trans_s_to_m
         << " E->M:" << m.trans_e_to_m
         << " M->S:" << m.trans_m_to_s
         << " X->I:" << m.trans_x_to_i
         << " }"
         << "\n";
  }
  SOUT << "-----------------------------------------------------------------------------------\n";
}

void Simulator::dump_bus_stats() const {
  SOUT << "Bus bytes: " << bus_->bytes()
       << " | BusRd=" << bus_->count_cmd(BusCmd::BusRd)
       << " | BusRdX=" << bus_->count_cmd(BusCmd::BusRdX)
       << " | Upgr="   << bus_->count_cmd(BusCmd::BusUpgr)
       << " | Flushes="<< bus_->flushes()
       << "\n";
}

double Simulator::ref_dot_cpu() const {
  double acc = 0.0;
  for (std::size_t i = 0; i < dot_.N; ++i) {
    double a = to_f64(mem_.read64(dot_.baseA + i*8));
    double b = to_f64(mem_.read64(dot_.baseB + i*8));
    acc += a * b;
  }
  return acc;
}

void Simulator::dump_all_pes_and_ref() const {
  SOUT << "\n================= DEBUG POR PE (REGISTROS + MEMORIA) =================\n";
  SOUT << std::fixed << std::setprecision(6);
  SOUT << "[Referencia CPU] dot(A,B) con N=" << dot_.N << " -> " << ref_dot_cpu() << "\n\n";

  for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) {
    SOUT << "---- PE" << pe << " -------------------------------------------------\n";

    SOUT << "REGISTROS:\n";
    for (int r = 0; r < 8; ++r) {
      std::uint64_t u = pes_[pe]->get_reg(r);
      SOUT << "  R" << r << " = 0x" << std::hex << u << std::dec;
      if (r >= 4) { SOUT << "  (f64=" << to_f64(u) << ")"; }
      if (r == 1 || r == 2 || r == 3) { SOUT << "  (addr-dec=" << u << ")"; }
      SOUT << "\n";
    }

    const std::size_t baseIdx = pe * dot_.seg;

    SOUT << "Segmento A[ " << baseIdx << " .. " << (baseIdx + dot_.seg - 1) << " ]\n";
    for (std::size_t k = 0; k < dot_.seg; ++k) {
      Addr addr = dot_.baseA + (baseIdx + k) * 8;
      double v = to_f64(mem_.read64(addr));
      SOUT << "  A[" << (baseIdx + k) << "] @0x" << std::hex << addr << std::dec
           << " = " << v << "\n";
    }

    SOUT << "Segmento B[ " << baseIdx << " .. " << (baseIdx + dot_.seg - 1) << " ]\n";
    for (std::size_t k = 0; k < dot_.seg; ++k) {
      Addr addr = dot_.baseB + (baseIdx + k) * 8;
      double v = to_f64(mem_.read64(addr));
      SOUT << "  B[" << (baseIdx + k) << "] @0x" << std::hex << addr << std::dec
           << " = " << v << "\n";
    }

    Addr ps = dot_.basePS + pe * 8;
    double vps = to_f64(mem_.read64(ps));
    SOUT << "partial_sums[" << pe << "] @0x" << std::hex << ps << std::dec
         << " = " << vps << "\n";

    double ref_partial = 0.0;
    for (std::size_t k = 0; k < dot_.seg; ++k) {
      double a = to_f64(mem_.read64(dot_.baseA + (baseIdx + k)*8));
      double b = to_f64(mem_.read64(dot_.baseB + (baseIdx + k)*8));
      ref_partial += a * b;
    }
    SOUT << "Parcial esperado PE" << pe << " = " << ref_partial << "\n";
    SOUT << "-----------------------------------------------------------------------\n\n";
  }
  SOUT << "=======================================================================\n";
}

// ---------- Ejecución: función común ----------
void Simulator::run_and_finalize(const std::function<void()>& runner) {
  runner(); // corre (por ciclos o hasta done)
  SOUT << "[Sim] Ejecución completada.\n\n";
  do_final_reduction_and_print();
  dump_metrics();
  dump_bus_stats();
  dump_all_pes_and_ref();
}

void Simulator::run_cycles(std::size_t cycles) {
  run_and_finalize([&](){
    for (std::size_t c = 0; c < cycles; ++c) {
      advance_one_tick_blocking();
    }
  });
}

void Simulator::run_until_done(std::size_t safety_max) {
  run_and_finalize([&](){
    std::size_t after_done_bus_steps = 0;
    for (std::size_t c = 0; c < safety_max; ++c) {
      const bool already_done = all_done();
      advance_one_tick_blocking();
      if (already_done) {
        if (++after_done_bus_steps >= 2) break;
      } else {
        after_done_bus_steps = 0;
      }
    }
  });
}

// ---------- Estado/consultas ----------
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
