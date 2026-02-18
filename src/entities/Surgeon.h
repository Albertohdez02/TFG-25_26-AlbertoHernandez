// Surgeon.h - Cirujano
// TFG Alberto Hernandez
//
// Cirujano con tiempo maximo de cirugia por dia.
// si se pasa, hay horas extra (penalizacion blanda).

#ifndef SRC_ENTITIES_SURGEON_H_
#define SRC_ENTITIES_SURGEON_H_

#include <string>
#include <vector>

#include "../common/types.h"

class Surgeon {
 public:
  Surgeon() : index_(kInvalidId) {}

  Surgeon(std::string id, SurgeonId index, std::vector<int> max_surgery_time)
      : id_(std::move(id)),
        index_(index),
        max_surgery_time_(std::move(max_surgery_time)) {}

  [[nodiscard]] const std::string& GetId() const noexcept { return id_; }
  [[nodiscard]] SurgeonId GetIndex() const noexcept { return index_; }

  [[nodiscard]] const std::vector<int>& GetMaxSurgeryTime() const noexcept {
    return max_surgery_time_;
  }

  [[nodiscard]] int GetMaxSurgeryTimeForDay(Day day) const noexcept {
    return (day >= 0 && day < static_cast<int>(max_surgery_time_.size()))
               ? max_surgery_time_[day]
               : 0;
  }

  // puede operar este dia?
  [[nodiscard]] bool IsAvailableOnDay(Day day) const noexcept {
    return GetMaxSurgeryTimeForDay(day) > 0;
  }

  // horas extra que hace 
  [[nodiscard]] int GetOvertime(Day day, int total_surgery_time) const noexcept {
    int max_time = GetMaxSurgeryTimeForDay(day);
    return (total_surgery_time > max_time) ? (total_surgery_time - max_time)
                                           : 0;
  }

  // tiempo que le queda (negativo si ya se paso)
  [[nodiscard]] int GetRemainingTime(Day day, int used_time) const noexcept {
    return GetMaxSurgeryTimeForDay(day) - used_time;
  }

 private:
  std::string id_;
  SurgeonId index_;
  std::vector<int> max_surgery_time_;  // tiempo max por dia
};

#endif  // SRC_ENTITIES_SURGEON_H_
