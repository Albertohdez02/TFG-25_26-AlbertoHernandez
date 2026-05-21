// ACOSolver.cpp - Colonia de Hormigas (MMAS) hibrido con VNS para IHTC 2024
// TFG Alberto Hernandez
//
// Cada hormiga construye una solucion guiada por dos matrices de feromona
// (tau_day y tau_room) combinadas con heuristica greedy (eta). Tras la
// construccion, la VNS mejora la solucion. Solo la mejor solucion de cada
// iteracion actualiza las feromonas (MMAS). Las cotas [tau_min, tau_max]
// evitan estancamiento por convergencia prematura.

#include "ACOSolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

#include "../evaluator/Evaluator.h"
#include "../evaluator/FeasibilityChecker.h"
#include "LocalSearch.h"
#include "NursePolisher.h"
#include "RandomGenerator.h"

// ============================================================================
// Run - bucle principal ACO + VNS
// ============================================================================

/** @brief Ejecuta el algoritmo ACO hibrido con VNS.
 *  @param problem  Datos del problema.
 *  @param rng      Generador aleatorio.
 *  @param max_ls_iter Iteraciones maximas de busqueda local por hormiga.
 *  @param time_limit_s Tiempo total disponible en segundos.
 *  @param params   Parametros ACO.
 *  @return Mejor solucion factible encontrada.
 */
Solution ACOSolver::Run(const ProblemData& problem, std::mt19937& rng,
                        int max_ls_iter, double time_limit_s,
                        const ACOParams& params) {
  auto t0 = std::chrono::steady_clock::now();
  auto elapsed_s = [&]() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now() - t0).count();
  };
  // Some-touches Fase 1: reservamos una porcion del tiempo total para la
  // fase final de pulido de enfermeras. El bucle ACO se ejecuta dentro de
  // (time_limit_s - polish_budget); el polisher al final usa polish_budget.
  // Acotamos: minimo 30s util de polisher, maximo 30% del tiempo total.
  double polish_budget = params.nurse_polish_budget_s;
  if (polish_budget > 0.0) {
    polish_budget = std::clamp(polish_budget, 30.0, time_limit_s * 0.30);
  }
  double aco_time_budget = time_limit_s - polish_budget;
  auto remaining_s = [&]() { return aco_time_budget - elapsed_s(); };

  int num_patients = problem.GetNumPatients();
  int num_days     = problem.GetNumDays();
  int num_rooms    = problem.GetNumRooms();
  int num_shifts   = problem.GetNumShiftTypes();
  int num_nurses   = problem.GetNumNurses();

  // matrices de feromona aplanadas: tau[pid * stride + idx]
  PheromoneMatrix tau_day(num_patients * num_days,  0.0);
  PheromoneMatrix tau_room(num_patients * num_rooms, 0.0);
  InitPheromones(tau_day, tau_room, problem, params.tau_init);

  // C1: tau_nurse[shift * num_nurses + nurse]. Solo activa si use_tau_nurse.
  // Tamano: 3 * N (e.g., 96 doubles en i22, N=32). Aprende que enfermeras
  // tienden a estar en buenas soluciones para cada turno.
  PheromoneMatrix tau_nurse(num_shifts * num_nurses, 0.0);
  if (params.use_tau_nurse) {
    InitPheromonesNurse(tau_nurse, problem, params.tau_init);
  }

  // heuristica precomputada (invariante durante toda la ejecucion)
  std::vector<double> eta_day(num_patients * num_days,  0.0);
  std::vector<double> eta_room(num_patients * num_rooms, 0.0);
  PrecomputeHeuristics(eta_day, eta_room, problem,
                       params.rich_eta_room, params.rich_eta_day);

  Solution best_solution(problem);
  int best_cost = std::numeric_limits<int>::max();
  int stagnation_count = 0;

  // Warm-start (Fase B): producir un seed con RandomGenerator+VNS y sembrar
  // la feromona con sus decisiones para arrancar el aprendizaje informado en
  // lugar de uniforme. Si el seed sale infactible (i16/i20 muy restrictivas),
  // se cae al esquema actual (tau_init uniforme) sin tocar nada.
  //
  // B3: con adaptive_warm_start, el presupuesto del seed escala con el tiempo
  // total disponible (8% del tiempo, acotado a [30s, 180s]). Asi en
  // ejecuciones de 1200-1800s el seed dispone de hasta 3 min para producir
  // una solucion de alta calidad y sembrar feromonas mucho mas informativas.
  double warm_budget;
  if (params.adaptive_warm_start) {
    warm_budget = std::clamp(time_limit_s * 0.08, 30.0, 180.0);
  } else {
    warm_budget = std::min(30.0, time_limit_s * 0.05);
  }
  if (warm_budget >= 1.0 && remaining_s() > warm_budget + 5.0) {
    std::mt19937 seed_rng(rng());
    Solution seed = RandomGenerator::Generate(problem, seed_rng);
    LocalSearch::Run(seed, max_ls_iter, seed_rng, warm_budget,
                     /*enabled_mask=*/0xFF, params.use_alns,
                     params.vns_config);
    if (FeasibilityChecker::Check(seed).feasible) {
      int seed_cost = Evaluator::Evaluate(seed);
      if (seed_cost > 0) {
        SeedPheromones(tau_day, tau_room, tau_nurse, seed, seed_cost,
                       problem, params);
        best_solution = seed;
        best_cost     = seed_cost;
      }
    }
  }

  // Fase 2.3: contador de resets suaves consecutivos para decidir cuando
  // hacer un reset duro (a tau_init uniforme).
  int soft_reset_count = 0;

  while (remaining_s() > 2.0) {
    int iteration_best_cost = std::numeric_limits<int>::max();
    Solution iteration_best(problem);

    // Fase 2.1: q0 dinamico. Arranca en q0_initial, baja linealmente a
    // q0_final con el tiempo transcurrido. Mas exploracion temprana,
    // exploitation final.
    ACOParams ant_params = params;  // copia para modificar q0 sin tocar params
    if (params.q0_dynamic) {
      double frac = std::clamp(elapsed_s() / aco_time_budget, 0.0, 1.0);
      ant_params.q0 = params.q0_initial -
                       (params.q0_initial - params.q0_final) * frac;
    }

    // tiempo por hormiga: con paralelismo, las hormigas de un mismo batch
    // corren simultaneamente, asi que el tiempo "lineal" disponible escala
    // con el tamano del pool (ceil(n_ants/pool_size) batches secuenciales).
    int pool_size = std::max(1, params.pool_size);
    int num_batches = (params.n_ants + pool_size - 1) / pool_size;
    double time_per_batch = remaining_s() / (num_batches + 1.0);
    double ls_time = std::max(0.5, time_per_batch);

    // semillas derivadas para reproducibilidad parcial: cada hormiga
    // recibe su propia subsemilla en lugar de compartir el rng principal
    std::vector<unsigned int> ant_seeds;
    ant_seeds.reserve(params.n_ants);
    for (int k = 0; k < params.n_ants; ++k) ant_seeds.push_back(rng());

    struct AntResult {
      Solution solution;
      int cost;
      bool valid;  // si la hormiga llego a ejecutarse
    };
    std::vector<AntResult> results;
    results.reserve(params.n_ants);
    for (int k = 0; k < params.n_ants; ++k) {
      results.push_back(AntResult{Solution(problem),
                                  std::numeric_limits<int>::max(), false});
    }

    int next_ant = 0;
    while (next_ant < params.n_ants && remaining_s() > 1.0) {
      int batch_end = std::min(next_ant + pool_size, params.n_ants);
      double batch_ls_time = std::min(ls_time, remaining_s() - 0.5);
      if (batch_ls_time < 0.5) break;

      std::vector<std::thread> threads;
      threads.reserve(batch_end - next_ant);
      for (int k = next_ant; k < batch_end; ++k) {
        threads.emplace_back([&, k]() {
          std::mt19937 local_rng(ant_seeds[k]);
          // 1. construccion guiada por feromonas (lectura concurrente segura:
          //    tau y eta son const en este bloque)
          Solution candidate = ConstructSolution(tau_day, tau_room, tau_nurse,
                                                 eta_day, eta_room,
                                                 problem, ant_params,
                                                 local_rng);
          // 2. mejora con VNS (tiempo fijo por batch)
          LocalSearchStats st = LocalSearch::Run(
              candidate, max_ls_iter, local_rng, batch_ls_time,
              /*enabled_mask=*/0xFF, params.use_alns,
              params.vns_config);
          results[k].solution = std::move(candidate);
          results[k].cost     = st.final_cost;
          results[k].valid    = true;
        });
      }
      for (auto& t : threads) t.join();
      next_ant = batch_end;
    }

    // 3 + 4. recoleccion secuencial (mejor de iteracion + mejor global)
    for (int k = 0; k < params.n_ants; ++k) {
      if (!results[k].valid) continue;
      int cost = results[k].cost;

      if (cost < iteration_best_cost) {
        iteration_best_cost = cost;
        iteration_best      = results[k].solution;
      }
      if (cost < best_cost &&
          FeasibilityChecker::Check(results[k].solution).feasible) {
        best_cost     = cost;
        best_solution = results[k].solution;
        stagnation_count = 0;
      }
    }

    // 5. actualizar feromonas con la mejor solucion disponible
    //    si existe una mejor global, se usa para reforzar el aprendizaje
    if (iteration_best_cost < std::numeric_limits<int>::max()) {
      bool use_global = (best_cost < std::numeric_limits<int>::max() &&
                         best_cost <= iteration_best_cost);
      const Solution& update_sol  = use_global ? best_solution : iteration_best;
      int             update_cost = use_global ? best_cost : iteration_best_cost;
      UpdatePheromones(tau_day, tau_room, tau_nurse,
                       update_sol, update_cost, problem, params);
    }

    ++stagnation_count;

    // 6. Fase 2.3: reset suave/duro segun config.
    // Cuando hay estancamiento, primero hacer reset suave (multiplicar tau
    // por soft_reset_factor) para preservar aprendizaje parcial. Tras N
    // resets suaves consecutivos sin mejora global, hacer reset duro
    // (vuelta a tau_init).
    if (stagnation_count >= params.stagnation_k) {
      if (params.soft_reset &&
          soft_reset_count < params.soft_resets_before_hard) {
        // Reset suave: multiplicar todas las entradas factibles por factor
        double f = params.soft_reset_factor;
        for (double& t : tau_day)  if (t > 0.0) t *= f;
        for (double& t : tau_room) if (t > 0.0) t *= f;
        if (params.use_tau_nurse) {
          for (double& t : tau_nurse) if (t > 0.0) t *= f;
        }
        ++soft_reset_count;
      } else {
        // Reset duro: vuelta a tau_init uniforme
        ResetPheromones(tau_day, tau_room, tau_nurse, problem,
                         params.tau_init);
        soft_reset_count = 0;
      }
      stagnation_count = 0;
    } else if (stagnation_count == 0) {
      // Hubo mejora global en esta iteracion: resetear contador soft
      soft_reset_count = 0;
    }
  }

  // Some-touches Fase 1 - Pulido final dedicado a enfermeras.
  // Se ejecuta sobre la mejor solucion factible encontrada, dentro de un
  // presupuesto separado (params.nurse_polish_budget_s). El presupuesto
  // sale del tiempo "guardado" para esta fase (el bucle ACO usa
  // time_limit_s - polish_budget; la convencion es que el caller le pase
  // un time_limit_s descontando el polish_budget, o internamente
  // recortamos el bucle ACO. Aqui se asume que el time_limit_s original
  // YA incluye el polish_budget, por lo que el bucle puede haber
  // sobrepasado su parte; usamos cualquier remaining_s positivo o el
  // budget configurado).
  if (params.nurse_polish_budget_s > 0.0 &&
      best_cost < std::numeric_limits<int>::max()) {
    double polish_budget = params.nurse_polish_budget_s;
    int before = best_cost;
    int improvements =
        NursePolisher::Polish(best_solution, polish_budget, rng);
    int after = Evaluator::Evaluate(best_solution);
    if (after < best_cost) best_cost = after;
    std::cout << "  Nurse polish: " << before << " -> " << after
              << " (" << improvements << " mejoras)\n";
  }

  return best_solution;
}

// ============================================================================
// InitPheromones
// ============================================================================

/** @brief Inicializa las matrices de feromona.
 *  Asigna tau_init a posiciones feasibles (ventana de tiempo y compatibilidad
 *  de habitacion) y 0.0 a posiciones infeasibles para que nunca se elijan.
 */
void ACOSolver::InitPheromones(PheromoneMatrix& tau_day,
                                PheromoneMatrix& tau_room,
                                const ProblemData& problem,
                                double tau_init) {
  std::fill(tau_day.begin(),  tau_day.end(),  0.0);
  std::fill(tau_room.begin(), tau_room.end(), 0.0);

  int num_patients = problem.GetNumPatients();
  int num_days     = problem.GetNumDays();
  int num_rooms    = problem.GetNumRooms();

  for (PatientId pid = 0; pid < num_patients; ++pid) {
    const Patient& p = problem.GetPatient(pid);

    // tau_day: solo dias dentro de la ventana del paciente
    Day release = p.GetSurgeryReleaseDay();
    Day upper   = p.IsMandatory()
                  ? std::min(p.GetSurgeryDueDay(), num_days - 1)
                  : num_days - 1;
    for (Day d = release; d <= upper && d < num_days; ++d) {
      tau_day[pid * num_days + d] = tau_init;
    }

    // tau_room: solo habitaciones compatibles (HC7)
    for (RoomId r = 0; r < num_rooms; ++r) {
      if (p.IsCompatibleWithRoom(r)) {
        tau_room[pid * num_rooms + r] = tau_init;
      }
    }
  }
}

// ============================================================================
// PrecomputeHeuristics
// ============================================================================

/** @brief Precomputa la heuristica eta para dias y habitaciones.
 *  eta_day[p][d]: inversamente proporcional al coste de delay (menos retraso = mejor).
 *  eta_room[p][r]: 1.0 si la habitacion es compatible con el paciente, 0.0 si no.
 */
void ACOSolver::PrecomputeHeuristics(std::vector<double>& eta_day,
                                      std::vector<double>& eta_room,
                                      const ProblemData& problem,
                                      bool rich_eta_room,
                                      bool rich_eta_day) {
  int num_patients = problem.GetNumPatients();
  int num_days     = problem.GetNumDays();
  int num_rooms    = problem.GetNumRooms();
  int num_ots      = problem.GetNumOperatingTheaters();
  const Weights& w = problem.GetWeights();

  // B2 (rich_eta_day): pre-calcular si hay algun OT abierto en cada dia
  std::vector<bool> any_ot_open(num_days, false);
  if (rich_eta_day) {
    for (Day d = 0; d < num_days; ++d) {
      for (OperatingTheaterId ot = 0; ot < num_ots; ++ot) {
        if (problem.GetOperatingTheater(ot).IsOpenOnDay(d)) {
          any_ot_open[d] = true;
          break;
        }
      }
    }
  }

  for (PatientId pid = 0; pid < num_patients; ++pid) {
    const Patient& p = problem.GetPatient(pid);
    Day release = p.GetSurgeryReleaseDay();
    Day upper   = p.IsMandatory()
                  ? std::min(p.GetSurgeryDueDay(), num_days - 1)
                  : num_days - 1;

    // eta_day: preferir dias tempranos para minimizar coste de retraso.
    // B2: con rich_eta_day, ademas penaliza dias sin OT abierto y dias con
    // carga prevista alta del cirujano del paciente (estimacion estatica
    // basada en el numero de pacientes obligatorios del mismo cirujano).
    SurgeonId surgeon = p.GetSurgeonId();
    for (Day d = release; d <= upper && d < num_days; ++d) {
      int delay = p.GetDelayDays(d);
      double penalty = static_cast<double>(delay * w.patient_delay);

      if (rich_eta_day) {
        // dias sin OT abierto: penalty alta (no se podra operar)
        if (!any_ot_open[d]) {
          penalty += 1000.0;
        }
        // dias en que el cirujano tiene poca disponibilidad: penalty
        int max_t = problem.GetSurgeon(surgeon).GetMaxSurgeryTimeForDay(d);
        if (max_t <= 0) {
          penalty += 500.0;  // cirujano no opera ese dia
        } else if (max_t < p.GetSurgeryDuration()) {
          penalty += 200.0;  // no cabe ni vacio el cirujano
        }
      }

      eta_day[pid * num_days + d] = 1.0 / (1.0 + penalty);
    }

    // eta_room: B1 - incluir penalty por ocupantes preexistentes de la
    // habitacion (genero distinto, edad distinta) y bonus por capacidad mayor.
    // Si rich_eta_room=false, mantiene comportamiento binario {0,1}.
    Gender pgender = p.GetGender();
    AgeGroup pagegroup = p.GetAgeGroup();
    for (RoomId r = 0; r < num_rooms; ++r) {
      if (!p.IsCompatibleWithRoom(r)) {
        eta_room[pid * num_rooms + r] = 0.0;
        continue;
      }
      if (!rich_eta_room) {
        eta_room[pid * num_rooms + r] = 1.0;
        continue;
      }

      double penalty = 0.0;
      // ocupantes preexistentes de la habitacion (conocidos al inicio)
      for (const auto& occ : problem.GetOccupants()) {
        if (occ.GetRoomId() != r) continue;
        if (occ.GetGender() != pgender) {
          penalty += static_cast<double>(w.room_gender_mix);
        }
        int age_diff = std::abs(static_cast<int>(occ.GetAgeGroup()) -
                                 static_cast<int>(pagegroup));
        penalty += static_cast<double>(w.room_mixed_age) * age_diff;
      }
      // pequeño bonus a capacidad mayor (rooms grandes diluyen mejor)
      int cap = problem.GetRoom(r).GetCapacity();
      if (cap > 0) {
        penalty += 1.0 / static_cast<double>(cap);
      }

      eta_room[pid * num_rooms + r] = 1.0 / (1.0 + penalty);
    }
  }
}

// ============================================================================
// SelectByScore - regla pseudoproporcional ACS
// ============================================================================

/** @brief Selecciona un indice de la lista de scores.
 *  Con probabilidad q0 devuelve el argmax (explotacion).
 *  Con probabilidad 1-q0 muestrea proporcionalmente (exploracion).
 *  @return Indice elegido, o -1 si todos los scores son 0.
 */
int ACOSolver::SelectByScore(const std::vector<double>& scores,
                              double q0, std::mt19937& rng) {
  if (scores.empty()) return -1;

  int best_idx = 0;
  double best_val = scores[0];
  double total    = scores[0];

  for (int i = 1; i < static_cast<int>(scores.size()); ++i) {
    total += scores[i];
    if (scores[i] > best_val) {
      best_val = scores[i];
      best_idx = i;
    }
  }

  if (total <= 0.0) return -1;  // sin candidatos validos

  std::uniform_real_distribution<double> uni(0.0, 1.0);
  if (uni(rng) < q0) return best_idx;  // explotacion: argmax

  // exploracion: seleccion proporcional por ruleta
  double threshold = std::uniform_real_distribution<double>(0.0, total)(rng);
  double cumsum = 0.0;
  for (int i = 0; i < static_cast<int>(scores.size()); ++i) {
    cumsum += scores[i];
    if (cumsum >= threshold) return i;
  }
  return best_idx;  // fallback por precision numerica
}

// ============================================================================
// ConstructSolution - construccion guiada por feromonas
// ============================================================================

/** @brief Construye una solucion siguiendo las feromonas y la heuristica.
 *  Orden de asignacion: obligatorios por urgencia (slack ascendente),
 *  luego opcionales en orden aleatorio.
 *  Para cada paciente: elige dia y habitacion por scores ACO, OT de forma greedy.
 *  Si no encuentra posicion feasible para un obligatorio, activa la reparacion.
 */
Solution ACOSolver::ConstructSolution(const PheromoneMatrix& tau_day,
                                       const PheromoneMatrix& tau_room,
                                       const PheromoneMatrix& tau_nurse,
                                       const std::vector<double>& eta_day,
                                       const std::vector<double>& eta_room,
                                       const ProblemData& problem,
                                       const ACOParams& params,
                                       std::mt19937& rng) {
  Solution sol(problem);
  int num_days     = problem.GetNumDays();
  int num_rooms    = problem.GetNumRooms();
  int num_ots      = problem.GetNumOperatingTheaters();

  // orden: obligatorios por slack ascendente, opcionales mezclados
  auto mandatory_ids = problem.GetMandatoryPatientIds();
  std::sort(mandatory_ids.begin(), mandatory_ids.end(),
            [&problem](PatientId a, PatientId b) {
              const Patient& pa = problem.GetPatient(a);
              const Patient& pb = problem.GetPatient(b);
              int slack_a = pa.GetSurgeryDueDay() - pa.GetSurgeryReleaseDay();
              int slack_b = pb.GetSurgeryDueDay() - pb.GetSurgeryReleaseDay();
              return slack_a < slack_b;
            });

  auto optional_ids = problem.GetOptionalPatientIds();
  std::shuffle(optional_ids.begin(), optional_ids.end(), rng);

  // --- funcion interna: calcula OTs factibles para un dia, ordenados por carga ---
  // prioriza OTs ya en uso (minimiza open_ot) sin violar capacidad
  auto get_ots_for_day = [&](Day day) {
    std::vector<OperatingTheaterId> ots;
    for (OperatingTheaterId ot = 0; ot < num_ots; ++ot) {
      if (problem.GetOperatingTheater(ot).IsOpenOnDay(day)) {
        ots.push_back(ot);
      }
    }
    // ordenar: primero OTs con carga > 0 (ya abiertos), luego los vacios
    std::sort(ots.begin(), ots.end(),
              [&sol, day](OperatingTheaterId a, OperatingTheaterId b) {
                int la = sol.GetOTLoad(a, day);
                int lb = sol.GetOTLoad(b, day);
                if ((la > 0) != (lb > 0)) return la > 0;
                return la > lb;  // entre usados, preferir el mas cargado (concentrar)
              });
    return ots;
  };

  // --- funcion interna: intenta asignar pid en un dia dado, habiendo elegido room ---
  // prueba OTs en orden de carga; devuelve true si lo consigue
  auto try_assign_day_room = [&](PatientId pid, Day day, RoomId room) -> bool {
    auto ots = get_ots_for_day(day);
    for (OperatingTheaterId ot : ots) {
      if (FeasibilityChecker::IsFeasiblePatientAssignment(sol, pid, room, day, ot)) {
        sol.AssignPatient(pid, room, day, ot);
        return true;
      }
    }
    return false;
  };

  // --- funcion principal de asignacion por ACO ---
  auto assign_one = [&](PatientId pid) -> bool {
    const Patient& p = problem.GetPatient(pid);
    Day release = p.GetSurgeryReleaseDay();
    Day upper   = p.IsMandatory()
                  ? std::min(p.GetSurgeryDueDay(), num_days - 1)
                  : num_days - 1;

    // --- 1. construir candidatos de dia con sus scores ACO ---
    std::vector<Day>    cand_days;
    std::vector<double> day_scores;
    cand_days.reserve(upper - release + 1);
    day_scores.reserve(upper - release + 1);

    for (Day d = release; d <= upper && d < num_days; ++d) {
      double tau = tau_day[pid * num_days + d];
      double eta = eta_day[pid * num_days + d];
      if (tau <= 0.0 || eta <= 0.0) continue;
      // descartar dias sin ningun OT abierto
      bool any_ot = false;
      for (OperatingTheaterId ot = 0; ot < num_ots && !any_ot; ++ot) {
        if (problem.GetOperatingTheater(ot).IsOpenOnDay(d)) any_ot = true;
      }
      if (!any_ot) continue;
      cand_days.push_back(d);
      day_scores.push_back(std::pow(tau, params.alpha) *
                            std::pow(eta, params.beta));
    }

    if (cand_days.empty()) return false;

    // --- 2. construir candidatos de habitacion con sus scores ACO ---
    std::vector<RoomId> cand_rooms;
    std::vector<double> room_scores;
    cand_rooms.reserve(num_rooms);
    room_scores.reserve(num_rooms);

    for (RoomId r = 0; r < num_rooms; ++r) {
      double tau = tau_room[pid * num_rooms + r];
      double eta = eta_room[pid * num_rooms + r];
      if (tau <= 0.0 || eta <= 0.0) continue;
      cand_rooms.push_back(r);
      room_scores.push_back(std::pow(tau, params.alpha) *
                             std::pow(eta, params.beta));
    }

    if (cand_rooms.empty()) return false;

    // --- 3. primer intento: dia y habitacion elegidos por ACO ---
    int day_idx  = SelectByScore(day_scores,  params.q0, rng);
    int room_idx = SelectByScore(room_scores, params.q0, rng);
    if (day_idx < 0 || room_idx < 0) return false;

    Day    chosen_day  = cand_days[day_idx];
    RoomId chosen_room = cand_rooms[room_idx];

    if (try_assign_day_room(pid, chosen_day, chosen_room)) return true;

    // --- 4. fallback: otras habitaciones para el mismo dia elegido ---
    // mezclar para no favorecer siempre el mismo orden
    std::vector<int> room_perm(cand_rooms.size());
    std::iota(room_perm.begin(), room_perm.end(), 0);
    std::shuffle(room_perm.begin(), room_perm.end(), rng);

    for (int ri : room_perm) {
      if (ri == room_idx) continue;
      if (try_assign_day_room(pid, chosen_day, cand_rooms[ri])) return true;
    }

    // --- 5. fallback: otros dias en orden de score descendente ---
    std::vector<int> day_perm(cand_days.size());
    std::iota(day_perm.begin(), day_perm.end(), 0);
    std::sort(day_perm.begin(), day_perm.end(),
              [&day_scores](int a, int b) {
                return day_scores[a] > day_scores[b];
              });

    for (int di : day_perm) {
      if (di == day_idx) continue;
      Day alt_day = cand_days[di];
      for (int ri : room_perm) {
        if (try_assign_day_room(pid, alt_day, cand_rooms[ri])) return true;
      }
    }

    return false;
  };

  // === asignacion de pacientes obligatorios ===
  for (PatientId pid : mandatory_ids) {
    if (!assign_one(pid)) {
      // reparacion: intentar sin guia de feromona
      if (!RandomGenerator::TryAssignPatientFeasibly(sol, pid, problem, rng)) {
        RandomGenerator::ForceAssignMandatory(sol, pid, problem, rng);
      }
    }
  }

  // === asignacion de pacientes opcionales ===
  for (PatientId pid : optional_ids) {
    if (!sol.IsPatientScheduled(pid)) {
      assign_one(pid);
    }
  }

  // === asignacion de enfermeras ===
  // C1: si use_tau_nurse, usar la variante ACO que combina tau_nurse y
  // eta_nurse heuristica. Si no, la greedy clasica (legacy).
  if (params.use_tau_nurse && !tau_nurse.empty()) {
    GenerateNurseAssignmentsACO(sol, tau_nurse, problem, params, rng);
  } else {
    RandomGenerator::GenerateNurseAssignments(sol, problem, rng);
  }

  return sol;
}

// ============================================================================
// UpdatePheromones - MMAS
// ============================================================================

/** @brief Actualiza las matrices de feromona segun MMAS.
 *  Evapara todas las feromonas y deposita en las componentes de la mejor solucion.
 *  Aplica cotas [tau_min, tau_max] para evitar convergencia prematura.
 *  @param best_cost Coste de la solucion que deposita feromona (> 0).
 */
void ACOSolver::UpdatePheromones(PheromoneMatrix& tau_day,
                                  PheromoneMatrix& tau_room,
                                  PheromoneMatrix& tau_nurse,
                                  const Solution& best_sol,
                                  int best_cost,
                                  const ProblemData& problem,
                                  const ACOParams& params) {
  if (best_cost <= 0) return;

  int num_patients = problem.GetNumPatients();
  int num_days     = problem.GetNumDays();
  int num_rooms    = problem.GetNumRooms();
  int num_shifts   = problem.GetNumShiftTypes();
  int num_nurses   = problem.GetNumNurses();

  // cotas MMAS: tau_max basado en el mejor coste conocido.
  // Fase 2.2: tau_min_factor controla el ratio tau_min/tau_max. Mayor =
  // mas contraste = mas exploracion. Default Fase 2: 50 (antes implicito 2).
  double tau_max = 1.0 / (params.rho * static_cast<double>(best_cost));
  int tmf = std::max(1, params.tau_min_factor);
  double tau_min = tau_max /
      (static_cast<double>(tmf) * static_cast<double>(num_patients));

  // 1. evaporacion global
  double evap = 1.0 - params.rho;
  for (double& t : tau_day)  t *= evap;
  for (double& t : tau_room) t *= evap;
  if (params.use_tau_nurse) {
    for (double& t : tau_nurse) t *= evap;
  }

  // 2. deposito: delta inversamente proporcional al coste
  double delta = 1.0 / static_cast<double>(best_cost);
  for (PatientId pid : best_sol.GetScheduledPatients()) {
    Day    d = best_sol.GetPatientAdmissionDay(pid);
    RoomId r = best_sol.GetPatientRoom(pid);
    tau_day[pid * num_days + d]   += delta;
    tau_room[pid * num_rooms + r] += delta;
  }
  // C1: deposito tau_nurse - para cada (room, day, shift) con enfermera
  // asignada en la mejor solucion, refuerza tau_nurse[shift][nurse]
  if (params.use_tau_nurse && !tau_nurse.empty()) {
    for (RoomId r = 0; r < num_rooms; ++r) {
      for (Day d = 0; d < num_days; ++d) {
        if (best_sol.GetRoomOccupancy(r, d) == 0) continue;
        for (Shift s = 0; s < num_shifts; ++s) {
          NurseId n = best_sol.GetNurseAssignment(r, d, s);
          if (n != kInvalidId) {
            tau_nurse[s * num_nurses + n] += delta;
          }
        }
      }
    }
  }

  // 3. cotas [tau_min, tau_max] solo sobre posiciones feasibles
  for (PatientId pid = 0; pid < num_patients; ++pid) {
    const Patient& p = problem.GetPatient(pid);

    Day release = p.GetSurgeryReleaseDay();
    Day upper   = p.IsMandatory()
                  ? std::min(p.GetSurgeryDueDay(), num_days - 1)
                  : num_days - 1;
    for (Day d = release; d <= upper && d < num_days; ++d) {
      double& t = tau_day[pid * num_days + d];
      if (t > 0.0) t = std::max(tau_min, std::min(tau_max, t));
    }

    for (RoomId r = 0; r < num_rooms; ++r) {
      if (p.IsCompatibleWithRoom(r)) {
        double& t = tau_room[pid * num_rooms + r];
        if (t > 0.0) t = std::max(tau_min, std::min(tau_max, t));
      }
    }
  }

  // cotas para tau_nurse
  if (params.use_tau_nurse && !tau_nurse.empty()) {
    for (double& t : tau_nurse) {
      if (t > 0.0) t = std::max(tau_min, std::min(tau_max, t));
    }
  }
}

// ============================================================================
// ResetPheromones
// ============================================================================

/** @brief Reinicializa todas las feromonas a tau_init.
 *  Se llama cuando se detecta estancamiento (stagnation_k iters sin mejora).
 */
void ACOSolver::ResetPheromones(PheromoneMatrix& tau_day,
                                 PheromoneMatrix& tau_room,
                                 PheromoneMatrix& tau_nurse,
                                 const ProblemData& problem,
                                 double tau_init) {
  InitPheromones(tau_day, tau_room, problem, tau_init);
  if (!tau_nurse.empty()) {
    InitPheromonesNurse(tau_nurse, problem, tau_init);
  }
}

// ============================================================================
// InitPheromonesNurse (C1)
// ============================================================================

/** @brief Inicializa tau_nurse[shift_type][nurse] a tau_init en las
 *         (s, n) donde la enfermera trabaja en al menos un dia con ese shift.
 *  Las (s, n) infeasibles (la enfermera nunca trabaja ese shift) quedan en 0
 *  para que nunca se elijan via score = tau^a * eta^b (eta tambien sera 0).
 */
void ACOSolver::InitPheromonesNurse(PheromoneMatrix& tau_nurse,
                                     const ProblemData& problem,
                                     double tau_init) {
  std::fill(tau_nurse.begin(), tau_nurse.end(), 0.0);
  int num_shifts = problem.GetNumShiftTypes();
  int num_days   = problem.GetNumDays();
  int num_nurses = problem.GetNumNurses();
  for (NurseId n = 0; n < num_nurses; ++n) {
    const Nurse& nurse = problem.GetNurse(n);
    for (Shift s = 0; s < num_shifts; ++s) {
      // si la enfermera trabaja ese shift en algun dia, tau_init; si no, 0.
      bool works = false;
      for (Day d = 0; d < num_days && !works; ++d) {
        if (nurse.IsAvailable(d, s)) works = true;
      }
      if (works) {
        tau_nurse[s * num_nurses + n] = tau_init;
      }
    }
  }
}

// ============================================================================
// SeedPheromones
// ============================================================================

/** @brief Sembra feromona alta en las decisiones de una solucion semilla y
 *         baja en el resto de posiciones feasibles, creando el contraste
 *         maximo permitido por las cotas MMAS.
 *  @param seed       Solucion factible producida por RandomGenerator+VNS.
 *  @param seed_cost  Coste blando de la solucion (positivo).
 *
 *  No se usa la solucion como hormiga de la primera iteracion para evitar
 *  que su coste domine el `iteration_best` y la feromona converja
 *  prematuramente. Solo sesga la inicializacion.
 */
void ACOSolver::SeedPheromones(PheromoneMatrix& tau_day,
                                PheromoneMatrix& tau_room,
                                PheromoneMatrix& tau_nurse,
                                const Solution& seed, int seed_cost,
                                const ProblemData& problem,
                                const ACOParams& params) {
  if (seed_cost <= 0) return;

  int num_patients = problem.GetNumPatients();
  int num_days     = problem.GetNumDays();
  int num_rooms    = problem.GetNumRooms();
  int num_shifts   = problem.GetNumShiftTypes();
  int num_nurses   = problem.GetNumNurses();

  // mismas cotas que en UpdatePheromones para mantener la coherencia MMAS
  double tau_max = 1.0 / (params.rho * static_cast<double>(seed_cost));
  // Fase 2.2: tau_min_factor controla el ratio. Coherente con UpdatePheromones.
  int tmf = std::max(1, params.tau_min_factor);
  double tau_min = tau_max /
      (static_cast<double>(tmf) * static_cast<double>(num_patients));
  // Fase 2.4: seed_dampen. Si activo, las decisiones del seed reciben
  // dampen_factor * tau_min en lugar de tau_max. Reduce la dominancia del
  // seed sobre las primeras hormigas (clonado quasi-determinista).
  double seed_value = params.seed_dampen
      ? params.seed_dampen_factor * tau_min
      : tau_max;

  // todas las posiciones feasibles arrancan en tau_min (el resto sigue en 0.0
  // tras InitPheromones porque son infeasibles y deben permanecer asi)
  for (PatientId pid = 0; pid < num_patients; ++pid) {
    const Patient& p = problem.GetPatient(pid);

    Day release = p.GetSurgeryReleaseDay();
    Day upper   = p.IsMandatory()
                  ? std::min(p.GetSurgeryDueDay(), num_days - 1)
                  : num_days - 1;
    for (Day d = release; d <= upper && d < num_days; ++d) {
      double& t = tau_day[pid * num_days + d];
      if (t > 0.0) t = tau_min;
    }
    for (RoomId r = 0; r < num_rooms; ++r) {
      if (p.IsCompatibleWithRoom(r)) {
        double& t = tau_room[pid * num_rooms + r];
        if (t > 0.0) t = tau_min;
      }
    }
  }

  // las decisiones del seed se elevan a seed_value
  // (Fase 2.4: seed_value = 3*tau_min si seed_dampen, sino tau_max).
  for (PatientId pid : seed.GetScheduledPatients()) {
    Day    d = seed.GetPatientAdmissionDay(pid);
    RoomId r = seed.GetPatientRoom(pid);
    tau_day[pid * num_days + d]   = seed_value;
    tau_room[pid * num_rooms + r] = seed_value;
  }

  // C1: tau_nurse - bajar todo a tau_min y elevar las nurses del seed.
  if (params.use_tau_nurse && !tau_nurse.empty()) {
    for (double& t : tau_nurse) {
      if (t > 0.0) t = tau_min;
    }
    for (RoomId r = 0; r < num_rooms; ++r) {
      for (Day d = 0; d < num_days; ++d) {
        if (seed.GetRoomOccupancy(r, d) == 0) continue;
        for (Shift s = 0; s < num_shifts; ++s) {
          NurseId n = seed.GetNurseAssignment(r, d, s);
          if (n != kInvalidId) {
            tau_nurse[s * num_nurses + n] = seed_value;
          }
        }
      }
    }
  }
}

// ============================================================================
// GenerateNurseAssignmentsACO (C1) — asignacion de enfermeras guiada por ACO
// ============================================================================

/** @brief Asigna enfermeras a (room, day, shift) con pacientes/ocupantes
 *         usando tau_nurse[shift][nurse] como feromona aprendida y una
 *         eta_nurse dinamica que penaliza skill insuficiente y sobrecarga,
 *         y bonifica continuidad con el dia anterior.
 *
 *  La heuristica eta_nurse se calcula on-the-fly por (room, day, shift)
 *  porque depende del estado de la solucion en construccion (workload
 *  acumulado, asignaciones previas). Para mantener determinismo dentro de
 *  una hormiga, recorremos (room, day) en orden lexicografico y dentro de
 *  cada room los dias en orden ascendente — asi day-1 ya esta resuelto
 *  cuando llegamos a day, lo que permite usar continuidad.
 *
 *  Score (room, day, shift, nurse) = pow(tau, alpha) * pow(eta, beta).
 *  Seleccion via SelectByScore (ACS pseudoproporcional con q0).
 *
 *  Si no hay candidatos validos (sin enfermera disponible ese turno), la
 *  celda queda sin asignar; el bug HC13/14 se evita con
 *  EnsureFullNurseCoverage como respaldo.
 */
void ACOSolver::GenerateNurseAssignmentsACO(Solution& solution,
                                             const PheromoneMatrix& tau_nurse,
                                             const ProblemData& problem,
                                             const ACOParams& params,
                                             std::mt19937& rng) {
  int num_rooms  = problem.GetNumRooms();
  int num_days   = problem.GetNumDays();
  int num_shifts = problem.GetNumShiftTypes();
  int num_nurses = problem.GetNumNurses();

  for (RoomId r = 0; r < num_rooms; ++r) {
    for (Day d = 0; d < num_days; ++d) {
      if (solution.GetRoomOccupancy(r, d) == 0) continue;

      for (Shift s = 0; s < num_shifts; ++s) {
        // si ya hay enfermera asignada por una hormiga anterior, no tocar
        // (solo aplica en flujos donde se reutilice una solucion semilla)
        if (solution.GetNurseAssignment(r, d, s) != kInvalidId) continue;

        // candidatos: enfermeras disponibles y compatibles
        std::vector<NurseId> candidates;
        std::vector<double>  scores;
        candidates.reserve(num_nurses);
        scores.reserve(num_nurses);

        NurseId prev_nurse = (d > 0) ? solution.GetNurseAssignment(r, d - 1, s)
                                      : kInvalidId;
        const auto& room_pats = solution.GetRoomPatients(r, d);

        for (NurseId n = 0; n < num_nurses; ++n) {
          double tau = tau_nurse[s * num_nurses + n];
          if (tau <= 0.0) continue;  // (s, n) infeasible aprendido en init
          if (!FeasibilityChecker::IsFeasibleNurseAssignment(solution, n, r,
                                                             d, s)) {
            continue;
          }

          // eta_nurse: heuristica dinamica
          // - skill insuficiente: penalty alta (cuanto mas deficit, peor)
          // - sobrecarga prevista: penalty proporcional al exceso
          // - continuidad con d-1: bonus si es la misma enfermera
          double penalty = 0.0;
          const Nurse& nurse = problem.GetNurse(n);
          SkillLevel nurse_skill = nurse.GetSkillLevel();

          for (PatientId pid : room_pats) {
            Day adm = solution.GetPatientAdmissionDay(pid);
            int day_in_stay = d - adm;
            SkillLevel req =
                problem.GetPatient(pid).GetSkillLevelAt(day_in_stay, s);
            if (nurse_skill < req) penalty += (req - nurse_skill) * 10.0;
          }
          for (const auto& occ : problem.GetOccupants()) {
            if (occ.GetRoomId() == r && occ.IsPresentOnDay(d)) {
              SkillLevel req = occ.GetSkillLevelAt(d, s);
              if (nurse_skill < req) penalty += (req - nurse_skill) * 10.0;
            }
          }

          // sobrecarga (current_workload del turno + carga que añadiriamos)
          int current_workload = solution.GetNurseWorkload(n, d, s);
          int added_workload = 0;
          for (PatientId pid : room_pats) {
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
          int max_load = nurse.GetMaxWorkload(d, s);
          if (current_workload + added_workload > max_load) {
            penalty += (current_workload + added_workload - max_load) * 1.0;
          }

          // continuidad: bonus si misma nurse que d-1
          double bonus = 0.0;
          if (n == prev_nurse && prev_nurse != kInvalidId) bonus = 5.0;

          double eta = (1.0 + bonus) / (1.0 + penalty);
          double score = std::pow(tau, params.alpha) *
                          std::pow(eta, params.beta);
          candidates.push_back(n);
          scores.push_back(score);
        }

        if (candidates.empty()) continue;  // EnsureFullNurseCoverage hara fallback

        int idx = SelectByScore(scores, params.q0, rng);
        if (idx >= 0 && idx < static_cast<int>(candidates.size())) {
          solution.AssignNurse(candidates[idx], r, d, s);
        }
      }
    }
  }
}
