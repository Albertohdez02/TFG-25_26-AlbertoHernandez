// Nurse.h - Enfermera
// TFG Alberto Hernandez
// Enfermera con nivel de habilidad y turnos en los que trabaja.
// Se asignan a habitaciones en turnos especificos.

#ifndef SRC_ENTITIES_NURSE_H_
#define SRC_ENTITIES_NURSE_H_

#include <string>
#include <vector>

#include "../common/types.h"

class Nurse {
 public:
  Nurse() : index_(kInvalidId), skill_level_(0) {}

  Nurse(std::string id, NurseId index, SkillLevel skill_level,
        std::vector<WorkingShift> working_shifts)
      : id_(std::move(id)),
        index_(index),
        skill_level_(skill_level),
        working_shifts_(std::move(working_shifts)) {}

  [[nodiscard]] const std::string& GetId() const noexcept { return id_; }
  [[nodiscard]] NurseId GetIndex() const noexcept { return index_; }
  [[nodiscard]] SkillLevel GetSkillLevel() const noexcept {
    return skill_level_;
  }

  [[nodiscard]] const std::vector<WorkingShift>& GetWorkingShifts()
      const noexcept {
    return working_shifts_;
  }

  // trabaja este dia y turno?
  [[nodiscard]] bool IsAvailable(Day day, Shift shift) const noexcept {
    for (const auto& ws : working_shifts_) {
      if (ws.day == day && ws.shift_index == shift) {
        return true;
      }
    }
    return false;
  }

  // carga maxima para ese dia-turno (0 si no trabaja)
  [[nodiscard]] int GetMaxWorkload(Day day, Shift shift) const noexcept {
    for (const auto& ws : working_shifts_) {
      if (ws.day == day && ws.shift_index == shift) {
        return ws.max_load;
      }
    }
    return 0;
  }

  // esta habilitada?
  [[nodiscard]] bool HasSkillLevel(SkillLevel required_skill) const noexcept {
    return skill_level_ >= required_skill;
  }

  // numero de turnos que trabaja
  [[nodiscard]] int GetNumWorkingShifts() const noexcept {
    return static_cast<int>(working_shifts_.size());
  }

 private:
  std::string id_;
  NurseId index_;
  SkillLevel skill_level_;
  std::vector<WorkingShift> working_shifts_;  // turnos y carga maxima
};

#endif  // SRC_ENTITIES_NURSE_H_
