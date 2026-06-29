// SolutionReader.h - Lector de soluciones en formato JSON oficial IHTC 2024
// TFG Alberto Hernandez
//
// Parsea un fichero de solucion JSON (tanto el generado por el solver como
// los de best-solutions del concurso) y reconstruye el objeto Solution.

#ifndef SRC_IO_SOLUTION_READER_H_
#define SRC_IO_SOLUTION_READER_H_

#include <fstream>
#include <stdexcept>
#include <string>

#include "../entities/ProblemData.h"
#include "../solution/Solution.h"
#include "json.hpp"

using json = nlohmann::json;

/** @brief Lector de soluciones en formato JSON oficial IHTC 2024. */
class SolutionReader {
 public:
  /** @brief Lee un fichero JSON de solucion y reconstruye el objeto Solution. */
  static Solution Read(const std::string& filepath, const ProblemData& problem) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
      throw std::runtime_error("No se pudo abrir: " + filepath);
    }

    json j;
    try {
      file >> j;
    } catch (const json::parse_error& e) {
      throw std::runtime_error("Error parseando JSON: " + std::string(e.what()));
    }

    Solution solution(problem);

    // Seccion patients
    if (j.contains("patients")) {
      for (const auto& p : j["patients"]) {
        std::string pid_str = p["id"].get<std::string>();
        PatientId pid = problem.GetPatientIdByString(pid_str);
        if (pid == kInvalidId) continue;

        // admission_day puede ser un entero o "none"
        if (p["admission_day"].is_string()) continue;  // "none" -> no programado

        int day = p["admission_day"].get<int>();
        std::string room_str = p["room"].get<std::string>();
        std::string ot_str   = p["operating_theater"].get<std::string>();

        RoomId room = problem.GetRoomIdByString(room_str);
        OperatingTheaterId ot = problem.GetOperatingTheaterIdByString(ot_str);

        if (room == kInvalidId || ot == kInvalidId) continue;

        solution.AssignPatient(pid, room, static_cast<Day>(day), ot);
      }
    }

    // Seccion nurses
    // Formato: [{id, assignments: [{day, shift, rooms:[...]}]}]
    if (j.contains("nurses")) {
      const auto& shift_names = problem.GetShiftNames();

      auto ShiftIndex = [&](const std::string& name) -> Shift {
        for (int i = 0; i < static_cast<int>(shift_names.size()); ++i) {
          if (shift_names[i] == name) return i;
        }
        return kInvalidId;
      };

      for (const auto& nurse_entry : j["nurses"]) {
        std::string nid_str = nurse_entry["id"].get<std::string>();
        NurseId nid = problem.GetNurseIdByString(nid_str);
        if (nid == kInvalidId) continue;

        if (!nurse_entry.contains("assignments")) continue;

        for (const auto& assignment : nurse_entry["assignments"]) {
          int day       = assignment["day"].get<int>();
          std::string shift_name = assignment["shift"].get<std::string>();
          Shift shift   = ShiftIndex(shift_name);
          if (shift == kInvalidId) continue;

          if (!assignment.contains("rooms")) continue;

          for (const auto& room_entry : assignment["rooms"]) {
            std::string rid_str = room_entry.get<std::string>();
            RoomId rid = problem.GetRoomIdByString(rid_str);
            if (rid == kInvalidId) continue;

            solution.AssignNurse(nid, rid, static_cast<Day>(day), shift);
          }
        }
      }
    }

    return solution;
  }
};

#endif  // SRC_IO_SOLUTION_READER_H_
