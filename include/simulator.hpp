#pragma once
/**
 * Simulator: orquesta Bus, Memoria, Caches y PEs.
 * Multihilo: 1 hilo por PE + 1 para el Bus. Avanza por "ticks": PEs -> Bus.
 * Barrera por tick para evitar carreras y bloqueos.
 */

#include <array>
#include <memory>
#include <optional>
#include <cstddef>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

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

  // ---- Stepping interactivo (implementado en stepping_ui.cpp)
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

  // ------------- Estado "dot product" (agrupa lo que antes eran globals) -------------
  struct DotCfg {
    std::size_t N{0};
    std::size_t seg{0};
    Addr baseA{0};
    Addr baseB{0};
    Addr basePS{0};
  } dot_;

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

  // --------- Helpers factoriza2 ----------
  // 1) Finalización común: reduce y printea resultado
  void do_final_reduction_and_print();
  // 2) Métricas y bus
  void dump_metrics() const;
  void dump_bus_stats() const;
  // 3) Dumps por PE + referencia CPU
  double ref_dot_cpu() const;
  void dump_all_pes_and_ref() const;
  void dump_initial_memory() const;
  // 4) Carga de A/B desde archivo
  bool init_vectors_from_file(Addr baseA, Addr baseB, std::size_t N);
  // 5) Run genérico
  void run_and_finalize(const std::function<void()>& runner);

  // Helper de iteración (inline por ser template)
  template <class F>
  inline void for_each_pe(F&& f) {
    for (std::size_t pe = 0; pe < cfg::kNumPEs; ++pe) f(pe);
  }
};

} // namespace sim
