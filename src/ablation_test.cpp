// ablation_test.cpp - Ablation test de vecindarios VNS para IHTC 2024
// TFG Alberto Hernandez
//
// Evalua la contribucion de cada operador ejecutando 18 configuraciones:
//   - all        : los 8 operadores activos (baseline)
//   - no_LS      : solo solucion aleatoria, sin busqueda local
//   - no_X (x8)  : todos los operadores SALVO X (leave-one-out)
//   - only_X (x8): SOLO el operador X
//
// Paralelismo: pool de num_threads hilos sobre la cola de tareas.
// Tiempo por tarea calculado automaticamente para respetar competition_time_s.
// Verifica factibilidad de cada solucion y registra el desglose completo de costes.
//
// Uso:
//   ./ihtc_ablation <instancia.json>
//                   [num_seeds=3]
//                   [num_restarts=3]
//                   [num_threads=4]
//                   [competition_time_s=600]
//
// Salida: ablation_results/<basename>_ablation.csv

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "common/types.h"
#include "entities/ProblemData.h"
#include "evaluator/Evaluator.h"
#include "evaluator/FeasibilityChecker.h"
#include "io/ProblemParser.h"
#include "solution/Solution.h"
#include "solver/LocalSearch.h"
#include "solver/RandomGenerator.h"

// ---------------------------------------------------------------------------
// Tipos
// ---------------------------------------------------------------------------

struct AblationConfig {
  std::string name;
  uint8_t mask;
};

struct RunResult {
  std::string instance;
  std::string config;
  unsigned int seed;
  int restart;
  bool feasible;
  int init_cost;
  int final_cost;
  double time_s;
  std::array<int, 8> op_improvements = {};
  CostBreakdown breakdown;
};

struct Task {
  int config_idx;
  unsigned int seed;
  int restart;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static double Mean(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

static double StdDev(const std::vector<double>& v, double mean) {
  if (v.size() < 2) return 0.0;
  double sq = 0.0;
  for (double x : v) sq += (x - mean) * (x - mean);
  return std::sqrt(sq / static_cast<double>(v.size() - 1));
}

static std::string Basename(const std::string& path) {
  size_t slash = path.find_last_of('/');
  size_t dot   = path.find_last_of('.');
  size_t start = slash == std::string::npos ? 0 : slash + 1;
  size_t len   = dot == std::string::npos || dot < start
                     ? std::string::npos
                     : dot - start;
  return path.substr(start, len);
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

static void Worker(const ProblemData& problem,
                   const std::vector<AblationConfig>& configs,
                   const std::string& instance_name,
                   double time_limit_per_run,
                   std::queue<Task>& task_queue,
                   std::mutex& queue_mutex,
                   std::vector<RunResult>& results,
                   std::mutex& results_mutex,
                   std::atomic<int>& completed,
                   int total_tasks) {
  while (true) {
    Task task;
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      if (task_queue.empty()) break;
      task = task_queue.front();
      task_queue.pop();
    }

    const AblationConfig& cfg = configs[task.config_idx];
    std::mt19937 rng(task.seed + static_cast<unsigned int>(task.restart) * 1000u);

    Solution sol = RandomGenerator::Generate(problem, rng);
    const int init_cost = Evaluator::Evaluate(sol);

    RunResult res;
    res.instance  = instance_name;
    res.config    = cfg.name;
    res.seed      = task.seed;
    res.restart   = task.restart;
    res.init_cost = init_cost;

    if (cfg.mask == 0x00) {
      res.final_cost = init_cost;
      res.time_s     = 0.0;
    } else {
      auto stats = LocalSearch::Run(sol, 999999, rng, time_limit_per_run, cfg.mask);
      res.final_cost     = stats.final_cost;
      res.time_s         = stats.elapsed_seconds;
      res.op_improvements = stats.op_improvements;
    }

    FeasibilityResult feas = FeasibilityChecker::Check(sol);
    res.feasible  = feas.feasible;
    res.breakdown = Evaluator::EvaluateDetailed(sol);

    {
      std::lock_guard<std::mutex> lock(results_mutex);
      results.push_back(res);
    }

    int done = ++completed;
    if (done % 4 == 0 || done == total_tasks) {
      std::lock_guard<std::mutex> lock(results_mutex);
      std::cout << "\r  Progreso: " << done << " / " << total_tasks
                << "  [" << cfg.name << "]           " << std::flush;
    }
  }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Uso: " << argv[0]
              << " <instancia.json> [seeds=3] [restarts=3]"
                 " [threads=4] [competition_s=600]\n";
    return 1;
  }

  const std::string instance_file   = argv[1];
  const int         num_seeds       = argc >= 3 ? std::atoi(argv[2]) : 3;
  const int         num_restarts    = argc >= 4 ? std::atoi(argv[3]) : 3;
  const int         num_threads     = argc >= 5 ? std::atoi(argv[4]) : 4;
  const double      competition_s   = argc >= 6 ? std::atof(argv[5]) : 600.0;
  constexpr unsigned int kBaseSeed  = 42;

  // Cargar instancia
  ProblemData problem;
  try {
    problem = ProblemParser::Parse(instance_file);
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  const std::string instance_name = Basename(instance_file);

  // ---------------------------------------------------------------------------
  // Definir configuraciones (18 en total)
  // ---------------------------------------------------------------------------
  std::vector<AblationConfig> configs;
  configs.push_back({"all",   0xFF});
  configs.push_back({"no_LS", 0x00});
  for (int i = 0; i < 8; ++i)
    configs.push_back({"no_"   + std::string(kOperatorNames[i]),
                        static_cast<uint8_t>(0xFF ^ (1 << i))});
  for (int i = 0; i < 8; ++i)
    configs.push_back({"only_" + std::string(kOperatorNames[i]),
                        static_cast<uint8_t>(1 << i)});

  // Tiempo por tarea: distribuir el presupuesto de competicion entre las tareas
  // que se ejecutaran en paralelo con num_threads hilos.
  //   wall_time = total_tasks * time_per_run / num_threads = competition_s
  //   => time_per_run = competition_s * num_threads / total_tasks
  // (no_LS no consume tiempo de LS, pero se reserva igualmente para uniformidad)
  const int    total_tasks     = static_cast<int>(configs.size()) * num_seeds * num_restarts;
  const double time_per_run    = competition_s * num_threads / static_cast<double>(total_tasks);
  const double est_wall_min    = (total_tasks * time_per_run / num_threads) / 60.0;

  std::cout << "=== Ablation Test IHTC 2024 ===\n";
  std::cout << "Instancia     : " << instance_file  << "\n";
  std::cout << "Seeds x Rest. : " << num_seeds << " x " << num_restarts << "\n";
  std::cout << "Hilos         : " << num_threads << "\n";
  std::cout << "Tiempo/tarea  : " << std::fixed << std::setprecision(1)
            << time_per_run << " s\n";
  std::cout << "Est. tiempo   : ~" << std::fixed << std::setprecision(1)
            << est_wall_min << " min\n\n";

  // ---------------------------------------------------------------------------
  // Construir cola de tareas
  // ---------------------------------------------------------------------------
  std::queue<Task> task_queue;
  for (int ci = 0; ci < static_cast<int>(configs.size()); ++ci)
    for (int si = 0; si < num_seeds; ++si)
      for (int ri = 1; ri <= num_restarts; ++ri)
        task_queue.push({ci, kBaseSeed + static_cast<unsigned int>(si), ri});

  std::mutex           queue_mutex;
  std::vector<RunResult> results;
  results.reserve(total_tasks);
  std::mutex           results_mutex;
  std::atomic<int>     completed{0};

  // ---------------------------------------------------------------------------
  // Lanzar pool de hilos
  // ---------------------------------------------------------------------------
  auto t_start = std::chrono::steady_clock::now();

  std::vector<std::thread> threads;
  threads.reserve(num_threads);
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back(Worker,
                         std::cref(problem),
                         std::cref(configs),
                         std::cref(instance_name),
                         time_per_run,
                         std::ref(task_queue),
                         std::ref(queue_mutex),
                         std::ref(results),
                         std::ref(results_mutex),
                         std::ref(completed),
                         total_tasks);
  }
  for (auto& t : threads) t.join();

  double wall_s = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - t_start).count();
  std::cout << "\n\nFinalizado en " << std::fixed << std::setprecision(1)
            << wall_s << " s\n\n";

  // ---------------------------------------------------------------------------
  // Ordenar resultados (config > seed > restart) para CSV reproducible
  // ---------------------------------------------------------------------------
  std::sort(results.begin(), results.end(), [&](const RunResult& a, const RunResult& b) {
    if (a.config != b.config) {
      // mantener orden de configs original
      auto idx = [&](const std::string& name) {
        for (int i = 0; i < static_cast<int>(configs.size()); ++i)
          if (configs[i].name == name) return i;
        return -1;
      };
      return idx(a.config) < idx(b.config);
    }
    if (a.seed    != b.seed)    return a.seed    < b.seed;
    return a.restart < b.restart;
  });

  // ---------------------------------------------------------------------------
  // Exportar CSV completo
  // ---------------------------------------------------------------------------
  std::string out_dir = "ablation_results";
  std::string csv_path = out_dir + "/" + instance_name + "_ablation.csv";
  {
    std::ofstream csv(csv_path);
    if (!csv) {
      // intentar crear directorio y reintentar
      std::system(("mkdir -p " + out_dir).c_str());
      csv.open(csv_path);
    }

    // Cabecera
    csv << "instance,config,seed,restart,feasible,init_cost,final_cost"
           ",improvement_pct,time_s";
    for (int i = 0; i < 8; ++i) csv << ",op_" << kOperatorNames[i];
    csv << ",cost_room_capacity,cost_room_gender_mix,cost_room_mixed_age"
           ",cost_patient_delay,cost_unscheduled_optional"
           ",cost_surgeon_overtime,cost_ot_overtime,cost_open_ot"
           ",cost_nurse_skill,cost_nurse_excessive_workload"
           ",cost_continuity_of_care,cost_surgeon_transfer"
           ",cost_total\n";

    for (const auto& r : results) {
      double pct = r.init_cost > 0
          ? 100.0 * (r.init_cost - r.final_cost) / r.init_cost : 0.0;
      csv << r.instance << "," << r.config << "," << r.seed << "," << r.restart
          << "," << (r.feasible ? "true" : "false")
          << "," << r.init_cost << "," << r.final_cost
          << "," << std::fixed << std::setprecision(3) << pct
          << "," << std::fixed << std::setprecision(3) << r.time_s;
      for (int i = 0; i < 8; ++i) csv << "," << r.op_improvements[i];
      const auto& b = r.breakdown;
      csv << "," << b.room_capacity
          << "," << b.room_gender_mix
          << "," << b.room_mixed_age
          << "," << b.patient_delay
          << "," << b.unscheduled_optional
          << "," << b.surgeon_overtime
          << "," << b.ot_overtime
          << "," << b.open_ot
          << "," << b.nurse_skill
          << "," << b.nurse_excessive_workload
          << "," << b.continuity_of_care
          << "," << b.surgeon_transfer
          << "," << b.Total()
          << "\n";
    }
  }
  std::cout << "CSV guardado en: " << csv_path << "\n\n";

  // ---------------------------------------------------------------------------
  // Tabla resumen en stdout
  // ---------------------------------------------------------------------------
  auto cfg_costs = [&](const std::string& name) {
    std::vector<double> v;
    for (const auto& r : results)
      if (r.config == name) v.push_back(r.final_cost);
    return v;
  };

  auto all_costs   = cfg_costs("all");
  auto no_ls_costs = cfg_costs("no_LS");
  double all_mean   = Mean(all_costs);
  double no_ls_mean = Mean(no_ls_costs);

  // Factibilidad
  int infeasible_count = 0;
  for (const auto& r : results) if (!r.feasible) ++infeasible_count;
  std::cout << "Soluciones infactibles: " << infeasible_count
            << " / " << total_tasks << "\n\n";

  std::cout << "=== RESUMEN POR CONFIGURACION ===\n\n";
  std::cout << std::left  << std::setw(24) << "Configuracion"
            << std::right
            << std::setw(9)  << "Media"
            << std::setw(8)  << "StdDev"
            << std::setw(7)  << "Mejor"
            << std::setw(7)  << "Peor"
            << std::setw(9)  << "vs noLS"
            << std::setw(9)  << "vs all"
            << "\n" << std::string(73, '-') << "\n";

  for (const auto& cfg : configs) {
    auto v = cfg_costs(cfg.name);
    if (v.empty()) continue;
    double m   = Mean(v);
    double sd  = StdDev(v, m);
    double best  = *std::min_element(v.begin(), v.end());
    double worst = *std::max_element(v.begin(), v.end());
    double vs_nols = no_ls_mean > 0 ? 100.0 * (no_ls_mean - m) / no_ls_mean : 0.0;
    double vs_all  = m - all_mean;
    std::cout << std::left  << std::setw(24) << cfg.name
              << std::right
              << std::setw(9)  << std::fixed << std::setprecision(1) << m
              << std::setw(8)  << std::fixed << std::setprecision(1) << sd
              << std::setw(7)  << static_cast<int>(best)
              << std::setw(7)  << static_cast<int>(worst)
              << std::setw(8)  << std::fixed << std::setprecision(1) << vs_nols << "%"
              << std::setw(8)  << std::fixed << std::setprecision(1) << vs_all
              << "\n";
  }

  // Leave-one-out ranking
  std::cout << "\n=== RANKING LEAVE-ONE-OUT (degradacion vs 'all') ===\n\n";
  std::vector<std::pair<std::string, double>> loo;
  for (int i = 0; i < 8; ++i) {
    std::string name = "no_" + std::string(kOperatorNames[i]);
    auto v = cfg_costs(name);
    if (!v.empty()) loo.push_back({name, Mean(v) - all_mean});
  }
  std::sort(loo.begin(), loo.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  std::cout << std::left  << std::setw(22) << "Sin operador"
            << std::right << std::setw(10) << "Delta"
            << std::setw(8)  << "Delta%\n"
            << std::string(40, '-') << "\n";
  for (const auto& [name, delta] : loo) {
    double pct = all_mean > 0 ? 100.0 * delta / all_mean : 0.0;
    std::cout << std::left  << std::setw(22) << name
              << std::right
              << std::setw(10) << std::fixed << std::setprecision(1) << delta
              << std::setw(7)  << std::fixed << std::setprecision(1) << pct << "%\n";
  }
  std::cout << "  all: " << std::fixed << std::setprecision(1) << all_mean
            << "  |  no_LS: " << no_ls_mean << "\n\n";

  // Contribucion por operador en config 'all'
  std::cout << "=== CONTRIBUCION EN CONFIG 'all' ===\n\n";
  std::array<long long, 8> op_total = {};
  long long grand = 0;
  for (const auto& r : results) {
    if (r.config != "all") continue;
    for (int i = 0; i < 8; ++i) { op_total[i] += r.op_improvements[i]; grand += r.op_improvements[i]; }
  }
  std::cout << std::left << std::setw(14) << "Operador"
            << std::right << std::setw(10) << "Mejoras" << std::setw(8) << "%\n"
            << std::string(32, '-') << "\n";
  for (int i = 0; i < 8; ++i) {
    double pct = grand > 0 ? 100.0 * op_total[i] / grand : 0.0;
    std::cout << std::left  << std::setw(14) << kOperatorNames[i]
              << std::right << std::setw(10) << op_total[i]
              << std::setw(7) << std::fixed << std::setprecision(1) << pct << "%\n";
  }

  return 0;
}
