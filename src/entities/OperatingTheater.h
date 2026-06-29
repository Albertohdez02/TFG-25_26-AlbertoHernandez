// OperatingTheater.h - Quirofano
// TFG Alberto Hernandez
//
// Quirofano con disponibilidad diaria (minutos).

#ifndef SRC_ENTITIES_OPERATING_THEATER_H_
#define SRC_ENTITIES_OPERATING_THEATER_H_

#include <string>
#include <vector>

#include "../common/types.h"

/** @brief Quirofano con disponibilidad diaria en minutos. */
class OperatingTheater {
 public:
  /** @brief Construye un quirofano vacio con indice invalido. */
  OperatingTheater() : index_(kInvalidId) {}

  /** @brief Construye un quirofano con id, indice y disponibilidad por dia. */
  OperatingTheater(std::string id, OperatingTheaterId index,
                   std::vector<int> availability)
      : id_(std::move(id)),
        index_(index),
        availability_(std::move(availability)) {}

  /** @brief Devuelve el id externo del quirofano. */
  [[nodiscard]] const std::string& GetId() const noexcept { return id_; }
  /** @brief Devuelve el indice interno del quirofano. */
  [[nodiscard]] OperatingTheaterId GetIndex() const noexcept { return index_; }

  /** @brief Devuelve el vector de minutos disponibles por dia. */
  [[nodiscard]] const std::vector<int>& GetAvailability() const noexcept {
    return availability_;
  }

  /** @brief Devuelve los minutos disponibles en el dia, o 0 si esta fuera de rango. */
  [[nodiscard]] int GetAvailabilityForDay(Day day) const noexcept {
    return (day >= 0 && day < static_cast<int>(availability_.size()))
               ? availability_[day]
               : 0;
  }

  /** @brief Indica si el quirofano esta abierto ese dia. */
  [[nodiscard]] bool IsOpenOnDay(Day day) const noexcept {
    return GetAvailabilityForDay(day) > 0;
  }

  /** @brief Indica si el quirofano esta cerrado ese dia (usarlo penaliza). */
  [[nodiscard]] bool IsClosedOnDay(Day day) const noexcept {
    return GetAvailabilityForDay(day) == 0;
  }

  /** @brief Devuelve los minutos de horas extra, o 0 si no se excede la disponibilidad. */
  [[nodiscard]] int GetOvertime(Day day, int total_used_time) const noexcept {
    int avail = GetAvailabilityForDay(day);
    return (total_used_time > avail) ? (total_used_time - avail) : 0;
  }

  /** @brief Devuelve el tiempo restante del dia (negativo si hay exceso). */
  [[nodiscard]] int GetRemainingTime(Day day, int used_time) const noexcept {
    return GetAvailabilityForDay(day) - used_time;
  }

 private:
  std::string id_;
  OperatingTheaterId index_;
  std::vector<int> availability_;  // minutos disponibles por dia
};

#endif  // SRC_ENTITIES_OPERATING_THEATER_H_
