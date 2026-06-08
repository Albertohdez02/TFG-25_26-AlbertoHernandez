// ALNSPerturbation.cpp - implementacion del modulo ALNS+SA.
// TFG Alberto Hernandez

#include "ALNSPerturbation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "../evaluator/Evaluator.h"
#include "../evaluator/FeasibilityChecker.h"
#include "RandomGenerator.h"

ALNSPerturbation::ALNSPerturbation(const ProblemData& problem,
                                    int initial_cost,
                                    const ALNSParams& params)
    : problem_(problem), params_(params) {
  // Hooks de tuning por entorno (no-op si la var no esta definida): permiten
  // calibrar el ALNS+SA sin recompilar. Se aplican aqui para cubrir los dos
  // sitios de instanciacion (VNS-ALNS e hybrid) con un solo cambio.
  if (const char* v = std::getenv("IHTC_ALNS_COOLING"); v && v[0])
    params_.cooling_rate = std::atof(v);
  if (const char* v = std::getenv("IHTC_ALNS_TEMP_FACTOR"); v && v[0])
    params_.initial_temp_factor = std::atof(v);
  if (const char* v = std::getenv("IHTC_ALNS_DESTROY_FACTOR"); v && v[0])
    params_.destroy_factor = std::atof(v);
  if (const char* v = std::getenv("IHTC_ALNS_MIN_DESTROY"); v && v[0])
    params_.min_destroy = std::atoi(v);
  if (const char* v = std::getenv("IHTC_ALNS_MAX_DESTROY"); v && v[0])
    params_.max_destroy = std::atoi(v);

  // T_0 proporcional al coste inicial. Para coste tipico ~5000-100000,
  // factor 0.05 => T_0 ~250-5000. Suficiente para aceptar deltas pequeños
  // con prob alta y deltas medianos con prob moderada al inicio.
  // Si initial_cost <= 0 (warm-start fallido) arrancamos con T = min_temp.
  if (initial_cost > 0) {
    temperature_ = std::max(params.min_temp,
        params.initial_temp_factor * static_cast<double>(initial_cost));
  } else {
    temperature_ = params.min_temp;
  }
}

std::vector<PatientId> ALNSPerturbation::RandomRemoval(
    Solution& solution, int k, std::mt19937& rng) {
  std::vector<PatientId> all_scheduled(
      solution.GetScheduledPatients().begin(),
      solution.GetScheduledPatients().end());
  std::shuffle(all_scheduled.begin(), all_scheduled.end(), rng);

  if (k > static_cast<int>(all_scheduled.size())) {
    k = static_cast<int>(all_scheduled.size());
  }

  std::vector<PatientId> removed(all_scheduled.begin(),
                                  all_scheduled.begin() + k);
  for (PatientId pid : removed) {
    solution.UnassignPatient(pid);
  }
  return removed;
}

std::vector<PatientId> ALNSPerturbation::SurgeonRemoval(
    Solution& solution, int k_cap, std::mt19937& rng) {
  // identificar cirujanos con overtime y muestrearlos por probabilidad
  // proporcional. Si no hay overtime en absoluto, fallback a RandomRemoval.
  int num_surgeons = problem_.GetNumSurgeons();
  int num_days     = problem_.GetNumDays();
  std::vector<int> over_total(num_surgeons, 0);
  long long total_over = 0;
  for (int s = 0; s < num_surgeons; ++s) {
    const Surgeon& sg = problem_.GetSurgeon(s);
    int t = 0;
    for (Day d = 0; d < num_days; ++d) {
      int max_t = sg.GetMaxSurgeryTimeForDay(d);
      if (max_t <= 0) continue;
      int load = solution.GetSurgeonLoad(s, d);
      if (load > max_t) t += (load - max_t);
    }
    over_total[s] = t;
    total_over += t;
  }
  if (total_over == 0) {
    return RandomRemoval(solution, k_cap, rng);
  }
  // muestreo por ruleta (peso = overtime)
  std::uniform_int_distribution<long long> uni(0, total_over - 1);
  long long pick = uni(rng);
  int chosen = 0;
  long long acc = 0;
  for (int s = 0; s < num_surgeons; ++s) {
    acc += over_total[s];
    if (acc > pick) { chosen = s; break; }
  }
  // recoger todos los pacientes del cirujano elegido (acotar por k_cap)
  std::vector<PatientId> targets;
  for (PatientId pid : solution.GetScheduledPatients()) {
    if (problem_.GetPatient(pid).GetSurgeonId() == chosen) {
      targets.push_back(pid);
    }
  }
  if (targets.empty()) {
    return RandomRemoval(solution, k_cap, rng);
  }
  std::shuffle(targets.begin(), targets.end(), rng);
  if (static_cast<int>(targets.size()) > k_cap) {
    targets.resize(k_cap);
  }
  for (PatientId pid : targets) solution.UnassignPatient(pid);
  return targets;
}

std::vector<PatientId> ALNSPerturbation::DayRemoval(
    Solution& solution, int k_cap, std::mt19937& rng) {
  // muestreo de dia proporcional al numero de pacientes admitidos ese dia
  int num_days = problem_.GetNumDays();
  std::vector<int> count_by_day(num_days, 0);
  for (PatientId pid : solution.GetScheduledPatients()) {
    Day d = solution.GetPatientAdmissionDay(pid);
    if (d >= 0 && d < num_days) count_by_day[d]++;
  }
  long long total = 0;
  for (int c : count_by_day) total += c;
  if (total == 0) return RandomRemoval(solution, k_cap, rng);

  std::uniform_int_distribution<long long> uni(0, total - 1);
  long long pick = uni(rng);
  Day chosen = 0;
  long long acc = 0;
  for (Day d = 0; d < num_days; ++d) {
    acc += count_by_day[d];
    if (acc > pick) { chosen = d; break; }
  }
  // recoger los pacientes admitidos ese dia
  std::vector<PatientId> targets;
  for (PatientId pid : solution.GetScheduledPatients()) {
    if (solution.GetPatientAdmissionDay(pid) == chosen) {
      targets.push_back(pid);
    }
  }
  if (targets.empty()) return RandomRemoval(solution, k_cap, rng);
  std::shuffle(targets.begin(), targets.end(), rng);
  if (static_cast<int>(targets.size()) > k_cap) {
    targets.resize(k_cap);
  }
  for (PatientId pid : targets) solution.UnassignPatient(pid);
  return targets;
}

void ALNSPerturbation::GreedyRepair(Solution& solution,
                                     std::vector<PatientId>& removed,
                                     std::mt19937& rng) {
  // Orden de re-insercion: obligatorios primero (por urgencia descendente),
  // luego opcionales aleatorios. Asi minimizamos la probabilidad de que un
  // obligatorio dispare ForceAssignMandatory.
  std::vector<PatientId> mandatories, optionals;
  mandatories.reserve(removed.size());
  optionals.reserve(removed.size());
  for (PatientId pid : removed) {
    if (problem_.GetPatient(pid).IsMandatory()) {
      mandatories.push_back(pid);
    } else {
      optionals.push_back(pid);
    }
  }

  // ordenar obligatorios por slack ascendente (los mas restringidos primero)
  std::sort(mandatories.begin(), mandatories.end(),
            [this](PatientId a, PatientId b) {
              const auto& pa = problem_.GetPatient(a);
              const auto& pb = problem_.GetPatient(b);
              int slack_a = pa.GetSurgeryDueDay() - pa.GetSurgeryReleaseDay();
              int slack_b = pb.GetSurgeryDueDay() - pb.GetSurgeryReleaseDay();
              return slack_a < slack_b;
            });
  std::shuffle(optionals.begin(), optionals.end(), rng);

  for (PatientId pid : mandatories) {
    if (RandomGenerator::TryAssignPatientFeasibly(solution, pid, problem_,
                                                   rng)) {
      continue;
    }
    // si falla, forzar (desaloja bloqueantes)
    RandomGenerator::ForceAssignMandatory(solution, pid, problem_, rng);
  }
  for (PatientId pid : optionals) {
    // los opcionales pueden quedar sin programar; coste blando aceptable
    RandomGenerator::TryAssignPatientFeasibly(solution, pid, problem_, rng);
  }
}

bool ALNSPerturbation::Apply(Solution& solution, int& current_cost,
                              std::mt19937& rng) {
  int p = static_cast<int>(solution.GetScheduledPatients().size());
  if (p < 2) return false;

  // Snapshot completo (Solution es copiable; cachés delta se copian con ella)
  Solution saved = solution;
  int saved_cost = current_cost;

  // 1. Destroy — elegir uniforme entre 3 operadores (sin adaptive weights
  //    en este MVP+; se añadiran si el experimento confirma mejora).
  int k = std::clamp(
      static_cast<int>(std::round(std::sqrt(static_cast<double>(p))
                                  * params_.destroy_factor)),
      params_.min_destroy, params_.max_destroy);
  std::uniform_int_distribution<int> destroy_pick(0, 2);
  int destroy_op = destroy_pick(rng);
  std::vector<PatientId> removed;
  switch (destroy_op) {
    case 0: removed = RandomRemoval(solution, k, rng); break;
    case 1: removed = SurgeonRemoval(solution, k, rng); break;
    case 2: removed = DayRemoval(solution, k, rng); break;
  }
  if (removed.empty()) return false;

  // 2. Repair
  GreedyRepair(solution, removed, rng);

  // 3. Cobertura de enfermeras (los movimientos pueden haber creado celdas
  //    nuevas sin nurse, igual que en LocalSearch::Run tras Perturb)
  RandomGenerator::EnsureFullNurseCoverage(solution, problem_, rng);

  // 4. SA acceptance
  int new_cost = Evaluator::Evaluate(solution);
  int delta = new_cost - current_cost;

  bool accept = false;
  if (delta <= 0) {
    accept = true;
  } else {
    double prob = std::exp(-static_cast<double>(delta) / temperature_);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    if (uni(rng) < prob) accept = true;
  }

  // cool down (siempre, acepte o no)
  temperature_ = std::max(params_.min_temp,
                           temperature_ * params_.cooling_rate);

  if (accept) {
    current_cost = new_cost;
    return true;
  }

  // RECHAZO: restaurar snapshot
  solution = saved;
  current_cost = saved_cost;
  return false;
}
