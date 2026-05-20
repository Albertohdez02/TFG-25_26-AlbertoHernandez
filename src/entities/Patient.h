// Patient.h - Paciente que necesita cirugia
// TFG Alberto Hernandez
//
// Representa un paciente que hay que operar y luego hospitalizar.
// puede ser obligatorio u opcional.

#ifndef SRC_ENTITIES_PATIENT_H_
#define SRC_ENTITIES_PATIENT_H_

#include <string>
#include <vector>

#include "../common/types.h"

class Patient {
 public:
  Patient()
      : index_(kInvalidId),
        gender_(kGenderAny),
        age_group_(0),
        length_of_stay_(0),
        mandatory_(false),
        surgery_release_day_(0),
        surgery_due_day_(0),
        surgery_duration_(0),
        surgeon_id_(kInvalidId) {}

  Patient(std::string id, PatientId index, Gender gender, AgeGroup age_group,
          int length_of_stay, std::vector<int> workload,
          std::vector<int> skill_req, bool mandatory, Day release_day,
          Day due_day, int surgery_duration, SurgeonId surgeon_id,
          std::vector<RoomId> incompatible_rooms)
      : id_(std::move(id)),
        index_(index),
        gender_(gender),
        age_group_(age_group),
        length_of_stay_(length_of_stay),
        workload_produced_(std::move(workload)),
        skill_level_required_(std::move(skill_req)),
        mandatory_(mandatory),
        surgery_release_day_(release_day),
        surgery_due_day_(due_day),
        surgery_duration_(surgery_duration),
        surgeon_id_(surgeon_id),
        incompatible_rooms_(std::move(incompatible_rooms)) {}

  // Getters
  [[nodiscard]] const std::string& GetId() const noexcept { return id_; }
  [[nodiscard]] PatientId GetIndex() const noexcept { return index_; }
  [[nodiscard]] Gender GetGender() const noexcept { return gender_; }
  [[nodiscard]] AgeGroup GetAgeGroup() const noexcept { return age_group_; }
  [[nodiscard]] int GetLengthOfStay() const noexcept { return length_of_stay_; }

  [[nodiscard]] const std::vector<int>& GetWorkloadProduced() const noexcept {
    return workload_produced_;
  }

  // carga producida en el (dia-de-estancia, turno).
  // workload_produced_ tiene length_of_stay * shifts_per_day entradas.
  [[nodiscard]] int GetWorkloadAt(int day_in_stay,
                                  Shift shift) const noexcept {
    if (length_of_stay_ <= 0) return 0;
    int shifts_per_day =
        static_cast<int>(workload_produced_.size()) / length_of_stay_;
    int idx = day_in_stay * shifts_per_day + shift;
    return (idx >= 0 && idx < static_cast<int>(workload_produced_.size()))
               ? workload_produced_[idx]
               : 0;
  }

  [[nodiscard]] const std::vector<int>& GetSkillLevelRequired() const noexcept {
    return skill_level_required_;
  }

  // skill exigido en el (dia-de-estancia, turno).
  // skill_level_required_ tiene length_of_stay * shifts_per_day entradas.
  [[nodiscard]] SkillLevel GetSkillLevelAt(int day_in_stay,
                                            Shift shift) const noexcept {
    if (length_of_stay_ <= 0) return 0;
    int shifts_per_day =
        static_cast<int>(skill_level_required_.size()) / length_of_stay_;
    int idx = day_in_stay * shifts_per_day + shift;
    return (idx >= 0 && idx < static_cast<int>(skill_level_required_.size()))
               ? skill_level_required_[idx]
               : 0;
  }

  [[nodiscard]] bool IsMandatory() const noexcept { return mandatory_; }
  [[nodiscard]] bool IsOptional() const noexcept { return !mandatory_; }

  [[nodiscard]] Day GetSurgeryReleaseDay() const noexcept {
    return surgery_release_day_;
  }
  [[nodiscard]] Day GetSurgeryDueDay() const noexcept {
    return surgery_due_day_;
  }
  [[nodiscard]] int GetSurgeryDuration() const noexcept {
    return surgery_duration_;
  }
  [[nodiscard]] SurgeonId GetSurgeonId() const noexcept { return surgeon_id_; }

  // habitaciones donde NO puede estar
  [[nodiscard]] const std::vector<RoomId>& GetIncompatibleRooms()
      const noexcept {
    return incompatible_rooms_;
  }

  // metodos auxiliares

  // comprueba si puede ir a esta habitacion
  [[nodiscard]] bool IsCompatibleWithRoom(RoomId room_id) const noexcept {
    for (RoomId incompatible : incompatible_rooms_) {
      if (incompatible == room_id) return false;
    }
    return true;
  }

  // puede operarse este dia?
  [[nodiscard]] bool CanHaveSurgeryOnDay(Day day) const noexcept {
    return day >= surgery_release_day_;
  }

  // dias de espera respecto a release_day (0 si entra el dia mas temprano).
  // Coincide con la definicion oficial de PatientDelay del IHTC 2024.
  [[nodiscard]] int GetDelayDays(Day admission_day) const noexcept {
    return (admission_day > surgery_release_day_)
               ? (admission_day - surgery_release_day_)
               : 0;
  }

  // ultimo dia que ocupa cama
  [[nodiscard]] Day GetLastDayOfStay(Day admission_day) const noexcept {
    return admission_day + length_of_stay_ - 1;
  }

 private:
  std::string id_;       // ID original del JSON
  PatientId index_;      // indice para asignado
  Gender gender_;        // genero (afecta restriccion de mezcla)
  AgeGroup age_group_;   // grupo de edad
  int length_of_stay_;   // dias de hospitalizacion post-cirugia
  std::vector<int> workload_produced_;      // carga por turno
  std::vector<int> skill_level_required_;   // skill necesario por turno
  bool mandatory_;             // si es obligatorio programarlo
  Day surgery_release_day_;    // primer dia que se puede operar
  Day surgery_due_day_;        // dia limite (si te pasas, penalizas)
  int surgery_duration_;       // duracion de la cirugia en minutos
  SurgeonId surgeon_id_;       // cirujano asignado (fijo)
  std::vector<RoomId> incompatible_rooms_;  // habitaciones prohibidas
};

#endif  // SRC_ENTITIES_PATIENT_H_
