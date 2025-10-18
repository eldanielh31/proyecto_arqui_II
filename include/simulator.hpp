#pragma once
//
// Simulator: orquesta Bus, Memoria, Caches y PEs.
// Multihilo: 1 hilo por PE + 1 para el Bus.
// Avanza por "ticks": primero corren los PEs, luego el Bus.
// Usa una barrera por tick para que nadie se adelante y evitar deadlocks.
//
#include <array>
#include <memory>
#include <optional>
#include <cstddef>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "config.hpp"
#include "types.hpp"
#include "memory.hpp"

namespace sim {

class Cache;
class Bus;
class Processor;
struct Program;

class Simulator {
public:
  Simulator();
  ~Simulator();  // Definido en .cpp (para evitar incomplete-type con unique_ptr)

  // ---- Setup / carga de programas
  void init_dot_problem(std::size_t N, Addr baseA, Addr baseB, Addr basePS); // prepara A·B y PS
  void load_demo_traces();                         // traza de demo
  void load_program_all(const Program &p);         // mismo prog para todos los PEs
  void load_program_all_from_file(const std::string &path);

  // ---- Ejecución
  void run_cycles(std::size_t cycles);             // corre N ticks
  void run_until_done(std::size_t safety_max = 100000); // hasta que todos terminen

  // ---- Stepping interactivo
  void run_stepping();  // ENTER=step | c=continuar | r=regs | b=bus | q=salir
  void step_one();      // 1 tick (PEs + Bus) con diffs/dumps
  bool all_done() const;

  // ---- Utilidades
  void dump_cache(std::size_t pe, std::optional<std::size_t> only_set = std::nullopt) const;
  void dump_regs(std::size_t pe) const;

private:
  // ------------- Componentes -------------
  std::unique_ptr<Bus> bus_;
  std::array<std::unique_ptr<Cache>,     cfg::kNumPEs> caches_;
  std::array<std::unique_ptr<Processor>, cfg::kNumPEs> pes_;
  Memory mem_;

  // ------------- Multihilo -------------
  enum class Phase { Idle, RunPE, RunBus, Halt };

  // Hilos (uno por PE + uno para el Bus)
  std::array<std::thread, cfg::kNumPEs> pe_threads_;
  std::thread bus_thread_;

  // Estado compartido
  mutable std::mutex m_;
  std::condition_variable cv_;
  Phase      phase_      = Phase::Idle; // qué toca en el tick actual

  // Tick global (se incrementa al completar PE+Bus)
  std::size_t tick_      = 0;

  // Contadores de completitud por fase
  std::size_t pe_done_count_ = 0; // PEs que ya hicieron su step en este tick
  bool        bus_done_      = false;

  // Último tick que procesó cada hilo (para no repetir)
  std::array<std::size_t, cfg::kNumPEs> pe_last_tick_{};
  std::size_t bus_last_tick_ = 0;

  bool threads_started_ = false;

  // Lanzar/terminar hilos y avanzar un tick (bloqueante)
  void start_threads();
  void stop_threads();
  void advance_one_tick_blocking();

  // Cuerpos de hilo
  void worker_pe(std::size_t pe_idx);
  void worker_bus();
};

} // namespace sim
