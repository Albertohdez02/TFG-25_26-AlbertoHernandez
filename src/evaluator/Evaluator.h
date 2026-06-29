// Evaluator.h - Funcion objetivo del IHTC 2024
// TFG Alberto Hernandez
//
// Calcula el coste total de una solucion sumando todas las penalizaciones
// por violaciones de restricciones blandas. Usa los caches de Solution
// para ser eficiente.

#ifndef SRC_EVALUATOR_EVALUATOR_H_
#define SRC_EVALUATOR_EVALUATOR_H_

#include <string>

#include "../common/types.h"
#include "../entities/ProblemData.h"
#include "../solution/Solution.h"

/** @brief Desglose del coste total por categoria de restriccion blanda. */
struct CostBreakdown {
  int room_capacity = 0;
  int room_gender_mix = 0;
  int room_mixed_age = 0;
  int patient_delay = 0;
  int unscheduled_optional = 0;
  int surgeon_overtime = 0;
  int ot_overtime = 0;
  int open_ot = 0;
  int nurse_skill = 0;
  int nurse_excessive_workload = 0;
  int continuity_of_care = 0;
  int surgeon_transfer = 0;

  /** @brief Suma todos los componentes de coste. */
  [[nodiscard]] int Total() const noexcept {
    return room_capacity + room_gender_mix + room_mixed_age +
           patient_delay + unscheduled_optional +
           surgeon_overtime + ot_overtime + open_ot +
           nurse_skill + nurse_excessive_workload +
           continuity_of_care + surgeon_transfer;
  }

  /** @brief Serializa el desglose a una cadena legible. */
  [[nodiscard]] std::string ToString() const;
};

/** @brief Evalua la funcion objetivo del IHTC 2024 sumando penalizaciones. */
class Evaluator {
 public:
  /** @brief Devuelve el coste total de la solucion. */
  static int Evaluate(const Solution& solution);

  /** @brief Devuelve el coste total desglosado por categoria. */
  static CostBreakdown EvaluateDetailed(const Solution& solution);

 private:
  /** @brief Coste por exceso de pacientes sobre la capacidad de la habitacion. */
  static int CalcRoomCapacityCost(const Solution& sol, const ProblemData& prob);

  /** @brief Coste por mezcla de generos en una misma habitacion. */
  static int CalcRoomGenderMixCost(const Solution& sol, const ProblemData& prob);

  /** @brief Coste por mezcla de grupos de edad en una misma habitacion. */
  static int CalcRoomMixedAgeCost(const Solution& sol, const ProblemData& prob);

  /** @brief Coste por retraso en el ingreso de cada paciente. */
  static int CalcPatientDelayCost(const Solution& sol, const ProblemData& prob);

  /** @brief Coste por pacientes opcionales no programados. */
  static int CalcUnscheduledOptionalCost(const Solution& sol, const ProblemData& prob);

  /** @brief Coste por horas extra de cada cirujano. */
  static int CalcSurgeonOvertimeCost(const Solution& sol, const ProblemData& prob);

  /** @brief Coste por horas extra de cada quirofano. */
  static int CalcOtOvertimeCost(const Solution& sol, const ProblemData& prob);

  /** @brief Coste por cada quirofano abierto en un dia. */
  static int CalcOpenOtCost(const Solution& sol, const ProblemData& prob);

  /** @brief Coste por enfermera con skill insuficiente para el paciente. */
  static int CalcNurseSkillCost(const Solution& sol, const ProblemData& prob);

  /** @brief Coste por exceso de carga de trabajo de cada enfermera. */
  static int CalcNurseExcessiveWorkloadCost(const Solution& sol, const ProblemData& prob);

  /** @brief Coste por ruptura de la continuidad de cuidados del paciente. */
  static int CalcContinuityOfCareCost(const Solution& sol, const ProblemData& prob);

  /** @brief Coste por traslado de pacientes de un cirujano entre quirofanos. */
  static int CalcSurgeonTransferCost(const Solution& sol, const ProblemData& prob);
};

#endif  // SRC_EVALUATOR_EVALUATOR_H_
