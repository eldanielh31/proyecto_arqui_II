#include "simulator.hpp"
#include "config.hpp"

// <-- Agregados necesarios para tener los tipos completos:
#include "processor.hpp"
#include "cache.hpp"

#include "debug_io.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace sim {

using dbg::print_reg_compact;
using dbg::print_reg_diff;

void Simulator::run_stepping() {
  SOUT << "\n===================== STEPPING INTERACTIVO =====================\n"
       << "ENTER=step | c=continuar | r=regs | b=bus | q=salir\n";

  bool auto_run = false;
  std::size_t step = 0;

  while (!all_done()) {
    if (!auto_run) {
      SOUT << "\n[step " << step << "] > ";
      std::string line;
      if (!std::getline(std::cin, line)) { SOUT << "\n[Stepping] stdin cerrado. Saliendo.\n"; break; }
      if (line == "q" || line == "Q") { SOUT << "[Stepping] Salir.\n"; break; }
      if (line == "c" || line == "C") { auto_run = true; SOUT << "[Stepping] Continuación automática habilitada.\n"; }
      else if (line == "r" || line == "R") { for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) dump_regs(pe); continue; }
      else if (line == "b" || line == "B") { dump_bus_stats(); continue; }
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
    if (pes_[pe]->is_done()) { SOUT << "[PE" << pe << "] DONE (no ejecuta)\n"; continue; }
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

  dump_bus_stats();

  // Dump de caché por PE
  SOUT << "\n----------------------- CACHE DUMP (por paso) -----------------------\n";
  for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) {
    SOUT << "[PE" << pe << "]\n";
    caches_[pe]->debug_dump(std::cout, std::nullopt, /*with_data=*/true);
    SOUT << "------------------------------------------------------------------\n";
  }
}

} // namespace sim
