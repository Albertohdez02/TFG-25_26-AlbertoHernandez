// RandomGenerator.h - Generador de soluciones aleatorias con codificacion espacial
// TFG Alberto Hernandez
//
// Genera soluciones aleatorias que respetan TODAS las restricciones duras.
// Construccion dia a dia para los obligatorios (mas urgentes primero),
// seguido de opcionales y asignacion de enfermeras greedy.
//
// Fases:
//   Fase 1: Obligatorios dia a dia (mas urgentes primero)
//   Fase 2: Reparacion de obligatorios que no cupieron
//   Fase 3: Opcionales dia a dia
//   Fase 4: Enfermeras (greedy: minimiza skill violations, maximiza continuidad)

#ifndef SRC_SOLVER_RANDOM_GENERATOR_H_
#define SRC_SOLVER_RANDOM_GENERATOR_H_

#include <random>

#include "../entities/ProblemData.h"
#include "../evaluator/FeasibilityChecker.h"
#include "../solution/Solution.h"

/** @brief Generador de soluciones aleatorias factibles. */
class RandomGenerator {
 public:
  /** @brief Genera una solucion aleatoria factible (pacientes + enfermeras). */
  static Solution Generate(const ProblemData& problem, std::mt19937& rng);

  /** @brief Genera solo las asignaciones de pacientes. */
  static void GeneratePatientAssignments(Solution& solution,
                                         const ProblemData& problem,
                                         std::mt19937& rng);

  /** @brief Genera solo las asignaciones de enfermeras (greedy). */
  static void GenerateNurseAssignments(Solution& solution,
                                       const ProblemData& problem,
                                       std::mt19937& rng);

  /** @brief Garantiza que toda (room, day, shift) con pacientes/ocupantes tiene enfermera.
   *  Idempotente: respeta las asignaciones existentes y solo rellena donde falta cobertura.
   *  Necesario tras movimientos de LocalSearch que crean nuevas celdas (room, day) pobladas
   *  sin pasar por GenerateNurseAssignments.
   */
  static void EnsureFullNurseCoverage(Solution& solution,
                                      const ProblemData& problem,
                                      std::mt19937& rng);

  /** @brief Borra todas las asignaciones de enfermera y las regenera desde cero.
   *  Util cuando la matriz nurse acumulo decisiones suboptimas tras muchos movimientos VNS
   *  (continuidad rota, sobrecarga). El llamante evalua la mejora y revierte si empeora.
   */
  static void RegenerateNurses(Solution& solution,
                                const ProblemData& problem,
                                std::mt19937& rng);

  /** @brief Intenta asignar un paciente probando todos los dias de su ventana (sin forzar). */
  static bool TryAssignPatientFeasibly(Solution& solution, PatientId pid,
                                       const ProblemData& problem,
                                       std::mt19937& rng);

  /** @brief Fuerza la asignacion de un obligatorio desalojando bloqueantes si hace falta. */
  static bool ForceAssignMandatory(Solution& solution, PatientId pid,
                                    const ProblemData& problem,
                                    std::mt19937& rng);

 private:
  /** @brief Intenta asignar un paciente en un dia concreto, probando rooms y OTs. */
  static bool TryAssignOnDay(Solution& solution, Day day, PatientId pid,
                             const ProblemData& problem, std::mt19937& rng);

  /** @brief Devuelve los dias feasibles de la ventana de admision del paciente. */
  static std::vector<Day> GetFeasibleDays(const Patient& patient,
                                           const ProblemData& problem);
  /** @brief Devuelve las rooms compatibles con el paciente. */
  static std::vector<RoomId> GetCompatibleRooms(const Patient& patient,
                                                 const ProblemData& problem);
  /** @brief Devuelve las OTs abiertas en el dia. */
  static std::vector<OperatingTheaterId> GetOpenOTs(Day day,
                                                     const ProblemData& problem);
};

#endif  // SRC_SOLVER_RANDOM_GENERATOR_H_
