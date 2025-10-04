#include "simulator.hpp"

int main() {
  sim::Simulator S;
  S.load_demo_traces();
  S.run_cycles(16);
  return 0;
}
