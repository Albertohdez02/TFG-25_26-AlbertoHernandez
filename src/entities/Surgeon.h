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

/** @brief Cirujano con tiempo maximo de cirugia por dia.
 *  Si se excede ese tiempo se generan horas extra (penalizacion blanda).
 */
class Surgeon {
 public:
  /** @brief Construye un cirujano vacio con indice invalido. */
  Surgeon() : index_(kInvalidId) {}

  /** @brief Construye un cirujano con su id, indice y tiempo maximo por dia. */
  Surgeon(std::string id, SurgeonId index, std::vector<int> max_surgery_time)
      : id_(std::move(id)),
        index_(index),
        max_surgery_time_(std::move(max_surgery_time)) {}

  /** @brief Devuelve el id del cirujano. */
  [[nodiscard]] const std::string& GetId() const noexcept { return id_; }
  /** @brief Devuelve el indice del cirujano. */
  [[nodiscard]] SurgeonId GetIndex() const noexcept { return index_; }

  /** @brief Devuelve el tiempo maximo de cirugia para todos los dias. */
  [[nodiscard]] const std::vector<int>& GetMaxSurgeryTime() const noexcept {
    return max_surgery_time_;
  }

  /** @brief Devuelve el tiempo maximo de cirugia del dia, o 0 si el dia esta fuera de rango. */
  [[nodiscard]] int GetMaxSurgeryTimeForDay(Day day) const noexcept {
    return (day >= 0 && day < static_cast<int>(max_surgery_time_.size()))
               ? max_surgery_time_[day]
               : 0;
  }

  /** @brief Indica si el cirujano puede operar ese dia. */
  [[nodiscard]] bool IsAvailableOnDay(Day day) const noexcept {
    return GetMaxSurgeryTimeForDay(day) > 0;
  }

  /** @brief Devuelve las horas extra del dia, o 0 si no se excede el tiempo maximo. */
  [[nodiscard]] int GetOvertime(Day day, int total_surgery_time) const noexcept {
    int max_time = GetMaxSurgeryTimeForDay(day);
    return (total_surgery_time > max_time) ? (total_surgery_time - max_time)
                                           : 0;
  }

  /** @brief Devuelve el tiempo restante del dia (negativo si ya se ha excedido). */
  [[nodiscard]] int GetRemainingTime(Day day, int used_time) const noexcept {
    return GetMaxSurgeryTimeForDay(day) - used_time;
  }

 private:
  std::string id_;
  SurgeonId index_;
  std::vector<int> max_surgery_time_;  // tiempo max por dia
};

#endif  // SRC_ENTITIES_SURGEON_H_
