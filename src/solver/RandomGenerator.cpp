// RandomGenerator.cpp - Generador de soluciones con codificacion espacial
// TFG Alberto Hernandez
//
// Construccion dia a dia para obligatorios (mas urgentes primero),
// con reparacion forzada para los que no caben. Los opcionales se
// intentan con probabilidad 0.7. Las enfermeras se asignan con heuristica
// greedy que minimiza violaciones de skill y maximiza continuidad.

#include "RandomGenerator.h"

#include <algorithm>
#include <limits>
#include <vector>

/** @brief Genera una solucion aleatoria factible. */
Solution RandomGenerator::Generate(const ProblemData& problem,
                                   std::mt19937& rng) {
  Solution solution(problem);

  GeneratePatientAssignments(solution, problem, rng);
  GenerateNurseAssignments(solution, problem, rng);

  return solution;
}


/** @brief Genera las asignaciones de pacientes.
 *  Construccion dia a dia para obligatorios (urgentes primero), reparacion
 *  forzada de los no asignados y opcionales con probabilidad 0.7.
 */
void RandomGenerator::GeneratePatientAssignments(Solution& solution,
                                                 const ProblemData& problem,
                                                 std::mt19937& rng) {
  int num_days = problem.GetNumDays();
  auto mandatory_ids = problem.GetMandatoryPatientIds();

  // Fase 1: obligatorios, dia a dia (urgentes primero)
  for (Day d = 0; d < num_days; ++d) {
    std::vector<PatientId> eligible;
    for (PatientId pid : mandatory_ids) {
      if (solution.IsPatientScheduled(pid)) continue;
      const Patient& p = problem.GetPatient(pid);
      if (d >= p.GetSurgeryReleaseDay() && d <= p.GetSurgeryDueDay()) {
        eligible.push_back(pid);
      }
    }

    // ordenar por urgencia: due_day - d ascendente (mas urgentes primero)
    std::sort(eligible.begin(), eligible.end(),
              [&problem, d](PatientId a, PatientId b) {
                int remaining_a =
                    problem.GetPatient(a).GetSurgeryDueDay() - d;
                int remaining_b =
                    problem.GetPatient(b).GetSurgeryDueDay() - d;
                return remaining_a < remaining_b;
              });

    for (PatientId pid : eligible) {
      if (solution.IsPatientScheduled(pid)) continue;
      TryAssignOnDay(solution, d, pid, problem, rng);
    }
  }

  // Fase 2: reparacion de obligatorios no asignados
  for (PatientId pid : mandatory_ids) {
    if (!solution.IsPatientScheduled(pid)) {
      if (!TryAssignPatientFeasibly(solution, pid, problem, rng)) {
        ForceAssignMandatory(solution, pid, problem, rng);
      }
    }
  }

  // Fase 3: opcionales, dia a dia
  auto optional_ids = problem.GetOptionalPatientIds();
  std::shuffle(optional_ids.begin(), optional_ids.end(), rng);

  std::uniform_real_distribution<double> prob_dist(0.0, 1.0);

  for (Day d = 0; d < num_days; ++d) {
    for (PatientId pid : optional_ids) {
      if (solution.IsPatientScheduled(pid)) continue;
      const Patient& p = problem.GetPatient(pid);
      if (d < p.GetSurgeryReleaseDay()) continue;

      if (prob_dist(rng) < 0.7) {
        TryAssignOnDay(solution, d, pid, problem, rng);
      }
    }
  }
}


/** @brief Regenera la matriz de enfermeras desde cero.
 *  Util cuando la matriz acumulo decisiones suboptimas tras muchos movimientos
 *  VNS. Borra todas las (room, day, shift) y vuelve a llamar a la greedy.
 *  El llamante decide si aceptar o revertir el resultado.
 */
void RandomGenerator::RegenerateNurses(Solution& solution,
                                       const ProblemData& problem,
                                       std::mt19937& rng) {
  int num_shifts = problem.GetNumShiftTypes();
  for (RoomId r = 0; r < problem.GetNumRooms(); ++r) {
    for (Day d = 0; d < problem.GetNumDays(); ++d) {
      for (Shift s = 0; s < num_shifts; ++s) {
        if (solution.GetNurseAssignment(r, d, s) != kInvalidId) {
          solution.UnassignNurse(r, d, s);
        }
      }
    }
  }
  GenerateNurseAssignments(solution, problem, rng);
}


/** @brief Genera las asignaciones de enfermeras con heuristica greedy.
 *  Para cada (room, day, shift) ocupado elige la candidata feasible con menor
 *  puntuacion: penaliza skill insuficiente y sobrecarga, bonifica continuidad.
 */
void RandomGenerator::GenerateNurseAssignments(Solution& solution,
                                               const ProblemData& problem,
                                               std::mt19937& rng) {
  int num_shifts = problem.GetNumShiftTypes();

  for (RoomId r = 0; r < problem.GetNumRooms(); ++r) {
    for (Day d = 0; d < problem.GetNumDays(); ++d) {
      if (solution.GetRoomOccupancy(r, d) == 0) continue;

      for (Shift s = 0; s < num_shifts; ++s) {
        std::vector<NurseId> candidates;
        for (NurseId n = 0; n < problem.GetNumNurses(); ++n) {
          if (FeasibilityChecker::IsFeasibleNurseAssignment(
                  solution, n, r, d, s)) {
            candidates.push_back(n);
          }
        }

        if (candidates.empty()) continue;

        NurseId best_nurse = candidates[0];
        int best_score = std::numeric_limits<int>::max();

        // enfermera del dia anterior en este turno (para continuidad)
        NurseId prev_nurse = kInvalidId;
        if (d > 0) {
          prev_nurse = solution.GetNurseAssignment(r, d - 1, s);
        }

        const auto& room_patients = solution.GetRoomPatients(r, d);

        for (NurseId n : candidates) {
          int score = 0;
          SkillLevel nurse_skill = problem.GetNurse(n).GetSkillLevel();

          // penalizar skill insuficiente
          for (PatientId pid : room_patients) {
            Day adm = solution.GetPatientAdmissionDay(pid);
            int day_in_stay = d - adm;
            SkillLevel req =
                problem.GetPatient(pid).GetSkillLevelAt(day_in_stay, s);
            if (nurse_skill < req) {
              score += 100;
            }
          }
          for (const auto& occ : problem.GetOccupants()) {
            if (occ.GetRoomId() == r && occ.IsPresentOnDay(d)) {
              SkillLevel req = occ.GetSkillLevelAt(d, s);
              if (nurse_skill < req) {
                score += 100;
              }
            }
          }

          // penalizar sobrecarga
          int max_load = problem.GetNurse(n).GetMaxWorkload(d, s);
          int current_workload = solution.GetNurseWorkload(n, d, s);
          int added_workload = 0;
          for (PatientId pid : room_patients) {
            Day adm = solution.GetPatientAdmissionDay(pid);
            int day_in_stay = d - adm;
            added_workload +=
                problem.GetPatient(pid).GetWorkloadAt(day_in_stay, s);
          }
          for (const auto& occ : problem.GetOccupants()) {
            if (occ.GetRoomId() == r && occ.IsPresentOnDay(d)) {
              added_workload += occ.GetWorkloadAt(d, s);
            }
          }
          if (current_workload + added_workload > max_load) {
            score += (current_workload + added_workload - max_load) * 10;
          }

          // bonificar continuidad de cuidado
          if (n == prev_nurse && prev_nurse != kInvalidId) {
            score -= 50;
          }

          if (score < best_score) {
            best_score = score;
            best_nurse = n;
          }
        }

        solution.AssignNurse(best_nurse, r, d, s);
      }
    }
  }
}


/** @brief Garantiza que toda (room, day, shift) con pacientes u ocupantes tiene enfermera.
 *  Idempotente: si ya hay una la deja intacta, solo rellena celdas vacias.
 *  Reutiliza la misma puntuacion que GenerateNurseAssignments para mantener
 *  consistencia (penaliza skill insuficiente y sobrecarga, bonifica
 *  continuidad con el dia anterior).
 */
void RandomGenerator::EnsureFullNurseCoverage(Solution& solution,
                                              const ProblemData& problem,
                                              std::mt19937& rng) {
  int num_shifts = problem.GetNumShiftTypes();

  for (RoomId r = 0; r < problem.GetNumRooms(); ++r) {
    for (Day d = 0; d < problem.GetNumDays(); ++d) {
      // sin pacientes ni ocupantes: nada que cubrir
      if (solution.GetRoomOccupancy(r, d) == 0) continue;

      for (Shift s = 0; s < num_shifts; ++s) {
        // ya hay enfermera, no tocar
        if (solution.GetNurseAssignment(r, d, s) != kInvalidId) continue;

        std::vector<NurseId> candidates;
        for (NurseId n = 0; n < problem.GetNumNurses(); ++n) {
          if (FeasibilityChecker::IsFeasibleNurseAssignment(solution, n, r, d,
                                                            s)) {
            candidates.push_back(n);
          }
        }
        if (candidates.empty()) continue;  // no hay nurses trabajando ese turno

        NurseId prev_nurse = kInvalidId;
        if (d > 0) prev_nurse = solution.GetNurseAssignment(r, d - 1, s);

        const auto& room_patients = solution.GetRoomPatients(r, d);

        NurseId best_nurse = candidates[0];
        int best_score = std::numeric_limits<int>::max();

        for (NurseId n : candidates) {
          int score = 0;
          SkillLevel nurse_skill = problem.GetNurse(n).GetSkillLevel();

          for (PatientId pid : room_patients) {
            Day adm = solution.GetPatientAdmissionDay(pid);
            int day_in_stay = d - adm;
            SkillLevel req =
                problem.GetPatient(pid).GetSkillLevelAt(day_in_stay, s);
            if (nurse_skill < req) score += 100;
          }
          for (const auto& occ : problem.GetOccupants()) {
            if (occ.GetRoomId() == r && occ.IsPresentOnDay(d)) {
              SkillLevel req = occ.GetSkillLevelAt(d, s);
              if (nurse_skill < req) score += 100;
            }
          }

          int max_load = problem.GetNurse(n).GetMaxWorkload(d, s);
          int current_workload = solution.GetNurseWorkload(n, d, s);
          int added_workload = 0;
          for (PatientId pid : room_patients) {
            Day adm = solution.GetPatientAdmissionDay(pid);
            int day_in_stay = d - adm;
            added_workload +=
                problem.GetPatient(pid).GetWorkloadAt(day_in_stay, s);
          }
          for (const auto& occ : problem.GetOccupants()) {
            if (occ.GetRoomId() == r && occ.IsPresentOnDay(d)) {
              added_workload += occ.GetWorkloadAt(d, s);
            }
          }
          if (current_workload + added_workload > max_load) {
            score += (current_workload + added_workload - max_load) * 10;
          }

          if (n == prev_nurse && prev_nurse != kInvalidId) score -= 50;

          if (score < best_score) {
            best_score = score;
            best_nurse = n;
          }
        }

        solution.AssignNurse(best_nurse, r, d, s);
      }
    }
  }
  (void)rng;  // determinista: el mejor score gana, sin desempate aleatorio
}


/** @brief Intenta asignar un paciente en un dia concreto.
 *  @return true si la asignacion tuvo exito, false en caso contrario.
 */
bool RandomGenerator::TryAssignOnDay(Solution& solution, Day day,
                                     PatientId pid,
                                     const ProblemData& problem,
                                     std::mt19937& rng) {
  const Patient& patient = problem.GetPatient(pid);

  auto rooms = GetCompatibleRooms(patient, problem);
  auto ots = GetOpenOTs(day, problem);

  if (rooms.empty() || ots.empty()) return false;

  // Heuristica informada (Fase A): se ordenan habitaciones por una clave
  // que aproxima los costes blandos `room_gender_mix` y `room_mixed_age`,
  // y a igualdad se prefieren las mas llenas (concentrar pacientes evita
  // abrir habitaciones extra y mejora continuity_of_care). Solo se baraja
  // el top-3 para mantener diversidad entre llamadas a Generate.
  Gender pgender = patient.GetGender();
  AgeGroup pagegroup = patient.GetAgeGroup();
  int stay = patient.GetLengthOfStay();
  int num_days = problem.GetNumDays();
  Day stay_end = std::min<Day>(day + stay - 1, num_days - 1);

  struct RoomScore {
    RoomId room;
    int gender_pen;
    int age_pen;
    int neg_occupancy;
  };
  std::vector<RoomScore> scored;
  scored.reserve(rooms.size());
  for (RoomId r : rooms) {
    int gender_pen = 0;
    int age_pen = 0;
    int occupancy = 0;
    for (Day cd = day; cd <= stay_end; ++cd) {
      Gender rg = solution.GetRoomGender(r, cd);
      // rg == -2 mezcla ya existente, == kGenderAny vacio,
      // valor concreto: genero homogeneo
      if (rg == -2) {
        gender_pen += 2;  // ya hay mezcla, peor opcion
      } else if (rg != kGenderAny && pgender != kGenderAny &&
                 rg != pgender) {
        gender_pen += 1;  // este paciente introduciria mezcla
      }
      // edad: contar grupos distintos al del paciente
      // (espejo de Evaluator::room_mixed_age)
      const auto& room_pats = solution.GetRoomPatients(r, cd);
      for (PatientId rp : room_pats) {
        if (problem.GetPatient(rp).GetAgeGroup() != pagegroup) {
          age_pen += 1;
        }
      }
      for (const auto& occ : problem.GetOccupants()) {
        if (occ.GetRoomId() == r && occ.IsPresentOnDay(cd)) {
          if (occ.GetAgeGroup() != pagegroup) age_pen += 1;
        }
      }
      occupancy += solution.GetRoomOccupancy(r, cd);
    }
    scored.push_back({r, gender_pen, age_pen, -occupancy});
  }

  std::sort(scored.begin(), scored.end(),
            [](const RoomScore& a, const RoomScore& b) {
              if (a.gender_pen != b.gender_pen)
                return a.gender_pen < b.gender_pen;
              if (a.age_pen != b.age_pen) return a.age_pen < b.age_pen;
              return a.neg_occupancy < b.neg_occupancy;  // mas llena primero
            });

  // Barajar solo el top-3 para mantener algo de diversidad
  std::size_t top_k = std::min<std::size_t>(3, scored.size());
  std::shuffle(scored.begin(), scored.begin() + top_k, rng);

  rooms.clear();
  rooms.reserve(scored.size());
  for (const auto& s : scored) rooms.push_back(s.room);

  // OTs: ordenar por carga descendente (concentrar cirugias minimiza
  // `open_ot`). Sin shuffle: el espacio es muy pequeno (2-5 OTs).
  std::sort(ots.begin(), ots.end(),
            [&solution, day](OperatingTheaterId a, OperatingTheaterId b) {
              return solution.GetOTLoad(a, day) >
                     solution.GetOTLoad(b, day);
            });

  for (RoomId room : rooms) {
    for (OperatingTheaterId ot : ots) {
      if (FeasibilityChecker::IsFeasiblePatientAssignment(
              solution, pid, room, day, ot)) {
        solution.AssignPatient(pid, room, day, ot);
        return true;
      }
    }
  }

  return false;
}


/** @brief Intenta asignar un paciente probando todos los dias de su ventana.
 *  @return true si la asignacion tuvo exito, false en caso contrario.
 */
bool RandomGenerator::TryAssignPatientFeasibly(Solution& solution,
                                               PatientId pid,
                                               const ProblemData& problem,
                                               std::mt19937& rng) {
  auto days = GetFeasibleDays(problem.GetPatient(pid), problem);
  if (days.empty()) return false;

  std::shuffle(days.begin(), days.end(), rng);

  for (Day day : days) {
    if (TryAssignOnDay(solution, day, pid, problem, rng)) {
      return true;
    }
  }

  return false;
}


/** @brief Fuerza la asignacion de un obligatorio desalojando a sus bloqueantes.
 *  Recoge bloqueantes por habitacion, cirujano y OT, los desaloja uno a uno e
 *  intenta reubicarlos. Si algun obligatorio desalojado no se recoloca, deshace
 *  todo y revierte al estado previo.
 *  @return true si la asignacion tuvo exito, false en caso contrario.
 */
bool RandomGenerator::ForceAssignMandatory(Solution& solution, PatientId pid,
                                            const ProblemData& problem,
                                            std::mt19937& rng) {
  const Patient& patient = problem.GetPatient(pid);
  auto days = GetFeasibleDays(patient, problem);
  auto rooms = GetCompatibleRooms(patient, problem);
  if (days.empty() || rooms.empty()) return false;

  int stay = patient.GetLengthOfStay();
  SurgeonId surgeon = patient.GetSurgeonId();

  std::shuffle(days.begin(), days.end(), rng);
  std::shuffle(rooms.begin(), rooms.end(), rng);

  for (Day day : days) {
    auto ots = GetOpenOTs(day, problem);
    if (ots.empty()) continue;
    std::shuffle(ots.begin(), ots.end(), rng);

    for (RoomId room : rooms) {
      for (OperatingTheaterId ot : ots) {
        // recoger bloqueantes de esta posicion
        std::vector<PatientId> blockers;

        // bloqueantes por habitacion (capacidad/genero)
        for (int d = 0; d < stay; ++d) {
          Day check_day = day + d;
          if (check_day >= problem.GetNumDays()) break;
          const auto& room_pats = solution.GetRoomPatients(room, check_day);
          for (PatientId rp : room_pats) {
            if (rp != pid &&
                std::find(blockers.begin(), blockers.end(), rp) ==
                    blockers.end()) {
              blockers.push_back(rp);
            }
          }
        }

        // bloqueantes por cirujano el mismo dia
        for (PatientId sp : solution.GetScheduledPatients()) {
          if (sp != pid &&
              problem.GetPatient(sp).GetSurgeonId() == surgeon &&
              solution.GetPatientAdmissionDay(sp) == day &&
              std::find(blockers.begin(), blockers.end(), sp) ==
                  blockers.end()) {
            blockers.push_back(sp);
          }
        }

        // bloqueantes por OT el mismo dia
        for (PatientId op : solution.GetScheduledPatients()) {
          if (op != pid && solution.GetPatientOT(op) == ot &&
              solution.GetPatientAdmissionDay(op) == day &&
              std::find(blockers.begin(), blockers.end(), op) ==
                  blockers.end()) {
            blockers.push_back(op);
          }
        }

        if (blockers.empty()) continue;

        struct SavedInfo {
          PatientId pid;
          Day day;
          RoomId room;
          OperatingTheaterId ot;
        };
        std::vector<SavedInfo> evicted;
        std::shuffle(blockers.begin(), blockers.end(), rng);

        for (PatientId blocker : blockers) {
          if (!solution.IsPatientScheduled(blocker)) continue;

          SavedInfo info{blocker, solution.GetPatientAdmissionDay(blocker),
                         solution.GetPatientRoom(blocker),
                         solution.GetPatientOT(blocker)};
          solution.UnassignPatient(blocker);
          evicted.push_back(info);

          if (FeasibilityChecker::IsFeasiblePatientAssignment(
                  solution, pid, room, day, ot)) {
            solution.AssignPatient(pid, room, day, ot);

            // intentar reubicar los desalojados
            bool mandatory_ok = true;
            for (auto& ev : evicted) {
              if (!TryAssignPatientFeasibly(solution, ev.pid, problem, rng)) {
                if (problem.GetPatient(ev.pid).IsMandatory()) {
                  mandatory_ok = false;
                  break;
                }
              }
            }

            if (mandatory_ok) return true;

            // no funciono, deshacer todo
            solution.UnassignPatient(pid);
            for (auto& ev : evicted) {
              if (solution.IsPatientScheduled(ev.pid)) {
                solution.UnassignPatient(ev.pid);
              }
            }
            for (auto it = evicted.rbegin(); it != evicted.rend(); ++it) {
              solution.AssignPatient(it->pid, it->room, it->day, it->ot);
            }
            evicted.clear();
            break;
          }
        }

        if (!evicted.empty()) {
          for (auto it = evicted.rbegin(); it != evicted.rend(); ++it) {
            if (!solution.IsPatientScheduled(it->pid)) {
              solution.AssignPatient(it->pid, it->room, it->day, it->ot);
            }
          }
        }
      }
    }
  }

  return false;
}


/** @brief Obtiene los dias factibles para la cirugia de un paciente.
 *  Para obligatorios acota el limite superior al surgery_due_day.
 */
std::vector<Day> RandomGenerator::GetFeasibleDays(const Patient& patient,
                                                   const ProblemData& problem) {
  Day release = patient.GetSurgeryReleaseDay();
  int num_days = problem.GetNumDays();

  Day last_possible = num_days - 1;
  if (last_possible < 0) return {};

  Day upper = last_possible;
  if (patient.IsMandatory()) {
    upper = std::min(upper, patient.GetSurgeryDueDay());
  }

  if (upper < release) return {};

  std::vector<Day> days;
  days.reserve(upper - release + 1);
  for (Day d = release; d <= upper; ++d) {
    days.push_back(d);
  }
  return days;
}

/** @brief Obtiene las habitaciones compatibles para un paciente. */
std::vector<RoomId> RandomGenerator::GetCompatibleRooms(
    const Patient& patient, const ProblemData& problem) {
  std::vector<RoomId> rooms;
  for (RoomId r = 0; r < problem.GetNumRooms(); ++r) {
    if (patient.IsCompatibleWithRoom(r)) {
      rooms.push_back(r);
    }
  }
  return rooms;
}

/** @brief Obtiene los quirofanos abiertos en un dia dado. */
std::vector<OperatingTheaterId> RandomGenerator::GetOpenOTs(
    Day day, const ProblemData& problem) {
  std::vector<OperatingTheaterId> ots;
  for (OperatingTheaterId ot = 0; ot < problem.GetNumOperatingTheaters();
       ++ot) {
    if (problem.GetOperatingTheater(ot).IsOpenOnDay(day)) {
      ots.push_back(ot);
    }
  }
  return ots;
}
