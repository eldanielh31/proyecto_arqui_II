#pragma once
/**
 * Simulator: orquesta Bus, Memoria, Caches y PEs.
 *
 * Multihilo (1 hilo por PE + 1 hilo para el BUS) y stepping.
 * Corrección de congelamiento: cada hilo procesa exactamente 1 "tick"
 * y espera explícitamente a que cambie tick_ antes de poder volver a
 * trabajar, evitando carreras/despertares espurios que dejaban al
 * orquestador esperando contadores que nunca llegaban.
 */

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
  ~Simulator();  // Def en .cpp (evita incomplete-type con unique_ptr)

  // ---- Inicialización / carga de programas
  void init_dot_problem(std::size_t N, Addr baseA, Addr baseB, Addr basePS);
  void load_demo_traces();
  void load_program_all(const Program &p);
  void load_program_all_from_file(const std::string &path);

  // ---- Ejecución
  void run_cycles(std::size_t cycles);
  void run_until_done(std::size_t safety_max = 100000);

  // ---- Stepping interactivo
  void run_stepping();  // ENTER=step | c=continuar | r=regs | b=bus | q=salir
  void step_one();      // 1 tick (PEs + Bus) con diffs y dumps
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
  Phase      phase_      = Phase::Idle;

  // Generación de tick (se incrementa por cada paso completo)
  std::size_t tick_      = 0;

  // Contadores para completitud de fases
  std::size_t pe_done_count_ = 0; // nº de PEs que ya procesaron el tick actual
  bool        bus_done_      = false;

  // Último tick procesado por cada hilo (para no repetir/correr dos veces)
  std::array<std::size_t, cfg::kNumPEs> pe_last_tick_{};
  std::size_t bus_last_tick_ = 0;

  bool threads_started_ = false;

  // Lanzado/parada de hilos y avance de 1 tick (bloqueante)
  void start_threads();
  void stop_threads();
  void advance_one_tick_blocking();

  // Cuerpos de los hilos
  void worker_pe(std::size_t pe_idx);
  void worker_bus();
};

} // namespace sim
