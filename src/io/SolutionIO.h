// SolutionIO.h - Exportacion de soluciones a JSON
// TFG Alberto Hernandez
//
// Escribe la solucion en formato JSON compatible con el evaluador del IHTC.
// Tambien puede imprimir un resumen por consola.

#ifndef SRC_IO_SOLUTION_IO_H_
#define SRC_IO_SOLUTION_IO_H_

#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include "../common/types.h"
#include "../entities/ProblemData.h"
#include "../solution/Solution.h"
#include "json.hpp"

using json = nlohmann::json;

class SolutionIO {
 public:
  // exporta la solucion a un fichero JSON
  static bool ExportJSON(const Solution& solution,
                         const std::string& filepath) {
    json j = ToJSON(solution);

    std::ofstream file(filepath);
    if (!file.is_open()) {
      std::cerr << "Error: no se pudo abrir " << filepath << " para escribir\n";
      return false;
    }

    file << j.dump(2);
    return true;
  }

  // convierte la solucion a JSON
  static json ToJSON(const Solution& solution) {
    const ProblemData& prob = solution.GetProblem();
    json j;

    // asignaciones de pacientes
    json patients_json = json::array();
    for (PatientId p = 0; p < prob.GetNumPatients(); ++p) {
      json patient_entry;
      patient_entry["id"] = prob.GetPatient(p).GetId();

      if (solution.IsPatientScheduled(p)) {
        RoomId room = solution.GetPatientRoom(p);
        Day day = solution.GetPatientAdmissionDay(p);
        OperatingTheaterId ot = solution.GetPatientOT(p);

        patient_entry["admission_day"] = day;
        patient_entry["room"] = prob.GetRoom(room).GetId();
        patient_entry["operating_theater"] =
            prob.GetOperatingTheater(ot).GetId();
      } else {
        patient_entry["admission_day"] = "none";
      }

      patients_json.push_back(patient_entry);
    }
    j["patients"] = patients_json;

    // asignaciones de enfermeras agrupadas por enfermera
    // formato: [{id, assignments: [{day, shift, rooms: [...]}]}]
    int num_shifts = prob.GetNumShiftTypes();
    const auto& shift_names = prob.GetShiftNames();

    // recopilar: nurse_id -> (day, shift) -> lista de rooms
    // usamos map para ordenar de forma determinista
    std::map<NurseId, std::map<std::pair<Day,Shift>, std::vector<RoomId>>> nurse_map;

    for (RoomId r = 0; r < prob.GetNumRooms(); ++r) {
      for (Day d = 0; d < prob.GetNumDays(); ++d) {
        for (Shift s = 0; s < num_shifts; ++s) {
          NurseId nurse = solution.GetNurseAssignment(r, d, s);
          if (nurse != kInvalidId) {
            nurse_map[nurse][{d, s}].push_back(r);
          }
        }
      }
    }

    json nurses_json = json::array();
    for (const auto& [nurse_id, day_shift_rooms] : nurse_map) {
      json nurse_entry;
      nurse_entry["id"] = prob.GetNurse(nurse_id).GetId();

      json assignments = json::array();
      for (const auto& [day_shift, rooms] : day_shift_rooms) {
        Day d = day_shift.first;
        Shift s = day_shift.second;

        json assignment;
        assignment["day"] = d;
        assignment["shift"] = (s < static_cast<int>(shift_names.size()))
                                  ? shift_names[s]
                                  : std::to_string(s);
        json rooms_json = json::array();
        for (RoomId r : rooms) {
          rooms_json.push_back(prob.GetRoom(r).GetId());
        }
        assignment["rooms"] = rooms_json;
        assignments.push_back(assignment);
      }
      nurse_entry["assignments"] = assignments;
      nurses_json.push_back(nurse_entry);
    }
    j["nurses"] = nurses_json;

    return j;
  }

  // imprime un resumen de la solucion por consola
  static void PrintSummary(const Solution& solution) {
    const ProblemData& prob = solution.GetProblem();

    std::cout << "\n=== Resumen de la solucion ===\n";
    std::cout << "Pacientes programados: " << solution.GetNumScheduledPatients()
              << "/" << prob.GetNumPatients() << "\n";

    // contar obligatorios y opcionales programados
    int mand_scheduled = 0, opt_scheduled = 0;
    for (PatientId p : prob.GetMandatoryPatientIds()) {
      if (solution.IsPatientScheduled(p)) mand_scheduled++;
    }
    for (PatientId p : prob.GetOptionalPatientIds()) {
      if (solution.IsPatientScheduled(p)) opt_scheduled++;
    }
    std::cout << "  Obligatorios: " << mand_scheduled << "/"
              << prob.GetNumMandatoryPatients() << "\n";
    std::cout << "  Opcionales:   " << opt_scheduled << "/"
              << prob.GetNumOptionalPatients() << "\n";

    // tabla de asignaciones
    std::cout << "\n" << std::setw(8) << "Patient" << std::setw(10)
              << "Type" << std::setw(10) << "Room" << std::setw(8)
              << "Day" << std::setw(8) << "OT" << std::setw(10) << "Status"
              << "\n";
    std::cout << std::string(54, '-') << "\n";

    for (PatientId p = 0; p < prob.GetNumPatients(); ++p) {
      const Patient& pat = prob.GetPatient(p);
      std::cout << std::setw(8) << pat.GetId();
      std::cout << std::setw(10) << (pat.IsMandatory() ? "MAND" : "OPT");

      if (solution.IsPatientScheduled(p)) {
        RoomId r = solution.GetPatientRoom(p);
        Day d = solution.GetPatientAdmissionDay(p);
        OperatingTheaterId ot = solution.GetPatientOT(p);
        std::cout << std::setw(10) << prob.GetRoom(r).GetId();
        std::cout << std::setw(8) << d;
        std::cout << std::setw(8) << prob.GetOperatingTheater(ot).GetId();
        std::cout << std::setw(10) << "SCHED";
      } else {
        std::cout << std::setw(10) << "-";
        std::cout << std::setw(8) << "-";
        std::cout << std::setw(8) << "-";
        std::cout << std::setw(10) << "UNSCHED";
      }
      std::cout << "\n";
    }
    std::cout << "\n";
  }
};

#endif  // SRC_IO_SOLUTION_IO_H_
