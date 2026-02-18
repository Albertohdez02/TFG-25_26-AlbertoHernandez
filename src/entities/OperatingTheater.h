// OperatingTheater.h - Quirofano
// TFG Alberto Hernandez
//
// Quirofano con disponibilidad diaria (minutos).

#ifndef SRC_ENTITIES_OPERATING_THEATER_H_
#define SRC_ENTITIES_OPERATING_THEATER_H_

#include <string>
#include <vector>

#include "../common/types.h"

class OperatingTheater {
 public:
  OperatingTheater() : index_(kInvalidId) {}

  OperatingTheater(std::string id, OperatingTheaterId index,
                   std::vector<int> availability)
      : id_(std::move(id)),
        index_(index),
        availability_(std::move(availability)) {}

  [[nodiscard]] const std::string& GetId() const noexcept { return id_; }
  [[nodiscard]] OperatingTheaterId GetIndex() const noexcept { return index_; }

  [[nodiscard]] const std::vector<int>& GetAvailability() const noexcept {
    return availability_;
  }

  [[nodiscard]] int GetAvailabilityForDay(Day day) const noexcept {
    return (day >= 0 && day < static_cast<int>(availability_.size()))
               ? availability_[day]
               : 0;
  }

  [[nodiscard]] bool IsOpenOnDay(Day day) const noexcept {
    return GetAvailabilityForDay(day) > 0;
  }

  // tsta cerrado? (usarlo = penalizacion)
  [[nodiscard]] bool IsClosedOnDay(Day day) const noexcept {
    return GetAvailabilityForDay(day) == 0;
  }

  // horas extra (0 si no se pasa)
  [[nodiscard]] int GetOvertime(Day day, int total_used_time) const noexcept {
    int avail = GetAvailabilityForDay(day);
    return (total_used_time > avail) ? (total_used_time - avail) : 0;
  }

  // tiempo restante (negativo si hay exceso)
  [[nodiscard]] int GetRemainingTime(Day day, int used_time) const noexcept {
    return GetAvailabilityForDay(day) - used_time;
  }

 private:
  std::string id_;
  OperatingTheaterId index_;
  std::vector<int> availability_;  // minutos disponibles por dia
};

#endif  // SRC_ENTITIES_OPERATING_THEATER_H_
