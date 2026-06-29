// CompoundMoves.cpp - Implementacion de movimientos compuestos
// TFG Alberto Hernandez

#include "CompoundMoves.h"

#include <algorithm>
#include <numeric>
#include <vector>

#include "../evaluator/Evaluator.h"
#include "../evaluator/FeasibilityChecker.h"
#include "RandomGenerator.h"

/** @brief Inserta un opcional desalojando un paciente bloqueante.
 *  Para cada opcional no programado explora destinos (room, day, ot) en su
 *  ventana factible. Si la insercion directa no es factible (la cubre Toggle),
 *  identifica heuristicamente un bloqueante en (room, day) o (ot, day), lo
 *  desaloja, inserta el opcional e intenta reubicar el bloqueante en su propia
 *  ventana. Commit si delta < 0; rollback en caso contrario.
 */
bool CompoundMoves::TryKickPatient(Solution& solution, int& current_cost,
                                    std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  int num_days = prob.GetNumDays();
  int num_rooms = prob.GetNumRooms();
  int num_ots = prob.GetNumOperatingTheaters();

  // recoger opcionales no programados
  std::vector<PatientId> unscheduled_opt;
  for (PatientId pid : prob.GetOptionalPatientIds()) {
    if (!solution.IsPatientScheduled(pid)) unscheduled_opt.push_back(pid);
  }
  if (unscheduled_opt.empty()) return false;
  std::shuffle(unscheduled_opt.begin(), unscheduled_opt.end(), rng);

  // limite de opcionales a intentar por llamada (evita explosion en
  // instancias con 90+ opcionales sin programar)
  constexpr int kMaxOptionalsToTry = 30;
  if (static_cast<int>(unscheduled_opt.size()) > kMaxOptionalsToTry) {
    unscheduled_opt.resize(kMaxOptionalsToTry);
  }

  for (PatientId p_in : unscheduled_opt) {
    const Patient& p_in_obj = prob.GetPatient(p_in);
    Day release = p_in_obj.GetSurgeryReleaseDay();
    Day last_day = num_days - 1;
    if (last_day < release) continue;

    // candidatos de dia (mezclar para diversidad)
    std::vector<Day> days;
    for (Day d = release; d <= last_day; ++d) days.push_back(d);
    std::shuffle(days.begin(), days.end(), rng);

    // candidatos de room (compatibles)
    std::vector<RoomId> rooms;
    for (RoomId r = 0; r < num_rooms; ++r) {
      if (p_in_obj.IsCompatibleWithRoom(r)) rooms.push_back(r);
    }
    if (rooms.empty()) continue;
    std::shuffle(rooms.begin(), rooms.end(), rng);

    // limite de combinaciones por opcional
    constexpr int kMaxKickAttempts = 40;
    int attempts = 0;

    for (Day day : days) {
      if (attempts >= kMaxKickAttempts) break;

      for (RoomId room : rooms) {
        if (attempts >= kMaxKickAttempts) break;

        for (OperatingTheaterId ot = 0; ot < num_ots; ++ot) {
          if (attempts >= kMaxKickAttempts) break;
          if (!prob.GetOperatingTheater(ot).IsOpenOnDay(day)) continue;

          // Si la inserccion ya es factible, no es kick (lo cubre Toggle)
          if (FeasibilityChecker::IsFeasiblePatientAssignment(
                  solution, p_in, room, day, ot)) {
            continue;
          }

          ++attempts;

          // Identificar bloqueante: heuristicamente, un paciente en
          // (room, day) que comparte la (room, day) o el (ot, day).
          // Tomamos el primero que encontremos.
          PatientId blocker = kInvalidId;
          const auto& room_pats = solution.GetRoomPatients(room, day);
          if (!room_pats.empty()) {
            // bloqueante por capacidad/genero: el ultimo en la lista
            blocker = room_pats[room_pats.size() - 1];
          } else {
            // bloqueante por OT: alguien con mismo ot y admission_day
            for (PatientId q : solution.GetScheduledPatients()) {
              if (solution.GetPatientOT(q) == ot &&
                  solution.GetPatientAdmissionDay(q) == day) {
                blocker = q;
                break;
              }
            }
          }
          if (blocker == kInvalidId) continue;

          // Snapshot completo via copy
          Solution snapshot = solution;
          int snapshot_cost = current_cost;

          // Guardar info del bloqueante para reubicarlo
          RoomId blk_room = solution.GetPatientRoom(blocker);
          Day blk_day = solution.GetPatientAdmissionDay(blocker);
          OperatingTheaterId blk_ot = solution.GetPatientOT(blocker);

          // Desasignar bloqueante e intentar meter el opcional
          solution.UnassignPatient(blocker);
          if (!FeasibilityChecker::IsFeasiblePatientAssignment(
                  solution, p_in, room, day, ot)) {
            // sigue siendo infactible (otro bloqueante); rollback
            solution = std::move(snapshot);
            current_cost = snapshot_cost;
            continue;
          }
          solution.AssignPatient(p_in, room, day, ot);

          // Reubicar el bloqueante en su propia ventana
          bool relocated =
              RandomGenerator::TryAssignPatientFeasibly(solution, blocker,
                                                          prob, rng);
          if (!relocated) {
            if (prob.GetPatient(blocker).IsMandatory()) {
              // obligatorio sin sitio -> rollback obligatorio
              solution = std::move(snapshot);
              current_cost = snapshot_cost;
              continue;
            }
            // si era opcional, queda fuera -> aceptable
          }

          // Cobertura de enfermeras tras los movimientos
          RandomGenerator::EnsureFullNurseCoverage(solution, prob, rng);
          int new_cost = Evaluator::Evaluate(solution);

          if (new_cost < current_cost) {
            current_cost = new_cost;
            return true;  // first-improvement
          }

          // No mejoro: rollback
          solution = std::move(snapshot);
          current_cost = snapshot_cost;
        }
      }
    }
  }

  return false;
}

/** @brief Reasigna todos los pacientes de un dia con overtime para reducirlo.
 *  Ataca los dias con mayor surgeon_overtime + ot_overtime. Por cada uno
 *  desasigna los pacientes con admission_day == d y los reasigna en orden de
 *  slack ascendente (mas restringidos primero). Commit si mejora; rollback si no.
 */
bool CompoundMoves::TryReorganizeDay(Solution& solution, int& current_cost,
                                      std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  int num_days = prob.GetNumDays();
  int num_surgeons = prob.GetNumSurgeons();
  int num_ots = prob.GetNumOperatingTheaters();

  // calcular overtime total por dia
  std::vector<int> day_overtime(num_days, 0);
  for (Day d = 0; d < num_days; ++d) {
    int over = 0;
    for (SurgeonId s = 0; s < num_surgeons; ++s) {
      int load = solution.GetSurgeonLoad(s, d);
      int maxt = prob.GetSurgeon(s).GetMaxSurgeryTimeForDay(d);
      if (load > maxt) over += (load - maxt);
    }
    for (OperatingTheaterId ot = 0; ot < num_ots; ++ot) {
      int load = solution.GetOTLoad(ot, d);
      int avail = prob.GetOperatingTheater(ot).GetAvailabilityForDay(d);
      if (load > avail) over += (load - avail);
    }
    day_overtime[d] = over;
  }

  // candidatos: dias con overtime > 0, ordenados descendente
  std::vector<Day> candidates;
  for (Day d = 0; d < num_days; ++d) {
    if (day_overtime[d] > 0) candidates.push_back(d);
  }
  if (candidates.empty()) return false;
  std::sort(candidates.begin(), candidates.end(),
            [&](Day a, Day b) { return day_overtime[a] > day_overtime[b]; });

  // limite (atacamos los 3 dias con mas overtime)
  constexpr int kMaxDays = 3;
  if (static_cast<int>(candidates.size()) > kMaxDays) {
    candidates.resize(kMaxDays);
  }

  for (Day d : candidates) {
    // pacientes con admission_day == d
    std::vector<PatientId> day_patients;
    for (PatientId pid : solution.GetScheduledPatients()) {
      if (solution.GetPatientAdmissionDay(pid) == d) {
        day_patients.push_back(pid);
      }
    }
    if (day_patients.size() < 2) continue;  // nada que reorganizar

    // Snapshot
    Solution snapshot = solution;
    int snapshot_cost = current_cost;

    // Desasignar todos los del dia
    for (PatientId pid : day_patients) {
      solution.UnassignPatient(pid);
    }

    // Reasignar en orden de slack asc (mas restringidos primero)
    std::sort(day_patients.begin(), day_patients.end(),
              [&prob](PatientId a, PatientId b) {
                const Patient& pa = prob.GetPatient(a);
                const Patient& pb = prob.GetPatient(b);
                int sa = pa.IsMandatory()
                            ? pa.GetSurgeryDueDay() - pa.GetSurgeryReleaseDay()
                            : 999;
                int sb = pb.IsMandatory()
                            ? pb.GetSurgeryDueDay() - pb.GetSurgeryReleaseDay()
                            : 999;
                return sa < sb;
              });

    // Re-asignar todos (preferiblemente en su mismo dia, pero permitiendo
    // que alguno cambie de dia si no encaja)
    bool all_ok = true;
    for (PatientId pid : day_patients) {
      bool ok = RandomGenerator::TryAssignPatientFeasibly(solution, pid,
                                                            prob, rng);
      if (!ok && prob.GetPatient(pid).IsMandatory()) {
        // forzar (puede desalojar y reubicar otros)
        ok = RandomGenerator::ForceAssignMandatory(solution, pid, prob, rng);
        if (!ok) { all_ok = false; break; }
      }
    }
    if (!all_ok) {
      solution = std::move(snapshot);
      current_cost = snapshot_cost;
      continue;
    }

    // Refrescar enfermeras
    RandomGenerator::EnsureFullNurseCoverage(solution, prob, rng);
    int new_cost = Evaluator::Evaluate(solution);

    if (new_cost < current_cost) {
      current_cost = new_cost;
      return true;
    }
    // No mejoro
    solution = std::move(snapshot);
    current_cost = snapshot_cost;
  }

  return false;
}

/** @brief Intercambia enfermeras de dos rooms en un shift, bloque de k=3 dias.
 *  Elige al azar room1 != room2, un shift y un dia inicial. Si en los k dias
 *  ambas rooms tienen nurse asignada y disponible en (day, shift), intercambia
 *  las 2k asignaciones. Commit si mejora; rollback si no.
 */
bool CompoundMoves::TrySwapNurseBlock(Solution& solution, int& current_cost,
                                       std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  int num_rooms = prob.GetNumRooms();
  int num_days = prob.GetNumDays();
  int num_shifts = prob.GetNumShiftTypes();

  constexpr int kBlockLen = 3;
  constexpr int kMaxAttempts = 30;

  std::uniform_int_distribution<int> room_dist(0, num_rooms - 1);
  std::uniform_int_distribution<int> shift_dist(0, num_shifts - 1);
  std::uniform_int_distribution<int> day_dist(0, num_days - kBlockLen);

  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    RoomId r1 = room_dist(rng);
    RoomId r2 = room_dist(rng);
    if (r1 == r2) continue;

    Shift s = shift_dist(rng);
    Day d0 = day_dist(rng);

    // verificar que para los k dias, ambas rooms tienen nurses asignadas
    // y son intercambiables (disponibilidad)
    bool ok = true;
    std::vector<NurseId> nurses_r1(kBlockLen);
    std::vector<NurseId> nurses_r2(kBlockLen);
    for (int k = 0; k < kBlockLen; ++k) {
      Day d = d0 + k;
      NurseId n1 = solution.GetNurseAssignment(r1, d, s);
      NurseId n2 = solution.GetNurseAssignment(r2, d, s);
      if (n1 == kInvalidId || n2 == kInvalidId || n1 == n2) {
        ok = false; break;
      }
      // ambas deben estar disponibles en (d, s) (deberian estarlo ya que
      // ya las usamos, pero por seguridad)
      if (!prob.GetNurse(n1).IsAvailable(d, s) ||
          !prob.GetNurse(n2).IsAvailable(d, s)) {
        ok = false; break;
      }
      nurses_r1[k] = n1;
      nurses_r2[k] = n2;
    }
    if (!ok) continue;

    // Snapshot
    Solution snapshot = solution;
    int snapshot_cost = current_cost;

    // Intercambiar
    for (int k = 0; k < kBlockLen; ++k) {
      Day d = d0 + k;
      solution.UnassignNurse(r1, d, s);
      solution.UnassignNurse(r2, d, s);
      solution.AssignNurse(nurses_r2[k], r1, d, s);
      solution.AssignNurse(nurses_r1[k], r2, d, s);
    }

    int new_cost = Evaluator::Evaluate(solution);
    if (new_cost < current_cost) {
      current_cost = new_cost;
      return true;
    }

    // Rollback
    solution = std::move(snapshot);
    current_cost = snapshot_cost;
  }

  return false;
}
