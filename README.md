# IHTC 2024 — Solver ACO/VNS para planificación hospitalaria integrada

Metaheurística para el **Integrated Healthcare Timetabling Competition 2024 (IHTC 2024)**. El solver
asigna a cada paciente un día de admisión, una habitación y un quirófano, y reparte las enfermeras
entre las habitaciones en cada turno, respetando las restricciones duras del problema y minimizando
una suma ponderada de penalizaciones blandas.

El enfoque principal es una **colonia de hormigas (ACO/MMAS)** cuya construcción se refina con una
**búsqueda de vecindario variable (VNS)** empleada como búsqueda local iterada (ILS). Se incluye
además un **constructor aleatorio multi-start** que sirve de línea base.

---

## Índice

- [Descripción del problema](#descripción-del-problema)
- [Algoritmos](#algoritmos)
- [Estructura del proyecto](#estructura-del-proyecto)
- [Dependencias](#dependencias)
- [Compilación](#compilación)
- [Ejecución](#ejecución)
- [Ajuste de parámetros (avanzado)](#ajuste-de-parámetros-avanzado)
- [Validación de soluciones](#validación-de-soluciones)
- [Contribuciones](#contribuciones)
- [Licencia](#licencia)
- [Contacto](#contacto)
- [Autor](#autor)

---

## Descripción del problema

El IHTC 2024 combina tres subproblemas de planificación hospitalaria en uno solo:

1. **Admisión de pacientes**: decidir el día de ingreso (dentro de una ventana), la habitación y el
   quirófano de cada paciente, o dejarlo sin programar si es opcional.
2. **Planificación quirúrgica**: respetar la disponibilidad de cirujanos y quirófanos por día.
3. **Asignación de enfermería**: cubrir cada habitación ocupada en cada (día, turno) con una enfermera.

Una solución es **factible** si cumple todas las restricciones duras (HC), verificadas por el
`FeasibilityChecker` y por el validador oficial. Entre las soluciones factibles, la calidad se mide
por el coste blando ponderado, que agrega las ocho penalizaciones oficiales de la competición:

| Penalización | Descripción |
|---|---|
| `RoomAgeMix` | Mezcla de grupos de edad en una misma habitación |
| `RoomSkillLevel` | Enfermera con cualificación inferior a la requerida |
| `ContinuityOfCare` | Número de enfermeras distintas que atienden a un paciente |
| `ExcessiveNurseWorkload` | Carga de trabajo por encima del máximo del turno |
| `OpenOperatingTheater` | Quirófanos abiertos innecesariamente |
| `SurgeonTransfer` | Cirujano operando en varios quirófanos el mismo día |
| `PatientDelay` | Retraso respecto al día de liberación del paciente |
| `ElectiveUnscheduledPatients` | Pacientes opcionales no programados |

---

## Algoritmos

El binario ofrece dos modos de resolución, seleccionables por línea de comandos.

### Modo `aco` (por defecto) — ACO/MMAS + VNS

Implementa un *Max-Min Ant System*. En cada iteración, un grupo de hormigas construye soluciones
guiadas por la feromona y una heurística, y cada solución se refina con la VNS:

- **Construcción**: regla pseudoproporcional ACS (con probabilidad `q0` se explota el mejor
  candidato; en caso contrario se muestrea por ruleta) sobre matrices de feromona para el día
  (`tau_day`), la habitación (`tau_room`) y, opcionalmente, la enfermera (`tau_nurse`).
- **Refinamiento (VNS/ILS)**: ocho vecindarios atómicos (cambio de habitación, día, quirófano,
  reubicación, intercambios, programar/desprogramar opcionales y cambio de enfermera) más tres
  movimientos compuestos (`KickPatient`, `ReorganizeDay`, `SwapNurseBlock`), explorados a
  *first-improvement*, con perturbación y reinicio estilo ILS.
- **Aprendizaje MMAS**: solo la mejor solución deposita feromona, acotada a `[tau_min, tau_max]`,
  con reinicialización ante estancamiento.
- **Arranque y pulido**: una solución inicial *warm-start* siembra la feromona, y una fase final
  (`NursePolisher`) pule la asignación de enfermeras.

### Modo `random` — constructor aleatorio multi-start

Genera repetidamente soluciones constructivas voraces aleatorizadas (`RandomGenerator`) y las mejora
con la misma VNS/ILS, conservando la mejor factible. Es útil como referencia para medir la aportación
del componente ACO.

---

## Estructura del proyecto

```
.
├── src/
│   ├── main.cpp                     # Punto de entrada: CLI, orquestación y exportación
│   ├── common/types.h               # Tipos, alias e identificadores
│   ├── entities/                    # Entidades del dominio
│   │   ├── Patient.h  Occupant.h  Room.h  Nurse.h
│   │   ├── Surgeon.h  OperatingTheater.h
│   │   └── ProblemData.h            # Instancia completa del problema
│   ├── solution/
│   │   └── Solution.{h,cpp}         # Representación de la solución y cachés
│   ├── evaluator/
│   │   ├── Evaluator.{h,cpp}        # Función objetivo (coste blando ponderado)
│   │   └── FeasibilityChecker.{h,cpp}  # Restricciones duras (HC)
│   ├── io/
│   │   ├── ProblemParser.h          # Lectura de instancias JSON
│   │   ├── SolutionReader.h         # Lectura de soluciones
│   │   ├── SolutionIO.h             # Exportación en formato de la competición
│   │   └── json.hpp                 # nlohmann/json (incluida)
│   └── solver/
│       ├── ACOSolver.{h,cpp}        # Colonia de hormigas MMAS
│       ├── LocalSearch.{h,cpp}      # VNS / ILS (8 atómicos + 3 compuestos)
│       ├── CompoundMoves.{h,cpp}    # Movimientos compuestos
│       ├── NursePolisher.{h,cpp}    # Pulido final de enfermería
│       └── RandomGenerator.{h,cpp}  # Constructor voraz aleatorizado
├── instances/                       # Instancias del problema (formato JSON)
│   ├── public-instances/            # Conjunto publico   (i01-i30)
│   ├── hidden-instances/            # Conjunto oculto    (m01-m30)
│   └── test-instances/              # Conjunto de prueba (test01-test10)
├── best-solutions/                  # 60 mejores soluciones conocidas (sol_i*, sol_m*)
├── raw-runs/                         # 600 ejecuciones ACO-tuned (IRACE) de referencia
│   ├── public-instances/            # 300 ejecuciones (i01-i30 × 10 semillas)
│   └── hidden-instances/            # 300 ejecuciones (m01-m30 × 10 semillas)
├── solutions/                       # Soluciones generadas (salida)
├── validator/                       # Validador oficial (IHTP_Validator)
├── CMakeLists.txt
└── README.md
```

---

## Dependencias

- **CMake** ≥ 3.16
- **Compilador C++20** (GCC 11+ o Clang 14+)
- **nlohmann/json**: incluida en el repositorio (`src/io/json.hpp`), no requiere instalación.

La compilación de *release* usa `-O3 -march=native -flto`; el indicador `-march=native` optimiza para
la CPU de la máquina donde se compila.

---

## Compilación

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

El ejecutable se genera en **`build/ihtc_solver`**.

---

## Ejecución

```bash
./build/ihtc_solver <instancia.json> [seed] [max_iter] [restarts] [time_s] [mode] [n_ants]
```

| Parámetro | Descripción | Por defecto |
|---|---|---|
| `instancia.json` | Fichero JSON con la instancia del problema | *(obligatorio)* |
| `seed` | Semilla del generador aleatorio (resultados reproducibles) | `42` |
| `max_iter` | Iteraciones máximas de búsqueda local | `5000` |
| `restarts` | Reinicios multi-start (**solo modo `random`**; ignorado en `aco`) | `100` |
| `time_s` | Presupuesto de tiempo global, en segundos | `600` |
| `mode` | `aco` (ACO+VNS) o `random` (multi-start) | `aco` |
| `n_ants` | Hormigas por iteración (**solo modo `aco`**) | `12` |

Los parámetros son posicionales y opcionales de izquierda a derecha. La solución se exporta
automáticamente a `solutions/<instancia>_solution.json`, y por la salida estándar se muestran la
factibilidad, el desglose de costes y el coste final.

### Ejemplos

```bash
# ACO + VNS con parámetros por defecto (600 s)
./build/ihtc_solver instances/public-instances/i05.json

# ACO + VNS, semilla 7, 5 minutos
./build/ihtc_solver instances/public-instances/i05.json 7 5000 100 300 aco

# Constructor aleatorio multi-start (línea base)
./build/ihtc_solver instances/public-instances/i05.json 42 5000 100 300 random

# Lote sobre el conjunto de prueba
for i in $(seq -w 1 10); do
  ./build/ihtc_solver instances/test-instances/test${i}.json
done
```

---

## Ajuste de parámetros (avanzado)

Todos los parámetros internos del ACO y de la VNS pueden sobrescribirse mediante variables de entorno,
sin recompilar. Si una variable no se define, se mantiene su valor por defecto, por lo que el
comportamiento base no cambia. Esto permite, por ejemplo, inyectar configuraciones halladas con
herramientas de *tuning* automático (irace).

**ACO**

| Variable | Parámetro | Defecto |
|---|---|---|
| `IHTC_N_ANTS` | Hormigas por iteración | `12` |
| `IHTC_ALPHA` | Exponente de la feromona | `1.0` |
| `IHTC_BETA` | Exponente de la heurística | `2.0` |
| `IHTC_RHO` | Tasa de evaporación | `0.10` |
| `IHTC_Q0` | Probabilidad de explotación (ACS) | `0.90` |
| `IHTC_TAU_MIN_FACTOR` | Factor de `tau_min` (MMAS) | `2` |
| `IHTC_STAGNATION_K` | Iteraciones sin mejora antes de reiniciar | `15` |
| `IHTC_POLISH_BUDGET` | Tiempo de la fase final de enfermería (s) | `60` |

**VNS / ILS**

| Variable | Parámetro | Defecto |
|---|---|---|
| `IHTC_VNS_PERTURB_BASE` | Intensidad mínima de perturbación | `4` |
| `IHTC_VNS_PERTURB_MAX` | Intensidad máxima de perturbación | `25` |
| `IHTC_VNS_PERTURB_FACTOR` | Factor proporcional al tamaño | `0.10` |
| `IHTC_VNS_SWAP_PAIRS` | Tope de pares en intercambios | `200` |
| `IHTC_VNS_RELOCATE` | Tope de combinaciones en reubicación | `200` |
| `IHTC_VNS_NURSE_POS` | Tope de posiciones en cambio de enfermera | `500` |
| `IHTC_VNS_COMPOUND` | Activa los movimientos compuestos (`0`/`1`) | `1` |

```bash
# Ejemplo: configuración ACO personalizada
IHTC_N_ANTS=6 IHTC_ALPHA=1.32 IHTC_BETA=1.35 IHTC_RHO=0.20 \
  ./build/ihtc_solver instances/public-instances/i05.json 42 5000 100 600 aco
```

---

## Validación de soluciones

El repositorio incluye el **validador oficial** ya compilado en `validator/IHTP_Validator`, que
comprueba la factibilidad y calcula el coste de una solución de forma independiente al solver:

```bash
./validator/IHTP_Validator instances/public-instances/i05.json solutions/i05_solution.json
```

Para recompilarlo:

```bash
g++ -std=c++17 -O2 -I validator validator/IHTP_Validator.cc -o validator/IHTP_Validator
```

---

## Contribuciones

Este repositorio forma parte de un Trabajo de Fin de Grado, por lo que su desarrollo principal está
cerrado a efectos académicos. Aun así, las sugerencias, los informes de errores y las propuestas de
mejora son bienvenidos: abre una *issue* describiendo el caso, o envía una *pull request* con el cambio
propuesto y una breve justificación. Se agradece que el código nuevo mantenga el estilo existente
(C++20, comentarios de cabecera `/** @brief */`) y que toda solución generada se valide con el
validador oficial.

## Licencia

Distribuido bajo la licencia **MIT**; consulta el fichero [`LICENSE`](LICENSE) para el texto completo.
El validador oficial (`validator/`) y la biblioteca `nlohmann/json` (`src/io/json.hpp`) conservan sus
respectivas licencias.

## Contacto

Puedes contactame a través del correo electrónico: *alu0101433905@ull.edu.es* o mediante LinkedIn: [Alberto Antonio Hernández Hernández](https://www.linkedin.com/in/albertoahdezhdez/).

## Autor

Desarrollado por **Alberto Antonio Hernández Hernández** como Trabajo de Fin de Grado (Grado en Ingeniería Informática) de la Universidad de La Laguna,
en el contexto del IHTC 2024.
