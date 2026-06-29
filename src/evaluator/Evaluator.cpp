// Evaluator.cpp - Implementacion de la funcion objetivo IHTC 2024
// TFG Alberto Hernandez
//
// Cada metodo calcula un componente de la funcion objetivo.
// Los caches de Solution evitan recalcular cosas caras.

#include "Evaluator.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

/** @brief Convierte el desglose de costes a una cadena de texto legible. */
std::string CostBreakdown::ToString() const {
  std::ostringstream oss;
  oss << "- Desglose de costes -\n";
  oss << "  Capacidad habitacion:    " << room_capacity << "\n";
  oss << "  Mezcla genero:           " << room_gender_mix << "\n";
  oss << "  Mezcla edad:             " << room_mixed_age << "\n";
  oss << "  Retraso pacientes:       " << patient_delay << "\n";
  oss << "  Opcionales no prog.:     " << unscheduled_optional << "\n";
  oss << "  Horas extra cirujano:    " << surgeon_overtime << "\n";
  oss << "  Horas extra quirofano:   " << ot_overtime << "\n";
  oss << "  Quirofanos abiertos:     " << open_ot << "\n";
  oss << "  Skill enfermera:         " << nurse_skill << "\n";
  oss << "  Sobrecarga enfermera:    " << nurse_excessive_workload << "\n";
  oss << "  Continuidad de cuidado:  " << continuity_of_care << "\n";
  oss << "  Transferencia cirujano:  " << surgeon_transfer << "\n";
  oss << "  -------------------------\n";
  oss << "  TOTAL:                   " << Total() << "\n";
  return oss.str();
}

/** @brief Evalua la funcion objetivo y devuelve el coste total. */
int Evaluator::Evaluate(const Solution& solution) {
  return EvaluateDetailed(solution).Total();
}

/** @brief Evalua cada componente de la funcion objetivo por separado.
 *  @return CostBreakdown con el coste de cada componente y el total.
 */
CostBreakdown Evaluator::EvaluateDetailed(const Solution& solution) {
  const ProblemData& prob = solution.GetProblem();
  CostBreakdown bd;

  bd.room_capacity = CalcRoomCapacityCost(solution, prob);
  bd.room_gender_mix = CalcRoomGenderMixCost(solution, prob);
  bd.room_mixed_age = CalcRoomMixedAgeCost(solution, prob);
  bd.patient_delay = CalcPatientDelayCost(solution, prob);
  bd.unscheduled_optional = CalcUnscheduledOptionalCost(solution, prob);
  bd.surgeon_overtime = CalcSurgeonOvertimeCost(solution, prob);
  bd.ot_overtime = CalcOtOvertimeCost(solution, prob);
  bd.open_ot = CalcOpenOtCost(solution, prob);
  bd.nurse_skill = CalcNurseSkillCost(solution, prob);
  bd.nurse_excessive_workload = CalcNurseExcessiveWorkloadCost(solution, prob);
  bd.continuity_of_care = CalcContinuityOfCareCost(solution, prob);
  bd.surgeon_transfer = CalcSurgeonTransferCost(solution, prob);

  return bd;
}

/** @brief Calcula el coste por violacion de la capacidad de las habitaciones (HC7). */
int Evaluator::CalcRoomCapacityCost(const Solution& sol,
                                    const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().room_capacity_violation;
  for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
    int cap = prob.GetRoom(r).GetCapacity();
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      int occ = sol.GetRoomOccupancy(r, d);
      if (occ > cap) {
        cost += (occ - cap) * weight;
      }
    }
  }
  return cost;
}

/** @brief Calcula el coste por violacion de la mezcla de genero en las habitaciones.
 *  Penaliza cada (habitacion, dia) marcado con genero mixto (g == -2).
 */
int Evaluator::CalcRoomGenderMixCost(const Solution& sol,
                                     const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().room_gender_mix;
  for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      Gender g = sol.GetRoomGender(r, d);
      if (g == -2) {
        cost += weight;
      }
    }
  }
  return cost;
}

/** @brief Calcula el coste por mezcla de grupos de edad en las habitaciones.
 *  Penaliza (numero de grupos de edad distintos - 1) por (habitacion, dia),
 *  contando tanto ocupantes previos como pacientes asignados.
 */
int Evaluator::CalcRoomMixedAgeCost(const Solution& sol,
                                    const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().room_mixed_age;
  if (weight == 0) return 0;

  for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      std::unordered_set<AgeGroup> age_groups;

      for (const auto& occ : prob.GetOccupants()) {
        if (occ.GetRoomId() == r && occ.IsPresentOnDay(d)) {
          age_groups.insert(occ.GetAgeGroup());
        }
      }

      const auto& patients = sol.GetRoomPatients(r, d);
      for (PatientId pid : patients) {
        age_groups.insert(prob.GetPatient(pid).GetAgeGroup());
      }

      if (age_groups.size() > 1) {
        cost += static_cast<int>(age_groups.size() - 1) * weight;
      }
    }
  }
  return cost;
}

/** @brief Calcula el coste por retraso en la admision de los pacientes programados. */
int Evaluator::CalcPatientDelayCost(const Solution& sol,
                                    const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().patient_delay;
  for (PatientId p = 0; p < prob.GetNumPatients(); ++p) {
    if (sol.IsPatientScheduled(p)) {
      Day admission = sol.GetPatientAdmissionDay(p);
      int delay = prob.GetPatient(p).GetDelayDays(admission);
      cost += delay * weight;
    }
  }
  return cost;
}

/** @brief Calcula el coste por pacientes opcionales no programados. */
int Evaluator::CalcUnscheduledOptionalCost(const Solution& sol,
                                           const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().unscheduled_optional;
  for (PatientId p : prob.GetOptionalPatientIds()) {
    if (!sol.IsPatientScheduled(p)) {
      cost += weight;
    }
  }
  return cost;
}

/** @brief Calcula el coste por horas extra de los cirujanos.
 *  Penaliza la carga diaria que excede el tiempo maximo de cirugia del cirujano.
 */
int Evaluator::CalcSurgeonOvertimeCost(const Solution& sol,
                                       const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().surgeon_overtime;
  for (SurgeonId s = 0; s < prob.GetNumSurgeons(); ++s) {
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      int load = sol.GetSurgeonLoad(s, d);
      int max_time = prob.GetSurgeon(s).GetMaxSurgeryTimeForDay(d);
      if (load > max_time) {
        cost += (load - max_time) * weight;
      }
    }
  }
  return cost;
}

/** @brief Calcula el coste por horas extra de los quirofanos.
 *  Penaliza la carga diaria que excede la disponibilidad del quirofano.
 */
int Evaluator::CalcOtOvertimeCost(const Solution& sol,
                                  const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().operating_theater_overtime;
  for (OperatingTheaterId ot = 0; ot < prob.GetNumOperatingTheaters(); ++ot) {
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      int load = sol.GetOTLoad(ot, d);
      int avail = prob.GetOperatingTheater(ot).GetAvailabilityForDay(d);
      if (load > avail) {
        cost += (load - avail) * weight;
      }
    }
  }
  return cost;
}

/** @brief Calcula el coste por quirofanos abiertos (con carga > 0). */
int Evaluator::CalcOpenOtCost(const Solution& sol, const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().open_operating_theater;
  if (weight == 0) return 0;

  for (OperatingTheaterId ot = 0; ot < prob.GetNumOperatingTheaters(); ++ot) {
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      if (sol.GetOTLoad(ot, d) > 0) {
        cost += weight;
      }
    }
  }
  return cost;
}

/** @brief Calcula el coste por falta de skill de las enfermeras frente a lo requerido.
 *  Por cada (habitacion, dia, turno) con enfermera asignada, penaliza el deficit
 *  de skill respecto al nivel requerido por cada paciente y ocupante presente.
 */
int Evaluator::CalcNurseSkillCost(const Solution& sol,
                                  const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().room_nurse_skill;
  if (weight == 0) return 0;

  int num_shifts = prob.GetNumShiftTypes();

  for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      for (Shift s = 0; s < num_shifts; ++s) {
        NurseId nurse_id = sol.GetNurseAssignment(r, d, s);
        if (nurse_id == kInvalidId) continue;

        SkillLevel nurse_skill = prob.GetNurse(nurse_id).GetSkillLevel();

        const auto& patients = sol.GetRoomPatients(r, d);
        for (PatientId pid : patients) {
          Day admission = sol.GetPatientAdmissionDay(pid);
          int day_in_stay = d - admission;
          SkillLevel required =
              prob.GetPatient(pid).GetSkillLevelAt(day_in_stay, s);
          if (nurse_skill < required) {
            cost += (required - nurse_skill) * weight;
          }
        }

        for (const auto& occ : prob.GetOccupants()) {
          if (occ.GetRoomId() == r && occ.IsPresentOnDay(d)) {
            SkillLevel required = occ.GetSkillLevelAt(d, s);
            if (nurse_skill < required) {
              cost += (required - nurse_skill) * weight;
            }
          }
        }
      }
    }
  }
  return cost;
}

/** @brief Calcula el coste por workload excesivo de las enfermeras.
 *  Penaliza la carga de cada turno trabajado que excede su max_load.
 */
int Evaluator::CalcNurseExcessiveWorkloadCost(const Solution& sol,
                                              const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().nurse_excessive_workload;
  if (weight == 0) return 0;

  for (NurseId n = 0; n < prob.GetNumNurses(); ++n) {
    const Nurse& nurse = prob.GetNurse(n);
    for (const auto& ws : nurse.GetWorkingShifts()) {
      int workload = sol.GetNurseWorkload(n, ws.day, ws.shift_index);
      if (workload > ws.max_load) {
        cost += (workload - ws.max_load) * weight;
      }
    }
  }
  return cost;
}

/** @brief Calcula el coste por falta de continuidad de cuidado.
 *  Por paciente y turno, penaliza (numero de enfermeras distintas - 1) que lo
 *  atienden a lo largo de su estancia.
 */
int Evaluator::CalcContinuityOfCareCost(const Solution& sol,
                                        const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().continuity_of_care;
  if (weight == 0) return 0;

  int num_shifts = prob.GetNumShiftTypes();

  for (PatientId p : sol.GetScheduledPatients()) {
    const Patient& patient = prob.GetPatient(p);
    RoomId room = sol.GetPatientRoom(p);
    Day admission = sol.GetPatientAdmissionDay(p);
    int stay = patient.GetLengthOfStay();

    for (Shift s = 0; s < num_shifts; ++s) {
      std::unordered_set<NurseId> nurses_seen;
      for (int d_offset = 0; d_offset < stay; ++d_offset) {
        Day day = admission + d_offset;
        if (day >= prob.GetNumDays()) break;
        NurseId nurse = sol.GetNurseAssignment(room, day, s);
        if (nurse != kInvalidId) {
          nurses_seen.insert(nurse);
        }
      }
      if (nurses_seen.size() > 1) {
        cost += static_cast<int>(nurses_seen.size() - 1) * weight;
      }
    }
  }
  return cost;
}

/** @brief Calcula el coste por transferencia de cirujanos entre quirofanos.
 *  Por (cirujano, dia), penaliza (numero de quirofanos distintos usados - 1).
 */
int Evaluator::CalcSurgeonTransferCost(const Solution& sol,
                                       const ProblemData& prob) {
  int cost = 0;
  int weight = prob.GetWeights().surgeon_transfer;
  if (weight == 0) return 0;

  int num_days = prob.GetNumDays();
  int num_surgeons = prob.GetNumSurgeons();

  std::vector<std::unordered_set<OperatingTheaterId>> surgeon_day_ots(
      num_surgeons * num_days);

  for (PatientId p : sol.GetScheduledPatients()) {
    const Patient& patient = prob.GetPatient(p);
    SurgeonId surgeon = patient.GetSurgeonId();
    Day admission = sol.GetPatientAdmissionDay(p);
    OperatingTheaterId ot = sol.GetPatientOT(p);

    int idx = surgeon * num_days + admission;
    surgeon_day_ots[idx].insert(ot);
  }

  for (int i = 0; i < num_surgeons * num_days; ++i) {
    if (surgeon_day_ots[i].size() > 1) {
      cost += static_cast<int>(surgeon_day_ots[i].size() - 1) * weight;
    }
  }

  return cost;
}
