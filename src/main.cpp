// main.cpp - Demo del solver IHTC
// TFG Alberto Hernandez - 4o Informatica
//
// Esto es un ejemplo de prueba para ver como funcionan las entidades
// y la clase Solution. Creo una instancia pequena a mano y juego con ella.

#include <iomanip>
#include <iostream>
#include <string>

#include "common/types.h"
#include "entities/Nurse.h"
#include "entities/Occupant.h"
#include "entities/OperatingTheater.h"
#include "entities/Patient.h"
#include "entities/ProblemData.h"
#include "entities/Room.h"
#include "entities/Surgeon.h"
#include "solution/Solution.h"

// Imprime una linea separadora bonita con titulo
void PrintSeparator(const std::string& title) {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "  " << title << "\n";
  std::cout << std::string(60, '=') << "\n";
}

// Imprime una tabla con la ocupacion de cada habitacion por dia
// Pone un ! si se pasa de capacidad
void PrintRoomOccupancy(const Solution& sol, const ProblemData& problem) {
  std::cout << "\nOcupacion de habitaciones (Room x Day):\n";
  std::cout << std::setw(10) << "Room";
  for (int d = 0; d < problem.GetNumDays(); ++d) {
    std::cout << std::setw(6) << ("D" + std::to_string(d));
  }
  std::cout << "\n" << std::string(10 + 6 * problem.GetNumDays(), '-') << "\n";

  for (RoomId r = 0; r < problem.GetNumRooms(); ++r) {
    std::cout << std::setw(10) << problem.GetRoom(r).GetId();
    for (Day d = 0; d < problem.GetNumDays(); ++d) {
      int occ = sol.GetRoomOccupancy(r, d);
      int cap = problem.GetRoom(r).GetCapacity();
      std::string cell = std::to_string(occ) + "/" + std::to_string(cap);
      if (occ > cap) cell += "!";
      std::cout << std::setw(6) << cell;
    }
    std::cout << "\n";
  }
}

// Muestra la carga de cada quirofano por dia
void PrintOTLoad(const Solution& sol, const ProblemData& problem) {
  std::cout << "\nCarga de quirofanos (OT x Day):\n";
  std::cout << std::setw(10) << "OT";
  for (int d = 0; d < problem.GetNumDays(); ++d) {
    std::cout << std::setw(8) << ("D" + std::to_string(d));
  }
  std::cout << "\n" << std::string(10 + 8 * problem.GetNumDays(), '-') << "\n";

  for (OperatingTheaterId ot = 0; ot < problem.GetNumOperatingTheaters(); ++ot) {
    std::cout << std::setw(10) << problem.GetOperatingTheater(ot).GetId();
    for (Day d = 0; d < problem.GetNumDays(); ++d) {
      int load = sol.GetOTLoad(ot, d);
      int avail = problem.GetOperatingTheater(ot).GetAvailabilityForDay(d);
      std::string cell = std::to_string(load) + "/" + std::to_string(avail);
      if (load > avail) cell += "!";
      std::cout << std::setw(8) << cell;
    }
    std::cout << "\n";
  }
}

// Muestra cuanto tiempo ha operado cada cirujano por dia
void PrintSurgeonLoad(const Solution& sol, const ProblemData& problem) {
  std::cout << "\nCarga de cirujanos (Surgeon x Day):\n";
  std::cout << std::setw(12) << "Surgeon";
  for (int d = 0; d < problem.GetNumDays(); ++d) {
    std::cout << std::setw(8) << ("D" + std::to_string(d));
  }
  std::cout << "\n" << std::string(12 + 8 * problem.GetNumDays(), '-') << "\n";

  for (SurgeonId s = 0; s < problem.GetNumSurgeons(); ++s) {
    std::cout << std::setw(12) << problem.GetSurgeon(s).GetId();
    for (Day d = 0; d < problem.GetNumDays(); ++d) {
      int load = sol.GetSurgeonLoad(s, d);
      int max = problem.GetSurgeon(s).GetMaxSurgeryTimeForDay(d);
      std::string cell = std::to_string(load) + "/" + std::to_string(max);
      if (load > max) cell += "!";
      std::cout << std::setw(8) << cell;
    }
    std::cout << "\n";
  }
}

int main() {
  std::cout << "  IHTC 2024 Solver - Demo de Entidades y Solucion " << std::endl << std::endl;
  // =========================================
  // PASO 1: Crear la instancia de problema
  // =========================================
  PrintSeparator("1. Creando instancia de problema de ejemplo");

  ProblemData problem;
  problem.SetFilename("demo_instance");
  problem.SetNumDays(5);
  problem.SetNumSkillLevels(3);
  problem.SetNumShiftTypes(3);
  problem.SetNumAgeGroups(3);
  problem.SetShiftNames({"Early", "Late", "Night"});

  // Los pesos determinan cuanto penaliza cada violacion
  Weights weights;
  weights.room_gender_mix = 50;
  weights.room_capacity_violation = 100;
  weights.patient_delay = 10;
  weights.unscheduled_optional = 200;
  weights.surgeon_overtime = 20;
  weights.operating_theater_overtime = 30;
  problem.SetWeights(weights);

  // Habitaciones: R1 caben 2, R2 caben 3, R3 caben 2
  problem.AddRoom(Room("R1", 0, 2));
  problem.AddRoom(Room("R2", 1, 3));
  problem.AddRoom(Room("R3", 2, 2));

  // Cirujanos con su maximo de minutos por dia
  problem.AddSurgeon(Surgeon("Dr_Garcia", 0, {120, 120, 120, 120, 120}));
  problem.AddSurgeon(Surgeon("Dr_Lopez", 1, {90, 90, 90, 90, 90}));

  // Quirofanos (OT2 cerrado el dia 2, por eso el 0)
  problem.AddOperatingTheater(
      OperatingTheater("OT1", 0, {180, 180, 180, 180, 180}));
  problem.AddOperatingTheater(
      OperatingTheater("OT2", 1, {120, 120, 0, 120, 120}));

  // Enfermeras con sus turnos (dia, turno, max carga)
  std::vector<WorkingShift> nurse1_shifts = {
      {0, 0, 10}, {0, 1, 10}, {1, 0, 10}, {1, 1, 10}, {2, 0, 10}};
  std::vector<WorkingShift> nurse2_shifts = {
      {0, 2, 8}, {1, 2, 8}, {2, 1, 8}, {2, 2, 8}, {3, 0, 8}};
  problem.AddNurse(Nurse("Nurse_Ana", 0, 2, nurse1_shifts));
  problem.AddNurse(Nurse("Nurse_Maria", 1, 3, nurse2_shifts));

  // Ocupante = paciente que ya estaba cuando empieza la planificacion
  problem.AddOccupant(Occupant("Occ1", 0, kGenderFemale, 1,
                               3,
                               {3, 2, 1},
                               {1, 1, 1},
                               0));

  // Pacientes nuevos que hay que programar
  // P1: obligatorio, lo opera Dr_Garcia, no puede ir a R3
  problem.AddPatient(Patient("P1", 0, kGenderMale, 1,
                             3,
                             {4, 3, 2},
                             {2, 1, 1},
                             true,
                             0, 2,
                             45,
                             0,
                             {2}));

  // P2: obligatorio, lo opera Dr_Lopez
  problem.AddPatient(Patient("P2", 1, kGenderFemale, 2, 2,
                             {3, 2, 1}, {1, 1, 1},
                             true,
                             0, 3,
                             60,
                             1,
                             {}));

  // P3: este es OPCIONAL (puede no programarse)
  problem.AddPatient(Patient("P3", 2, kGenderMale, 0, 2, {2, 2, 1}, {1, 1, 1},
                             false,
                             1, 4,
                             30,
                             0,
                             {}));

  // P4: obligatorio, operacion larga
  problem.AddPatient(Patient("P4", 3, kGenderFemale, 1, 4,
                             {5, 4, 2}, {2, 2, 1},
                             true,
                             0, 1,
                             75,
                             0,
                             {}));

  std::cout << "Instancia creada:\n";
  std::cout << "  - Dias de planificacion: " << problem.GetNumDays() << "\n";
  std::cout << "  - Habitaciones: " << problem.GetNumRooms() << "\n";
  std::cout << "  - Cirujanos: " << problem.GetNumSurgeons() << "\n";
  std::cout << "  - Quirofanos: " << problem.GetNumOperatingTheaters() << "\n";
  std::cout << "  - Enfermeras: " << problem.GetNumNurses() << "\n";
  std::cout << "  - Ocupantes: " << problem.GetNumOccupants() << "\n";
  std::cout << "  - Pacientes: " << problem.GetNumPatients() << "\n";
  std::cout << "    - Obligatorios: " << problem.GetNumMandatoryPatients()
            << "\n";
  std::cout << "    - Opcionales: " << problem.GetNumOptionalPatients() << "\n";

  // =========================================
  // PASO 2: Crear solucion vacia
  // =========================================
  PrintSeparator("2. Creando solucion vacia");

  Solution sol(problem);

  std::cout << "Solucion inicial (solo ocupantes pre-existentes):\n";
  std::cout << "  - Pacientes programados: " << sol.GetNumScheduledPatients()
            << "\n";

  PrintRoomOccupancy(sol, problem);

  // =========================================
  // PASO 3: Asignar pacientes
  // =========================================
  PrintSeparator("3. Asignando pacientes");

  // Asigno P1 a R2, dia 0, quirofano OT1
  bool ok = sol.AssignPatient(0, 1, 0, 0);
  std::cout << "Asignando P1 (obligatorio) a R2, dia 0, OT1: "
            << (ok ? "OK" : "FALLO") << "\n";

  // Asigno P2 a R1, dia 1, quirofano OT1
  ok = sol.AssignPatient(1, 0, 1, 0);
  std::cout << "Asignando P2 (obligatorio) a R1, dia 1, OT1: "
            << (ok ? "OK" : "FALLO") << "\n";

  // Asigno P4 a R2, dia 0 (mismo dia que P1 en la misma habitacion)
  ok = sol.AssignPatient(3, 1, 0, 0);
  std::cout << "Asignando P4 (obligatorio) a R2, dia 0, OT1: "
            << (ok ? "OK" : "FALLO") << "\n";

  std::cout << "\n  Pacientes programados: " << sol.GetNumScheduledPatients()
            << "/" << problem.GetNumPatients() << "\n";

  // =========================================
  // PASO 4: Ver el estado de los caches
  // =========================================
  PrintSeparator("4. Estado de la solucion (caches)");

  PrintRoomOccupancy(sol, problem);
  PrintOTLoad(sol, problem);
  PrintSurgeonLoad(sol, problem);

  // =========================================
  // PASO 5: Probar a mover pacientes
  // =========================================
  PrintSeparator("5. Probando reassign y unassign");

  std::cout << "Reasignando P1 de (R2, dia 0) a (R3, dia 1)...\n";
  ok = sol.ReassignPatient(0, 2, 1, 1);
  std::cout << "  Resultado: " << (ok ? "OK" : "FALLO") << "\n";

  PrintRoomOccupancy(sol, problem);
  PrintOTLoad(sol, problem);

  std::cout << "\nDesasignando P4...\n";
  ok = sol.UnassignPatient(3);
  std::cout << "  Resultado: " << (ok ? "OK" : "FALLO") << "\n";
  std::cout << "  Pacientes programados: " << sol.GetNumScheduledPatients()
            << "\n";

  PrintRoomOccupancy(sol, problem);

  // =========================================
  // PASO 6: Ver detalle de cada paciente
  // =========================================
  PrintSeparator("6. Detalle de asignaciones de pacientes");

  std::cout << std::setw(8) << "Patient" << std::setw(10) << "Type"
            << std::setw(10) << "Room" << std::setw(8) << "Day" << std::setw(8)
            << "OT" << std::setw(10) << "Status"
            << "\n";
  std::cout << std::string(54, '-') << "\n";

  for (PatientId p = 0; p < problem.GetNumPatients(); ++p) {
    const Patient& pat = problem.GetPatient(p);
    std::cout << std::setw(8) << pat.GetId();
    std::cout << std::setw(10) << (pat.IsMandatory() ? "MAND" : "OPT");

    if (sol.IsPatientScheduled(p)) {
      RoomId r = sol.GetPatientRoom(p);
      Day d = sol.GetPatientAdmissionDay(p);
      OperatingTheaterId ot = sol.GetPatientOT(p);
      std::cout << std::setw(10) << problem.GetRoom(r).GetId();
      std::cout << std::setw(8) << d;
      std::cout << std::setw(8) << problem.GetOperatingTheater(ot).GetId();
      std::cout << std::setw(10) << "SCHED";
    } else {
      std::cout << std::setw(10) << "-";
      std::cout << std::setw(8) << "-";
      std::cout << std::setw(8) << "-";
      std::cout << std::setw(10) << "UNSCHED";
    }
    std::cout << "\n";
  }

  // =========================================
  // PASO 7: Ver generos por habitacion
  // =========================================
  PrintSeparator("7. Genero por habitacion-dia");

  // Lambda para convertir genero a string
  auto gender_str = [](Gender g) -> std::string {
    if (g == kGenderAny) return "ANY";
    if (g == kGenderFemale) return "F";
    if (g == kGenderMale) return "M";
    return "MIX!";
  };

  std::cout << std::setw(10) << "Room";
  for (int d = 0; d < problem.GetNumDays(); ++d) {
    std::cout << std::setw(6) << ("D" + std::to_string(d));
  }
  std::cout << "\n" << std::string(10 + 6 * problem.GetNumDays(), '-') << "\n";

  for (RoomId r = 0; r < problem.GetNumRooms(); ++r) {
    std::cout << std::setw(10) << problem.GetRoom(r).GetId();
    for (Day d = 0; d < problem.GetNumDays(); ++d) {
      std::cout << std::setw(6) << gender_str(sol.GetRoomGender(r, d));
    }
    std::cout << "\n";
  }

  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "  Demo completada exitosamente!\n";
  std::cout << std::string(60, '=') << "\n\n";

  return 0;
}
