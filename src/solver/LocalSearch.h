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

/** @brief Nombres de los vecindarios (8 atomicos + 3 compuestos = 11 total).
 *  Indices 0..7 corresponden al bitmask de 8 bits usado historicamente.
 *  Indices 8..10 son los compound moves anadidos en Plan II/Fase F.
 */
static constexpr std::array<const char*, 11> kOperatorNames = {
    "ChangeRoom",   "ChangeDay",     "ChangeOT",         "Relocate",
    "SwapRooms",    "SwapDays",      "ToggleOpt",        "ChangeNurse",
    "KickPatient",  "ReorganizeDay", "SwapNurseBlock"};

/** @brief Estadisticas de la busqueda local. */
struct LocalSearchStats {
  int iterations = 0;
  int improvements = 0;
  int initial_cost = 0;
  int final_cost = 0;
  double elapsed_seconds = 0.0;
  // Mejoras producidas por cada operador (mismo orden que kOperatorNames).
  // Solo se usan las primeras 11; el resto es padding por si se anaden.
  std::array<int, 16> op_improvements = {};

  /** @brief Serializa las estadisticas a texto. */
  [[nodiscard]] std::string ToString() const;
};

/** @brief Parametros de la VNS / ILS. Defaults agresivos (Bloque A de mejoras).
 *  Para volver a comportamiento legacy basta con construir un VNSConfig con
 *  los valores anteriores: max_patients_per_op=60, exhaustive_optional=false,
 *  max_combos_relocate=30, max_positions_nurse=100, perturb_strength_*=fijo 4,
 *  refresh_nurses=false.
 */
struct VNSConfig {
  // A1: cap de pacientes por operador patient-based (0 = sin cap)
  int max_patients_per_op = 0;

  // A2: TryToggleOptional exhaustivo (true = prueba hasta max_insertions
  // posiciones por opcional; false = comportamiento legacy con 1 intento)
  bool exhaustive_optional = true;
  int  max_insertions_per_optional = 50;

  // A3: perturbacion proporcional al tamano:
  //   strength = clamp(round(perturb_strength_factor * num_scheduled),
  //                    perturb_strength_base, perturb_strength_max)
  int    perturb_strength_base = 4;
  int    perturb_strength_max  = 25;
  double perturb_strength_factor = 0.10;

  // A4: caps levantados (legacy = 200 / 30 / 100)
  int max_pairs_swap        = 200;   // TrySwapRooms, TrySwapDays (mantenido)
  int max_combos_relocate   = 200;   // TryRelocate (era 30)
  int max_positions_nurse   = 500;   // TryChangeNurse (era 100)

  // A5: reconstruccion periodica de enfermeras (0 = desactivado)
  // Cada N iteraciones del bucle LS, regenera nurse desde cero y acepta solo
  // si no empeora mas del nurse_refresh_tolerance_pct %.
  int    nurse_refresh_every          = 50;
  double nurse_refresh_tolerance_pct  = 2.0;
  bool   refresh_nurses               = true;

  // Fase F (compound moves): activar/desactivar los 3 operadores compuestos
  // (KickPatient, ReorganizeDay, SwapNurseBlock) introducidos en Plan II.
  // Si false, el bucle LS se comporta exactamente como antes (8 atomicos).
  bool   enable_compound = true;
};

/** @brief Busqueda local ILS con vecindarios sobre la codificacion patient-centric. */
class LocalSearch {
 public:
  /** @brief Ejecuta busqueda local ILS sobre la solucion (time limit en segundos).
   *  @param enabled_mask bitmask de 8 bits; bit i = 1 activa el operador i (0xFF = todos)
   *  @param config parametros configurables de la VNS (caps y flags); default agresivo,
   *    pasar valores legacy para reproducir comportamiento previo
   */
  static LocalSearchStats Run(Solution& solution, int max_iterations,
                              std::mt19937& rng,
                              double time_limit_seconds = 30.0,
                              uint8_t enabled_mask = 0xFF,
                              const VNSConfig& config = {});

 private:
  // Vecindarios exhaustivos: iteran todos los pacientes, first-improvement.

  /** @brief Cambia la habitacion de un paciente. */
  static bool TryChangeRoom(Solution& solution, int& current_cost,
                             std::mt19937& rng);
  /** @brief Cambia el dia de admision de un paciente (ventana completa). */
  static bool TryChangeDay(Solution& solution, int& current_cost,
                            std::mt19937& rng);
  /** @brief Cambia el quirofano asignado a un paciente. */
  static bool TryChangeOT(Solution& solution, int& current_cost,
                           std::mt19937& rng);
  /** @brief Intercambia las habitaciones de dos pacientes. */
  static bool TrySwapRooms(Solution& solution, int& current_cost,
                            std::mt19937& rng);
  /** @brief Intercambia los dias de admision de dos pacientes. */
  static bool TrySwapDays(Solution& solution, int& current_cost,
                           std::mt19937& rng);
  /** @brief Programa o desprograma un paciente opcional. */
  static bool TryToggleOptional(Solution& solution, int& current_cost,
                                 std::mt19937& rng);
  /** @brief Cambia la enfermera asignada. */
  static bool TryChangeNurse(Solution& solution, int& current_cost,
                              std::mt19937& rng);
  /** @brief Reubicacion compuesta de un paciente (day+room+ot simultaneo). */
  static bool TryRelocate(Solution& solution, int& current_cost,
                           std::mt19937& rng);

  /** @brief Perturbacion ILS de intensidad strength. */
  static void Perturb(Solution& solution, int strength, std::mt19937& rng);

  /** @brief Devuelve la lista de pacientes programados en orden aleatorio. */
  static std::vector<PatientId> GetShuffledScheduled(const Solution& solution,
                                                     std::mt19937& rng);
};

#endif  // SRC_SOLVER_LOCAL_SEARCH_H_
