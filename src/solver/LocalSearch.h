// LocalSearch.h - Busqueda local con vecindarios para IHTC 2024
// TFG Alberto Hernandez
//
// Vecindarios adaptados a la codificacion espacial (patient-centric):
//   1. Cambiar habitacion de un paciente (exhaustivo)
//   2. Cambiar dia de admision - ventana completa (exhaustivo)
//   3. Cambiar quirofano (exhaustivo)
//   4. Intercambiar habitaciones de dos pacientes
//   5. Intercambiar dias de admision de dos pacientes
//   6. Programar/desprogramar paciente opcional (exhaustivo)
//   7. Cambiar enfermera asignada
//   8. Reubicacion compuesta (day+room+ot simultaneo)
//
// Estrategia: first-improvement exhaustivo por vecindario.
// Cada vecindario itera TODOS los pacientes programados (shuffled).
// ILS: perturbacion + reinicio desde la mejor solucion encontrada.

#ifndef SRC_SOLVER_LOCAL_SEARCH_H_
#define SRC_SOLVER_LOCAL_SEARCH_H_

#include <array>
#include <cstdint>
#include <random>
#include <string>

#include "../entities/ProblemData.h"
#include "../evaluator/Evaluator.h"
#include "../solution/Solution.h"

// Nombres de los 8 vecindarios (mismo orden que el vector interno de Run)
static constexpr std::array<const char*, 8> kOperatorNames = {
    "ChangeRoom", "ChangeDay",  "ChangeOT",   "Relocate",
    "SwapRooms",  "SwapDays",   "ToggleOpt",  "ChangeNurse"};

// Estadisticas de la busqueda local
struct LocalSearchStats {
  int iterations = 0;
  int improvements = 0;
  int initial_cost = 0;
  int final_cost = 0;
  double elapsed_seconds = 0.0;
  // Mejoras producidas por cada operador (mismo orden que kOperatorNames)
  std::array<int, 8> op_improvements = {};

  [[nodiscard]] std::string ToString() const;
};

class LocalSearch {
 public:
  // ejecuta busqueda local ILS sobre la solucion (con time limit en segundos)
  // enabled_mask: bitmask de 8 bits; bit i = 1 activa el operador i.
  //   0xFF = todos activos (comportamiento por defecto)
  // use_alns: si true, sustituye la perturbacion ILS clasica por una iteracion
  //   ALNS (destroy/repair + Simulated Annealing acceptance). Default false
  //   mantiene comportamiento v2 (ILS clasico).
  static LocalSearchStats Run(Solution& solution, int max_iterations,
                              std::mt19937& rng,
                              double time_limit_seconds = 30.0,
                              uint8_t enabled_mask = 0xFF,
                              bool use_alns = false);

 private:
  // vecindarios exhaustivos (iteran todos los pacientes, first-improvement)
  static bool TryChangeRoom(Solution& solution, int& current_cost,
                             std::mt19937& rng);
  static bool TryChangeDay(Solution& solution, int& current_cost,
                            std::mt19937& rng);
  static bool TryChangeOT(Solution& solution, int& current_cost,
                           std::mt19937& rng);
  static bool TrySwapRooms(Solution& solution, int& current_cost,
                            std::mt19937& rng);
  static bool TrySwapDays(Solution& solution, int& current_cost,
                           std::mt19937& rng);
  static bool TryToggleOptional(Solution& solution, int& current_cost,
                                 std::mt19937& rng);
  static bool TryChangeNurse(Solution& solution, int& current_cost,
                              std::mt19937& rng);
  static bool TryRelocate(Solution& solution, int& current_cost,
                           std::mt19937& rng);

  // perturbacion ILS
  static void Perturb(Solution& solution, int strength, std::mt19937& rng);

  // helper: obtiene lista shuffled de pacientes programados
  static std::vector<PatientId> GetShuffledScheduled(const Solution& solution,
                                                     std::mt19937& rng);
};

#endif  // SRC_SOLVER_LOCAL_SEARCH_H_
