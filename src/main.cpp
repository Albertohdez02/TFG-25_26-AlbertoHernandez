// main.cpp - Solver IHTC 2024 con ACO+VNS o multi-start greedy+ILS
// TFG Alberto Hernandez - 4o Informatica
//
// Pipeline:
//   1. Parsear instancia del JSON
//   2a. [modo aco]    ACOSolver::Run - bucle ACO+VNS con aprendizaje de feromona
//   2b. [modo random] Multi-start: generar N soluciones greedy, mejorar con ILS
//   3. Evaluar y mostrar desglose final
//   4. Exportar solucion a JSON
//
// Uso: ./ihtc_solver <instancia.json> [seed] [max_iter] [restarts] [time_s] [mode] [n_ants]
//   - seed:     semilla aleatoria (default: 42)
//   - max_iter: iteraciones maximas de busqueda local (default: 5000)
//   - restarts: reinicios multi-start, solo en modo random (default: 100)
//   - time_s:   tiempo global maximo en segundos (default: 600)
//   - mode:     "aco" (default) o "random"
//   - n_ants:   hormigas por iteracion (default: 12, solo modo aco)

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>

#include "common/types.h"
#include "entities/ProblemData.h"
#include "evaluator/Evaluator.h"
#include "evaluator/FeasibilityChecker.h"
#include "io/ProblemParser.h"
#include "io/SolutionIO.h"
#include "solution/Solution.h"
#include "solver/ACOSolver.h"
#include "solver/LocalSearch.h"
#include "solver/RandomGenerator.h"

/** @brief Sobrescribe un double desde una variable de entorno si esta definida.
 *  Si la variable NO esta definida, el valor por defecto se conserva intacto =>
 *  comportamiento identico al baseline cuando no se exporta ninguna.
 */
static void EnvOverrideDouble(const char* name, double& target) {
  const char* v = std::getenv(name);
  if (v != nullptr && v[0] != '\0') target = std::atof(v);
}
/** @brief Sobrescribe un int desde una variable de entorno si esta definida. */
static void EnvOverrideInt(const char* name, int& target) {
  const char* v = std::getenv(name);
  if (v != nullptr && v[0] != '\0') target = std::atoi(v);
}
/** @brief Sobrescribe un bool desde una variable de entorno si esta definida. */
static void EnvOverrideBool(const char* name, bool& target) {
  const char* v = std::getenv(name);
  if (v != nullptr && v[0] != '\0') target = (std::atoi(v) != 0);
}

/** @brief Aplica overrides de los parametros ACO sweepables desde el entorno. */
static void ApplyACOEnvOverrides(ACOParams& p) {
  EnvOverrideInt("IHTC_N_ANTS", p.n_ants);
  EnvOverrideDouble("IHTC_ALPHA", p.alpha);
  EnvOverrideDouble("IHTC_BETA", p.beta);
  EnvOverrideDouble("IHTC_RHO", p.rho);
  EnvOverrideDouble("IHTC_Q0", p.q0);
  EnvOverrideInt("IHTC_TAU_MIN_FACTOR", p.tau_min_factor);
  EnvOverrideInt("IHTC_STAGNATION_K", p.stagnation_k);
  EnvOverrideDouble("IHTC_POLISH_BUDGET", p.nurse_polish_budget_s);
  // Fase 2 anti-convergencia (opt-in): activables por entorno
  EnvOverrideBool("IHTC_Q0_DYNAMIC", p.q0_dynamic);
  EnvOverrideDouble("IHTC_Q0_INITIAL", p.q0_initial);
  EnvOverrideDouble("IHTC_Q0_FINAL", p.q0_final);
  EnvOverrideBool("IHTC_SOFT_RESET", p.soft_reset);
  EnvOverrideBool("IHTC_SEED_DAMPEN", p.seed_dampen);
  EnvOverrideDouble("IHTC_SEED_DAMPEN_FACTOR", p.seed_dampen_factor);
}

/** @brief Aplica overrides de los parametros VNS/ILS sweepables desde el entorno. */
static void ApplyVNSEnvOverrides(VNSConfig& c) {
  EnvOverrideInt("IHTC_VNS_MAX_PATIENTS", c.max_patients_per_op);
  EnvOverrideBool("IHTC_VNS_EXHAUSTIVE_OPT", c.exhaustive_optional);
  EnvOverrideInt("IHTC_VNS_INSERTIONS", c.max_insertions_per_optional);
  EnvOverrideInt("IHTC_VNS_PERTURB_BASE", c.perturb_strength_base);
  EnvOverrideInt("IHTC_VNS_PERTURB_MAX", c.perturb_strength_max);
  EnvOverrideDouble("IHTC_VNS_PERTURB_FACTOR", c.perturb_strength_factor);
  EnvOverrideInt("IHTC_VNS_SWAP_PAIRS", c.max_pairs_swap);
  EnvOverrideInt("IHTC_VNS_RELOCATE", c.max_combos_relocate);
  EnvOverrideInt("IHTC_VNS_NURSE_POS", c.max_positions_nurse);
  EnvOverrideInt("IHTC_VNS_REFRESH_EVERY", c.nurse_refresh_every);
  EnvOverrideDouble("IHTC_VNS_REFRESH_TOL", c.nurse_refresh_tolerance_pct);
  EnvOverrideBool("IHTC_VNS_REFRESH", c.refresh_nurses);
  EnvOverrideBool("IHTC_VNS_COMPOUND", c.enable_compound);
}

/** @brief Imprime un separador con titulo enmarcado entre lineas de '='. */
void PrintSeparator(const std::string& title) {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "  " << title << "\n";
  std::cout << std::string(60, '=') << "\n";
}

/** @brief Imprime la tabla de ocupacion de habitaciones (Room x Day).
 *  Marca con '!' las celdas donde la ocupacion supera la capacidad.
 */
void PrintRoomOccupancy(const Solution& sol, const ProblemData& problem) {
  std::cout << "\nOcupacion de habitaciones (Room x Day):\n";
  std::cout << std::setw(10) << "Room";
  for (int d = 0; d < problem.GetNumDays(); ++d) {
    std::cout << std::setw(6) << ("D" + std::to_string(d));
  }
  std::cout << "\n" << std::string(10 + 6 * problem.GetNumDays(), '-') << "\n";

  for (RoomId r = 0; r < problem.GetNumRooms(); ++r) {
    std::cout << std::setw(10) << problem.GetRoom(r).GetId();
    for (Day d = 0; d < problem.GetNumDays(); ++d) {
      int occ = sol.GetRoomOccupancy(r, d);
      int cap = problem.GetRoom(r).GetCapacity();
      std::string cell = std::to_string(occ) + "/" + std::to_string(cap);
      if (occ > cap) cell += "!";
      std::cout << std::setw(6) << cell;
    }
    std::cout << "\n";
  }
}

/** @brief Punto de entrada: parsea CLI, ejecuta el solver y exporta la solucion. */
int main(int argc, char* argv[]) {
  std::cout << "  IHTC 2024 Solver - ACO+VNS / Generacion Aleatoria+ILS\n\n";

  // Parsear argumentos
  std::string instance_file;
  unsigned int seed = 42;
  int max_iterations = 5000;
  int num_restarts = 100;          // solo en modo random; ACO ignora este parametro
  double global_time_s = 600.0;   // 10 min (requisito de competicion)
  std::string mode = "aco";        // "aco" o "random"
  int n_ants = -1;                 // <0 = usar default de ACOParams

  if (argc < 2) {
    std::cerr << "Uso: " << argv[0]
              << " <instancia.json> [seed] [max_iter] [restarts] [time_s] [mode] [n_ants]\n";
    std::cerr << "  mode: \"aco\" (default) o \"random\"\n";
    std::cerr << "  n_ants: hormigas por iteracion (default 12, solo modo aco)\n";
    return 1;
  }

  instance_file = argv[1];
  if (argc >= 3)  seed           = static_cast<unsigned int>(std::atoi(argv[2]));
  if (argc >= 4)  max_iterations = std::atoi(argv[3]);
  if (argc >= 5)  num_restarts   = std::atoi(argv[4]);
  if (argc >= 6)  global_time_s  = std::atof(argv[5]);
  if (argc >= 7)  mode           = argv[6];
  if (argc >= 8)  n_ants         = std::atoi(argv[7]);

  // Config VNS por defecto (Bloque A+B+C activos). Overrides por entorno
  // (no-op si no se exporta ninguna var); se aplican sobre el default, asi
  // afectan tanto a modo aco como random.
  VNSConfig vns_cfg;
  ApplyVNSEnvOverrides(vns_cfg);

  std::mt19937 rng(seed);

  // PASO 1: Cargar el problema
  PrintSeparator("1. Cargando instancia desde JSON");

  ProblemData problem;
  try {
    problem = ProblemParser::Parse(instance_file);
    std::cout << "Instancia cargada: " << instance_file << "\n";
  } catch (const std::exception& e) {
    std::cerr << "Error al leer la instancia: " << e.what() << "\n";
    return 1;
  }

  std::cout << "  Dias: " << problem.GetNumDays() << "\n";
  std::cout << "  Habitaciones: " << problem.GetNumRooms() << "\n";
  std::cout << "  Cirujanos: " << problem.GetNumSurgeons() << "\n";
  std::cout << "  Quirofanos: " << problem.GetNumOperatingTheaters() << "\n";
  std::cout << "  Enfermeras: " << problem.GetNumNurses() << "\n";
  std::cout << "  Ocupantes: " << problem.GetNumOccupants() << "\n";
  std::cout << "  Pacientes: " << problem.GetNumPatients()
            << " (mand=" << problem.GetNumMandatoryPatients()
            << ", opt=" << problem.GetNumOptionalPatients() << ")\n";
  std::cout << "  Seed: " << seed << "\n";
  std::cout << "  Max iteraciones LS: " << max_iterations << "\n";
  std::cout << "  Modo: " << mode << "\n";
  if (mode == "random") {
    std::cout << "  Reinicios multi-start: " << num_restarts << "\n";
  }
  std::cout << "  Tiempo global maximo: " << global_time_s << " s\n";


  // PASO 2: Busqueda de la mejor solucion (ACO+VNS o multi-start greedy+ILS)
  PrintSeparator("2. Optimizacion: " + mode);

  auto t0 = std::chrono::steady_clock::now();
  auto elapsed_s = [&]() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();
  };
  auto remaining_s = [&]() { return global_time_s - elapsed_s(); };

  Solution best_solution(problem);
  int best_cost = std::numeric_limits<int>::max();

  if (mode == "aco") {
    ACOParams aco_params;  // parametros por defecto (Bloque A+B+C activos)
    if (n_ants > 0) aco_params.n_ants = n_ants;
    aco_params.vns_config = vns_cfg;
    // Overrides de tuning por entorno (no-op si no se exporta ninguna var).
    ApplyACOEnvOverrides(aco_params);
    std::cout << "  Hormigas por iteracion: " << aco_params.n_ants << "\n";
    std::cout << "  Params ACO: alpha=" << aco_params.alpha
              << " beta=" << aco_params.beta << " rho=" << aco_params.rho
              << " q0=" << aco_params.q0
              << " tau_min_factor=" << aco_params.tau_min_factor << "\n";
    best_solution = ACOSolver::Run(problem, rng, max_iterations,
                                   global_time_s, aco_params);
    if (best_solution.GetNumScheduledPatients() > 0) {
      best_cost = Evaluator::Evaluate(best_solution);
    }

  } else {
    int total_improvements = 0;
    std::vector<int>    init_costs;
    std::vector<int>    final_costs;
    std::vector<int>    improvement_counts;
    std::vector<double> times;

    for (int restart = 0; restart < num_restarts && remaining_s() > 2.0; ++restart) {
      int restarts_left = num_restarts - restart;
      double time_for_restart = remaining_s() / restarts_left;

      Solution candidate = RandomGenerator::Generate(problem, rng);
      int init_cost = Evaluator::Evaluate(candidate);

      LocalSearchStats ls_stats =
          LocalSearch::Run(candidate, max_iterations, rng, time_for_restart,
                            /*enabled_mask=*/0xFF, vns_cfg);
      total_improvements += ls_stats.improvements;

      init_costs.push_back(init_cost);
      final_costs.push_back(ls_stats.final_cost);
      improvement_counts.push_back(ls_stats.improvements);
      times.push_back(ls_stats.elapsed_seconds);

      if (ls_stats.final_cost < best_cost &&
          FeasibilityChecker::Check(candidate).feasible) {
        best_cost     = ls_stats.final_cost;
        best_solution = std::move(candidate);
      }

      std::cout << "  Restart " << (restart + 1) << "/" << num_restarts
                << ": init=" << init_cost << " -> final=" << ls_stats.final_cost
                << " (" << ls_stats.improvements << " mejoras, "
                << std::fixed << std::setprecision(2)
                << ls_stats.elapsed_seconds << "s)\n";
    }

    // tabla comparativa modo random
    PrintSeparator("Comparativa greedy vs ILS");

    int num_done = static_cast<int>(init_costs.size());
    std::cout << std::setw(10) << "Restart"
              << std::setw(12) << "Inicial"
              << std::setw(12) << "Final"
              << std::setw(12) << "Reduccion"
              << std::setw(10) << "Mejora%"
              << std::setw(10) << "Mejoras"
              << std::setw(10) << "Tiempo"
              << "\n";
    std::cout << std::string(76, '-') << "\n";

    int sum_init = 0, sum_final = 0;
    for (int i = 0; i < num_done; ++i) {
      int reduccion = init_costs[i] - final_costs[i];
      double pct = init_costs[i] > 0
          ? 100.0 * reduccion / init_costs[i] : 0.0;
      sum_init  += init_costs[i];
      sum_final += final_costs[i];

      std::cout << std::setw(10) << (i + 1)
                << std::setw(12) << init_costs[i]
                << std::setw(12) << final_costs[i]
                << std::setw(12) << reduccion
                << std::setw(9)  << std::fixed << std::setprecision(1) << pct << "%"
                << std::setw(10) << improvement_counts[i]
                << std::setw(9)  << std::fixed << std::setprecision(2) << times[i] << "s"
                << "\n";
    }

    if (num_done > 0) {
      std::cout << std::string(76, '-') << "\n";
      double avg_init  = static_cast<double>(sum_init)  / num_done;
      double avg_final = static_cast<double>(sum_final) / num_done;
      double avg_pct   = avg_init > 0
          ? 100.0 * (avg_init - avg_final) / avg_init : 0.0;
      std::cout << std::setw(10) << "Media"
                << std::setw(12) << std::fixed << std::setprecision(0) << avg_init
                << std::setw(12) << std::fixed << std::setprecision(0) << avg_final
                << std::setw(12) << std::fixed << std::setprecision(0)
                << (avg_init - avg_final)
                << std::setw(9)  << std::fixed << std::setprecision(1) << avg_pct << "%"
                << "\n";

      int best_init = *std::min_element(init_costs.begin(), init_costs.end());
      double pct_vs = best_init > 0
          ? 100.0 * (best_init - best_cost) / best_init : 0.0;
      std::cout << "\n  Mejor coste inicial (greedy):  " << best_init << "\n";
      std::cout << "  Mejor coste final (ILS):       " << best_cost << "\n";
      std::cout << "  Mejora absoluta:               "
                << (best_init - best_cost) << "\n";
      std::cout << "  Mejora relativa:               "
                << std::fixed << std::setprecision(1) << pct_vs << "%\n";
    }

    std::cout << "  Mejoras totales LS: " << total_improvements << "\n";
  }


  // PASO 3: Evaluar mejor solucion
  PrintSeparator("3. Evaluacion de la mejor solucion");

  FeasibilityResult feas = FeasibilityChecker::Check(best_solution);
  std::cout << feas.ToString();

  CostBreakdown breakdown = Evaluator::EvaluateDetailed(best_solution);
  std::cout << breakdown.ToString();

  SolutionIO::PrintSummary(best_solution);

  if (problem.GetNumRooms() <= 10 && problem.GetNumDays() <= 10) {
    PrintRoomOccupancy(best_solution, problem);
  }


  // PASO 4: Exportar solucion
  PrintSeparator("4. Exportando solucion");

  // nombre de salida basado en la instancia
  size_t last_slash = instance_file.find_last_of('/');
  size_t last_dot = instance_file.find_last_of('.');
  std::string basename = instance_file.substr(
      last_slash == std::string::npos ? 0 : last_slash + 1,
      last_dot == std::string::npos
          ? std::string::npos
          : last_dot -
                (last_slash == std::string::npos ? 0 : last_slash + 1));
  std::string output_file = "solutions/" + basename + "_solution.json";

  if (SolutionIO::ExportJSON(best_solution, output_file)) {
    std::cout << "Solucion exportada a: " << output_file << "\n";
  } else {
    std::cerr << "Error al exportar la solucion\n";
  }


  // Resumen final
  PrintSeparator("Resumen final");

  double total_time = elapsed_s();
  std::cout << "Modo:         " << mode << "\n";
  std::cout << "Coste final:  " << best_cost << "\n";
  std::cout << "Tiempo total: " << std::fixed << std::setprecision(3)
            << total_time << " s\n";

  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "  Ejecucion completada!\n";
  std::cout << std::string(60, '=') << "\n\n";

  return 0;
}
