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

// Desglose de costes por categoria

/**
  @brief Estructura que almacena el desglose de costes por categoría.
 */
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

  [[nodiscard]] int Total() const noexcept {
    return room_capacity + room_gender_mix + room_mixed_age +
           patient_delay + unscheduled_optional +
           surgeon_overtime + ot_overtime + open_ot +
           nurse_skill + nurse_excessive_workload +
           continuity_of_care + surgeon_transfer;
  }

  [[nodiscard]] std::string ToString() const;
};

/**
  @brief Clase que evalúa la función objetivo del problema.
 */
class Evaluator {
 public:
  static int Evaluate(const Solution& solution);
  static CostBreakdown EvaluateDetailed(const Solution& solution);

 private:
  static int CalcRoomCapacityCost(const Solution& sol, const ProblemData& prob);
  static int CalcRoomGenderMixCost(const Solution& sol, const ProblemData& prob);
  static int CalcRoomMixedAgeCost(const Solution& sol, const ProblemData& prob);
  static int CalcPatientDelayCost(const Solution& sol, const ProblemData& prob);
  static int CalcUnscheduledOptionalCost(const Solution& sol, const ProblemData& prob);
  static int CalcSurgeonOvertimeCost(const Solution& sol, const ProblemData& prob);
  static int CalcOtOvertimeCost(const Solution& sol, const ProblemData& prob);
  static int CalcOpenOtCost(const Solution& sol, const ProblemData& prob);
  static int CalcNurseSkillCost(const Solution& sol, const ProblemData& prob);
  static int CalcNurseExcessiveWorkloadCost(const Solution& sol, const ProblemData& prob);
  static int CalcContinuityOfCareCost(const Solution& sol, const ProblemData& prob);
  static int CalcSurgeonTransferCost(const Solution& sol, const ProblemData& prob);
};

#endif  // SRC_EVALUATOR_EVALUATOR_H_
