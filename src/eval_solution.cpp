// eval_solution.cpp - Evaluador de soluciones existentes
// TFG Alberto Hernandez
//
// Lee una instancia y una solucion JSON ya generada, evalua su coste y
// comprueba factibilidad. Salida en CSV para comparativas.
//
// Uso:
//   ./ihtc_eval <instancia.json> <solucion.json> [--csv]
//
//   --csv: salida minima en formato CSV (instance,feasible,cost,...)
//   Sin flag: salida detallada por pantalla

#include <iostream>
#include <string>

#include "common/types.h"
#include "entities/ProblemData.h"
#include "evaluator/Evaluator.h"
#include "evaluator/FeasibilityChecker.h"
#include "io/ProblemParser.h"
#include "io/SolutionReader.h"
#include "solution/Solution.h"

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "Uso: " << argv[0]
              << " <instancia.json> <solucion.json> [--csv]\n";
    return 1;
  }

  std::string instance_file = argv[1];
  std::string solution_file = argv[2];
  bool csv_mode = (argc >= 4 && std::string(argv[3]) == "--csv");

  // Cargar instancia
  ProblemData problem;
  try {
    problem = ProblemParser::Parse(instance_file);
  } catch (const std::exception& e) {
    std::cerr << "Error cargando instancia: " << e.what() << "\n";
    return 1;
  }

  // Cargar solucion
  Solution solution(problem);
  try {
    solution = SolutionReader::Read(solution_file, problem);
  } catch (const std::exception& e) {
    std::cerr << "Error cargando solucion: " << e.what() << "\n";
    return 1;
  }

  // Evaluar
  FeasibilityResult feas = FeasibilityChecker::Check(solution);
  CostBreakdown bd = Evaluator::EvaluateDetailed(solution);

  int scheduled = solution.GetNumScheduledPatients();
  int mandatory_sched = 0;
  int optional_sched  = 0;
  for (PatientId p : problem.GetMandatoryPatientIds()) {
    if (solution.IsPatientScheduled(p)) mandatory_sched++;
  }
  for (PatientId p : problem.GetOptionalPatientIds()) {
    if (solution.IsPatientScheduled(p)) optional_sched++;
  }

  if (csv_mode) {
    // CSV: instance,feasible,cost_total,scheduled,mandatory,optional,
    //      room_cap,gender_mix,mixed_age,delay,unscheduled_opt,
    //      surg_ot,ot_ot,open_ot,nurse_skill,nurse_work,continuity,surg_transfer
    std::cout
        << (feas.feasible ? "true" : "false") << ","
        << bd.Total() << ","
        << scheduled << ","
        << mandatory_sched << ","
        << optional_sched << ","
        << bd.room_capacity << ","
        << bd.room_gender_mix << ","
        << bd.room_mixed_age << ","
        << bd.patient_delay << ","
        << bd.unscheduled_optional << ","
        << bd.surgeon_overtime << ","
        << bd.ot_overtime << ","
        << bd.open_ot << ","
        << bd.nurse_skill << ","
        << bd.nurse_excessive_workload << ","
        << bd.continuity_of_care << ","
        << bd.surgeon_transfer << "\n";
  } else {
    std::cout << "Instancia: " << instance_file << "\n";
    std::cout << "Solucion:  " << solution_file << "\n";
    std::cout << (feas.feasible ? "FACTIBLE\n" : "INFACTIBLE\n");
    if (!feas.feasible) {
      for (const auto& v : feas.violations) {
        std::cout << "  [" << v.constraint << "] " << v.description << "\n";
      }
    }
    std::cout << bd.ToString();
  }

  return feas.feasible ? 0 : 2;
}
