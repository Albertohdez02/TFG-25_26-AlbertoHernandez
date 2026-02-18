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

class ProblemData {
 public:
  ProblemData()
      : num_days_(0),
        num_skill_levels_(0),
        num_shift_types_(kDefaultNumShifts),
        num_age_groups_(0) {}

  [[nodiscard]] const std::string& GetFilename() const noexcept {
    return filename_;
  }
  [[nodiscard]] int GetNumDays() const noexcept { return num_days_; }
  [[nodiscard]] int GetNumSkillLevels() const noexcept {
    return num_skill_levels_;
  }
  [[nodiscard]] int GetNumShiftTypes() const noexcept {
    return num_shift_types_;
  }
  [[nodiscard]] int GetNumAgeGroups() const noexcept { return num_age_groups_; }
  [[nodiscard]] const std::vector<std::string>& GetShiftNames() const noexcept {
    return shift_names_;
  }

  // contadores de entidades (para iterar)
  [[nodiscard]] int GetNumPatients() const noexcept {
    return static_cast<int>(patients_.size());
  }
  [[nodiscard]] int GetNumOccupants() const noexcept {
    return static_cast<int>(occupants_.size());
  }
  [[nodiscard]] int GetNumRooms() const noexcept {
    return static_cast<int>(rooms_.size());
  }
  [[nodiscard]] int GetNumNurses() const noexcept {
    return static_cast<int>(nurses_.size());
  }
  [[nodiscard]] int GetNumSurgeons() const noexcept {
    return static_cast<int>(surgeons_.size());
  }
  [[nodiscard]] int GetNumOperatingTheaters() const noexcept {
    return static_cast<int>(operating_theaters_.size());
  }
  [[nodiscard]] int GetNumMandatoryPatients() const noexcept {
    return static_cast<int>(mandatory_patient_ids_.size());
  }
  [[nodiscard]] int GetNumOptionalPatients() const noexcept {
    return static_cast<int>(optional_patient_ids_.size());
  }

  // colecciones de entidades (para iterar tambien)

  [[nodiscard]] const std::vector<Patient>& GetPatients() const noexcept {
    return patients_;
  }
  [[nodiscard]] const std::vector<Occupant>& GetOccupants() const noexcept {
    return occupants_;
  }
  [[nodiscard]] const std::vector<Room>& GetRooms() const noexcept {
    return rooms_;
  }
  [[nodiscard]] const std::vector<Nurse>& GetNurses() const noexcept {
    return nurses_;
  }
  [[nodiscard]] const std::vector<Surgeon>& GetSurgeons() const noexcept {
    return surgeons_;
  }
  [[nodiscard]] const std::vector<OperatingTheater>& GetOperatingTheaters()
      const noexcept {
    return operating_theaters_;
  }

  [[nodiscard]] const std::vector<PatientId>& GetMandatoryPatientIds()
      const noexcept {
    return mandatory_patient_ids_;
  }
  [[nodiscard]] const std::vector<PatientId>& GetOptionalPatientIds()
      const noexcept {
    return optional_patient_ids_;
  }

  // entidades por ID

  [[nodiscard]] const Patient& GetPatient(PatientId id) const noexcept {
    return patients_[id];
  }
  [[nodiscard]] const Occupant& GetOccupant(OccupantId id) const noexcept {
    return occupants_[id];
  }
  [[nodiscard]] const Room& GetRoom(RoomId id) const noexcept {
    return rooms_[id];
  }
  [[nodiscard]] const Nurse& GetNurse(NurseId id) const noexcept {
    return nurses_[id];
  }
  [[nodiscard]] const Surgeon& GetSurgeon(SurgeonId id) const noexcept {
    return surgeons_[id];
  }
  [[nodiscard]] const OperatingTheater& GetOperatingTheater(
      OperatingTheaterId id) const noexcept {
    return operating_theaters_[id];
  }

  // pesos para la funcion objetivo

  [[nodiscard]] const Weights& GetWeights() const noexcept { return weights_; }

  // para convertir IDs de string a indice (al parsear)

  [[nodiscard]] PatientId GetPatientIdByString(
      const std::string& str_id) const {
    auto it = patient_id_map_.find(str_id);
    return (it != patient_id_map_.end()) ? it->second : kInvalidId;
  }

  [[nodiscard]] RoomId GetRoomIdByString(const std::string& str_id) const {
    auto it = room_id_map_.find(str_id);
    return (it != room_id_map_.end()) ? it->second : kInvalidId;
  }

  [[nodiscard]] NurseId GetNurseIdByString(const std::string& str_id) const {
    auto it = nurse_id_map_.find(str_id);
    return (it != nurse_id_map_.end()) ? it->second : kInvalidId;
  }

  [[nodiscard]] SurgeonId GetSurgeonIdByString(
      const std::string& str_id) const {
    auto it = surgeon_id_map_.find(str_id);
    return (it != surgeon_id_map_.end()) ? it->second : kInvalidId;
  }

  [[nodiscard]] OperatingTheaterId GetOperatingTheaterIdByString(
      const std::string& str_id) const {
    auto it = ot_id_map_.find(str_id);
    return (it != ot_id_map_.end()) ? it->second : kInvalidId;
  }

  // setters para cargar datos (al parsear)

  void SetFilename(std::string filename) { filename_ = std::move(filename); }
  void SetNumDays(int days) { num_days_ = days; }
  void SetNumSkillLevels(int levels) { num_skill_levels_ = levels; }
  void SetNumShiftTypes(int shifts) { num_shift_types_ = shifts; }
  void SetNumAgeGroups(int groups) { num_age_groups_ = groups; }
  void SetShiftNames(std::vector<std::string> names) {
    shift_names_ = std::move(names);
  }
  void SetWeights(Weights w) { weights_ = w; }

  // añade paciente y actualiza los mapas de indices
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

  void AddOccupant(Occupant occupant) {
    occupant_id_map_[occupant.GetId()] =
        static_cast<OccupantId>(occupants_.size());
    occupants_.push_back(std::move(occupant));
  }

  void AddRoom(Room room) {
    room_id_map_[room.GetId()] = static_cast<RoomId>(rooms_.size());
    rooms_.push_back(std::move(room));
  }

  void AddNurse(Nurse nurse) {
    nurse_id_map_[nurse.GetId()] = static_cast<NurseId>(nurses_.size());
    nurses_.push_back(std::move(nurse));
  }

  void AddSurgeon(Surgeon surgeon) {
    surgeon_id_map_[surgeon.GetId()] =
        static_cast<SurgeonId>(surgeons_.size());
    surgeons_.push_back(std::move(surgeon));
  }

  void AddOperatingTheater(OperatingTheater ot) {
    ot_id_map_[ot.GetId()] =
        static_cast<OperatingTheaterId>(operating_theaters_.size());
    operating_theaters_.push_back(std::move(ot));
  }

  // se llama a este metodo antes de cargar datos para evitar reallocs de vectores
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
  int num_days_;                           // planificacion
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

  // listas precomputadas (para iterar solo por pacientes obligatorios o opcionales)
  std::vector<PatientId> mandatory_patient_ids_;
  std::vector<PatientId> optional_patient_ids_;

  // mapas de string a indice (para parseo)
  std::unordered_map<std::string, PatientId> patient_id_map_;
  std::unordered_map<std::string, OccupantId> occupant_id_map_;
  std::unordered_map<std::string, RoomId> room_id_map_;
  std::unordered_map<std::string, NurseId> nurse_id_map_;
  std::unordered_map<std::string, SurgeonId> surgeon_id_map_;
  std::unordered_map<std::string, OperatingTheaterId> ot_id_map_;
};

#endif  // SRC_ENTITIES_PROBLEM_DATA_H_
