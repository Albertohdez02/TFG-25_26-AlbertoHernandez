// Solution.cpp - Implementacion de la clase Solution
// TFG Alberto Hernandez
//
// Aqui esta toda la logica de asignar/desasignar pacientes
// y mantener los caches actualizados

#include "Solution.h"

#include <algorithm>

// inicializa todos los vectores vacios y carga los ocupantes preexistentes
Solution::Solution(const ProblemData& problem)
    : problem_(problem),
      num_rooms_(problem.GetNumRooms()),
      num_days_(problem.GetNumDays()),
      num_shifts_(problem.GetNumShiftTypes()),
      num_ots_(problem.GetNumOperatingTheaters()),
      num_surgeons_(problem.GetNumSurgeons()),
      num_nurses_(problem.GetNumNurses()),
      num_patients_(problem.GetNumPatients()) {
  // vectores de decisionsin asignar al principio
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

  // los ocupantes ya estaban en el hospital, se añaden a los caches
  InitializeOccupancyFromOccupants();
}

// copy constructor - copia profunda de todo
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

// move constructor - mueve todo con std::move (mas eficiente)
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

// operador de asignacion por copia
Solution& Solution::operator=(const Solution& other) {
  if (this != &other) {
    // problem_ es const ref, tiene que ser el mismo problema
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

// operadpr de asignacion por movimiento
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

// ASIGNACION DE PACIENTES

// asigna un paciente, false si ya estaba asignado
bool Solution::AssignPatient(PatientId patient_id, RoomId room_id,
                             Day admission_day, OperatingTheaterId ot_id) {
  // si ya esta programado, no lo vuelvo a meter
  if (scheduled_patients_.count(patient_id) > 0) {
    return false;
  }

  // guardar la asignacion
  patient_room_[patient_id] = room_id;
  patient_admission_day_[patient_id] = admission_day;
  patient_ot_[patient_id] = ot_id;
  scheduled_patients_.insert(patient_id);

  // actualizar los caches
  AddPatientToCaches(patient_id, room_id, admission_day, ot_id);

  return true;
}

// quita un paciente del planning
bool Solution::UnassignPatient(PatientId patient_id) {
  if (scheduled_patients_.count(patient_id) == 0) {
    return false;  // no estaba asignado
  }

  // guardar los valores antes de borrarlos para actualizar caches
  RoomId room_id = patient_room_[patient_id];
  Day admission_day = patient_admission_day_[patient_id];
  OperatingTheaterId ot_id = patient_ot_[patient_id];

  // primero actualizar, luego borro
  RemovePatientFromCaches(patient_id, room_id, admission_day, ot_id);

  // marcar como no asignado
  patient_room_[patient_id] = kInvalidId;
  patient_admission_day_[patient_id] = kInvalidId;
  patient_ot_[patient_id] = kInvalidId;
  scheduled_patients_.erase(patient_id);

  return true;
}

// mueve un paciente a otra posicion 
bool Solution::ReassignPatient(PatientId patient_id, RoomId new_room_id,
                               Day new_admission_day,
                               OperatingTheaterId new_ot_id) {
  if (scheduled_patients_.count(patient_id) == 0) {
    return false;  // tiene que estar ya asignado para reasignar
  }

  // guardar la asignacion vieja
  RoomId old_room_id = patient_room_[patient_id];
  Day old_admission_day = patient_admission_day_[patient_id];
  OperatingTheaterId old_ot_id = patient_ot_[patient_id];

  RemovePatientFromCaches(patient_id, old_room_id, old_admission_day,
                          old_ot_id);

  // actualizar las variables de decision
  patient_room_[patient_id] = new_room_id;
  patient_admission_day_[patient_id] = new_admission_day;
  patient_ot_[patient_id] = new_ot_id;

  // añadir a los caches nuevos
  AddPatientToCaches(patient_id, new_room_id, new_admission_day, new_ot_id);

  return true;
}

// ASIGNACION DE ENFERMERAS

// asigna enfermera a una habitacion-dia-turno
bool Solution::AssignNurse(NurseId nurse_id, RoomId room_id, Day day,
                           Shift shift) {
  int idx = RoomDayShiftIndex(room_id, day, shift);

  // hay una enfermera asignada, error
  if (nurse_assignments_[idx] != kInvalidId) {
    return false;
  }

  nurse_assignments_[idx] = nurse_id;

  // calcular la carga de trabajo sumando todos los pacientes de esa habitacion ese dia
  const auto& patients_in_room = GetRoomPatients(room_id, day);
  int total_workload = 0;
  for (PatientId pid : patients_in_room) {
    total_workload += problem_.GetPatient(pid).GetWorkloadForShift(shift);
  }

  // tambien los ocupantes
  for (const auto& occ : problem_.GetOccupants()) {
    if (occ.GetRoomId() == room_id && occ.IsPresentOnDay(day)) {
      total_workload += occ.GetWorkloadForShift(shift);
    }
  }

  int nurse_idx = NurseDayShiftIndex(nurse_id, day, shift);
  nurse_workload_[nurse_idx] += total_workload;

  return true;
}

// quita enfermera de habitacion-dia-turno
bool Solution::UnassignNurse(RoomId room_id, Day day, Shift shift) {
  int idx = RoomDayShiftIndex(room_id, day, shift);
  NurseId nurse_id = nurse_assignments_[idx];

  if (nurse_id == kInvalidId) {
    return false;  // no habia nadie asignado
  }

  // cuanta carga hay que quitar
  const auto& patients_in_room = GetRoomPatients(room_id, day);
  int total_workload = 0;
  for (PatientId pid : patients_in_room) {
    total_workload += problem_.GetPatient(pid).GetWorkloadForShift(shift);
  }

  for (const auto& occ : problem_.GetOccupants()) {
    if (occ.GetRoomId() == room_id && occ.IsPresentOnDay(day)) {
      total_workload += occ.GetWorkloadForShift(shift);
    }
  }

  int nurse_idx = NurseDayShiftIndex(nurse_id, day, shift);
  nurse_workload_[nurse_idx] -= total_workload;

  nurse_assignments_[idx] = kInvalidId;

  return true;
}

// CONSULTAS SOBRE ASIGNACIONES

bool Solution::IsPatientScheduled(PatientId patient_id) const noexcept {
  return scheduled_patients_.count(patient_id) > 0;
}

RoomId Solution::GetPatientRoom(PatientId patient_id) const noexcept {
  return patient_room_[patient_id];
}

Day Solution::GetPatientAdmissionDay(PatientId patient_id) const noexcept {
  return patient_admission_day_[patient_id];
}

OperatingTheaterId Solution::GetPatientOT(PatientId patient_id) const noexcept {
  return patient_ot_[patient_id];
}

NurseId Solution::GetNurseAssignment(RoomId room_id, Day day,
                                     Shift shift) const noexcept {
  return nurse_assignments_[RoomDayShiftIndex(room_id, day, shift)];
}

const std::unordered_set<PatientId>& Solution::GetScheduledPatients()
    const noexcept {
  return scheduled_patients_;
}

int Solution::GetNumScheduledPatients() const noexcept {
  return static_cast<int>(scheduled_patients_.size());
}

// CONSULTAS SOBRE CACHES

int Solution::GetRoomOccupancy(RoomId room_id, Day day) const noexcept {
  return room_occupancy_[RoomDayIndex(room_id, day)];
}

int Solution::GetOTLoad(OperatingTheaterId ot_id, Day day) const noexcept {
  return ot_load_[OtDayIndex(ot_id, day)];
}

int Solution::GetSurgeonLoad(SurgeonId surgeon_id, Day day) const noexcept {
  return surgeon_load_[SurgeonDayIndex(surgeon_id, day)];
}

int Solution::GetNurseWorkload(NurseId nurse_id, Day day,
                               Shift shift) const noexcept {
  return nurse_workload_[NurseDayShiftIndex(nurse_id, day, shift)];
}

// devuelve el genero de la habitacion ese dia, o -2 si hay mezcla
Gender Solution::GetRoomGender(RoomId room_id, Day day) const noexcept {
  return room_gender_[RoomDayIndex(room_id, day)];
}

// devuelve la lista de pacientes asignados a esa habitacion ese dia (no incluye ocupantes)
const std::vector<PatientId>& Solution::GetRoomPatients(RoomId room_id,
                                                        Day day) const noexcept {
  return room_day_patients_[RoomDayIndex(room_id, day)];
}

const ProblemData& Solution::GetProblem() const noexcept {
  return problem_;
}

// METODOS AUXILIARES 

// carga la ocupacion inicial de los ocupantes
void Solution::InitializeOccupancyFromOccupants() {
  for (const auto& occupant : problem_.GetOccupants()) {
    RoomId room_id = occupant.GetRoomId();
    int stay_length = occupant.GetLengthOfStay();
    Gender gender = occupant.GetGender();

    // el ocupante esta desde dia 0 hasta (length_of_stay - 1)
    for (Day day = 0; day < stay_length && day < num_days_; ++day) {
      int idx = RoomDayIndex(room_id, day);
      room_occupancy_[idx] += 1;

      // actualizar el genero de la habitacion
      if (room_gender_[idx] == kGenderAny) {
        room_gender_[idx] = gender;
      } else if (room_gender_[idx] != gender && gender != kGenderAny) {
        room_gender_[idx] = -2;  // mezcla de generos = violacion
      }
    }
  }
}

// añade un paciente a todos los caches (ocupacion, carga de quirofano, 
// cirujano, enfermeras) cuando lo asignas
void Solution::AddPatientToCaches(PatientId patient_id, RoomId room_id,
                                  Day admission_day, OperatingTheaterId ot_id) {
  const Patient& patient = problem_.GetPatient(patient_id);
  int stay_length = patient.GetLengthOfStay();
  int surgery_duration = patient.GetSurgeryDuration();
  SurgeonId surgeon_id = patient.GetSurgeonId();

  // ocupacion de habitacion: +1 por cada dia que esta
  for (int d = 0; d < stay_length; ++d) {
    Day day = admission_day + d;
    if (day >= num_days_) break;  // no me pasar del horizonte

    int idx = RoomDayIndex(room_id, day);
    room_occupancy_[idx] += 1;
    room_day_patients_[idx].push_back(patient_id);

    UpdateRoomGender(room_id, day);
  }

  // carga del quirofano (la cirugia es el dia de ingreso)
  ot_load_[OtDayIndex(ot_id, admission_day)] += surgery_duration;

  // carga del cirujano
  surgeon_load_[SurgeonDayIndex(surgeon_id, admission_day)] += surgery_duration;

  // carga de enfermeras: si hay enfermera asignada, sumar la carga
  for (int d = 0; d < stay_length; ++d) {
    Day day = admission_day + d;
    if (day >= num_days_) break;

    for (Shift shift = 0; shift < num_shifts_; ++shift) {
      NurseId nurse_id = GetNurseAssignment(room_id, day, shift);
      if (nurse_id != kInvalidId) {
        int workload = patient.GetWorkloadForShift(shift);
        nurse_workload_[NurseDayShiftIndex(nurse_id, day, shift)] += workload;
      }
    }
  }
}

// quita un paciente de todos los caches (cuando se desasigna o se reasigna)
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

  // quitar la carga del quirofano
  ot_load_[OtDayIndex(ot_id, admission_day)] -= surgery_duration;

  // quitar la carga del cirujano
  surgeon_load_[SurgeonDayIndex(surgeon_id, admission_day)] -= surgery_duration;

  // quitar la carga de enfermeras
  for (int d = 0; d < stay_length; ++d) {
    Day day = admission_day + d;
    if (day >= num_days_) break;

    for (Shift shift = 0; shift < num_shifts_; ++shift) {
      NurseId nurse_id = GetNurseAssignment(room_id, day, shift);
      if (nurse_id != kInvalidId) {
        int workload = patient.GetWorkloadForShift(shift);
        nurse_workload_[NurseDayShiftIndex(nurse_id, day, shift)] -= workload;
      }
    }
  }
}

// recalcular el genero de una habitacion-dia (para saber si hay mezcla)
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
