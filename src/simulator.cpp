#include "simulator.hpp"
#include "config.hpp"
#include "assembler.hpp"
#include "types.hpp"

// Incluir definiciones completas que usamos en este .cpp:
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

namespace sim
{

  // ===== Helpers locales =====
  static inline std::uint64_t to_u64(double d)
  {
    std::uint64_t v;
    std::memcpy(&v, &d, sizeof(double));
    return v;
  }

  static inline double to_f64(std::uint64_t u)
  {
    double d;
    std::memcpy(&d, &u, sizeof(double));
    return d;
  }

  // Pretty helpers para el stepping
  static void print_reg_compact(std::ostream& os, int r, std::uint64_t v)
  {
    os << "R" << r << "=";
    if (r >= 4) {
      os << "0x" << std::hex << v << std::dec
         << " (f64=" << std::fixed << std::setprecision(6) << to_f64(v) << ")";
    } else if (r == 1 || r == 2 || r == 3) {
      os << "0x" << std::hex << v << std::dec
         << " (addr-dec=" << v << ")";
    } else {
      os << "0x" << std::hex << v << std::dec;
    }
  }

  static void print_reg_diff(std::ostream& os, int r, std::uint64_t before, std::uint64_t after)
  {
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

  // Guardamos configuración de problema para logs post-mortem (sin tocar headers)
  static std::size_t gN = 0;
  static std::size_t gSeg = 0;
  static Addr gBaseA = 0;
  static Addr gBaseB = 0;
  static Addr gBasePS = 0;

  // Destructor OUT-OF-LINE: aquí las clases Bus/Cache/Processor ya son tipos completos,
  // lo que evita el error de unique_ptr con tipos incompletos.
  Simulator::~Simulator() = default;

  Simulator::Simulator()
  {
    // Construimos el Bus sin cachés y luego enlazamos (evita depender de orden de construcción)
    std::vector<Cache *> tmp;
    bus_ = std::make_unique<Bus>(tmp);

    // Creamos cachés y las conectamos al bus/mem
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
      caches_[i] = std::make_unique<Cache>(static_cast<PEId>(i), *bus_, mem_);

    // Registramos los punteros de caché en el bus
    std::vector<Cache *> ptrs;
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
      ptrs.push_back(caches_[i].get());
    bus_->set_caches(ptrs);

    // Creamos los procesadores (PEs) y les asignamos su caché
    for (std::size_t i = 0; i < cfg::kNumPEs; ++i)
      pes_[i] = std::make_unique<Processor>(static_cast<PEId>(i), *caches_[i]);
  }

  void Simulator::init_dot_problem(std::size_t N, Addr baseA, Addr baseB, Addr basePS)
  {
    gN = N;
    gBaseA = baseA;
    gBaseB = baseB;
    gBasePS = basePS;

    // --- DRAM: A y B como double (8B) ---
    {
      std::ifstream fin("input.txt");
      bool loaded = false;

      if (fin)
      {
        std::string lineA, lineB;

        auto nonempty = [](const std::string& s){
          return s.find_first_not_of(" \t\r\n") != std::string::npos;
        };

        // Tomar primera y segunda línea no vacías
        while (std::getline(fin, lineA) && !nonempty(lineA)) {}
        while (std::getline(fin, lineB) && !nonempty(lineB)) {}

        if (!lineA.empty() && !lineB.empty())
        {
          auto parse_line = [](const std::string &s) {
            std::vector<double> v;
            std::istringstream is(s);
            double d;
            while (is >> d) v.push_back(d);
            return v;
          };

          std::vector<double> va = parse_line(lineA);
          std::vector<double> vb = parse_line(lineB);

          if (!va.empty() && !vb.empty())
          {
            std::size_t M = std::min<std::size_t>({va.size(), vb.size(), N});
            for (std::size_t i = 0; i < M; ++i) {
              mem_.write64(baseA + i * 8, to_u64(va[i]));
              mem_.write64(baseB + i * 8, to_u64(vb[i]));
            }
            for (std::size_t i = M; i < N; ++i) {
              mem_.write64(baseA + i * 8, to_u64(0.0));
              mem_.write64(baseB + i * 8, to_u64(0.0));
            }
            LOG_IF(cfg::kLogSim, "[InitDot] input.txt cargado: M=" << M << " (N solicitado=" << N << ")");
            loaded = true;
          }
        }
      }

      if (!loaded)
      {
        for (std::size_t i = 0; i < N; ++i)
        {
          mem_.write64(baseA + i * 8, to_u64(static_cast<double>(i + 1))); // A[i] = i+1
          mem_.write64(baseB + i * 8, to_u64(1.0));                        // B[i] = 1.0
        }
        LOG_IF(cfg::kLogSim, "[InitDot] input.txt no encontrado o incompleto, usando valores por defecto");
      }
    }

    // partial_sums[PE] = 0.0
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe)
      mem_.write64(basePS + pe * 8, to_u64(0.0));

    // Partición por PE:
    //   REG0 = contador (N/kNumPEs)
    //   REG1 = base de A del segmento del PE
    //   REG2 = base de B del segmento del PE
    //   REG3 = &partial_sums[PE]
    gSeg = N / cfg::kNumPEs;
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe)
    {
      pes_[pe]->set_reg(0, gSeg);
      pes_[pe]->set_reg(1, baseA + pe * gSeg * 8);
      pes_[pe]->set_reg(2, baseB + pe * gSeg * 8);
      pes_[pe]->set_reg(3, basePS + pe * 8);
    }

    LOG_IF(cfg::kLogSim,
           "[InitDot] N=" << N
                          << " baseA=0x" << std::hex << baseA
                          << " baseB=0x" << baseB
                          << " basePS=0x" << basePS << std::dec
                          << " seg=" << gSeg << " (INC avanza " << cfg::kWordBytes << "B)");

    // Dump de memoria inicial
    std::cout << "\n========== CONTENIDO DE MEMORIA (inicial) ==========\n";
    for (std::size_t addr = 0; addr < cfg::kMemWords * cfg::kWordBytes; addr += 8)
    {
      std::uint64_t v = mem_.read64(addr);
      double d; std::memcpy(&d, &v, sizeof(double));
      std::cout << "0x" << std::hex << std::setw(4) << addr << std::dec << " : "
                << std::fixed << std::setprecision(6) << d << "\n";
    }
    std::cout << "====================================================\n";
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

  // Ejecuta N ciclos “grandes” (mantiene coherencia alternando con el bus)
  void Simulator::run_cycles(std::size_t cycles)
  {
    for (std::size_t c = 0; c < cycles; ++c)
    {
      for (auto &pe : pes_)
        pe->step();
      bus_->step();
    }

    std::cout << "[Sim] Ejecución completada.\n\n";

    // === Reducción final en PE0 con instrucción REDUCE ===
    {
      Program p;
      Instr warm1; warm1.op = OpCode::MOVI;  warm1.rd = 1; warm1.imm = gBasePS + 0x8; // 0x208
      Instr warm2; warm2.op = OpCode::LOAD;  warm2.rd = 7; warm2.ra  = 1;             // LOAD R7, [R1]

      Instr m1;   m1.op   = OpCode::MOVI;    m1.rd = 1; m1.imm = gBasePS;             // basePS
      Instr m2;   m2.op   = OpCode::MOVI;    m2.rd = 2; m2.imm = cfg::kNumPEs;        // count
      Instr r;    r.op    = OpCode::REDUCE;  r.rd = 4; r.ra = 1; r.rb = 2;            // R4 = sum(base,count)
      Instr st;   st.op   = OpCode::STORE;   st.ra = 4; st.rd = 3;                    // STORE R4 -> [R3]

      p.code = { warm1, warm2, m1, m2, r, st };
      pes_[0]->load_program(p);

      std::size_t k = 0;
      while (!pes_[0]->is_done() && k++ < 2000)
      {
        pes_[0]->step();
        bus_->step();
      }

      std::uint64_t bits = pes_[0]->get_reg(4);
      double final;
      std::memcpy(&final, &bits, sizeof(double));
      std::cerr << "\n[Resultado final en PE0] Producto punto = "
                << std::fixed << std::setprecision(6) << final << "\n";
    }

    // ----- Métricas (se conservan) -----
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
    std::cout << "-----------------------------------------------------------------------------------\n";
    std::cout << "Bus bytes: " << bus_->bytes()
              << " | BusRd=" << bus_->count_cmd(BusCmd::BusRd)
              << " | BusRdX=" << bus_->count_cmd(BusCmd::BusRdX)
              << " | Upgr=" << bus_->count_cmd(BusCmd::BusUpgr)
              << " | Flushes=" << bus_->flushes()
              << "\n";

    // ====== DUMPS de REGISTROS y MEMORIA por PE (post-mortem) ======
    std::cout << "\n================= DEBUG POR PE (REGISTROS + MEMORIA) =================\n";
    std::cout << std::fixed << std::setprecision(6);

    // Referencia (CPU) del producto punto para comparar
    double ref_dot = 0.0;
    for (std::size_t i = 0; i < gN; ++i) {
      double a = to_f64(mem_.read64(gBaseA + i*8));
      double b = to_f64(mem_.read64(gBaseB + i*8));
      ref_dot += a * b;
    }
    std::cout << "[Referencia CPU] dot(A,B) con N=" << gN << " -> " << ref_dot << "\n\n";

    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe)
    {
      std::cout << "---- PE" << pe << " -------------------------------------------------\n";

      // REGISTROS
      std::cout << "REGISTROS:\n";
      for (int r = 0; r < 8; ++r)
      {
        std::uint64_t u = pes_[pe]->get_reg(r);
        std::cout << "  R" << r
                  << " = 0x" << std::hex << u << std::dec;

        if (r >= 4) { // R4..R7 como double también
          std::cout << "  (f64=" << to_f64(u) << ")";
        }
        if (r <= 3 && (r == 1 || r == 2 || r == 3)) { // punteros y PS
          std::cout << "  (addr-dec=" << u << ")";
        }
        std::cout << "\n";
      }

      // SEGMENTO DE A y B que procesa cada PE
      const std::size_t baseIdx = pe * gSeg;

      std::cout << "Segmento A[ " << baseIdx << " .. " << (baseIdx + gSeg - 1) << " ]\n";
      for (std::size_t k = 0; k < gSeg; ++k) {
        Addr addr = gBaseA + (baseIdx + k) * 8;
        double v = to_f64(mem_.read64(addr));
        std::cout << "  A[" << (baseIdx + k) << "] @0x" << std::hex << addr << std::dec
                  << " = " << v << "\n";
      }

      std::cout << "Segmento B[ " << baseIdx << " .. " << (baseIdx + gSeg - 1) << " ]\n";
      for (std::size_t k = 0; k < gSeg; ++k) {
        Addr addr = gBaseB + (baseIdx + k) * 8;
        double v = to_f64(mem_.read64(addr));
        std::cout << "  B[" << (baseIdx + k) << "] @0x" << std::hex << addr << std::dec
                  << " = " << v << "\n";
      }

      // Suma parcial en memoria
      {
        Addr ps = gBasePS + pe * 8;
        double v = to_f64(mem_.read64(ps));
        std::cout << "partial_sums[" << pe << "] @0x" << std::hex << ps << std::dec
                  << " = " << v << "\n";
      }

      // Suma parcial "esperada" de este PE (para cotejo)
      double ref_partial = 0.0;
      for (std::size_t k = 0; k < gSeg; ++k) {
        double a = to_f64(mem_.read64(gBaseA + (baseIdx + k)*8));
        double b = to_f64(mem_.read64(gBaseB + (baseIdx + k)*8));
        ref_partial += a * b;
      }
      std::cout << "Parcial esperado PE" << pe << " = " << ref_partial << "\n";

      std::cout << "-----------------------------------------------------------------------\n\n";
    }
    std::cout << "=======================================================================\n";
  }

  // Ejecuta hasta que todos los PE terminen (con margen de seguridad)
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

      bus_->step();

      if (done == cfg::kNumPEs)
      {
        if (++after_done_bus_steps >= 2) break;
      }
    }

    // Reducción final en PE0 (idéntica a run_cycles)
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
      while (!pes_[0]->is_done() && k++ < 2000)
      {
        pes_[0]->step();
        bus_->step();
      }

      std::uint64_t bits = pes_[0]->get_reg(4);
      double final;
      std::memcpy(&final, &bits, sizeof(double));
      std::cerr << "\n[Resultado final en PE0] Producto punto = "
                << std::fixed << std::setprecision(6) << final << "\n";
    }

    // ===== NUEVO: Métricas al final también en run_until_done() =====
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
    std::cout << "-----------------------------------------------------------------------------------\n";
    std::cout << "Bus bytes: " << bus_->bytes()
              << " | BusRd=" << bus_->count_cmd(BusCmd::BusRd)
              << " | BusRdX=" << bus_->count_cmd(BusCmd::BusRdX)
              << " | Upgr=" << bus_->count_cmd(BusCmd::BusUpgr)
              << " | Flushes=" << bus_->flushes()
              << "\n";

    // ====== DUMPS de REGISTROS y MEMORIA por PE (post-mortem) ======
    std::cout << "\n================= DEBUG POR PE (REGISTROS + MEMORIA) =================\n";
    std::cout << std::fixed << std::setprecision(6);

    double ref_dot = 0.0;
    for (std::size_t i = 0; i < gN; ++i) {
      double a = to_f64(mem_.read64(gBaseA + i*8));
      double b = to_f64(mem_.read64(gBaseB + i*8));
      ref_dot += a * b;
    }
    std::cout << "[Referencia CPU] dot(A,B) con N=" << gN << " -> " << ref_dot << "\n\n";

    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe)
    {
      std::cout << "---- PE" << pe << " -------------------------------------------------\n";
      std::cout << "REGISTROS:\n";
      for (int r = 0; r < 8; ++r)
      {
        std::uint64_t u = pes_[pe]->get_reg(r);
        std::cout << "  R" << r
                  << " = 0x" << std::hex << u << std::dec;
        if (r >= 4) { std::cout << "  (f64=" << to_f64(u) << ")"; }
        if (r <= 3 && (r == 1 || r == 2 || r == 3)) { std::cout << "  (addr-dec=" << u << ")"; }
        std::cout << "\n";
      }

      const std::size_t baseIdx = pe * gSeg;

      std::cout << "Segmento A[ " << baseIdx << " .. " << (baseIdx + gSeg - 1) << " ]\n";
      for (std::size_t k = 0; k < gSeg; ++k) {
        Addr addr = gBaseA + (baseIdx + k) * 8;
        double v = to_f64(mem_.read64(addr));
        std::cout << "  A[" << (baseIdx + k) << "] @0x" << std::hex << addr << std::dec
                  << " = " << v << "\n";
      }

      std::cout << "Segmento B[ " << baseIdx << " .. " << (baseIdx + gSeg - 1) << " ]\n";
      for (std::size_t k = 0; k < gSeg; ++k) {
        Addr addr = gBaseB + (baseIdx + k) * 8;
        double v = to_f64(mem_.read64(addr));
        std::cout << "  B[" << (baseIdx + k) << "] @0x" << std::hex << addr << std::dec
                  << " = " << v << "\n";
      }

      Addr ps = gBasePS + pe * 8;
      double vps = to_f64(mem_.read64(ps));
      std::cout << "partial_sums[" << pe << "] @0x" << std::hex << ps << std::dec
                << " = " << vps << "\n";

      double ref_partial = 0.0;
      for (std::size_t k = 0; k < gSeg; ++k) {
        double a = to_f64(mem_.read64(gBaseA + (baseIdx + k)*8));
        double b = to_f64(mem_.read64(gBaseB + (baseIdx + k)*8));
        ref_partial += a * b;
      }
      std::cout << "Parcial esperado PE" << pe << " = " << ref_partial << "\n";
      std::cout << "-----------------------------------------------------------------------\n\n";
    }
    std::cout << "=======================================================================\n";
  }

  // ============================================================================
  // Stepping interactivo
  // ----------------------------------------------------------------------------

  void Simulator::run_stepping() {
    std::cout << "\n===================== STEPPING INTERACTIVO =====================\n"
              << "ENTER=step | c=continuar | r=regs | b=bus | q=salir\n";

    bool auto_run = false;
    std::size_t step = 0;

    while (!all_done()) {
      if (!auto_run) {
        std::cout << "\n[step " << step << "] > ";
        std::string line;
        if (!std::getline(std::cin, line)) {
          std::cout << "\n[Stepping] stdin cerrado. Saliendo.\n";
          break;
        }
        if (line == "q" || line == "Q") {
          std::cout << "[Stepping] Salir.\n";
          break;
        }
        if (line == "c" || line == "C") {
          auto_run = true;
          std::cout << "[Stepping] Continuación automática habilitada.\n";
        } else if (line == "r" || line == "R") {
          for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) dump_regs(pe);
          continue;
        } else if (line == "b" || line == "B") {
          std::cout << "[Bus] bytes=" << bus_->bytes()
                    << " | BusRd="  << bus_->count_cmd(BusCmd::BusRd)
                    << " | BusRdX=" << bus_->count_cmd(BusCmd::BusRdX)
                    << " | Upgr="   << bus_->count_cmd(BusCmd::BusUpgr)
                    << " | Flushes="<< bus_->flushes()
                    << "\n";
          continue;
        }
      }

      std::cout << "\n===== STEP " << step << " =====\n";
      step_one();
      ++step;
    }

    std::cout << "\n[Stepping] Terminado (auto_run=" << (auto_run ? "true" : "false") << ").\n";
  }

  void Simulator::step_one() {
    // Snapshot BEFORE de todos los PE (para poder comparar AFTER)
    std::array<std::array<std::uint64_t, 8>, cfg::kNumPEs> before{};
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) {
      for (int r = 0; r < 8; ++r) before[pe][r] = pes_[pe]->get_reg(r);
    }

    // Mostrar BEFORE compacto por PE (te da contexto previo al log de instrucción)
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) {
      if (pes_[pe]->is_done()) {
        std::cout << "[PE" << pe << "] DONE (no ejecuta)\n";
        continue;
      }
      std::cout << "[PE" << pe << "] BEFORE: ";
      // R0..R4 suelen bastar para entender el estado del loop
      print_reg_compact(std::cout, 0, before[pe][0]); std::cout << " | ";
      print_reg_compact(std::cout, 1, before[pe][1]); std::cout << " | ";
      print_reg_compact(std::cout, 2, before[pe][2]); std::cout << " | ";
      print_reg_compact(std::cout, 3, before[pe][3]); std::cout << " | ";
      print_reg_compact(std::cout, 4, before[pe][4]);
      std::cout << "\n";
    }

    // Ejecutar UNA instrucción (si puede) en cada PE
    for (auto& pe : pes_) {
      if (!pe->is_done()) pe->step();   // Aquí se imprimen los logs de instrucción (LOAD, FMUL, etc.)
    }

    // Un avance del bus
    std::cout << "[BUS] step()\n";
    bus_->step();

    // Snapshot AFTER y diffs por PE
    std::cout << "\n--- REG DIFFS (AFTER) ---\n";
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) {
      std::array<std::uint64_t, 8> after{};
      for (int r = 0; r < 8; ++r) after[r] = pes_[pe]->get_reg(r);

      bool any = false;
      std::ostringstream oss;
      for (int r = 0; r < 8; ++r) {
        std::ostringstream line;
        std::streambuf* old = std::cout.rdbuf(line.rdbuf());
        print_reg_diff(std::cout, r, before[pe][r], after[r]);
        std::cout.rdbuf(old);
        if (!line.str().empty()) {
          any = true;
          oss << line.str();
        }
      }

      if (any) {
        std::cout << "[PE" << pe << "]\n" << oss.str();
      } else {
        std::cout << "[PE" << pe << "] (sin cambios en registros)\n";
      }
    }

    // Resumen breve del bus tras este paso (útil al depurar)
    std::cout << "[BUS] bytes=" << bus_->bytes()
              << " | BusRd="  << bus_->count_cmd(BusCmd::BusRd)
              << " | BusRdX=" << bus_->count_cmd(BusCmd::BusRdX)
              << " | Upgr="   << bus_->count_cmd(BusCmd::BusUpgr)
              << " | Flushes="<< bus_->flushes()
              << "\n";

    // Dump de caché por PE (se mantiene)
    std::cout << "\n----------------------- CACHE DUMP (por paso) -----------------------\n";
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) {
      std::cout << "[PE" << pe << "]\n";
      caches_[pe]->debug_dump(std::cout, std::nullopt, /*with_data=*/true);
      std::cout << "------------------------------------------------------------------\n";
    }
  }

  bool Simulator::all_done() const {
    for (const auto& pe : pes_) {
      if (!pe->is_done()) return false;
    }
    return true;
  }

  void Simulator::dump_cache(std::size_t pe, std::optional<std::size_t> only_set) const {
    if (pe >= cfg::kNumPEs) return;
    caches_[pe]->debug_dump(std::cout, only_set, /*with_data=*/true);
  }

  void Simulator::dump_regs(std::size_t pe) const {
    if (pe >= cfg::kNumPEs) return;
    std::cout << "REGISTROS PE" << pe << ":\n";
    for (int r = 0; r < 8; ++r) {
      std::uint64_t u = pes_[pe]->get_reg(r);
      std::cout << "  R" << r << " = 0x" << std::hex << u << std::dec;
      if (r >= 4) {
        double d; std::memcpy(&d, &u, sizeof(double));
        std::cout << "  (f64=" << d << ")";
      }
      if (r <= 3 && (r == 1 || r == 2 || r == 3)) {
        std::cout << "  (addr-dec=" << u << ")";
      }
      std::cout << "\n";
    }
  }

} // namespace sim
