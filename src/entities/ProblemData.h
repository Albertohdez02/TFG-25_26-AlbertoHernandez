// ProblemData.h - Contenedor de datos del problema
// TFG Alberto Hernandez
//
// Guarda todos los datos de la instancia (pacientes, habitaciones, etc.)
// es inmutable despues de parsearlo, solo se lee.

#ifndef SRC_ENTITIES_PROBLEM_DATA_H_
#define SRC_ENTITIES_PROBLEM_DATA_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "../common/types.h"
#include "Nurse.h"
#include "Occupant.h"
#include "OperatingTheater.h"
#include "Patient.h"
#include "Room.h"
#include "Surgeon.h"

/** @brief Contenedor inmutable de los datos de una instancia IHTC.
 *  Almacena pacientes, habitaciones, enfermeras, quirofanos y pesos; tras el
 *  parseo solo se lee.
 */
class ProblemData {
 public:
  /** @brief Construye un contenedor vacio con contadores a cero. */
  ProblemData()
      : num_days_(0),
        num_skill_levels_(0),
        num_shift_types_(kDefaultNumShifts),
        num_age_groups_(0) {}

  /** @brief Devuelve el nombre del fichero de procedencia. */
  [[nodiscard]] const std::string& GetFilename() const noexcept {
    return filename_;
  }
  /** @brief Devuelve el horizonte de planificacion en dias. */
  [[nodiscard]] int GetNumDays() const noexcept { return num_days_; }
  /** @brief Devuelve el numero de niveles de habilidad de enfermeria. */
  [[nodiscard]] int GetNumSkillLevels() const noexcept {
    return num_skill_levels_;
  }
  /** @brief Devuelve el numero de tipos de turno por dia. */
  [[nodiscard]] int GetNumShiftTypes() const noexcept {
    return num_shift_types_;
  }
  /** @brief Devuelve el numero de grupos de edad. */
  [[nodiscard]] int GetNumAgeGroups() const noexcept { return num_age_groups_; }
  /** @brief Devuelve los nombres de los turnos. */
  [[nodiscard]] const std::vector<std::string>& GetShiftNames() const noexcept {
    return shift_names_;
  }

  /** @brief Numero de pacientes. */
  [[nodiscard]] int GetNumPatients() const noexcept {
    return static_cast<int>(patients_.size());
  }
  /** @brief Numero de ocupantes. */
  [[nodiscard]] int GetNumOccupants() const noexcept {
    return static_cast<int>(occupants_.size());
  }
  /** @brief Numero de habitaciones. */
  [[nodiscard]] int GetNumRooms() const noexcept {
    return static_cast<int>(rooms_.size());
  }
  /** @brief Numero de enfermeras. */
  [[nodiscard]] int GetNumNurses() const noexcept {
    return static_cast<int>(nurses_.size());
  }
  /** @brief Numero de cirujanos. */
  [[nodiscard]] int GetNumSurgeons() const noexcept {
    return static_cast<int>(surgeons_.size());
  }
  /** @brief Numero de quirofanos. */
  [[nodiscard]] int GetNumOperatingTheaters() const noexcept {
    return static_cast<int>(operating_theaters_.size());
  }
  /** @brief Numero de pacientes obligatorios. */
  [[nodiscard]] int GetNumMandatoryPatients() const noexcept {
    return static_cast<int>(mandatory_patient_ids_.size());
  }
  /** @brief Numero de pacientes opcionales. */
  [[nodiscard]] int GetNumOptionalPatients() const noexcept {
    return static_cast<int>(optional_patient_ids_.size());
  }

  /** @brief Vector de pacientes. */
  [[nodiscard]] const std::vector<Patient>& GetPatients() const noexcept {
    return patients_;
  }
  /** @brief Vector de ocupantes. */
  [[nodiscard]] const std::vector<Occupant>& GetOccupants() const noexcept {
    return occupants_;
  }
  /** @brief Vector de habitaciones. */
  [[nodiscard]] const std::vector<Room>& GetRooms() const noexcept {
    return rooms_;
  }
  /** @brief Vector de enfermeras. */
  [[nodiscard]] const std::vector<Nurse>& GetNurses() const noexcept {
    return nurses_;
  }
  /** @brief Vector de cirujanos. */
  [[nodiscard]] const std::vector<Surgeon>& GetSurgeons() const noexcept {
    return surgeons_;
  }
  /** @brief Vector de quirofanos. */
  [[nodiscard]] const std::vector<OperatingTheater>& GetOperatingTheaters()
      const noexcept {
    return operating_theaters_;
  }

  /** @brief Devuelve los IDs de los pacientes obligatorios. */
  [[nodiscard]] const std::vector<PatientId>& GetMandatoryPatientIds()
      const noexcept {
    return mandatory_patient_ids_;
  }
  /** @brief Devuelve los IDs de los pacientes opcionales. */
  [[nodiscard]] const std::vector<PatientId>& GetOptionalPatientIds()
      const noexcept {
    return optional_patient_ids_;
  }

  /** @brief Paciente por su ID. */
  [[nodiscard]] const Patient& GetPatient(PatientId id) const noexcept {
    return patients_[id];
  }
  /** @brief Ocupante por su ID. */
  [[nodiscard]] const Occupant& GetOccupant(OccupantId id) const noexcept {
    return occupants_[id];
  }
  /** @brief Habitacion por su ID. */
  [[nodiscard]] const Room& GetRoom(RoomId id) const noexcept {
    return rooms_[id];
  }
  /** @brief Enfermera por su ID. */
  [[nodiscard]] const Nurse& GetNurse(NurseId id) const noexcept {
    return nurses_[id];
  }
  /** @brief Cirujano por su ID. */
  [[nodiscard]] const Surgeon& GetSurgeon(SurgeonId id) const noexcept {
    return surgeons_[id];
  }
  /** @brief Quirofano por su ID. */
  [[nodiscard]] const OperatingTheater& GetOperatingTheater(
      OperatingTheaterId id) const noexcept {
    return operating_theaters_[id];
  }

  /** @brief Devuelve los pesos de la funcion objetivo. */
  [[nodiscard]] const Weights& GetWeights() const noexcept { return weights_; }

  /** @brief Traduce un ID textual a indice; kInvalidId si no existe.
   *  Usado durante el parseo para resolver referencias entre entidades.
   */
  [[nodiscard]] PatientId GetPatientIdByString(
      const std::string& str_id) const {
    auto it = patient_id_map_.find(str_id);
    return (it != patient_id_map_.end()) ? it->second : kInvalidId;
  }

  /** @brief Traduce un ID textual de habitacion a indice; kInvalidId si falta. */
  [[nodiscard]] RoomId GetRoomIdByString(const std::string& str_id) const {
    auto it = room_id_map_.find(str_id);
    return (it != room_id_map_.end()) ? it->second : kInvalidId;
  }

  /** @brief Traduce un ID textual de enfermera a indice; kInvalidId si falta. */
  [[nodiscard]] NurseId GetNurseIdByString(const std::string& str_id) const {
    auto it = nurse_id_map_.find(str_id);
    return (it != nurse_id_map_.end()) ? it->second : kInvalidId;
  }

  /** @brief Traduce un ID textual de cirujano a indice; kInvalidId si falta. */
  [[nodiscard]] SurgeonId GetSurgeonIdByString(
      const std::string& str_id) const {
    auto it = surgeon_id_map_.find(str_id);
    return (it != surgeon_id_map_.end()) ? it->second : kInvalidId;
  }

  /** @brief Traduce un ID textual de quirofano a indice; kInvalidId si falta. */
  [[nodiscard]] OperatingTheaterId GetOperatingTheaterIdByString(
      const std::string& str_id) const {
    auto it = ot_id_map_.find(str_id);
    return (it != ot_id_map_.end()) ? it->second : kInvalidId;
  }

  /** @brief Fija el nombre de fichero de la instancia. */
  void SetFilename(std::string filename) { filename_ = std::move(filename); }
  /** @brief Fija el numero de dias del horizonte. */
  void SetNumDays(int days) { num_days_ = days; }
  /** @brief Fija el numero de niveles de habilidad. */
  void SetNumSkillLevels(int levels) { num_skill_levels_ = levels; }
  /** @brief Fija el numero de tipos de turno. */
  void SetNumShiftTypes(int shifts) { num_shift_types_ = shifts; }
  /** @brief Fija el numero de grupos de edad. */
  void SetNumAgeGroups(int groups) { num_age_groups_ = groups; }
  /** @brief Fija los nombres de los turnos. */
  void SetShiftNames(std::vector<std::string> names) {
    shift_names_ = std::move(names);
  }
  /** @brief Fija los pesos de la funcion objetivo. */
  void SetWeights(Weights w) { weights_ = w; }

  /** @brief Anade un paciente y lo registra en el mapa y en la lista
   *  obligatoria u opcional segun su tipo.
   */
  void AddPatient(Patient patient) {
    PatientId idx = static_cast<PatientId>(patients_.size());
    patient_id_map_[patient.GetId()] = idx;
    if (patient.IsMandatory()) {
      mandatory_patient_ids_.push_back(idx);
    } else {
      optional_patient_ids_.push_back(idx);
    }
    patients_.push_back(std::move(patient));
  }

  /** @brief Anade un ocupante y lo registra en su mapa de indices. */
  void AddOccupant(Occupant occupant) {
    occupant_id_map_[occupant.GetId()] =
        static_cast<OccupantId>(occupants_.size());
    occupants_.push_back(std::move(occupant));
  }

  /** @brief Anade una habitacion y la registra en su mapa de indices. */
  void AddRoom(Room room) {
    room_id_map_[room.GetId()] = static_cast<RoomId>(rooms_.size());
    rooms_.push_back(std::move(room));
  }

  /** @brief Anade una enfermera y la registra en su mapa de indices. */
  void AddNurse(Nurse nurse) {
    nurse_id_map_[nurse.GetId()] = static_cast<NurseId>(nurses_.size());
    nurses_.push_back(std::move(nurse));
  }

  /** @brief Anade un cirujano y lo registra en su mapa de indices. */
  void AddSurgeon(Surgeon surgeon) {
    surgeon_id_map_[surgeon.GetId()] =
        static_cast<SurgeonId>(surgeons_.size());
    surgeons_.push_back(std::move(surgeon));
  }

  /** @brief Anade un quirofano y lo registra en su mapa de indices. */
  void AddOperatingTheater(OperatingTheater ot) {
    ot_id_map_[ot.GetId()] =
        static_cast<OperatingTheaterId>(operating_theaters_.size());
    operating_theaters_.push_back(std::move(ot));
  }

  /** @brief Reserva capacidad en los vectores antes de cargar datos.
   *  Evita reallocs y rehashing durante el parseo.
   */
  void Reserve(int patients, int occupants, int rooms, int nurses, int surgeons,
               int ots) {
    patients_.reserve(patients);
    occupants_.reserve(occupants);
    rooms_.reserve(rooms);
    nurses_.reserve(nurses);
    surgeons_.reserve(surgeons);
    operating_theaters_.reserve(ots);
  }

 private:
  std::string filename_;                   // fichero de procedencia
  int num_days_;                           // horizonte de planificacion
  int num_skill_levels_;
  int num_shift_types_;
  int num_age_groups_;
  std::vector<std::string> shift_names_;

  std::vector<Patient> patients_;
  std::vector<Occupant> occupants_;
  std::vector<Room> rooms_;
  std::vector<Nurse> nurses_;
  std::vector<Surgeon> surgeons_;
  std::vector<OperatingTheater> operating_theaters_;

  Weights weights_;

  // listas precomputadas para iterar solo obligatorios u opcionales
  std::vector<PatientId> mandatory_patient_ids_;
  std::vector<PatientId> optional_patient_ids_;

  // mapas de ID textual a indice, usados solo en parseo
  std::unordered_map<std::string, PatientId> patient_id_map_;
  std::unordered_map<std::string, OccupantId> occupant_id_map_;
  std::unordered_map<std::string, RoomId> room_id_map_;
  std::unordered_map<std::string, NurseId> nurse_id_map_;
  std::unordered_map<std::string, SurgeonId> surgeon_id_map_;
  std::unordered_map<std::string, OperatingTheaterId> ot_id_map_;
};

#endif  // SRC_ENTITIES_PROBLEM_DATA_H_
