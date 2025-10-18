#include "simulator.hpp"
#include <iostream>
#include <string>

/**
 * Modo normal vs. modo stepping:
 *   - Normal: run_until_done() o demo por defecto
 *   - Stepping: --step | -s para activar; ENTER=step, c=continuar, r=regs, b=bus, q=salir
 */
int main(int argc, char **argv)
{
  sim::Simulator mesi;

  bool stepping = false;
  std::string filePath;

  // Parse simple de argumentos:
  //   --step/-s activa stepping; el primer no-flag es el path del asm
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--step" || a == "-s") {
      stepping = true;
    } else {
      filePath = a;
    }
  }

  if (!filePath.empty())
  {
    // Precarga datos para N múltiplo de 4 (ej. N=16)
    mesi.init_dot_problem(/*N=*/16, /*baseA=*/0x000, /*baseB=*/0x100, /*basePS=*/0x200);

    SERR << "[Main] Cargando ASM desde: " << filePath << "\n";
    mesi.load_program_all_from_file(filePath);

    if (stepping) mesi.run_stepping();
    else          mesi.run_until_done();
  }
  else
  {
    // Demo por defecto
    mesi.load_demo_traces();

    if (stepping) mesi.run_stepping();
    else          mesi.run_until_done();
  }
  return 0;
}
