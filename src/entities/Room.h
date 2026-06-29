// Room.h - Habitacion de hospital
// TFG Alberto Hernandez
//
// Habitacion con capacidad fija. Las restricciones de mezcla de genero/edad
// se evaluan a nivel de solucion, no aqui.

#ifndef SRC_ENTITIES_ROOM_H_
#define SRC_ENTITIES_ROOM_H_

#include <string>

#include "../common/types.h"

/** @brief Habitacion de hospital con capacidad fija. */
class Room {
 public:
  /** @brief Construye una habitacion vacia con indice invalido. */
  Room() : index_(kInvalidId), capacity_(0) {}

  /** @brief Construye una habitacion con su id, indice y capacidad. */
  Room(std::string id, RoomId index, int capacity)
      : id_(std::move(id)), index_(index), capacity_(capacity) {}

  /** @brief Devuelve el id externo de la habitacion. */
  [[nodiscard]] const std::string& GetId() const noexcept { return id_; }
  /** @brief Devuelve el indice interno de la habitacion. */
  [[nodiscard]] RoomId GetIndex() const noexcept { return index_; }
  /** @brief Devuelve la capacidad maxima de pacientes. */
  [[nodiscard]] int GetCapacity() const noexcept { return capacity_; }

  /** @brief Indica si caben additional pacientes mas dada la ocupacion actual. */
  [[nodiscard]] bool HasCapacityFor(int current_occupancy,
                                    int additional = 1) const noexcept {
    return (current_occupancy + additional) <= capacity_;
  }

  /** @brief Devuelve el numero de pacientes que exceden la capacidad (0 si no se pasa). */
  [[nodiscard]] int GetCapacityViolation(int current_occupancy) const noexcept {
    return (current_occupancy > capacity_) ? (current_occupancy - capacity_)
                                           : 0;
  }

 private:
  std::string id_;
  RoomId index_;
  int capacity_;  // maximo de pacientes a la vez
};

#endif  // SRC_ENTITIES_ROOM_H_
