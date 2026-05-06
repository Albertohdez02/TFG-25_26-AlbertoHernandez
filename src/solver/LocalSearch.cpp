// LocalSearch.cpp - Busqueda local con vecindarios para IHTC 2024
// TFG Alberto Hernandez
//
// Estrategia first-improvement EXHAUSTIVA con 8 vecindarios.
// Cada vecindario itera TODOS los pacientes programados (shuffled),
// probando todos los candidatos hasta encontrar mejora.
//
// Mutaciones usan la API espacial (AssignPatient/UnassignPatient)
// y validan factibilidad con IsFeasiblePatientAssignment.

#include "LocalSearch.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <numeric>
#include <sstream>
#include <vector>

#include "../evaluator/FeasibilityChecker.h"
#include "RandomGenerator.h"

// ============================================================================
// Estadisticas
// ============================================================================

std::string LocalSearchStats::ToString() const {
  std::ostringstream oss;
  oss << "=== Estadisticas de busqueda local ===\n";
  oss << "  Iteraciones:   " << iterations << "\n";
  oss << "  Mejoras:       " << improvements << "\n";
  oss << "  Coste inicial: " << initial_cost << "\n";
  oss << "  Coste final:   " << final_cost << "\n";
  oss << "  Mejora total:  " << (initial_cost - final_cost) << " ("
      << (initial_cost > 0
              ? (100.0 * (initial_cost - final_cost) / initial_cost)
              : 0.0)
      << "%)\n";
  oss << "  Tiempo:        " << elapsed_seconds << " s\n";
  oss << "  Mejoras por operador:\n";
  for (int i = 0; i < 8; ++i) {
    if (op_improvements[i] > 0) {
      double pct = improvements > 0
          ? 100.0 * op_improvements[i] / improvements : 0.0;
      oss << "    " << kOperatorNames[i] << ": " << op_improvements[i]
          << " (" << pct << "%)\n";
    }
  }
  return oss.str();
}

// ============================================================================
// Busqueda local principal con ILS
// ============================================================================

LocalSearchStats LocalSearch::Run(Solution& solution, int max_iterations,
                                  std::mt19937& rng,
                                  double time_limit_seconds,
                                  uint8_t enabled_mask) {
  LocalSearchStats stats;
  auto start_time = std::chrono::steady_clock::now();
  const ProblemData& prob = solution.GetProblem();

  // Cobertura de enfermeras inicial: la solucion entrante puede tener celdas
  // (room, day) sin enfermera tras una construccion ACO o un Perturb previo.
  // Sin esto, el coste evaluado por LS oculta el coste de las celdas
  // descubiertas y produce decisiones engañosas.
  RandomGenerator::EnsureFullNurseCoverage(solution, prob, rng);

  int current_cost = Evaluator::Evaluate(solution);
  stats.initial_cost = current_cost;

  // guardar la mejor solucion (ILS)
  Solution best_solution = solution;
  int best_cost = current_cost;

  // 8 vecindarios en orden fijo; se filtran por enabled_mask
  using MoveFunc = std::function<bool(Solution&, int&, std::mt19937&)>;
  const std::array<MoveFunc, 8> all_ops = {
      TryChangeRoom,  TryChangeDay,      TryChangeOT,      TryRelocate,
      TrySwapRooms,   TrySwapDays,       TryToggleOptional, TryChangeNurse};

  // active[k] = {indice_global, funcion} para los operadores activos
  std::vector<std::pair<int, MoveFunc>> active;
  for (int i = 0; i < 8; ++i) {
    if (enabled_mask & static_cast<uint8_t>(1 << i)) {
      active.push_back({i, all_ops[i]});
    }
  }

  std::vector<int> order(active.size());
  std::iota(order.begin(), order.end(), 0);

  constexpr int kMaxPerturbations = 15;
  constexpr int kPerturbStrength = 4;

  int perturbation_count = 0;
  int iter = 0;

  auto time_remaining = [&]() {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start_time).count();
    return elapsed < time_limit_seconds;
  };

  while (iter < max_iterations && perturbation_count <= kMaxPerturbations &&
         time_remaining()) {
    // === Fase LS: busqueda local hasta optimo local ===
    bool ls_improved = true;
    while (ls_improved && iter < max_iterations && time_remaining()) {
      ++iter;
      stats.iterations = iter;
      ls_improved = false;

      std::shuffle(order.begin(), order.end(), rng);

      for (int local_idx : order) {
        auto& [global_idx, op] = active[local_idx];
        if (op(solution, current_cost, rng)) {
          // Tras aceptar un movimiento de paciente, la nueva celda
          // (room, day) puede haber quedado sin enfermera. Rellenamos solo
          // donde falta y reevaluamos para no maquillar el coste.
          if (global_idx != 7) {  // ChangeNurse no necesita refresh
            RandomGenerator::EnsureFullNurseCoverage(solution, prob, rng);
            current_cost = Evaluator::Evaluate(solution);
          }
          stats.op_improvements[global_idx]++;
          stats.improvements++;
          ls_improved = true;
          break;  // first-improvement
        }
      }
    }

    // actualizar mejor solucion
    if (current_cost < best_cost) {
      best_solution = solution;
      best_cost = current_cost;
    }

    // === Fase ILS: perturbar y reiniciar ===
    if (perturbation_count >= kMaxPerturbations || iter >= max_iterations) {
      break;
    }

    perturbation_count++;
    solution = best_solution;
    current_cost = best_cost;
    Perturb(solution, kPerturbStrength, rng);
    // Perturb mueve pacientes y puede dejar celdas sin enfermera
    RandomGenerator::EnsureFullNurseCoverage(solution, prob, rng);
    current_cost = Evaluator::Evaluate(solution);
  }

  // restaurar la mejor solucion (ya tiene cobertura completa por construccion)
  solution = best_solution;

  stats.final_cost = best_cost;

  auto end_time = std::chrono::steady_clock::now();
  stats.elapsed_seconds =
      std::chrono::duration<double>(end_time - start_time).count();

  return stats;
}

// ============================================================================
// Helper
// ============================================================================

std::vector<PatientId> LocalSearch::GetShuffledScheduled(
    const Solution& solution, std::mt19937& rng) {
  const auto& scheduled = solution.GetScheduledPatients();
  std::vector<PatientId> patients(scheduled.begin(), scheduled.end());
  std::shuffle(patients.begin(), patients.end(), rng);
  constexpr int kMaxPatients = 60;
  if (static_cast<int>(patients.size()) > kMaxPatients) {
    patients.resize(kMaxPatients);
  }
  return patients;
}

// ============================================================================
// Vecindario 1: Cambiar habitacion (EXHAUSTIVO)
// ============================================================================

bool LocalSearch::TryChangeRoom(Solution& solution, int& current_cost,
                                std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  auto patients = GetShuffledScheduled(solution, rng);

  for (PatientId pid : patients) {
    const Patient& patient = prob.GetPatient(pid);
    RoomId old_room = solution.GetPatientRoom(pid);
    Day day = solution.GetPatientAdmissionDay(pid);
    OperatingTheaterId ot = solution.GetPatientOT(pid);

    std::vector<RoomId> rooms;
    for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
      if (r != old_room && patient.IsCompatibleWithRoom(r)) {
        rooms.push_back(r);
      }
    }
    if (rooms.empty()) continue;
    std::shuffle(rooms.begin(), rooms.end(), rng);

    solution.UnassignPatient(pid);

    for (RoomId new_room : rooms) {
      if (FeasibilityChecker::IsFeasiblePatientAssignment(
              solution, pid, new_room, day, ot)) {
        solution.AssignPatient(pid, new_room, day, ot);
        int new_cost = Evaluator::Evaluate(solution);
        if (new_cost < current_cost) {
          current_cost = new_cost;
          return true;
        }
        solution.UnassignPatient(pid);
      }
    }

    // restaurar
    solution.AssignPatient(pid, old_room, day, ot);
  }

  return false;
}

// ============================================================================
// Vecindario 2: Cambiar dia - VENTANA COMPLETA (EXHAUSTIVO)
// ============================================================================

bool LocalSearch::TryChangeDay(Solution& solution, int& current_cost,
                               std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  auto patients = GetShuffledScheduled(solution, rng);

  for (PatientId pid : patients) {
    const Patient& patient = prob.GetPatient(pid);
    RoomId room = solution.GetPatientRoom(pid);
    Day old_day = solution.GetPatientAdmissionDay(pid);
    OperatingTheaterId ot = solution.GetPatientOT(pid);

    Day release = patient.GetSurgeryReleaseDay();
    Day last_possible = prob.GetNumDays() - 1;
    if (last_possible < release) continue;

    Day upper_bound = last_possible;
    if (patient.IsMandatory()) {
      upper_bound = std::min(upper_bound, patient.GetSurgeryDueDay());
    }
    if (upper_bound < release) continue;

    std::vector<Day> days;
    for (Day d = release; d <= upper_bound; ++d) {
      if (d != old_day) {
        days.push_back(d);
      }
    }
    if (days.empty()) continue;
    std::shuffle(days.begin(), days.end(), rng);

    solution.UnassignPatient(pid);

    for (Day new_day : days) {
      if (!prob.GetOperatingTheater(ot).IsOpenOnDay(new_day)) continue;

      if (FeasibilityChecker::IsFeasiblePatientAssignment(
              solution, pid, room, new_day, ot)) {
        solution.AssignPatient(pid, room, new_day, ot);
        int new_cost = Evaluator::Evaluate(solution);
        if (new_cost < current_cost) {
          current_cost = new_cost;
          return true;
        }
        solution.UnassignPatient(pid);
      }
    }

    solution.AssignPatient(pid, room, old_day, ot);
  }

  return false;
}

// ============================================================================
// Vecindario 3: Cambiar quirofano (EXHAUSTIVO)
// ============================================================================

bool LocalSearch::TryChangeOT(Solution& solution, int& current_cost,
                               std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  auto patients = GetShuffledScheduled(solution, rng);

  for (PatientId pid : patients) {
    RoomId room = solution.GetPatientRoom(pid);
    Day day = solution.GetPatientAdmissionDay(pid);
    OperatingTheaterId old_ot = solution.GetPatientOT(pid);

    std::vector<OperatingTheaterId> ots;
    for (OperatingTheaterId ot = 0; ot < prob.GetNumOperatingTheaters(); ++ot) {
      if (ot != old_ot && prob.GetOperatingTheater(ot).IsOpenOnDay(day)) {
        ots.push_back(ot);
      }
    }
    if (ots.empty()) continue;
    std::shuffle(ots.begin(), ots.end(), rng);

    solution.UnassignPatient(pid);

    for (OperatingTheaterId new_ot : ots) {
      if (FeasibilityChecker::IsFeasiblePatientAssignment(
              solution, pid, room, day, new_ot)) {
        solution.AssignPatient(pid, room, day, new_ot);
        int new_cost = Evaluator::Evaluate(solution);
        if (new_cost < current_cost) {
          current_cost = new_cost;
          return true;
        }
        solution.UnassignPatient(pid);
      }
    }

    solution.AssignPatient(pid, room, day, old_ot);
  }

  return false;
}

// ============================================================================
// Vecindario 4: Intercambiar habitaciones (EXHAUSTIVO sobre pares)
// ============================================================================

bool LocalSearch::TrySwapRooms(Solution& solution, int& current_cost,
                                std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  auto patients = GetShuffledScheduled(solution, rng);
  int n = static_cast<int>(patients.size());
  if (n < 2) return false;

  constexpr int kMaxPairs = 200;
  int pairs_tried = 0;

  for (int i = 0; i < n && pairs_tried < kMaxPairs; ++i) {
    PatientId p1 = patients[i];
    RoomId room1 = solution.GetPatientRoom(p1);
    Day day1 = solution.GetPatientAdmissionDay(p1);
    OperatingTheaterId ot1 = solution.GetPatientOT(p1);
    const Patient& pat1 = prob.GetPatient(p1);

    for (int j = i + 1; j < n && pairs_tried < kMaxPairs; ++j) {
      PatientId p2 = patients[j];
      RoomId room2 = solution.GetPatientRoom(p2);
      if (room1 == room2) continue;

      const Patient& pat2 = prob.GetPatient(p2);
      if (!pat1.IsCompatibleWithRoom(room2) ||
          !pat2.IsCompatibleWithRoom(room1))
        continue;

      pairs_tried++;

      Day day2 = solution.GetPatientAdmissionDay(p2);
      OperatingTheaterId ot2 = solution.GetPatientOT(p2);

      solution.UnassignPatient(p1);
      solution.UnassignPatient(p2);

      if (FeasibilityChecker::IsFeasiblePatientAssignment(
              solution, p1, room2, day1, ot1)) {
        solution.AssignPatient(p1, room2, day1, ot1);

        if (FeasibilityChecker::IsFeasiblePatientAssignment(
                solution, p2, room1, day2, ot2)) {
          solution.AssignPatient(p2, room1, day2, ot2);

          int new_cost = Evaluator::Evaluate(solution);
          if (new_cost < current_cost) {
            current_cost = new_cost;
            return true;
          }

          solution.UnassignPatient(p2);
        }

        solution.UnassignPatient(p1);
      }

      // restaurar
      solution.AssignPatient(p1, room1, day1, ot1);
      solution.AssignPatient(p2, room2, day2, ot2);
    }
  }

  return false;
}

// ============================================================================
// Vecindario 5: Intercambiar dias (EXHAUSTIVO sobre pares)
// ============================================================================

bool LocalSearch::TrySwapDays(Solution& solution, int& current_cost,
                               std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  auto patients = GetShuffledScheduled(solution, rng);
  int n = static_cast<int>(patients.size());
  if (n < 2) return false;

  constexpr int kMaxPairs = 200;
  int pairs_tried = 0;

  for (int i = 0; i < n && pairs_tried < kMaxPairs; ++i) {
    PatientId p1 = patients[i];
    Day day1 = solution.GetPatientAdmissionDay(p1);
    RoomId room1 = solution.GetPatientRoom(p1);
    OperatingTheaterId ot1 = solution.GetPatientOT(p1);
    const Patient& pat1 = prob.GetPatient(p1);

    for (int j = i + 1; j < n && pairs_tried < kMaxPairs; ++j) {
      PatientId p2 = patients[j];
      Day day2 = solution.GetPatientAdmissionDay(p2);
      if (day1 == day2) continue;

      const Patient& pat2 = prob.GetPatient(p2);
      RoomId room2 = solution.GetPatientRoom(p2);
      OperatingTheaterId ot2 = solution.GetPatientOT(p2);

      // verificar ventanas de admision
      if (day2 < pat1.GetSurgeryReleaseDay()) continue;
      if (pat1.IsMandatory() && day2 > pat1.GetSurgeryDueDay()) continue;
      if (day1 < pat2.GetSurgeryReleaseDay()) continue;
      if (pat2.IsMandatory() && day1 > pat2.GetSurgeryDueDay()) continue;

      // verificar OTs abiertos en los dias intercambiados
      if (!prob.GetOperatingTheater(ot1).IsOpenOnDay(day2)) continue;
      if (!prob.GetOperatingTheater(ot2).IsOpenOnDay(day1)) continue;

      pairs_tried++;

      solution.UnassignPatient(p1);
      solution.UnassignPatient(p2);

      if (FeasibilityChecker::IsFeasiblePatientAssignment(
              solution, p1, room1, day2, ot1)) {
        solution.AssignPatient(p1, room1, day2, ot1);

        if (FeasibilityChecker::IsFeasiblePatientAssignment(
                solution, p2, room2, day1, ot2)) {
          solution.AssignPatient(p2, room2, day1, ot2);

          int new_cost = Evaluator::Evaluate(solution);
          if (new_cost < current_cost) {
            current_cost = new_cost;
            return true;
          }

          solution.UnassignPatient(p2);
        }

        solution.UnassignPatient(p1);
      }

      // restaurar
      solution.AssignPatient(p1, room1, day1, ot1);
      solution.AssignPatient(p2, room2, day2, ot2);
    }
  }

  return false;
}

// ============================================================================
// Vecindario 6: Programar/desprogramar paciente opcional (EXHAUSTIVO)
// ============================================================================

bool LocalSearch::TryToggleOptional(Solution& solution, int& current_cost,
                                     std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  auto optionals = prob.GetOptionalPatientIds();
  if (optionals.empty()) return false;
  std::shuffle(optionals.begin(), optionals.end(), rng);

  for (PatientId pid : optionals) {
    if (solution.IsPatientScheduled(pid)) {
      // intentar desprogramarlo
      Day old_day = solution.GetPatientAdmissionDay(pid);
      RoomId old_room = solution.GetPatientRoom(pid);
      OperatingTheaterId old_ot = solution.GetPatientOT(pid);

      solution.UnassignPatient(pid);
      int new_cost = Evaluator::Evaluate(solution);

      if (new_cost < current_cost) {
        current_cost = new_cost;
        return true;
      }

      // deshacer
      solution.AssignPatient(pid, old_room, old_day, old_ot);
    } else {
      // intentar programarlo
      const Patient& patient = prob.GetPatient(pid);

      std::vector<RoomId> rooms;
      for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
        if (patient.IsCompatibleWithRoom(r)) rooms.push_back(r);
      }
      if (rooms.empty()) continue;

      Day release = patient.GetSurgeryReleaseDay();
      Day last_possible = prob.GetNumDays() - 1;
      if (last_possible < release) continue;

      std::shuffle(rooms.begin(), rooms.end(), rng);

      std::vector<Day> days;
      for (Day d = release; d <= last_possible; ++d) {
        days.push_back(d);
      }
      std::shuffle(days.begin(), days.end(), rng);

      for (Day day : days) {
        std::vector<OperatingTheaterId> open_ots;
        for (OperatingTheaterId ot = 0; ot < prob.GetNumOperatingTheaters();
             ++ot) {
          if (prob.GetOperatingTheater(ot).IsOpenOnDay(day)) {
            open_ots.push_back(ot);
          }
        }
        if (open_ots.empty()) continue;
        std::shuffle(open_ots.begin(), open_ots.end(), rng);

        for (RoomId room : rooms) {
          for (OperatingTheaterId ot : open_ots) {
            if (FeasibilityChecker::IsFeasiblePatientAssignment(
                    solution, pid, room, day, ot)) {
              solution.AssignPatient(pid, room, day, ot);
              int new_cost = Evaluator::Evaluate(solution);

              if (new_cost < current_cost) {
                current_cost = new_cost;
                return true;
              }

              solution.UnassignPatient(pid);
              goto next_optional;  // solo 1 intento por paciente
            }
          }
        }
      }
      next_optional:;
    }
  }

  return false;
}

// ============================================================================
// Vecindario 7: Cambiar enfermera (EXHAUSTIVO sobre posiciones)
// ============================================================================

bool LocalSearch::TryChangeNurse(Solution& solution, int& current_cost,
                                  std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  int num_rooms = prob.GetNumRooms();
  int num_days = prob.GetNumDays();
  int num_shifts = prob.GetNumShiftTypes();

  std::vector<std::tuple<RoomId, Day, Shift>> positions;
  for (RoomId r = 0; r < num_rooms; ++r) {
    for (Day d = 0; d < num_days; ++d) {
      if (solution.GetRoomOccupancy(r, d) > 0) {
        for (Shift s = 0; s < num_shifts; ++s) {
          positions.emplace_back(r, d, s);
        }
      }
    }
  }
  if (positions.empty()) return false;
  std::shuffle(positions.begin(), positions.end(), rng);
  constexpr int kMaxPositions = 100;
  if (static_cast<int>(positions.size()) > kMaxPositions) {
    positions.resize(kMaxPositions);
  }

  for (auto [room, day, shift] : positions) {
    NurseId old_nurse = solution.GetNurseAssignment(room, day, shift);

    std::vector<NurseId> candidates;
    for (NurseId n = 0; n < prob.GetNumNurses(); ++n) {
      if (n != old_nurse &&
          FeasibilityChecker::IsFeasibleNurseAssignment(solution, n, room,
                                                        day, shift)) {
        candidates.push_back(n);
      }
    }
    if (candidates.empty()) continue;
    std::shuffle(candidates.begin(), candidates.end(), rng);

    for (NurseId new_nurse : candidates) {
      if (old_nurse != kInvalidId) {
        solution.UnassignNurse(room, day, shift);
      }
      solution.AssignNurse(new_nurse, room, day, shift);

      int new_cost = Evaluator::Evaluate(solution);
      if (new_cost < current_cost) {
        current_cost = new_cost;
        return true;
      }

      // deshacer
      solution.UnassignNurse(room, day, shift);
      if (old_nurse != kInvalidId) {
        solution.AssignNurse(old_nurse, room, day, shift);
      }
    }
  }

  return false;
}

// ============================================================================
// Vecindario 8: Reubicacion compuesta (day + room + ot simultaneo)
// ============================================================================

bool LocalSearch::TryRelocate(Solution& solution, int& current_cost,
                               std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  auto patients = GetShuffledScheduled(solution, rng);

  for (PatientId pid : patients) {
    const Patient& patient = prob.GetPatient(pid);
    Day old_day = solution.GetPatientAdmissionDay(pid);
    RoomId old_room = solution.GetPatientRoom(pid);
    OperatingTheaterId old_ot = solution.GetPatientOT(pid);

    Day release = patient.GetSurgeryReleaseDay();
    Day last_possible = prob.GetNumDays() - 1;
    Day upper_bound = last_possible;
    if (patient.IsMandatory()) {
      upper_bound = std::min(upper_bound, patient.GetSurgeryDueDay());
    }
    if (upper_bound < release) continue;

    std::vector<Day> days;
    for (Day d = release; d <= upper_bound; ++d) {
      days.push_back(d);
    }
    std::shuffle(days.begin(), days.end(), rng);

    std::vector<RoomId> rooms;
    for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
      if (patient.IsCompatibleWithRoom(r)) {
        rooms.push_back(r);
      }
    }
    if (rooms.empty()) continue;
    std::shuffle(rooms.begin(), rooms.end(), rng);

    solution.UnassignPatient(pid);

    constexpr int kMaxCombos = 30;
    int combos_tried = 0;

    for (Day new_day : days) {
      if (combos_tried >= kMaxCombos) break;

      std::vector<OperatingTheaterId> ots;
      for (OperatingTheaterId ot = 0; ot < prob.GetNumOperatingTheaters();
           ++ot) {
        if (prob.GetOperatingTheater(ot).IsOpenOnDay(new_day)) {
          ots.push_back(ot);
        }
      }
      if (ots.empty()) continue;
      std::shuffle(ots.begin(), ots.end(), rng);

      for (RoomId new_room : rooms) {
        if (combos_tried >= kMaxCombos) break;

        for (OperatingTheaterId new_ot : ots) {
          if (combos_tried >= kMaxCombos) break;

          // saltar la combinacion actual
          if (new_day == old_day && new_room == old_room &&
              new_ot == old_ot)
            continue;

          // saltar movimientos simples (ya cubiertos)
          int changes = (new_day != old_day ? 1 : 0) +
                        (new_room != old_room ? 1 : 0) +
                        (new_ot != old_ot ? 1 : 0);
          if (changes < 2) continue;

          combos_tried++;

          if (FeasibilityChecker::IsFeasiblePatientAssignment(
                  solution, pid, new_room, new_day, new_ot)) {
            solution.AssignPatient(pid, new_room, new_day, new_ot);
            int new_cost = Evaluator::Evaluate(solution);
            if (new_cost < current_cost) {
              current_cost = new_cost;
              return true;
            }
            solution.UnassignPatient(pid);
          }
        }
      }
    }

    // restaurar
    solution.AssignPatient(pid, old_room, old_day, old_ot);
  }

  return false;
}

// ============================================================================
// Perturbacion ILS: reubica K pacientes a posiciones factibles aleatorias
// ============================================================================

void LocalSearch::Perturb(Solution& solution, int strength,
                          std::mt19937& rng) {
  const ProblemData& prob = solution.GetProblem();
  auto patients = GetShuffledScheduled(solution, rng);
  int to_perturb = std::min(strength, static_cast<int>(patients.size()));

  for (int i = 0; i < to_perturb; ++i) {
    PatientId pid = patients[i];
    const Patient& patient = prob.GetPatient(pid);

    Day old_day = solution.GetPatientAdmissionDay(pid);
    RoomId old_room = solution.GetPatientRoom(pid);
    OperatingTheaterId old_ot = solution.GetPatientOT(pid);

    Day release = patient.GetSurgeryReleaseDay();
    Day last_possible = prob.GetNumDays() - 1;
    Day upper_bound = last_possible;
    if (patient.IsMandatory()) {
      upper_bound = std::min(upper_bound, patient.GetSurgeryDueDay());
    }
    if (upper_bound < release) continue;

    std::vector<Day> days;
    for (Day d = release; d <= upper_bound; ++d) {
      if (d != old_day) days.push_back(d);
    }
    if (days.empty()) continue;
    std::shuffle(days.begin(), days.end(), rng);

    std::vector<RoomId> rooms;
    for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
      if (patient.IsCompatibleWithRoom(r)) rooms.push_back(r);
    }
    std::shuffle(rooms.begin(), rooms.end(), rng);

    solution.UnassignPatient(pid);

    bool relocated = false;
    for (Day d : days) {
      std::vector<OperatingTheaterId> ots;
      for (OperatingTheaterId ot = 0; ot < prob.GetNumOperatingTheaters();
           ++ot) {
        if (prob.GetOperatingTheater(ot).IsOpenOnDay(d)) {
          ots.push_back(ot);
        }
      }
      if (ots.empty()) continue;
      std::shuffle(ots.begin(), ots.end(), rng);

      for (RoomId r : rooms) {
        for (OperatingTheaterId ot : ots) {
          if (FeasibilityChecker::IsFeasiblePatientAssignment(
                  solution, pid, r, d, ot)) {
            solution.AssignPatient(pid, r, d, ot);
            relocated = true;
            goto perturb_done;
          }
        }
      }
    }
    perturb_done:
    if (!relocated) {
      // no encontro alternativa factible, restaurar
      solution.AssignPatient(pid, old_room, old_day, old_ot);
    }
  }
}
