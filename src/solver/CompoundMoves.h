// CompoundMoves.h - Movimientos compuestos para IHTC 2024
// TFG Alberto Hernandez
//
// Operadores VNS que aplican cadenas de movimientos atomicos en un solo
// paso, con commit/rollback via snapshot completo de Solution. Inspirados
// en el patron MoveChain de la submission IMADA pero implementados de
// forma independiente.
//
// Diferencia clave con los 8 operadores atomicos clasicos:
//   - Atomicos: una sola decision (mover paciente X a destino Y).
//   - Compuestos: secuencia de decisiones interdependientes que solo
//     tienen sentido conjuntas (e.g., desalojar paciente A para meter
//     paciente B y reubicar A en otro destino).
//
// Esquema de cada compound:
//   1. Snapshot del estado actual (copy de Solution + coste).
//   2. Aplicar la cadena de movimientos atomicos.
//   3. Evaluar coste final.
//   4. Si mejora: commit (la solucion ya esta actualizada).
//      Si no: rollback (restaurar snapshot).
//
// Compuestos implementados:
//   - TryKickPatient:    Mete un opcional desalojando un bloqueante.
//   - TryReorganizeDay:  Desasigna todos los pacientes de un dia
//                        problematico y los reasigna por urgencia.
//   - TrySwapNurseBlock: Intercambia la nurse de dos habitaciones para
//                        un bloque de dias consecutivos.

#ifndef SRC_SOLVER_COMPOUND_MOVES_H_
#define SRC_SOLVER_COMPOUND_MOVES_H_

#include <random>

#include "../solution/Solution.h"

class CompoundMoves {
 public:
  /** @brief Mete un opcional no programado desalojando a un bloqueante.
   *  Para cada opcional, busca un (room, day, ot) donde la insercion no es
   *  factible por capacidad/genero/cirujano-OT, identifica al paciente
   *  bloqueante, lo desaloja y lo reubica en su ventana factible. Acepta si el
   *  coste neto mejora.
   *  @return true si encontro una mejora.
   */
  static bool TryKickPatient(Solution& solution, int& current_cost,
                              std::mt19937& rng);

  /** @brief Reorganiza dias con overtime de cirujano u OT.
   *  Desasigna todos los pacientes del dia y los reasigna ordenados por
   *  urgencia (slack ascendente). Snapshot/rollback si no mejora.
   */
  static bool TryReorganizeDay(Solution& solution, int& current_cost,
                                std::mt19937& rng);

  /** @brief Intercambia las nurses de dos habitaciones en un bloque de dias.
   *  Para k dias consecutivos (k=3 por defecto) intercambia las nurses de
   *  (room1, days, shift) con (room2, days, shift), solo si ambas son
   *  factibles en los k dias. Ataca continuity_of_care.
   */
  static bool TrySwapNurseBlock(Solution& solution, int& current_cost,
                                 std::mt19937& rng);
};

#endif  // SRC_SOLVER_COMPOUND_MOVES_H_
