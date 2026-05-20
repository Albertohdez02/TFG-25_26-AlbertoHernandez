// ACOSolver.h - Colonia de Hormigas (MMAS) hibrido con VNS para IHTC 2024
// TFG Alberto Hernandez
//
// Construye soluciones guiadas por feromonas y las mejora con VNS.
// Sustituye al generador aleatorio (RandomGenerator) en el pipeline principal.
//
// Modelo de componentes ACO:
//   - tau_day[patient][day]   : feromona de asignar al paciente p el dia d
//   - tau_room[patient][room] : feromona de asignar al paciente p la habitacion r
//   El quirofano se elige de forma greedy (menor carga activa).
//
// Variante MMAS (Max-Min Ant System):
//   - Solo la mejor solucion de cada iteracion deposita feromona
//   - Cotas [tau_min, tau_max] evitan convergencia prematura
//   - Reinicializacion automatica ante estancamiento
//
// Regla de seleccion ACS pseudoproporcional:
//   - con prob q0: elige el candidato de mayor score (explotacion)
//   - con prob 1-q0: muestrea proporcionalmente a los scores (exploracion)
//
// Pipeline de cada hormiga:
//   ConstructSolution (guiada por tau × eta) -> VNS -> actualiza feromonas

#ifndef SRC_SOLVER_ACO_SOLVER_H_
#define SRC_SOLVER_ACO_SOLVER_H_

#include <random>
#include <vector>

#include "../entities/ProblemData.h"
#include "../solution/Solution.h"
#include "LocalSearch.h"  // VNSConfig

// Parametros del algoritmo ACO
struct ACOParams {
  int    n_ants       = 12;   // hormigas por iteracion (mas hormigas, VNS mas corta)
  double alpha        = 1.0;  // exponente de la feromona en la regla de seleccion
  double beta         = 2.0;  // exponente de la heuristica en la regla de seleccion
  double rho          = 0.10; // tasa de evaporacion [0,1]
  double q0           = 0.90; // probabilidad de explotacion (argmax vs ruleta)
  double tau_init     = 1.0;  // feromona inicial para posiciones feasibles
  int    stagnation_k = 15;   // iteraciones sin mejora global antes de reinicializar
  int    pool_size    = 4;    // hilos paralelos por iteracion (regla IHTC: max 4)
  bool   use_alns     = false;// si true, usa ALNS+SA en lugar del ILS clasico

  // B1: si true, eta_room incluye penalizaciones por capacidad y compatibilidad
  // de genero/edad. Si false, eta_room sigue siendo binaria {0,1} (legacy).
  bool   rich_eta_room = true;

  // B2: si true, eta_day incluye penalizaciones por OT no abierto y carga de
  // cirujano ese dia. Si false, solo PatientDelay (legacy).
  bool   rich_eta_day  = true;

  // B3: si true, warm_budget = clamp(0.08 * time_limit, 30s, 180s). Si false,
  // warm_budget = min(30s, 5% del tiempo) (legacy).
  bool   adaptive_warm_start = true;

  // Configuracion de la VNS (caps, exhaustive, refresh nurses, etc.).
  // Default = agresivo (Bloque A activo). Para legacy, pasar el resultado
  // de MakeLegacyVNSConfig() en main.cpp.
  VNSConfig vns_config = {};
};

class ACOSolver {
 public:
  // ejecuta el bucle ACO+VNS durante time_limit_s segundos
  // devuelve la mejor solucion factible encontrada
  static Solution Run(const ProblemData& problem, std::mt19937& rng,
                      int max_ls_iter, double time_limit_s,
                      const ACOParams& params = {});

 private:
  // tipo alias: vector 1D aplanado que representa una matriz [pid * stride + idx]
  using PheromoneMatrix = std::vector<double>;

  // inicializa feromonas: tau_init en posiciones feasibles, 0.0 en infeasibles
  static void InitPheromones(PheromoneMatrix& tau_day,
                              PheromoneMatrix& tau_room,
                              const ProblemData& problem, double tau_init);

  // precomputa eta_day y eta_room (invariante durante toda la ejecucion)
  // B1/B2: si rich_eta_room/day estan activos, incluye penalizaciones
  // adicionales para diferenciar habitaciones/dias por mas factores que
  // la mera compatibilidad y el delay.
  static void PrecomputeHeuristics(std::vector<double>& eta_day,
                                   std::vector<double>& eta_room,
                                   const ProblemData& problem,
                                   bool rich_eta_room = true,
                                   bool rich_eta_day  = true);

  // construye una solucion siguiendo feromonas y heuristica
  static Solution ConstructSolution(const PheromoneMatrix& tau_day,
                                    const PheromoneMatrix& tau_room,
                                    const std::vector<double>& eta_day,
                                    const std::vector<double>& eta_room,
                                    const ProblemData& problem,
                                    const ACOParams& params,
                                    std::mt19937& rng);

  // actualiza feromonas MMAS: evaporacion global + deposito de la mejor solucion
  static void UpdatePheromones(PheromoneMatrix& tau_day,
                                PheromoneMatrix& tau_room,
                                const Solution& best_sol, int best_cost,
                                const ProblemData& problem,
                                const ACOParams& params);

  // reinicializa todas las feromonas a tau_init (recuperacion ante estancamiento)
  static void ResetPheromones(PheromoneMatrix& tau_day,
                               PheromoneMatrix& tau_room,
                               const ProblemData& problem, double tau_init);

  // siembra feromona elevada en las decisiones de una solucion semilla
  // (warm-start). Para los arcs del seed coloca tau_max; el resto de
  // posiciones feasibles se ponen a tau_min para crear el contraste MMAS.
  static void SeedPheromones(PheromoneMatrix& tau_day,
                              PheromoneMatrix& tau_room,
                              const Solution& seed, int seed_cost,
                              const ProblemData& problem,
                              const ACOParams& params);

  // seleccion pseudoproporcional ACS sobre un vector de scores
  // con prob q0 devuelve el argmax (explotacion)
  // con prob 1-q0 muestrea por ruleta (exploracion)
  // devuelve -1 si no hay candidatos validos (todos los scores son 0)
  [[nodiscard]] static int SelectByScore(const std::vector<double>& scores,
                                         double q0, std::mt19937& rng);
};

#endif  // SRC_SOLVER_ACO_SOLVER_H_
