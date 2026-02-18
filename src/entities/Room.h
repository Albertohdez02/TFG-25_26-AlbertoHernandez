// Room.h - Habitacion de hospital
// TFG Alberto Hernandez
//
// Habitacion con capacidad fija. Las restricciones de mezcla de genero/edad
// se evaluan a nivel de solucion, no aqui.

#ifndef SRC_ENTITIES_ROOM_H_
#define SRC_ENTITIES_ROOM_H_

#include <string>

#include "../common/types.h"

class Room {
 public:
  Room() : index_(kInvalidId), capacity_(0) {}

  Room(std::string id, RoomId index, int capacity)
      : id_(std::move(id)), index_(index), capacity_(capacity) {}

  [[nodiscard]] const std::string& GetId() const noexcept { return id_; }
  [[nodiscard]] RoomId GetIndex() const noexcept { return index_; }
  [[nodiscard]] int GetCapacity() const noexcept { return capacity_; }

  // se pueden mwter mas pacientes?
  [[nodiscard]] bool HasCapacityFor(int current_occupancy,
                                    int additional = 1) const noexcept {
    return (current_occupancy + additional) <= capacity_;
  }

  // pacientes de mas hay (0 si no se pasa)
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
