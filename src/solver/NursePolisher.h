// NursePolisher.h - Fase final de pulido dedicada a la matriz de enfermeras
// TFG Alberto Hernandez (Plan Some-touches, Fase 1)
//
// Tras la busqueda principal (ACO+VNS+compounds) la matriz de enfermeras
// queda con margen de mejora porque los operadores VNS dedican la mayor
// parte del tiempo a mover pacientes. Esta fase recibe una solucion
// factible y trabaja EXCLUSIVAMENTE sobre las asignaciones nurse->celda,
// con tres operadores:
//
//   - ChangeOneNurse:    best-improvement por celda (room, day, shift).
//   - SwapTwoNurses:     intercambia las nurses de dos celdas (mismo shift).
//   - PromoteContinuity: para cada (room, shift), intenta unificar la
//                         nurse a lo largo del stay de los pacientes.
//
// Hill climbing puro: solo movimientos que mejoran el coste se aceptan.
// No SA, no tabu. La razon: los optimos locales de la sub-matriz nurse
// son menos profundos que los de pacientes y un hill climbing dedicado
// suele encontrar mejoras significativas en pocos segundos.
//
// No toca asignaciones de pacientes ni rompe HC. Garantiza cobertura
// HC13/HC14 al respetar las celdas ya cubiertas y solo cambiar nurses
// dentro de las celdas con room_occupancy > 0.

#ifndef SRC_SOLVER_NURSE_POLISHER_H_
#define SRC_SOLVER_NURSE_POLISHER_H_

#include <random>

#include "../solution/Solution.h"

class NursePolisher {
 public:
  // Pule la matriz de enfermeras de `solution` durante `time_limit_s`
  // segundos. Devuelve el numero de mejoras aceptadas.
  // No modifica las asignaciones de pacientes.
  static int Polish(Solution& solution, double time_limit_s,
                     std::mt19937& rng);
};

#endif  // SRC_SOLVER_NURSE_POLISHER_H_
