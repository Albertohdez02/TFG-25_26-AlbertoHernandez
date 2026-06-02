// ALNSPerturbation.h - Perturbacion ALNS (Adaptive Large Neighborhood Search)
// con acceptance Simulated Annealing. Reemplaza la fase ILS de
// LocalSearch::Run cuando use_alns=true.
// TFG Alberto Hernandez
//
// Esquema de cada Apply():
//   1. Destroy: elegir k pacientes y removerlos de la solucion.
//   2. Repair: re-insertar greedy.
//   3. Acceptance SA: aceptar si delta <= 0 o con probabilidad
//      exp(-delta / T). Si rechaza, restaura el estado pre-Apply.
//
// MVP (esta version): un destroy (RandomRemoval) y un repair
// (TryAssignPatientFeasibly + ForceAssignMandatory). Adaptive weights y
// operadores cluster (Surgeon/Day/Room/WorstRemoval) se añaden tras
// validar el MVP empiricamente. Esa decision viene del hallazgo de que
// los operadores cluster fallan bajo first-improvement pero deben
// funcionar bajo SA-acceptance. Probamos primero el SA solo (MVP) y si
// aporta, añadimos los operadores cluster.

#ifndef SRC_SOLVER_ALNS_PERTURBATION_H_
#define SRC_SOLVER_ALNS_PERTURBATION_H_

#include <random>
#include <vector>

#include "../entities/ProblemData.h"
#include "../solution/Solution.h"

struct ALNSParams {
  // Schedule SA: T_0 calibrado al instanciar (proporcional al coste inicial).
  double initial_temp_factor = 0.05;  // T_0 = factor * cost_inicial
  double cooling_rate = 0.998;        // T *= cooling_rate por Apply
  double min_temp = 0.5;              // suelo para evitar T = 0 (eviar overflow)

  // Tamano del destroy (k pacientes): k = round(sqrt(P) * factor)
  // acotado por [min_destroy, max_destroy].
  double destroy_factor = 0.5;
  int min_destroy = 4;
  int max_destroy = 30;
};

class ALNSPerturbation {
 public:
  // Inicializa el modulo. `initial_cost` se usa para calibrar T_0; si <=0
  // usa T_0 = min_temp (modo defensivo).
  ALNSPerturbation(const ProblemData& problem, int initial_cost,
                    const ALNSParams& params = {});

  // Aplica una iteracion destroy + repair + SA-accept.
  // Modifica `solution` y `current_cost` si acepta el movimiento.
  // Devuelve true si la solucion cambio (acepto), false si rechazo.
  bool Apply(Solution& solution, int& current_cost, std::mt19937& rng);

  // Temperatura actual (informativo)
  [[nodiscard]] double GetTemperature() const noexcept { return temperature_; }

 private:
  // RandomRemoval: elige k pacientes uniformes al azar de los programados
  // y los retira de la solucion. Devuelve los pids removidos.
  std::vector<PatientId> RandomRemoval(Solution& solution, int k,
                                         std::mt19937& rng);

  // SurgeonRemoval: elige un cirujano con overtime en algun dia (probabilidad
  // proporcional al overtime) y retira todos sus pacientes (acotado por
  // max_destroy). Si ningun cirujano tiene overtime, hace RandomRemoval.
  std::vector<PatientId> SurgeonRemoval(Solution& solution, int k_cap,
                                          std::mt19937& rng);

  // DayRemoval: elige un dia con muchos pacientes (proporcional a carga) y
  // retira hasta k_cap de los pacientes admitidos ese dia. Ataca el coste
  // de open_ot/ot_overtime al re-organizar.
  std::vector<PatientId> DayRemoval(Solution& solution, int k_cap,
                                      std::mt19937& rng);

  // GreedyRepair: para cada pid removido (en orden barajado), intenta
  // re-asignarlo con TryAssignPatientFeasibly. Si falla y es obligatorio,
  // ForceAssignMandatory.
  void GreedyRepair(Solution& solution,
                     std::vector<PatientId>& removed,
                     std::mt19937& rng);

  const ProblemData& problem_;
  ALNSParams params_;
  double temperature_;
};

#endif  // SRC_SOLVER_ALNS_PERTURBATION_H_
