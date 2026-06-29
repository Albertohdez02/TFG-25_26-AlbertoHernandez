// types.h - tipos globales y constantes
// TFG Alberto Hernandez

#ifndef SRC_COMMON_TYPES_H_
#define SRC_COMMON_TYPES_H_

#include <cstdint>
#include <limits>
#include <string>

// Tipos de ID: todos int para usarlos como indices de vectores directamente.
using PatientId = int;
using OccupantId = int;
using RoomId = int;
using NurseId = int;
using SurgeonId = int;
using OperatingTheaterId = int;
using Day = int;                // dia (empieza en 0)
using Shift = int;              // turno (0=manana, 1=tarde, 2=noche)
using SkillLevel = int;
using AgeGroup = int;
using Gender = int;             // genero (0=mujer, 1=hombre, -1=cualquiera)

// Constantes globales.
constexpr int kInvalidId = -1;  // cuando algo no esta asignado
constexpr Gender kGenderAny = -1;
constexpr Gender kGenderFemale = 0;
constexpr Gender kGenderMale = 1;
constexpr int kMaxDays = 21;    // maximo de dias en el horizonte
constexpr int kDefaultNumShifts = 3;
constexpr int kInfiniteCost = std::numeric_limits<int>::max() / 2;  // coste alto para penalizaciones

/** @brief Pesos de la funcion objetivo: penalizacion por violar cada restriccion blanda. */
struct Weights {
  int room_mixed_age = 0;              // mezclar edades en habitacion
  int room_nurse_skill = 0;            // enfermera sin suficiente skill
  int continuity_of_care = 0;          // cambiar de enfermera durante estancia
  int patient_delay = 0;               // retraso por dia
  int unscheduled_optional = 0;        // no programar paciente opcional
  int room_gender_mix = 0;             // mezclar generos en habitacion
  int room_capacity_violation = 0;     // pasarse de capacidad
  int nurse_excessive_workload = 0;    // sobrecargar enfermera
  int open_operating_theater = 0;      // abrir quirofano extra
  int surgeon_overtime = 0;            // horas extra del cirujano
  int operating_theater_overtime = 0;  // horas extra del quirofano
  int surgeon_transfer = 0;            // cirujano cambiando de quirofano
};

/** @brief Turno de trabajo de enfermera: dia, turno y carga maxima que aguanta. */
struct WorkingShift {
  Day day;
  Shift shift_index;
  int max_load;

  WorkingShift() : day(0), shift_index(0), max_load(0) {}
  WorkingShift(Day d, Shift s, int ml) : day(d), shift_index(s), max_load(ml) {}
};

// Aplanado de indices: un solo vector en vez de vector<vector<int>> evita
// saltos de memoria.

/** @brief Convierte coordenadas 2D a indice 1D: idx = row * num_cols + col. */
[[nodiscard]] inline constexpr int FlatIndex2D(int row, int col,
                                               int num_cols) noexcept {
  return row * num_cols + col;
}

/** @brief Convierte coordenadas 3D a indice 1D. */
[[nodiscard]] inline constexpr int FlatIndex3D(int d1, int d2, int d3,
                                               int size_d2,
                                               int size_d3) noexcept {
  return (d1 * size_d2 + d2) * size_d3 + d3;
}

#endif // SRC_COMMON_TYPES_H_
