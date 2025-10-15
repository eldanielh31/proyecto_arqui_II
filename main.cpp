#include "simulator.hpp"
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
  sim::Simulator mesi;
  if (argc > 1)
  {
    std::string filePath = argv[1];

    // Precarga datos para N múltiplo de 4 (e.g., N=16)
    mesi.init_dot_problem(/*N=*/16, /*baseA=*/0x000, /*baseB=*/0x100, /*basePS=*/0x200);

    std::cerr << "[Main] Cargando ASM desde: " << filePath << "\n";
    mesi.load_program_all_from_file(filePath);
    mesi.run_until_done(); // <-- ahora se detiene automáticamente
  }
  else
  {
    // Demo para procesador
    mesi.load_demo_traces();
    mesi.run_until_done();
  }
  return 0;
}
