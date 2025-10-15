#include "simulator.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  sim::Simulator S;
  if (argc > 1) {
    std::string path = argv[1];

    // Precarga datos para N múltiplo de 4 (e.g., N=16)
    S.init_dot_problem(/*N=*/16, /*baseA=*/0x000, /*baseB=*/0x100, /*basePS=*/0x200);

    std::cerr << "[Main] Cargando ASM desde: " << path << "\n";
    S.load_program_all_from_file(path);
    S.run_until_done();      // <-- ahora se detiene automáticamente
  } else {
    S.load_demo_traces();
    S.run_until_done();      // <-- también auto-stop para el demo
  }
  return 0;
}
