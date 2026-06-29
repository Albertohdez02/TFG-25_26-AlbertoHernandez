// FeasibilityChecker.cpp - Verificador de restricciones duras IHTC 2024
// TFG Alberto Hernandez

#include "FeasibilityChecker.h"

#include <sstream>
#include <unordered_map>
#include <unordered_set>


/** @brief Convierte el resultado de la verificacion a una cadena de texto.
 *  Muestra el conteo por restriccion y el detalle de las primeras 20 violaciones.
 */
std::string FeasibilityResult::ToString() const {
  std::ostringstream oss;
  if (feasible) {
    oss << "=== Solucion FACTIBLE (0 violaciones duras) ===\n";
  } else {
    oss << "=== Solucion INFACTIBLE (" << violations.size()
        << " violaciones duras) ===\n";

    std::unordered_map<std::string, int> counts;
    for (const auto& v : violations) {
      counts[v.constraint]++;
    }
    for (const auto& [constraint, count] : counts) {
      oss << "  " << constraint << ": " << count << " violaciones\n";
    }

    int shown = 0;
    oss << "\nDetalle (primeras 20):\n";
    for (const auto& v : violations) {
      oss << "  [" << v.constraint << "] " << v.description << "\n";
      if (++shown >= 20) {
        if (static_cast<int>(violations.size()) > 20) {
          oss << "  ... y " << (violations.size() - 20) << " mas\n";
        }
        break;
      }
    }
  }
  return oss.str();
}

/** @brief Cuenta las violaciones de una restriccion concreta (p.ej. "HC5"). */
int FeasibilityResult::CountByConstraint(
    const std::string& constraint) const {
  int count = 0;
  for (const auto& v : violations) {
    if (v.constraint == constraint) count++;
  }
  return count;
}


/** @brief Verifica una solucion completa contra todas las restricciones duras. */
FeasibilityResult FeasibilityChecker::Check(const Solution& solution) {
  FeasibilityResult result;
  const ProblemData& prob = solution.GetProblem();

  CheckMandatoryScheduled(solution, prob, result);
  CheckAdmissionWindows(solution, prob, result);
  CheckStayInHorizon(solution, prob, result);
  CheckRoomCapacity(solution, prob, result);
  CheckGenderMixing(solution, prob, result);
  CheckRoomCompatibility(solution, prob, result);
  CheckOTOpen(solution, prob, result);
  CheckNurseAvailability(solution, prob, result);
  CheckSurgeonOvertime(solution, prob, result);
  CheckOTOvertime(solution, prob, result);
  CheckRoomCoverage(solution, prob, result);

  return result;
}


/** @brief Comprobacion rapida de una asignacion individual de paciente (SPATIAL: patient-first).
 *  El paciente NO debe estar asignado (caller debe UnassignPatient antes).
 *  Verifica HC2/HC3, HC4, HC5, HC6, HC7, HC8, HC12 y HC13.
 */
bool FeasibilityChecker::IsFeasiblePatientAssignment(
    const Solution& solution, PatientId pid, RoomId room,
    Day admission_day, OperatingTheaterId ot) {
  const ProblemData& prob = solution.GetProblem();
  const Patient& patient = prob.GetPatient(pid);
  int num_days = prob.GetNumDays();
  int stay = patient.GetLengthOfStay();

  // HC2/HC3: ventana de admision
  if (admission_day < patient.GetSurgeryReleaseDay()) return false;
  if (patient.IsMandatory() && admission_day > patient.GetSurgeryDueDay())
    return false;

  // HC4: dia de admision dentro del horizonte
  if (admission_day >= num_days) return false;

  // HC7: habitacion compatible
  if (!patient.IsCompatibleWithRoom(room)) return false;

  // HC8: quirofano abierto
  if (!prob.GetOperatingTheater(ot).IsOpenOnDay(admission_day)) return false;

  // HC5: capacidad (comprobar cada dia de la estancia)
  for (int d = 0; d < stay; ++d) {
    Day day = admission_day + d;
    if (day >= num_days) break;
    int current_occ = solution.GetRoomOccupancy(room, day);
    if (current_occ >= prob.GetRoom(room).GetCapacity()) return false;
  }

  // HC6: genero (comprobar cada dia de la estancia)
  Gender patient_gender = patient.GetGender();
  for (int d = 0; d < stay; ++d) {
    Day day = admission_day + d;
    if (day >= num_days) break;
    Gender room_gender = solution.GetRoomGender(room, day);
    if (room_gender != kGenderAny && room_gender != patient_gender) {
      return false;
    }
  }

  // HC12: overtime del cirujano
  SurgeonId surgeon = patient.GetSurgeonId();
  int surgery_dur = patient.GetSurgeryDuration();
  int current_surgeon_load = solution.GetSurgeonLoad(surgeon, admission_day);
  if (current_surgeon_load + surgery_dur >
      prob.GetSurgeon(surgeon).GetMaxSurgeryTimeForDay(admission_day)) {
    return false;
  }

  // HC13: overtime del quirofano
  int current_ot_load = solution.GetOTLoad(ot, admission_day);
  if (current_ot_load + surgery_dur >
      prob.GetOperatingTheater(ot).GetAvailabilityForDay(admission_day)) {
    return false;
  }

  return true;
}

/** @brief Comprobacion rapida de una asignacion individual de enfermera.
 *  Verifica HC9 (una enfermera por room/dia/turno) y HC10 (disponibilidad).
 */
bool FeasibilityChecker::IsFeasibleNurseAssignment(
    const Solution& solution, NurseId nurse_id, RoomId room_id, Day day,
    Shift shift) {
  const ProblemData& prob = solution.GetProblem();

  // HC9: una habitacion tiene como maximo una enfermera por (dia, turno)
  NurseId assigned = solution.GetNurseAssignment(room_id, day, shift);
  if (assigned != kInvalidId && assigned != nurse_id) return false;

  // HC10: enfermera disponible
  if (!prob.GetNurse(nurse_id).IsAvailable(day, shift)) return false;

  return true;
}


/** @brief HC1: todos los pacientes obligatorios deben estar programados. */
void FeasibilityChecker::CheckMandatoryScheduled(const Solution& sol,
                                                 const ProblemData& prob,
                                                 FeasibilityResult& result) {
  for (PatientId pid : prob.GetMandatoryPatientIds()) {
    if (!sol.IsPatientScheduled(pid)) {
      result.AddViolation(
          "HC1", "Paciente obligatorio " + prob.GetPatient(pid).GetId() +
                     " no programado");
    }
  }
}

/** @brief HC2/HC3: admision dentro de la ventana [release_day, due_day]. */
void FeasibilityChecker::CheckAdmissionWindows(const Solution& sol,
                                               const ProblemData& prob,
                                               FeasibilityResult& result) {
  for (PatientId pid : sol.GetScheduledPatients()) {
    const Patient& patient = prob.GetPatient(pid);
    Day admission = sol.GetPatientAdmissionDay(pid);

    if (admission < patient.GetSurgeryReleaseDay()) {
      result.AddViolation(
          "HC3", patient.GetId() + ": admision dia " +
                     std::to_string(admission) + " < release_day " +
                     std::to_string(patient.GetSurgeryReleaseDay()));
    }

    if (patient.IsMandatory() && admission > patient.GetSurgeryDueDay()) {
      result.AddViolation(
          "HC2", patient.GetId() + ": admision dia " +
                     std::to_string(admission) + " > due_day " +
                     std::to_string(patient.GetSurgeryDueDay()));
    }
  }
}

/** @brief HC4: el dia de admision debe quedar dentro del horizonte. */
void FeasibilityChecker::CheckStayInHorizon(const Solution& sol,
                                            const ProblemData& prob,
                                            FeasibilityResult& result) {
  int num_days = prob.GetNumDays();
  for (PatientId pid : sol.GetScheduledPatients()) {
    Day admission = sol.GetPatientAdmissionDay(pid);
    if (admission >= num_days) {
      const Patient& patient = prob.GetPatient(pid);
      result.AddViolation("HC4",
                          patient.GetId() + ": admision dia " +
                              std::to_string(admission) + " >= horizonte " +
                              std::to_string(num_days));
    }
  }
}

/** @brief HC5: la ocupacion de cada habitacion no excede su capacidad. */
void FeasibilityChecker::CheckRoomCapacity(const Solution& sol,
                                           const ProblemData& prob,
                                           FeasibilityResult& result) {
  for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
    int cap = prob.GetRoom(r).GetCapacity();
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      int occ = sol.GetRoomOccupancy(r, d);
      if (occ > cap) {
        result.AddViolation(
            "HC5", prob.GetRoom(r).GetId() + " dia " + std::to_string(d) +
                       ": ocupacion " + std::to_string(occ) +
                       " > capacidad " + std::to_string(cap));
      }
    }
  }
}

/** @brief HC6: no se mezclan generos en una habitacion el mismo dia.
 *  El valor -2 de GetRoomGender marca el estado de mezcla.
 */
void FeasibilityChecker::CheckGenderMixing(const Solution& sol,
                                           const ProblemData& prob,
                                           FeasibilityResult& result) {
  for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      Gender g = sol.GetRoomGender(r, d);
      if (g == -2) {
        result.AddViolation("HC6", prob.GetRoom(r).GetId() + " dia " +
                                       std::to_string(d) +
                                       ": mezcla de generos");
      }
    }
  }
}

/** @brief HC7: cada paciente esta en una habitacion compatible. */
void FeasibilityChecker::CheckRoomCompatibility(const Solution& sol,
                                                const ProblemData& prob,
                                                FeasibilityResult& result) {
  for (PatientId pid : sol.GetScheduledPatients()) {
    const Patient& patient = prob.GetPatient(pid);
    RoomId room = sol.GetPatientRoom(pid);
    if (!patient.IsCompatibleWithRoom(room)) {
      result.AddViolation("HC7", patient.GetId() + " en habitacion " +
                                     prob.GetRoom(room).GetId() +
                                     " (incompatible)");
    }
  }
}

/** @brief HC8: el quirofano esta abierto el dia de la cirugia. */
void FeasibilityChecker::CheckOTOpen(const Solution& sol,
                                     const ProblemData& prob,
                                     FeasibilityResult& result) {
  for (PatientId pid : sol.GetScheduledPatients()) {
    OperatingTheaterId ot = sol.GetPatientOT(pid);
    Day admission = sol.GetPatientAdmissionDay(pid);
    if (!prob.GetOperatingTheater(ot).IsOpenOnDay(admission)) {
      result.AddViolation("HC8",
                          prob.GetPatient(pid).GetId() + " en " +
                              prob.GetOperatingTheater(ot).GetId() + " dia " +
                              std::to_string(admission) + " (cerrado)");
    }
  }
}

/** @brief HC10: las enfermeras asignadas estan disponibles en su dia/turno. */
void FeasibilityChecker::CheckNurseAvailability(const Solution& sol,
                                                const ProblemData& prob,
                                                FeasibilityResult& result) {
  int num_shifts = prob.GetNumShiftTypes();
  for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      for (Shift s = 0; s < num_shifts; ++s) {
        NurseId nurse = sol.GetNurseAssignment(r, d, s);
        if (nurse != kInvalidId) {
          if (!prob.GetNurse(nurse).IsAvailable(d, s)) {
            result.AddViolation(
                "HC10",
                prob.GetNurse(nurse).GetId() + " en " +
                    prob.GetRoom(r).GetId() + " dia " + std::to_string(d) +
                    " turno " + std::to_string(s) + " (no disponible)");
          }
        }
      }
    }
  }
}

/** @brief HC12: la carga del cirujano no supera max_surgery_time por dia. */
void FeasibilityChecker::CheckSurgeonOvertime(const Solution& sol,
                                              const ProblemData& prob,
                                              FeasibilityResult& result) {
  for (SurgeonId s = 0; s < prob.GetNumSurgeons(); ++s) {
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      int load = sol.GetSurgeonLoad(s, d);
      int max_time = prob.GetSurgeon(s).GetMaxSurgeryTimeForDay(d);
      if (load > max_time) {
        result.AddViolation(
            "HC12", prob.GetSurgeon(s).GetId() + " dia " + std::to_string(d) +
                        ": carga " + std::to_string(load) + " > max " +
                        std::to_string(max_time));
      }
    }
  }
}

/** @brief HC13: la carga del quirofano no supera su availability por dia. */
void FeasibilityChecker::CheckOTOvertime(const Solution& sol,
                                         const ProblemData& prob,
                                         FeasibilityResult& result) {
  for (OperatingTheaterId ot = 0; ot < prob.GetNumOperatingTheaters(); ++ot) {
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      int load = sol.GetOTLoad(ot, d);
      int avail = prob.GetOperatingTheater(ot).GetAvailabilityForDay(d);
      if (load > avail) {
        result.AddViolation(
            "HC13",
            prob.GetOperatingTheater(ot).GetId() + " dia " +
                std::to_string(d) + ": carga " + std::to_string(load) +
                " > disponibilidad " + std::to_string(avail));
      }
    }
  }
}

/** @brief HC14: toda habitacion ocupada tiene enfermera en cada turno.
 *  Toda (room, day, shift) con ocupantes debe tener una enfermera asignada.
 *  Equivale al UncoveredRoom del validador oficial IHTC.
 */
void FeasibilityChecker::CheckRoomCoverage(const Solution& sol,
                                           const ProblemData& prob,
                                           FeasibilityResult& result) {
  int num_shifts = prob.GetNumShiftTypes();
  for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
    for (Day d = 0; d < prob.GetNumDays(); ++d) {
      if (sol.GetRoomOccupancy(r, d) == 0) continue;
      for (Shift s = 0; s < num_shifts; ++s) {
        if (sol.GetNurseAssignment(r, d, s) == kInvalidId) {
          result.AddViolation(
              "HC14", prob.GetRoom(r).GetId() + " dia " + std::to_string(d) +
                          " turno " + std::to_string(s) +
                          ": habitacion sin enfermera (UncoveredRoom)");
        }
      }
    }
  }
}
