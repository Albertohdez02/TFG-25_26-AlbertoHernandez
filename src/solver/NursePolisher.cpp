// NursePolisher.cpp - Implementacion del pulido dedicado de enfermeras.

#include "NursePolisher.h"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <unordered_map>
#include <vector>

#include "../evaluator/Evaluator.h"
#include "../evaluator/FeasibilityChecker.h"

namespace {

// Recoge todas las celdas (r, d, s) que tienen pacientes/ocupantes y por
// tanto requieren nurse. Las celdas vacias no se tocan.
struct Cell {
  RoomId r;
  Day d;
  Shift s;
};

std::vector<Cell> CollectActiveCells(const Solution& sol) {
  const ProblemData& prob = sol.GetProblem();
  int num_rooms = prob.GetNumRooms();
  int num_days = prob.GetNumDays();
  int num_shifts = prob.GetNumShiftTypes();
  std::vector<Cell> out;
  out.reserve(num_rooms * num_days * num_shifts / 2);
  for (RoomId r = 0; r < num_rooms; ++r) {
    for (Day d = 0; d < num_days; ++d) {
      if (sol.GetRoomOccupancy(r, d) == 0) continue;
      for (Shift s = 0; s < num_shifts; ++s) {
        out.push_back({r, d, s});
      }
    }
  }
  return out;
}

// =====================================================================
// Operador 1: ChangeOneNurse - best-improvement por celda
// =====================================================================
// Para cada celda con nurse asignada, prueba TODAS las nurses alternativas
// factibles y elige la que mas reduce el coste. Es best-improvement (no
// first), por lo que pasa por todas las candidatas antes de decidir.
// Diferencia clave con TryChangeNurse de VNS: sin cap, exhaustivo.
bool ChangeOneNursePass(Solution& sol, int& current_cost,
                        const std::vector<Cell>& cells) {
  const ProblemData& prob = sol.GetProblem();
  int num_nurses = prob.GetNumNurses();
  bool improved_any = false;

  for (const Cell& c : cells) {
    NurseId old_nurse = sol.GetNurseAssignment(c.r, c.d, c.s);

    NurseId best_nurse = old_nurse;
    int best_cost = current_cost;

    for (NurseId n = 0; n < num_nurses; ++n) {
      if (n == old_nurse) continue;
      if (!FeasibilityChecker::IsFeasibleNurseAssignment(sol, n, c.r, c.d,
                                                          c.s)) {
        continue;
      }

      // Aplicar cambio temporal
      if (old_nurse != kInvalidId) sol.UnassignNurse(c.r, c.d, c.s);
      sol.AssignNurse(n, c.r, c.d, c.s);
      int new_cost = Evaluator::Evaluate(sol);

      if (new_cost < best_cost) {
        best_cost = new_cost;
        best_nurse = n;
      }

      // Restaurar para seguir probando
      sol.UnassignNurse(c.r, c.d, c.s);
      if (old_nurse != kInvalidId) {
        sol.AssignNurse(old_nurse, c.r, c.d, c.s);
      }
    }

    // Si encontramos mejor, aplicar definitivamente
    if (best_nurse != old_nurse) {
      if (old_nurse != kInvalidId) sol.UnassignNurse(c.r, c.d, c.s);
      sol.AssignNurse(best_nurse, c.r, c.d, c.s);
      current_cost = best_cost;
      improved_any = true;
    }
  }
  return improved_any;
}

// =====================================================================
// Operador 2: SwapTwoNurses - intercambio entre dos celdas
// =====================================================================
// Para cada par de celdas (mismo shift, diferentes nurses), intenta
// intercambiar las dos nurses. Acepta si baja el coste.
// First-improvement por celda exterior para evitar O(N^2) prohibitivo.
bool SwapTwoNursesPass(Solution& sol, int& current_cost,
                       const std::vector<Cell>& cells, std::mt19937& rng) {
  bool improved_any = false;
  int n = static_cast<int>(cells.size());
  if (n < 2) return false;

  // Iterar shuffled para evitar sesgo de orden
  std::vector<int> order(n);
  std::iota(order.begin(), order.end(), 0);
  std::shuffle(order.begin(), order.end(), rng);

  // Cap para evitar O(n^2) explosivo
  constexpr int kMaxOuterCells = 200;
  int outer_cap = std::min(n, kMaxOuterCells);

  for (int oi = 0; oi < outer_cap; ++oi) {
    const Cell& c1 = cells[order[oi]];
    NurseId n1 = sol.GetNurseAssignment(c1.r, c1.d, c1.s);
    if (n1 == kInvalidId) continue;

    // Buscar pareja
    for (int oj = oi + 1; oj < n; ++oj) {
      const Cell& c2 = cells[order[oj]];
      if (c1.s != c2.s) continue;  // solo mismo shift
      NurseId n2 = sol.GetNurseAssignment(c2.r, c2.d, c2.s);
      if (n2 == kInvalidId || n2 == n1) continue;

      // Comprobar factibilidad cruzada: n1 debe estar disponible en
      // (c2.d, c2.s) y viceversa. Como ya estaban asignadas en sus
      // shifts originales, lo unico que cambia es la habitacion; HC10
      // (disponibilidad por dia-shift) se mantiene.
      // Igualmente, verificamos con IsFeasibleNurseAssignment tras
      // desasignar para asegurar HC9 (una nurse por celda).

      sol.UnassignNurse(c1.r, c1.d, c1.s);
      sol.UnassignNurse(c2.r, c2.d, c2.s);

      bool ok1 = FeasibilityChecker::IsFeasibleNurseAssignment(
                     sol, n2, c1.r, c1.d, c1.s);
      bool ok2 = FeasibilityChecker::IsFeasibleNurseAssignment(
                     sol, n1, c2.r, c2.d, c2.s);
      if (!ok1 || !ok2) {
        // restaurar
        sol.AssignNurse(n1, c1.r, c1.d, c1.s);
        sol.AssignNurse(n2, c2.r, c2.d, c2.s);
        continue;
      }

      sol.AssignNurse(n2, c1.r, c1.d, c1.s);
      sol.AssignNurse(n1, c2.r, c2.d, c2.s);
      int new_cost = Evaluator::Evaluate(sol);

      if (new_cost < current_cost) {
        current_cost = new_cost;
        improved_any = true;
        break;  // first-improvement para la celda externa
      }

      // Rollback
      sol.UnassignNurse(c1.r, c1.d, c1.s);
      sol.UnassignNurse(c2.r, c2.d, c2.s);
      sol.AssignNurse(n1, c1.r, c1.d, c1.s);
      sol.AssignNurse(n2, c2.r, c2.d, c2.s);
    }
  }
  return improved_any;
}

// =====================================================================
// Operador 3: PromoteContinuity
// =====================================================================
// Para cada (room, shift), identifica la nurse mas frecuente en la ventana
// de dias activos. Intenta asignarla a todos los dias activos donde
// (a) es factible y (b) reduce el coste total. Ataca directamente
// continuity_of_care.
bool PromoteContinuityPass(Solution& sol, int& current_cost) {
  const ProblemData& prob = sol.GetProblem();
  int num_rooms = prob.GetNumRooms();
  int num_days = prob.GetNumDays();
  int num_shifts = prob.GetNumShiftTypes();
  bool improved_any = false;

  for (RoomId r = 0; r < num_rooms; ++r) {
    for (Shift s = 0; s < num_shifts; ++s) {
      // 1. Recoger dias activos para (r, s)
      std::vector<Day> active_days;
      std::unordered_map<NurseId, int> nurse_freq;
      for (Day d = 0; d < num_days; ++d) {
        if (sol.GetRoomOccupancy(r, d) == 0) continue;
        active_days.push_back(d);
        NurseId n = sol.GetNurseAssignment(r, d, s);
        if (n != kInvalidId) nurse_freq[n]++;
      }
      if (active_days.size() < 2 || nurse_freq.empty()) continue;

      // 2. Identificar nurses candidatas en orden de frecuencia desc
      std::vector<std::pair<NurseId, int>> sorted_nurses(nurse_freq.begin(),
                                                          nurse_freq.end());
      std::sort(sorted_nurses.begin(), sorted_nurses.end(),
                [](const auto& a, const auto& b) {
                  return a.second > b.second;
                });

      // 3. Para la nurse mas frecuente, intentar propagarla a TODOS los
      //    dias activos donde no esta. Aceptamos solo si baja el coste.
      for (const auto& [candidate, freq] : sorted_nurses) {
        if (static_cast<int>(active_days.size()) == freq) break;  // ya cubre todo

        bool round_improved = false;
        for (Day d : active_days) {
          NurseId current = sol.GetNurseAssignment(r, d, s);
          if (current == candidate) continue;

          // Probar reemplazo
          if (current != kInvalidId) sol.UnassignNurse(r, d, s);
          if (!FeasibilityChecker::IsFeasibleNurseAssignment(sol, candidate,
                                                              r, d, s)) {
            if (current != kInvalidId) sol.AssignNurse(current, r, d, s);
            continue;
          }
          sol.AssignNurse(candidate, r, d, s);
          int new_cost = Evaluator::Evaluate(sol);

          if (new_cost < current_cost) {
            current_cost = new_cost;
            improved_any = true;
            round_improved = true;
            // commit el cambio (no rollback)
          } else {
            // rollback
            sol.UnassignNurse(r, d, s);
            if (current != kInvalidId) sol.AssignNurse(current, r, d, s);
          }
        }
        if (round_improved) break;  // pasamos a la siguiente (r, s)
      }
    }
  }
  return improved_any;
}

}  // namespace

// =====================================================================
// Punto de entrada
// =====================================================================
int NursePolisher::Polish(Solution& solution, double time_limit_s,
                           std::mt19937& rng) {
  if (time_limit_s <= 0.0) return 0;

  auto t0 = std::chrono::steady_clock::now();
  auto time_remaining = [&]() {
    double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count();
    return time_limit_s - elapsed;
  };

  int initial_cost = Evaluator::Evaluate(solution);
  int current_cost = initial_cost;
  int total_improvements = 0;

  // Recoger celdas activas una sola vez (no cambian: dependen de la
  // asignacion de pacientes, que esta fase NO toca)
  auto cells = CollectActiveCells(solution);

  // Bucle principal: aplicar los 3 operadores hasta convergencia o
  // agotamiento de tiempo. Cada operador aplicado a fondo en cada pase.
  bool improved_overall = true;
  int pass = 0;
  while (improved_overall && time_remaining() > 0.1) {
    improved_overall = false;
    ++pass;

    // Operador 1: cambio individual best-improvement
    if (time_remaining() > 0.1) {
      int before = current_cost;
      bool imp = ChangeOneNursePass(solution, current_cost, cells);
      if (imp) {
        improved_overall = true;
        total_improvements += (before - current_cost > 0 ? 1 : 0);
      }
    }

    // Operador 2: swap entre celdas
    if (time_remaining() > 0.1) {
      int before = current_cost;
      bool imp = SwapTwoNursesPass(solution, current_cost, cells, rng);
      if (imp) {
        improved_overall = true;
        total_improvements += (before - current_cost > 0 ? 1 : 0);
      }
    }

    // Operador 3: promover continuidad
    if (time_remaining() > 0.1) {
      int before = current_cost;
      bool imp = PromoteContinuityPass(solution, current_cost);
      if (imp) {
        improved_overall = true;
        total_improvements += (before - current_cost > 0 ? 1 : 0);
      }
    }

    // Safety: si tras 5 pases no hay mejora, salir
    if (pass >= 20) break;
  }

  return total_improvements;
}
