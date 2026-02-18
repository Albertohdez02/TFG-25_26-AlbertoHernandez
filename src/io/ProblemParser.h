// ProblemParser.h - Parser de ficheros JSON de instancias
// TFG Alberto Hernandez
//
// Lee los ficheros JSON del IHTC y los convierte en ProblemData.
// usa nlohmann/json para parsear el JSON

#ifndef SRC_IO_PROBLEM_PARSER_H_
#define SRC_IO_PROBLEM_PARSER_H_

#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "../entities/ProblemData.h"
#include "json.hpp"

using json = nlohmann::json;

class ProblemParser {
 public:
  // parsea un fichero JSON y devuelve el ProblemData relleno
  static ProblemData Parse(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
      throw std::runtime_error("Cannot open file: " + filepath);
    }

    json j;
    try {
      file >> j;
    } catch (const json::parse_error& e) {
      throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    }

    ProblemData problem;
    problem.SetFilename(filepath);
    problem.SetNumDays(j.at("days").get<int>());
    problem.SetNumSkillLevels(j.at("skill_levels").get<int>());

    // parsear tipos de turno
    std::vector<std::string> shift_names;
    std::unordered_map<std::string, Shift> shift_map;
    for (size_t i = 0; i < j.at("shift_types").size(); ++i) {
      std::string name = j.at("shift_types")[i].get<std::string>();
      shift_names.push_back(name);
      shift_map[name] = static_cast<Shift>(i);
    }
    problem.SetNumShiftTypes(static_cast<int>(shift_names.size()));
    problem.SetShiftNames(shift_names);

    // parsear grupos de edad
    std::unordered_map<std::string, AgeGroup> age_map;
    for (size_t i = 0; i < j.at("age_groups").size(); ++i) {
      age_map[j.at("age_groups")[i].get<std::string>()] =
          static_cast<AgeGroup>(i);
    }
    problem.SetNumAgeGroups(static_cast<int>(age_map.size()));

    // parsear habitaciones primero (necesarias para mapear IDs)
    std::unordered_map<std::string, RoomId> room_map;
    int room_idx = 0;
    for (const auto& r : j.at("rooms")) {
      std::string id = r.at("id").get<std::string>();
      room_map[id] = room_idx;
      problem.AddRoom(Room(id, room_idx, r.at("capacity").get<int>()));
      ++room_idx;
    }

    // parsear cirujanos
    std::unordered_map<std::string, SurgeonId> surgeon_map;
    int surgeon_idx = 0;
    for (const auto& s : j.at("surgeons")) {
      std::string id = s.at("id").get<std::string>();
      std::vector<int> max_time =
          s.at("max_surgery_time").get<std::vector<int>>();
      surgeon_map[id] = surgeon_idx;
      problem.AddSurgeon(Surgeon(id, surgeon_idx, std::move(max_time)));
      ++surgeon_idx;
    }

    // parsear quirofanos
    int ot_idx = 0;
    for (const auto& ot : j.at("operating_theaters")) {
      std::string id = ot.at("id").get<std::string>();
      std::vector<int> avail = ot.at("availability").get<std::vector<int>>();
      problem.AddOperatingTheater(
          OperatingTheater(id, ot_idx, std::move(avail)));
      ++ot_idx;
    }

    // parsear enfermeras
    int nurse_idx = 0;
    for (const auto& n : j.at("nurses")) {
      std::string id = n.at("id").get<std::string>();
      SkillLevel skill = n.at("skill_level").get<int>();
      std::vector<WorkingShift> shifts;
      for (const auto& ws : n.at("working_shifts")) {
        shifts.emplace_back(ws.at("day").get<int>(),
                            shift_map.at(ws.at("shift").get<std::string>()),
                            ws.at("max_load").get<int>());
      }
      problem.AddNurse(Nurse(id, nurse_idx, skill, std::move(shifts)));
      ++nurse_idx;
    }

    // parsear ocupantes
    int occ_idx = 0;
    for (const auto& occ : j.at("occupants")) {
      problem.AddOccupant(Occupant(
          occ.at("id").get<std::string>(), occ_idx,
          ParseGender(occ.at("gender").get<std::string>()),
          age_map.at(occ.at("age_group").get<std::string>()),
          occ.at("length_of_stay").get<int>(),
          occ.at("workload_produced").get<std::vector<int>>(),
          occ.at("skill_level_required").get<std::vector<int>>(),
          room_map.at(occ.at("room_id").get<std::string>())));
      ++occ_idx;
    }

    // parsear pacientes
    int pat_idx = 0;
    for (const auto& p : j.at("patients")) {
      bool mandatory = p.at("mandatory").get<bool>();
      Day due_day = mandatory ? p.at("surgery_due_day").get<int>()
                              : (problem.GetNumDays() - 1);

      std::vector<RoomId> incompatible;
      for (const auto& rid : p.at("incompatible_room_ids")) {
        incompatible.push_back(room_map.at(rid.get<std::string>()));
      }

      problem.AddPatient(Patient(
          p.at("id").get<std::string>(), pat_idx,
          ParseGender(p.at("gender").get<std::string>()),
          age_map.at(p.at("age_group").get<std::string>()),
          p.at("length_of_stay").get<int>(),
          p.at("workload_produced").get<std::vector<int>>(),
          p.at("skill_level_required").get<std::vector<int>>(), mandatory,
          p.at("surgery_release_day").get<int>(), due_day,
          p.at("surgery_duration").get<int>(),
          surgeon_map.at(p.at("surgeon_id").get<std::string>()),
          std::move(incompatible)));
      ++pat_idx;
    }

    // parsear pesos 
    const auto& w = j.at("weights");
    Weights weights;
    weights.room_mixed_age = w.at("room_mixed_age").get<int>();
    weights.room_nurse_skill = w.at("room_nurse_skill").get<int>();
    weights.continuity_of_care = w.at("continuity_of_care").get<int>();
    weights.nurse_excessive_workload =
        w.at("nurse_eccessive_workload").get<int>();
    weights.open_operating_theater = w.at("open_operating_theater").get<int>();
    weights.surgeon_transfer = w.at("surgeon_transfer").get<int>();
    weights.patient_delay = w.at("patient_delay").get<int>();
    weights.unscheduled_optional = w.at("unscheduled_optional").get<int>();
    problem.SetWeights(weights);

    return problem;
  }

 private:
  // transforma un string de genero (A/B) a la constante correspondiente
  static Gender ParseGender(const std::string& g) {
    if (g == "A") return kGenderFemale;
    if (g == "B") return kGenderMale;
    return kGenderAny;
  }
};

#endif  // SRC_IO_PROBLEM_PARSER_H_
