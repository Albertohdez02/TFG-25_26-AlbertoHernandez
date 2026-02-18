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

// Clase que representa una solucion (asignacion de pacientes a recursos)
// variables de decision: que pacientes se programan, en que dia,
// habitacion, quirofano, y que enfermeras se asignan a cada turno
class Solution {
 public:
  explicit Solution(const ProblemData& problem);  // crea solucion vacia
  Solution(const Solution& other);                 // copia
  Solution(Solution&& other) noexcept;            // move (no se copia, se transfiere la propiedad de los datos)
  Solution& operator=(const Solution& other);
  Solution& operator=(Solution&& other) noexcept;
  
  // asigna un paciente a habitacion, dia y quirofano
  // false si ya estaba asignado
  bool AssignPatient(PatientId patient_id, RoomId room_id, Day admission_day,
                     OperatingTheaterId ot_id);

  // quita un paciente del planning
  bool UnassignPatient(PatientId patient_id);

  // mueve un paciente a otra habitacion/dia/quirofano (mas eficiente que quitar+poner)
  bool ReassignPatient(PatientId patient_id, RoomId new_room_id,
                       Day new_admission_day, OperatingTheaterId new_ot_id);

  // operaciones de asignacion de enfermeras
  bool AssignNurse(NurseId nurse_id, RoomId room_id, Day day, Shift shift);
  bool UnassignNurse(RoomId room_id, Day day, Shift shift);

  // consultas sobre variables de decision de los pacientes
  [[nodiscard]] bool IsPatientScheduled(PatientId patient_id) const noexcept;
  [[nodiscard]] RoomId GetPatientRoom(PatientId patient_id) const noexcept;
  [[nodiscard]] Day GetPatientAdmissionDay(PatientId patient_id) const noexcept;
  [[nodiscard]] OperatingTheaterId GetPatientOT(PatientId patient_id) const noexcept;
  [[nodiscard]] NurseId GetNurseAssignment(RoomId room_id, Day day, Shift shift) const noexcept;
  [[nodiscard]] const std::unordered_set<PatientId>& GetScheduledPatients() const noexcept;
  [[nodiscard]] int GetNumScheduledPatients() const noexcept;

  // consultas a los caches
  [[nodiscard]] int GetRoomOccupancy(RoomId room_id, Day day) const noexcept;
  [[nodiscard]] int GetOTLoad(OperatingTheaterId ot_id, Day day) const noexcept;
  [[nodiscard]] int GetSurgeonLoad(SurgeonId surgeon_id, Day day) const noexcept;
  [[nodiscard]] int GetNurseWorkload(NurseId nurse_id, Day day, Shift shift) const noexcept;
  [[nodiscard]] Gender GetRoomGender(RoomId room_id, Day day) const noexcept;
  [[nodiscard]] const std::vector<PatientId>& GetRoomPatients(RoomId room_id, Day day) const noexcept;

  [[nodiscard]] const ProblemData& GetProblem() const noexcept;

 private:
  // metodos auxiliares para mantener los caches actualizados al asignar/desasignar pacientes
  void InitializeOccupancyFromOccupants();  // carga ocupantes preexistentes
  void AddPatientToCaches(PatientId patient_id, RoomId room_id,
                          Day admission_day, OperatingTheaterId ot_id);
  void RemovePatientFromCaches(PatientId patient_id, RoomId room_id,
                               Day admission_day, OperatingTheaterId ot_id);
  void UpdateRoomGender(RoomId room_id, Day day);

  // helpers para calcular indices planos en los arrays aplanados
  
  [[nodiscard]] int RoomDayIndex(RoomId room, Day day) const noexcept {
    return room * num_days_ + day;
  }

  [[nodiscard]] int OtDayIndex(OperatingTheaterId ot, Day day) const noexcept {
    return ot * num_days_ + day;
  }

  [[nodiscard]] int SurgeonDayIndex(SurgeonId surgeon, Day day) const noexcept {
    return surgeon * num_days_ + day;
  }

  [[nodiscard]] int RoomDayShiftIndex(RoomId room, Day day, Shift shift) const noexcept {
    return FlatIndex3D(room, day, shift, num_days_, num_shifts_);
  }

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

  // Caches delta 
  // se actualizan incrementalmente al asignar/desasignar pacientes
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
