// Test de lectura de instancias IHTC 2024.
// todas las instancias JSON se cargan correctamente

#include <filesystem>
#include <iomanip>
#include <iostream>

#include "io/ProblemParser.h"
#include "solution/Solution.h"

namespace fs = std::filesystem;

void PrintHeader() {
  std::cout << std::left << std::setw(15) << "Instance" << std::right
            << std::setw(6) << "Days" << std::setw(7) << "Rooms"
            << std::setw(7) << "Surg" << std::setw(6) << "OTs"
            << std::setw(8) << "Nurses" << std::setw(6) << "Occ"
            << std::setw(8) << "Pat" << std::setw(8) << "(Mand)"
            << std::setw(10) << "Capacity" << "\n";
  std::cout << std::string(81, '-') << "\n";
}

void PrintInstanceSummary(const std::string& name, const ProblemData& p) {
  // Calcular capacidad total de habitaciones
  int total_capacity = 0;
  for (RoomId r = 0; r < p.GetNumRooms(); ++r) {
    total_capacity += p.GetRoom(r).GetCapacity();
  }

  std::cout << std::left << std::setw(15) << name << std::right
            << std::setw(6) << p.GetNumDays() << std::setw(7) << p.GetNumRooms()
            << std::setw(7) << p.GetNumSurgeons() << std::setw(6)
            << p.GetNumOperatingTheaters() << std::setw(8) << p.GetNumNurses()
            << std::setw(6) << p.GetNumOccupants() << std::setw(8)
            << p.GetNumPatients() << std::setw(8) << p.GetNumMandatoryPatients()
            << std::setw(10) << total_capacity << "\n";
}

void PrintDetailedInfo(const ProblemData& p) {
  std::cout << "\n  Weights:\n";
  const auto& w = p.GetWeights();
  std::cout << "    room_mixed_age=" << w.room_mixed_age
            << ", room_nurse_skill=" << w.room_nurse_skill
            << ", continuity_of_care=" << w.continuity_of_care << "\n";
  std::cout << "    patient_delay=" << w.patient_delay
            << ", unscheduled_optional=" << w.unscheduled_optional
            << ", open_ot=" << w.open_operating_theater << "\n";

  std::cout << "  Shifts: ";
  for (const auto& s : p.GetShiftNames()) {
    std::cout << s << " ";
  }
  std::cout << "\n";

  std::cout << "  Rooms: ";
  for (RoomId r = 0; r < p.GetNumRooms(); ++r) {
    std::cout << p.GetRoom(r).GetId() << "(cap=" << p.GetRoom(r).GetCapacity()
              << ") ";
  }
  std::cout << "\n";
}

int main(int argc, char* argv[]) {
  std::string data_dir = (argc > 1) ? argv[1] : "data";
  bool verbose = (argc > 2 && std::string(argv[2]) == "-v");

  std::cout       << "IHTC 2024 Instance Parser - Validation Test" << std::endl;

  std::cout << "Reading instances from: " << data_dir << std::endl << std::endl;

  int success = 0, failed = 0;
  std::vector<std::pair<std::string, std::string>> errors;

  PrintHeader();

  // ordenar archivos
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(data_dir)) {
    if (entry.path().extension() == ".json") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());

  for (const auto& filepath : files) {
    std::string name = filepath.stem().string();
    try {
      // 1. Parse the instance
      ProblemData problem = ProblemParser::Parse(filepath.string());

      // 2. Create empty solution to verify initialization
      Solution sol(problem);

      // 3. Verify basic invariants
      if (sol.GetNumScheduledPatients() != 0) {
        throw std::runtime_error("Solution should start with 0 patients");
      }

      // 4. Verify occupant occupancy is initialized
      bool occupancy_ok = true;
      for (const auto& occ : problem.GetOccupants()) {
        RoomId room = occ.GetRoomId();
        for (Day d = 0; d < occ.GetLengthOfStay() && d < problem.GetNumDays();
             ++d) {
          if (sol.GetRoomOccupancy(room, d) < 1) {
            occupancy_ok = false;
            break;
          }
        }
      }
      if (!occupancy_ok) {
        throw std::runtime_error("Occupant occupancy not initialized");
      }

      PrintInstanceSummary(name, problem);

      if (verbose) {
        PrintDetailedInfo(problem);
      }

      ++success;
    } catch (const std::exception& e) {
      std::cout << std::left << std::setw(15) << name << " *** ERROR ***\n";
      errors.emplace_back(name, e.what());
      ++failed;
    }
  }

  std::cout << std::string(81, '-') << "\n";

  // Print errors if any
  if (!errors.empty()) {
    std::cout << "\nErrors:\n";
    for (const auto& [name, msg] : errors) {
      std::cout << "  " << name << ": " << msg << "\n";
    }
  }

  std::cout << "\n╔═══════════════════════════════════════╗\n";
  std::cout << "║  Results: " << std::setw(3) << success << " OK, " << std::setw(3)
            << failed << " FAILED          ║\n";
  std::cout << "╚═══════════════════════════════════════╝\n";

  if (failed == 0 && success > 0) {
    std::cout << "\n✓ All instances parsed and validated successfully!\n";
  }

  return failed;
}
