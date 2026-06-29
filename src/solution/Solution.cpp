// Solution.cpp - Implementacion de la clase Solution
// TFG Alberto Hernandez
//
// Logica de asignar/desasignar pacientes y mantenimiento de los caches.

#include "Solution.h"

#include <algorithm>

/** @brief Inicializa los vectores vacios y carga los ocupantes preexistentes. */
Solution::Solution(const ProblemData& problem)
    : problem_(problem),
      num_rooms_(problem.GetNumRooms()),
      num_days_(problem.GetNumDays()),
      num_shifts_(problem.GetNumShiftTypes()),
      num_ots_(problem.GetNumOperatingTheaters()),
      num_surgeons_(problem.GetNumSurgeons()),
      num_nurses_(problem.GetNumNurses()),
      num_patients_(problem.GetNumPatients()) {
  // variables de decision sin asignar al principio
  patient_room_.resize(num_patients_, kInvalidId);
  patient_admission_day_.resize(num_patients_, kInvalidId);
  patient_ot_.resize(num_patients_, kInvalidId);
  nurse_assignments_.resize(num_rooms_ * num_days_ * num_shifts_, kInvalidId);

  // caches a cero
  room_occupancy_.resize(num_rooms_ * num_days_, 0);
  ot_load_.resize(num_ots_ * num_days_, 0);
  surgeon_load_.resize(num_surgeons_ * num_days_, 0);
  nurse_workload_.resize(num_nurses_ * num_days_ * num_shifts_, 0);
  room_gender_.resize(num_rooms_ * num_days_, kGenderAny);

  room_day_patients_.resize(num_rooms_ * num_days_);

  // los ocupantes ya estaban en el hospital: se anaden a los caches
  InitializeOccupancyFromOccupants();
}

/** @brief Copy constructor: copia profunda de todo el estado. */
Solution::Solution(const Solution& other)
    : problem_(other.problem_),
      patient_room_(other.patient_room_),
      patient_admission_day_(other.patient_admission_day_),
      patient_ot_(other.patient_ot_),
      scheduled_patients_(other.scheduled_patients_),
      nurse_assignments_(other.nurse_assignments_),
      room_occupancy_(other.room_occupancy_),
      ot_load_(other.ot_load_),
      surgeon_load_(other.surgeon_load_),
      nurse_workload_(other.nurse_workload_),
      room_gender_(other.room_gender_),
      room_day_patients_(other.room_day_patients_),
      num_rooms_(other.num_rooms_),
      num_days_(other.num_days_),
      num_shifts_(other.num_shifts_),
      num_ots_(other.num_ots_),
      num_surgeons_(other.num_surgeons_),
      num_nurses_(other.num_nurses_),
      num_patients_(other.num_patients_) {}

/** @brief Move constructor: mueve los vectores con std::move. */
Solution::Solution(Solution&& other) noexcept
    : problem_(other.problem_),
      patient_room_(std::move(other.patient_room_)),
      patient_admission_day_(std::move(other.patient_admission_day_)),
      patient_ot_(std::move(other.patient_ot_)),
      scheduled_patients_(std::move(other.scheduled_patients_)),
      nurse_assignments_(std::move(other.nurse_assignments_)),
      room_occupancy_(std::move(other.room_occupancy_)),
      ot_load_(std::move(other.ot_load_)),
      surgeon_load_(std::move(other.surgeon_load_)),
      nurse_workload_(std::move(other.nurse_workload_)),
      room_gender_(std::move(other.room_gender_)),
      room_day_patients_(std::move(other.room_day_patients_)),
      num_rooms_(other.num_rooms_),
      num_days_(other.num_days_),
      num_shifts_(other.num_shifts_),
      num_ots_(other.num_ots_),
      num_surgeons_(other.num_surgeons_),
      num_nurses_(other.num_nurses_),
      num_patients_(other.num_patients_) {}

/** @brief Operador de asignacion por copia. */
Solution& Solution::operator=(const Solution& other) {
  if (this != &other) {
    // problem_ es const ref: tiene que ser el mismo problema
    patient_room_ = other.patient_room_;
    patient_admission_day_ = other.patient_admission_day_;
    patient_ot_ = other.patient_ot_;
    scheduled_patients_ = other.scheduled_patients_;
    nurse_assignments_ = other.nurse_assignments_;
    room_occupancy_ = other.room_occupancy_;
    ot_load_ = other.ot_load_;
    surgeon_load_ = other.surgeon_load_;
    nurse_workload_ = other.nurse_workload_;
    room_gender_ = other.room_gender_;
    room_day_patients_ = other.room_day_patients_;
  }
  return *this;
}

/** @brief Operador de asignacion por movimiento. */
Solution& Solution::operator=(Solution&& other) noexcept {
  if (this != &other) {
    patient_room_ = std::move(other.patient_room_);
    patient_admission_day_ = std::move(other.patient_admission_day_);
    patient_ot_ = std::move(other.patient_ot_);
    scheduled_patients_ = std::move(other.scheduled_patients_);
    nurse_assignments_ = std::move(other.nurse_assignments_);
    room_occupancy_ = std::move(other.room_occupancy_);
    ot_load_ = std::move(other.ot_load_);
    surgeon_load_ = std::move(other.surgeon_load_);
    nurse_workload_ = std::move(other.nurse_workload_);
    room_gender_ = std::move(other.room_gender_);
    room_day_patients_ = std::move(other.room_day_patients_);
  }
  return *this;
}

/** @brief Asigna un paciente y actualiza los caches; false si ya estaba asignado. */
bool Solution::AssignPatient(PatientId patient_id, RoomId room_id,
                             Day admission_day, OperatingTheaterId ot_id) {
  if (scheduled_patients_.count(patient_id) > 0) {
    return false;
  }

  patient_room_[patient_id] = room_id;
  patient_admission_day_[patient_id] = admission_day;
  patient_ot_[patient_id] = ot_id;
  scheduled_patients_.insert(patient_id);

  AddPatientToCaches(patient_id, room_id, admission_day, ot_id);

  return true;
}

/** @brief Quita un paciente del planning; false si no estaba asignado. */
bool Solution::UnassignPatient(PatientId patient_id) {
  if (scheduled_patients_.count(patient_id) == 0) {
    return false;
  }

  // guardar los valores antes de borrarlos para actualizar caches
  RoomId room_id = patient_room_[patient_id];
  Day admission_day = patient_admission_day_[patient_id];
  OperatingTheaterId ot_id = patient_ot_[patient_id];

  // primero actualizar los caches, luego borrar las variables
  RemovePatientFromCaches(patient_id, room_id, admission_day, ot_id);

  patient_room_[patient_id] = kInvalidId;
  patient_admission_day_[patient_id] = kInvalidId;
  patient_ot_[patient_id] = kInvalidId;
  scheduled_patients_.erase(patient_id);

  return true;
}

/** @brief Mueve un paciente a otra posicion; false si no estaba ya asignado. */
bool Solution::ReassignPatient(PatientId patient_id, RoomId new_room_id,
                               Day new_admission_day,
                               OperatingTheaterId new_ot_id) {
  if (scheduled_patients_.count(patient_id) == 0) {
    return false;
  }

  // guardar la asignacion vieja
  RoomId old_room_id = patient_room_[patient_id];
  Day old_admission_day = patient_admission_day_[patient_id];
  OperatingTheaterId old_ot_id = patient_ot_[patient_id];

  RemovePatientFromCaches(patient_id, old_room_id, old_admission_day,
                          old_ot_id);

  patient_room_[patient_id] = new_room_id;
  patient_admission_day_[patient_id] = new_admission_day;
  patient_ot_[patient_id] = new_ot_id;

  AddPatientToCaches(patient_id, new_room_id, new_admission_day, new_ot_id);

  return true;
}

/** @brief Asigna una enfermera a una habitacion-dia-turno; false si ya hay una.
 *  Suma a la carga de la enfermera el workload de los pacientes y ocupantes
 *  presentes en esa habitacion ese dia y turno.
 */
bool Solution::AssignNurse(NurseId nurse_id, RoomId room_id, Day day,
                           Shift shift) {
  int idx = RoomDayShiftIndex(room_id, day, shift);

  if (nurse_assignments_[idx] != kInvalidId) {
    return false;
  }

  nurse_assignments_[idx] = nurse_id;

  const auto& patients_in_room = GetRoomPatients(room_id, day);
  int total_workload = 0;
  for (PatientId pid : patients_in_room) {
    Day admission = patient_admission_day_[pid];
    int day_in_stay = day - admission;
    total_workload +=
        problem_.GetPatient(pid).GetWorkloadAt(day_in_stay, shift);
  }

  // tambien los ocupantes
  for (const auto& occ : problem_.GetOccupants()) {
    if (occ.GetRoomId() == room_id && occ.IsPresentOnDay(day)) {
      total_workload += occ.GetWorkloadAt(day, shift);
    }
  }

  int nurse_idx = NurseDayShiftIndex(nurse_id, day, shift);
  nurse_workload_[nurse_idx] += total_workload;

  return true;
}

/** @brief Quita la enfermera de una habitacion-dia-turno; false si no habia.
 *  Resta de la carga de la enfermera el workload de pacientes y ocupantes.
 */
bool Solution::UnassignNurse(RoomId room_id, Day day, Shift shift) {
  int idx = RoomDayShiftIndex(room_id, day, shift);
  NurseId nurse_id = nurse_assignments_[idx];

  if (nurse_id == kInvalidId) {
    return false;
  }

  const auto& patients_in_room = GetRoomPatients(room_id, day);
  int total_workload = 0;
  for (PatientId pid : patients_in_room) {
    Day admission = patient_admission_day_[pid];
    int day_in_stay = day - admission;
    total_workload +=
        problem_.GetPatient(pid).GetWorkloadAt(day_in_stay, shift);
  }

  for (const auto& occ : problem_.GetOccupants()) {
    if (occ.GetRoomId() == room_id && occ.IsPresentOnDay(day)) {
      total_workload += occ.GetWorkloadAt(day, shift);
    }
  }

  int nurse_idx = NurseDayShiftIndex(nurse_id, day, shift);
  nurse_workload_[nurse_idx] -= total_workload;

  nurse_assignments_[idx] = kInvalidId;

  return true;
}

/** @brief Indica si el paciente esta programado. */
bool Solution::IsPatientScheduled(PatientId patient_id) const noexcept {
  return scheduled_patients_.count(patient_id) > 0;
}

/** @brief Devuelve la habitacion asignada al paciente. */
RoomId Solution::GetPatientRoom(PatientId patient_id) const noexcept {
  return patient_room_[patient_id];
}

/** @brief Devuelve el dia de ingreso del paciente. */
Day Solution::GetPatientAdmissionDay(PatientId patient_id) const noexcept {
  return patient_admission_day_[patient_id];
}

/** @brief Devuelve el quirofano asignado al paciente. */
OperatingTheaterId Solution::GetPatientOT(PatientId patient_id) const noexcept {
  return patient_ot_[patient_id];
}

/** @brief Devuelve la enfermera asignada a una habitacion-dia-turno. */
NurseId Solution::GetNurseAssignment(RoomId room_id, Day day,
                                     Shift shift) const noexcept {
  return nurse_assignments_[RoomDayShiftIndex(room_id, day, shift)];
}

/** @brief Devuelve el conjunto de pacientes programados. */
const std::unordered_set<PatientId>& Solution::GetScheduledPatients()
    const noexcept {
  return scheduled_patients_;
}

/** @brief Devuelve el numero de pacientes programados. */
int Solution::GetNumScheduledPatients() const noexcept {
  return static_cast<int>(scheduled_patients_.size());
}

/** @brief Devuelve la ocupacion de una habitacion en un dia. */
int Solution::GetRoomOccupancy(RoomId room_id, Day day) const noexcept {
  return room_occupancy_[RoomDayIndex(room_id, day)];
}

/** @brief Devuelve la carga del quirofano en un dia. */
int Solution::GetOTLoad(OperatingTheaterId ot_id, Day day) const noexcept {
  return ot_load_[OtDayIndex(ot_id, day)];
}

/** @brief Devuelve la carga del cirujano en un dia. */
int Solution::GetSurgeonLoad(SurgeonId surgeon_id, Day day) const noexcept {
  return surgeon_load_[SurgeonDayIndex(surgeon_id, day)];
}

/** @brief Devuelve la carga de la enfermera en un dia-turno. */
int Solution::GetNurseWorkload(NurseId nurse_id, Day day,
                               Shift shift) const noexcept {
  return nurse_workload_[NurseDayShiftIndex(nurse_id, day, shift)];
}

/** @brief Devuelve el genero de la habitacion ese dia, o -2 si hay mezcla. */
Gender Solution::GetRoomGender(RoomId room_id, Day day) const noexcept {
  return room_gender_[RoomDayIndex(room_id, day)];
}

/** @brief Devuelve los pacientes de una habitacion ese dia (sin ocupantes). */
const std::vector<PatientId>& Solution::GetRoomPatients(RoomId room_id,
                                                        Day day) const noexcept {
  return room_day_patients_[RoomDayIndex(room_id, day)];
}

/** @brief Devuelve los datos del problema. */
const ProblemData& Solution::GetProblem() const noexcept {
  return problem_;
}

/** @brief Carga en los caches la ocupacion inicial de los ocupantes. */
void Solution::InitializeOccupancyFromOccupants() {
  for (const auto& occupant : problem_.GetOccupants()) {
    RoomId room_id = occupant.GetRoomId();
    int stay_length = occupant.GetLengthOfStay();
    Gender gender = occupant.GetGender();

    // el ocupante esta desde dia 0 hasta (length_of_stay - 1)
    for (Day day = 0; day < stay_length && day < num_days_; ++day) {
      int idx = RoomDayIndex(room_id, day);
      room_occupancy_[idx] += 1;

      if (room_gender_[idx] == kGenderAny) {
        room_gender_[idx] = gender;
      } else if (room_gender_[idx] != gender && gender != kGenderAny) {
        room_gender_[idx] = -2;  // mezcla de generos = violacion
      }
    }
  }
}

/** @brief Anade un paciente a todos los caches al asignarlo.
 *  Actualiza ocupacion de habitacion, carga de quirofano, carga de cirujano y
 *  carga de enfermeras de cada dia de la estancia.
 */
void Solution::AddPatientToCaches(PatientId patient_id, RoomId room_id,
                                  Day admission_day, OperatingTheaterId ot_id) {
  const Patient& patient = problem_.GetPatient(patient_id);
  int stay_length = patient.GetLengthOfStay();
  int surgery_duration = patient.GetSurgeryDuration();
  SurgeonId surgeon_id = patient.GetSurgeonId();

  // ocupacion de habitacion: +1 por cada dia de estancia
  for (int d = 0; d < stay_length; ++d) {
    Day day = admission_day + d;
    if (day >= num_days_) break;  // no pasar del horizonte

    int idx = RoomDayIndex(room_id, day);
    room_occupancy_[idx] += 1;
    room_day_patients_[idx].push_back(patient_id);

    UpdateRoomGender(room_id, day);
  }

  // carga del quirofano (la cirugia es el dia de ingreso)
  ot_load_[OtDayIndex(ot_id, admission_day)] += surgery_duration;

  surgeon_load_[SurgeonDayIndex(surgeon_id, admission_day)] += surgery_duration;

  // carga de enfermeras: solo si hay enfermera asignada a esa habitacion-dia-turno
  for (int d = 0; d < stay_length; ++d) {
    Day day = admission_day + d;
    if (day >= num_days_) break;

    for (Shift shift = 0; shift < num_shifts_; ++shift) {
      NurseId nurse_id = GetNurseAssignment(room_id, day, shift);
      if (nurse_id != kInvalidId) {
        int workload = patient.GetWorkloadAt(d, shift);
        nurse_workload_[NurseDayShiftIndex(nurse_id, day, shift)] += workload;
      }
    }
  }
}

/** @brief Quita un paciente de todos los caches al desasignar o reasignar. */
void Solution::RemovePatientFromCaches(PatientId patient_id, RoomId room_id,
                                       Day admission_day,
                                       OperatingTheaterId ot_id) {
  const Patient& patient = problem_.GetPatient(patient_id);
  int stay_length = patient.GetLengthOfStay();
  int surgery_duration = patient.GetSurgeryDuration();
  SurgeonId surgeon_id = patient.GetSurgeonId();

  // quitar de la ocupacion de habitacion
  for (int d = 0; d < stay_length; ++d) {
    Day day = admission_day + d;
    if (day >= num_days_) break;

    int idx = RoomDayIndex(room_id, day);
    room_occupancy_[idx] -= 1;

    // borrar de la lista de pacientes de esa habitacion-dia
    auto& patients = room_day_patients_[idx];
    patients.erase(std::remove(patients.begin(), patients.end(), patient_id),
                   patients.end());

    UpdateRoomGender(room_id, day);
  }

  ot_load_[OtDayIndex(ot_id, admission_day)] -= surgery_duration;

  surgeon_load_[SurgeonDayIndex(surgeon_id, admission_day)] -= surgery_duration;

  // quitar la carga de enfermeras
  for (int d = 0; d < stay_length; ++d) {
    Day day = admission_day + d;
    if (day >= num_days_) break;

    for (Shift shift = 0; shift < num_shifts_; ++shift) {
      NurseId nurse_id = GetNurseAssignment(room_id, day, shift);
      if (nurse_id != kInvalidId) {
        int workload = patient.GetWorkloadAt(d, shift);
        nurse_workload_[NurseDayShiftIndex(nurse_id, day, shift)] -= workload;
      }
    }
  }
}

/** @brief Recalcula el genero de una habitacion-dia (-2 si hay mezcla). */
void Solution::UpdateRoomGender(RoomId room_id, Day day) {
  int idx = RoomDayIndex(room_id, day);
  const auto& patients = room_day_patients_[idx];

  // mirar los ocupantes
  Gender current_gender = kGenderAny;
  for (const auto& occupant : problem_.GetOccupants()) {
    if (occupant.GetRoomId() == room_id && occupant.IsPresentOnDay(day)) {
      Gender occ_gender = occupant.GetGender();
      if (current_gender == kGenderAny) {
        current_gender = occ_gender;
      } else if (current_gender != occ_gender && occ_gender != kGenderAny) {
        current_gender = -2;  // hay mezcla
        break;
      }
    }
  }

  // mirar los pacientes
  for (PatientId pid : patients) {
    Gender patient_gender = problem_.GetPatient(pid).GetGender();
    if (current_gender == kGenderAny) {
      current_gender = patient_gender;
    } else if (current_gender != patient_gender &&
               patient_gender != kGenderAny && current_gender != -2) {
      current_gender = -2;  // hay mezcla
      break;
    }
  }

  room_gender_[idx] = current_gender;
}
