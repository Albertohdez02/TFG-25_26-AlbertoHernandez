// Solution.h - Representacion de una solucion para IHTC 2024
// TFG Alberto Hernandez
//
// Guarda las asignaciones de pacientes a habitaciones, dias y quirofanos.
//
// Data-Oriented Design:
// - Vectores planos en vez de objetos
// - Arrays multidimensionales aplanados
// - Caches delta para evaluar movimientos en O(1) sin recalcular todo
//
// agilisa la implementacion de operadores de movimiento (swap, reassign, etc)
// y la evaluacion de restricciones y funciones objetivo

#ifndef SRC_SOLUTION_SOLUTION_H_
#define SRC_SOLUTION_SOLUTION_H_

#include <unordered_set>
#include <vector>

#include "../common/types.h"
#include "../entities/ProblemData.h"

/** @brief Representa una solucion: asignacion de pacientes y enfermeras a recursos.
 *  Variables de decision: que pacientes se programan, en que dia, habitacion,
 *  quirofano, y que enfermeras se asignan a cada turno.
 */
class Solution {
 public:
  /** @brief Crea una solucion vacia a partir de los datos del problema. */
  explicit Solution(const ProblemData& problem);
  /** @brief Constructor de copia. */
  Solution(const Solution& other);
  /** @brief Constructor de movimiento; transfiere la propiedad de los datos. */
  Solution(Solution&& other) noexcept;
  /** @brief Asignacion por copia. */
  Solution& operator=(const Solution& other);
  /** @brief Asignacion por movimiento. */
  Solution& operator=(Solution&& other) noexcept;

  /** @brief Asigna un paciente a habitacion, dia y quirofano.
   *  @return false si el paciente ya estaba asignado.
   */
  bool AssignPatient(PatientId patient_id, RoomId room_id, Day admission_day,
                     OperatingTheaterId ot_id);

  /** @brief Quita un paciente del planning. */
  bool UnassignPatient(PatientId patient_id);

  /** @brief Mueve un paciente a otra habitacion/dia/quirofano.
   *  Mas eficiente que quitar y volver a poner.
   */
  bool ReassignPatient(PatientId patient_id, RoomId new_room_id,
                       Day new_admission_day, OperatingTheaterId new_ot_id);

  /** @brief Asigna una enfermera a una habitacion-dia-turno. */
  bool AssignNurse(NurseId nurse_id, RoomId room_id, Day day, Shift shift);
  /** @brief Quita la enfermera de una habitacion-dia-turno. */
  bool UnassignNurse(RoomId room_id, Day day, Shift shift);

  /** @brief Indica si el paciente esta programado. */
  [[nodiscard]] bool IsPatientScheduled(PatientId patient_id) const noexcept;
  /** @brief Devuelve la habitacion asignada al paciente. */
  [[nodiscard]] RoomId GetPatientRoom(PatientId patient_id) const noexcept;
  /** @brief Devuelve el dia de ingreso del paciente. */
  [[nodiscard]] Day GetPatientAdmissionDay(PatientId patient_id) const noexcept;
  /** @brief Devuelve el quirofano asignado al paciente. */
  [[nodiscard]] OperatingTheaterId GetPatientOT(PatientId patient_id) const noexcept;
  /** @brief Devuelve la enfermera asignada a una habitacion-dia-turno. */
  [[nodiscard]] NurseId GetNurseAssignment(RoomId room_id, Day day, Shift shift) const noexcept;
  /** @brief Devuelve el conjunto de pacientes programados. */
  [[nodiscard]] const std::unordered_set<PatientId>& GetScheduledPatients() const noexcept;
  /** @brief Devuelve el numero de pacientes programados. */
  [[nodiscard]] int GetNumScheduledPatients() const noexcept;

  /** @brief Devuelve la ocupacion de una habitacion en un dia. */
  [[nodiscard]] int GetRoomOccupancy(RoomId room_id, Day day) const noexcept;
  /** @brief Devuelve la carga de un quirofano en un dia. */
  [[nodiscard]] int GetOTLoad(OperatingTheaterId ot_id, Day day) const noexcept;
  /** @brief Devuelve la carga de un cirujano en un dia. */
  [[nodiscard]] int GetSurgeonLoad(SurgeonId surgeon_id, Day day) const noexcept;
  /** @brief Devuelve la carga de una enfermera en un dia-turno. */
  [[nodiscard]] int GetNurseWorkload(NurseId nurse_id, Day day, Shift shift) const noexcept;
  /** @brief Devuelve el genero de una habitacion-dia (restriccion de mezcla). */
  [[nodiscard]] Gender GetRoomGender(RoomId room_id, Day day) const noexcept;
  /** @brief Devuelve la lista de pacientes de una habitacion-dia. */
  [[nodiscard]] const std::vector<PatientId>& GetRoomPatients(RoomId room_id, Day day) const noexcept;

  /** @brief Devuelve los datos del problema asociados. */
  [[nodiscard]] const ProblemData& GetProblem() const noexcept;

 private:
  /** @brief Carga en los caches los ocupantes preexistentes. */
  void InitializeOccupancyFromOccupants();
  /** @brief Actualiza los caches al asignar un paciente. */
  void AddPatientToCaches(PatientId patient_id, RoomId room_id,
                          Day admission_day, OperatingTheaterId ot_id);
  /** @brief Actualiza los caches al desasignar un paciente. */
  void RemovePatientFromCaches(PatientId patient_id, RoomId room_id,
                               Day admission_day, OperatingTheaterId ot_id);
  /** @brief Recalcula el genero de una habitacion-dia. */
  void UpdateRoomGender(RoomId room_id, Day day);

  /** @brief Indice plano para el cache habitacion-dia. */
  [[nodiscard]] int RoomDayIndex(RoomId room, Day day) const noexcept {
    return room * num_days_ + day;
  }

  /** @brief Indice plano para el cache quirofano-dia. */
  [[nodiscard]] int OtDayIndex(OperatingTheaterId ot, Day day) const noexcept {
    return ot * num_days_ + day;
  }

  /** @brief Indice plano para el cache cirujano-dia. */
  [[nodiscard]] int SurgeonDayIndex(SurgeonId surgeon, Day day) const noexcept {
    return surgeon * num_days_ + day;
  }

  /** @brief Indice plano 3D para el cache habitacion-dia-turno. */
  [[nodiscard]] int RoomDayShiftIndex(RoomId room, Day day, Shift shift) const noexcept {
    return FlatIndex3D(room, day, shift, num_days_, num_shifts_);
  }

  /** @brief Indice plano 3D para el cache enfermera-dia-turno. */
  [[nodiscard]] int NurseDayShiftIndex(NurseId nurse, Day day, Shift shift) const noexcept {
    return FlatIndex3D(nurse, day, shift, num_days_, num_shifts_);
  }

  const ProblemData& problem_;  // referencia a los datos del problema

  // Vectores planos indexados por PatientId. Si vale kInvalidId no esta asignado
  std::vector<RoomId> patient_room_;              // habitacion de cada paciente
  std::vector<Day> patient_admission_day_;        // dia de ingreso
  std::vector<OperatingTheaterId> patient_ot_;    // quirofano asignado
  std::unordered_set<PatientId> scheduled_patients_;  // para saber rapido quien esta programado

  // asignacion de enfermeras: array 3D aplanado [room][day][shift] -> nurse_id
  std::vector<NurseId> nurse_assignments_;

  // Caches delta: se actualizan incrementalmente al asignar/desasignar pacientes
  std::vector<int> room_occupancy_;     // ocupacion por habitacion-dia
  std::vector<int> ot_load_;            // carga de cada quirofano por dia
  std::vector<int> surgeon_load_;       // carga de cada cirujano por dia
  std::vector<int> nurse_workload_;     // carga de cada enfermera por dia-turno
  std::vector<Gender> room_gender_;     // genero de cada hab-dia (para restriccion de mezcla)
  std::vector<std::vector<PatientId>> room_day_patients_;  // lista de pacientes por hab-dia

  // dimensiones del problema
  int num_rooms_;
  int num_days_;
  int num_shifts_;
  int num_ots_;
  int num_surgeons_;
  int num_nurses_;
  int num_patients_;
};

#endif  // SRC_SOLUTION_SOLUTION_H_
