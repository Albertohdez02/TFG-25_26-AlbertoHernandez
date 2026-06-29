// FeasibilityChecker.h - Verificador de restricciones duras IHTC 2024
// TFG Alberto Hernandez
//
// Comprueba que una solucion cumple TODAS las restricciones duras.
// Si alguna se viola, la solucion no es factible ni valida.
//
// Restricciones duras del IHTC 2024:
//   HC1: Todos los pacientes obligatorios programados
//   HC2: Dia de cirugia en [release_day, due_day] para obligatorios
//   HC3: Dia de cirugia >= release_day para opcionales (si programados)
//   HC4: La estancia cabe en el horizonte de planificacion
//   HC5: Ocupacion <= capacidad en cada (habitacion, dia)
//   HC6: Sin mezcla de generos en ninguna (habitacion, dia)
//   HC7: Paciente no asignado a habitacion incompatible
//   HC8: Quirofano abierto el dia de cirugia (availability > 0)
//   HC9: Maximo una enfermera por (habitacion, dia, turno) [implicito en Solution]
//   HC10: Enfermera disponible en el (dia, turno) asignado
//   HC11: Carga cirujano <= max_surgery_time en cada dia
//   HC12: Carga quirofano <= availability en cada dia
//   HC13: Toda (habitacion, dia, turno) con pacientes/ocupantes tiene enfermera
//         (analogo a UncoveredRoom del validador oficial IHTC)

#ifndef SRC_EVALUATOR_FEASIBILITY_CHECKER_H_
#define SRC_EVALUATOR_FEASIBILITY_CHECKER_H_

#include <string>
#include <vector>

#include "../common/types.h"
#include "../entities/ProblemData.h"
#include "../solution/Solution.h"

/** @brief Violacion individual de una restriccion dura con descripcion legible. */
struct Violation {
  std::string constraint;  // ej. "HC5"
  std::string description; // ej. "Room R1 Day 3: occupancy 4 > capacity 2"

  Violation(std::string c, std::string d)
      : constraint(std::move(c)), description(std::move(d)) {}
};

/** @brief Resultado completo de la comprobacion de factibilidad. */
struct FeasibilityResult {
  bool feasible = true;
  std::vector<Violation> violations;

  /** @brief Registra una violacion y marca la solucion como no feasible. */
  void AddViolation(const std::string& constraint,
                    const std::string& description) {
    feasible = false;
    violations.emplace_back(constraint, description);
  }

  /** @brief Devuelve un resumen legible de todas las violaciones. */
  [[nodiscard]] std::string ToString() const;

  /** @brief Cuenta las violaciones de una restriccion concreta. */
  [[nodiscard]] int CountByConstraint(const std::string& constraint) const;
};

/** @brief Verificador de las restricciones duras (HC) del IHTC 2024. */
class FeasibilityChecker {
 public:
  /** @brief Comprueba todas las restricciones duras de la solucion. */
  static FeasibilityResult Check(const Solution& solution);

  /** @brief Comprobacion rapida de una asignacion individual (patient-first, spatial).
   *  El paciente NO debe estar asignado cuando se llama a esta funcion.
   */
  static bool IsFeasiblePatientAssignment(const Solution& solution,
                                          PatientId pid, RoomId room,
                                          Day admission_day,
                                          OperatingTheaterId ot);

  /** @brief Comprueba si una enfermera puede asignarse a (room, day, shift). */
  static bool IsFeasibleNurseAssignment(const Solution& solution,
                                        NurseId nurse_id, RoomId room_id,
                                        Day day, Shift shift);

 private:
  /** @brief HC1: todos los pacientes obligatorios estan programados. */
  static void CheckMandatoryScheduled(const Solution& sol,
                                      const ProblemData& prob,
                                      FeasibilityResult& result);

  /** @brief HC2/HC3: dia de cirugia dentro de la ventana de admision. */
  static void CheckAdmissionWindows(const Solution& sol,
                                    const ProblemData& prob,
                                    FeasibilityResult& result);

  /** @brief HC4: la estancia cabe en el horizonte de planificacion. */
  static void CheckStayInHorizon(const Solution& sol, const ProblemData& prob,
                                 FeasibilityResult& result);

  /** @brief HC5: ocupacion <= capacidad en cada (habitacion, dia). */
  static void CheckRoomCapacity(const Solution& sol, const ProblemData& prob,
                                FeasibilityResult& result);

  /** @brief HC6: sin mezcla de generos en ninguna (habitacion, dia). */
  static void CheckGenderMixing(const Solution& sol, const ProblemData& prob,
                                FeasibilityResult& result);

  /** @brief HC7: paciente no asignado a habitacion incompatible. */
  static void CheckRoomCompatibility(const Solution& sol,
                                     const ProblemData& prob,
                                     FeasibilityResult& result);

  /** @brief HC8: quirofano abierto el dia de cirugia (availability > 0). */
  static void CheckOTOpen(const Solution& sol, const ProblemData& prob,
                          FeasibilityResult& result);

  /** @brief HC10: enfermera disponible en el (dia, turno) asignado. */
  static void CheckNurseAvailability(const Solution& sol,
                                     const ProblemData& prob,
                                     FeasibilityResult& result);

  /** @brief HC12: carga del cirujano <= max_surgery_time en cada dia. */
  static void CheckSurgeonOvertime(const Solution& sol, const ProblemData& prob,
                                   FeasibilityResult& result);

  /** @brief HC13: carga del quirofano <= availability en cada dia. */
  static void CheckOTOvertime(const Solution& sol, const ProblemData& prob,
                              FeasibilityResult& result);

  /** @brief HC14: toda (habitacion, dia, turno) con ocupantes tiene enfermera. */
  static void CheckRoomCoverage(const Solution& sol, const ProblemData& prob,
                                FeasibilityResult& result);
};

#endif  // SRC_EVALUATOR_FEASIBILITY_CHECKER_H_
