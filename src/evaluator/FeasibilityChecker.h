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
//   HC11: (desactivada en esta rama) una enfermera puede cubrir varias habitaciones
//   HC12: Carga cirujano <= max_surgery_time en cada dia
//   HC13: Carga quirofano <= availability en cada dia

#ifndef SRC_EVALUATOR_FEASIBILITY_CHECKER_H_
#define SRC_EVALUATOR_FEASIBILITY_CHECKER_H_

#include <string>
#include <vector>

#include "../common/types.h"
#include "../entities/ProblemData.h"
#include "../solution/Solution.h"

// Una violacion individual con descripcion legible
struct Violation {
  std::string constraint;  // ej. "HC5"
  std::string description; // ej. "Room R1 Day 3: occupancy 4 > capacity 2"

  Violation(std::string c, std::string d)
      : constraint(std::move(c)), description(std::move(d)) {}
};

// Resultado completo de la comprobacion de factibilidad
struct FeasibilityResult {
  bool feasible = true;
  std::vector<Violation> violations;

  void AddViolation(const std::string& constraint,
                    const std::string& description) {
    feasible = false;
    violations.emplace_back(constraint, description);
  }

  [[nodiscard]] std::string ToString() const;
  [[nodiscard]] int CountByConstraint(const std::string& constraint) const;
};

class FeasibilityChecker {
 public:
  // comprueba todas las restricciones duras
  static FeasibilityResult Check(const Solution& solution);

  // comprobacion rapida de una asignacion individual (patient-first, spatial)
  // el paciente NO debe estar asignado cuando se llama a esta funcion
  static bool IsFeasiblePatientAssignment(const Solution& solution,
                                          PatientId pid, RoomId room,
                                          Day admission_day,
                                          OperatingTheaterId ot);

  // comprueba si una enfermera puede asignarse a (room, day, shift)
  static bool IsFeasibleNurseAssignment(const Solution& solution,
                                        NurseId nurse_id, RoomId room_id,
                                        Day day, Shift shift);

 private:
  static void CheckMandatoryScheduled(const Solution& sol,
                                      const ProblemData& prob,
                                      FeasibilityResult& result);
  static void CheckAdmissionWindows(const Solution& sol,
                                    const ProblemData& prob,
                                    FeasibilityResult& result);
  static void CheckStayInHorizon(const Solution& sol, const ProblemData& prob,
                                 FeasibilityResult& result);
  static void CheckRoomCapacity(const Solution& sol, const ProblemData& prob,
                                FeasibilityResult& result);
  static void CheckGenderMixing(const Solution& sol, const ProblemData& prob,
                                FeasibilityResult& result);
  static void CheckRoomCompatibility(const Solution& sol,
                                     const ProblemData& prob,
                                     FeasibilityResult& result);
  static void CheckOTOpen(const Solution& sol, const ProblemData& prob,
                          FeasibilityResult& result);
  static void CheckNurseAvailability(const Solution& sol,
                                     const ProblemData& prob,
                                     FeasibilityResult& result);
  static void CheckSurgeonOvertime(const Solution& sol, const ProblemData& prob,
                                   FeasibilityResult& result);
  static void CheckOTOvertime(const Solution& sol, const ProblemData& prob,
                              FeasibilityResult& result);
};

#endif  // SRC_EVALUATOR_FEASIBILITY_CHECKER_H_
