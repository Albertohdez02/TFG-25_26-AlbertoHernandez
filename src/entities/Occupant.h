// Occupant.h - Paciente ya hospitalizado
// TFG Alberto Hernandez
//
// Son pacientes que ya estaban en el hospital antes de empezar la planificacion.
// Tienen habitacion fija (no se pueden mover) pero generan carga de trabajo.

#ifndef SRC_ENTITIES_OCCUPANT_H_
#define SRC_ENTITIES_OCCUPANT_H_

#include <string>
#include <vector>

#include "../common/types.h"

/** @brief Paciente ya ingresado antes de la planificacion: habitacion fija pero genera carga. */
class Occupant {
 public:
  /** @brief Construye un ocupante vacio con identificadores invalidos. */
  Occupant()
      : index_(kInvalidId),
        gender_(kGenderAny),
        age_group_(0),
        length_of_stay_(0),
        room_id_(kInvalidId) {}

  /** @brief Construye un ocupante con todos sus datos. */
  Occupant(std::string id, OccupantId index, Gender gender, AgeGroup age_group,
           int length_of_stay, std::vector<int> workload,
           std::vector<int> skill_req, RoomId room_id)
      : id_(std::move(id)),
        index_(index),
        gender_(gender),
        age_group_(age_group),
        length_of_stay_(length_of_stay),
        workload_produced_(std::move(workload)),
        skill_level_required_(std::move(skill_req)),
        room_id_(room_id) {}

  /** @brief Devuelve el identificador textual del ocupante. */
  [[nodiscard]] const std::string& GetId() const noexcept { return id_; }
  /** @brief Devuelve el indice interno del ocupante. */
  [[nodiscard]] OccupantId GetIndex() const noexcept { return index_; }
  /** @brief Devuelve el genero del ocupante. */
  [[nodiscard]] Gender GetGender() const noexcept { return gender_; }
  /** @brief Devuelve el grupo de edad del ocupante. */
  [[nodiscard]] AgeGroup GetAgeGroup() const noexcept { return age_group_; }
  /** @brief Devuelve los dias de estancia restantes. */
  [[nodiscard]] int GetLengthOfStay() const noexcept { return length_of_stay_; }

  /** @brief Devuelve el vector de carga producida por turno. */
  [[nodiscard]] const std::vector<int>& GetWorkloadProduced() const noexcept {
    return workload_produced_;
  }

  /** @brief Carga producida en (dia, turno).
   *  Para ocupantes el dia-de-estancia coincide con el dia absoluto (ya estan
   *  ingresados desde el dia 0).
   */
  [[nodiscard]] int GetWorkloadAt(int day, Shift shift) const noexcept {
    if (length_of_stay_ <= 0) return 0;
    int shifts_per_day =
        static_cast<int>(workload_produced_.size()) / length_of_stay_;
    int idx = day * shifts_per_day + shift;
    return (idx >= 0 && idx < static_cast<int>(workload_produced_.size()))
               ? workload_produced_[idx]
               : 0;
  }

  /** @brief Devuelve el vector de skill exigido por turno. */
  [[nodiscard]] const std::vector<int>& GetSkillLevelRequired() const noexcept {
    return skill_level_required_;
  }

  /** @brief Skill exigido en (dia, turno) durante la estancia del ocupante. */
  [[nodiscard]] SkillLevel GetSkillLevelAt(int day,
                                            Shift shift) const noexcept {
    if (length_of_stay_ <= 0) return 0;
    int shifts_per_day =
        static_cast<int>(skill_level_required_.size()) / length_of_stay_;
    int idx = day * shifts_per_day + shift;
    return (idx >= 0 && idx < static_cast<int>(skill_level_required_.size()))
               ? skill_level_required_[idx]
               : 0;
  }

  /** @brief Devuelve la habitacion asignada (fija, no cambia). */
  [[nodiscard]] RoomId GetRoomId() const noexcept { return room_id_; }

  /** @brief Indica si el ocupante sigue en el hospital ese dia. */
  [[nodiscard]] bool IsPresentOnDay(Day day) const noexcept {
    return day < length_of_stay_;
  }

  /** @brief Devuelve el ultimo dia de estancia del ocupante. */
  [[nodiscard]] Day GetLastDayOfStay() const noexcept {
    return length_of_stay_ - 1;
  }

 private:
  std::string id_;
  OccupantId index_;
  Gender gender_;
  AgeGroup age_group_;
  int length_of_stay_;                    // dias que le quedan
  std::vector<int> workload_produced_;    // carga por turno
  std::vector<int> skill_level_required_; // skill necesario por turno
  RoomId room_id_;                        // habitacion FIJA
};

#endif  // SRC_ENTITIES_OCCUPANT_H_
