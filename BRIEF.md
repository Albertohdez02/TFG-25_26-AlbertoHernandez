# BRIEF — TFG IHTC 2024 Solver
**Alberto Hernández · 4º Ingeniería Informática**

---

## Índice

1. [El problema: IHTC 2024](#1-el-problema-ihtc-2024)
2. [Entidades del dominio](#2-entidades-del-dominio)
3. [Codificación de la solución](#3-codificación-de-la-solución)
4. [Serialización JSON](#4-serialización-json)
5. [Gestión de restricciones duras](#5-gestión-de-restricciones-duras)
6. [Función objetivo — restricciones blandas](#6-función-objetivo--restricciones-blandas)
7. [Generador aleatorio de soluciones](#7-generador-aleatorio-de-soluciones)
8. [Búsqueda local con VNS + ILS](#8-búsqueda-local-con-vns--ils)
9. [Ablation test — análisis y resultados](#9-ablation-test--análisis-y-resultados)
10. [Conclusiones y siguientes pasos — ACO](#10-conclusiones-y-siguientes-pasos--aco)

---

## 1. El problema: IHTC 2024

La **Integrated Healthcare Timetabling Competition 2024** plantea el siguiente problema de optimización combinatoria:

Dado un horizonte de planificación de hasta 21 días, asignar a cada paciente (obligatorio u opcional):
- Un **día de admisión** dentro de su ventana `[release_day, due_day]`
- Una **habitación** compatible con su estado clínico y género
- Un **quirófano** abierto ese día

Y para cada enfermera:
- Asignarla a turnos `(día, turno)` cubriendo una o varias habitaciones

El objetivo es **minimizar el coste total** de violaciones de restricciones blandas, manteniendo en todo momento la **factibilidad**: cumplimiento estricto de todas las restricciones duras.

Las instancias de competición se dividen en tres grupos: `test` (pequeñas, para validación), `i` (medianas) y `m` (grandes, hasta centenares de pacientes y 30 días).

---

## 2. Entidades del dominio

Todas las entidades son inmutables tras el parseo y se almacenan en `ProblemData`. Los IDs son enteros que actúan directamente como índices de vector (diseño data-oriented).

| Entidad | Fichero | Campos relevantes |
|---|---|---|
| `Patient` | `entities/Patient.h` | `mandatory`, `release_day`, `due_day`, `surgery_duration`, `length_of_stay`, `gender`, `age_group`, `incompatible_rooms`, `skill_level_required`, `workload_per_shift[]` |
| `Occupant` | `entities/Occupant.h` | Paciente ya ingresado (no se decide su ingreso). `room_id`, `length_of_stay`, `gender`, `workload_per_shift[]` |
| `Room` | `entities/Room.h` | `capacity`, `compatible_genders`, `incompatible_patients[]` |
| `Surgeon` | `entities/Surgeon.h` | `max_surgery_time_per_day[]` — límite por día |
| `OperatingTheater` | `entities/OperatingTheater.h` | `availability[]` — capacidad quirúrgica en minutos por día |
| `Nurse` | `entities/Nurse.h` | `skill_level`, `working_shifts[]` (día, turno, carga_máxima) |
| `ProblemData` | `entities/ProblemData.h` | Contenedor de todas las entidades + pesos de la función objetivo + mapas string→índice para el parseo |

Los tipos básicos (`PatientId`, `RoomId`, `Day`, `Shift`, `Gender`, ...) se definen en `common/types.h` como alias de `int`, lo que elimina el overhead de objetos y permite usarlos directamente como índices.

---

## 3. Codificación de la solución

### Diseño data-oriented

La clase `Solution` (`src/solution/`) almacena la asignación en **vectores planos** en lugar de objetos o mapas. Esto maximiza la localidad de caché durante la búsqueda local, donde se accede millones de veces a los mismos arrays.

**Variables de decisión:**

```
patient_room_[pid]          → RoomId asignada (kInvalidId = no programado)
patient_admission_day_[pid] → Day de ingreso
patient_ot_[pid]            → OperatingTheaterId
nurse_assignments_[r*D*S + d*S + s] → NurseId que cubre (room, day, shift)
```

**Caches delta** (se actualizan incrementalmente en O(stay_length) al asignar/desasignar un paciente):

```
room_occupancy_[r*D + d]        → nº de pacientes en (room, day)
ot_load_[ot*D + d]              → minutos quirúrgicos acumulados en (OT, day)
surgeon_load_[surg*D + d]       → minutos acumulados en (surgeon, day)
nurse_workload_[n*D*S + d*s]    → carga de trabajo de (nurse, day, shift)
room_gender_[r*D + d]           → género actual de la habitación ese día
room_day_patients_[r*D + d]     → lista de pacientes por (room, day)
```

La invariante es que estos caches reflejan en todo momento el estado real de la solución. Esto permite evaluar la mayor parte de las restricciones en O(1) consultando el cache en lugar de recorrer todos los pacientes.

### API de la solución

```cpp
bool AssignPatient(pid, room, day, ot)   // asigna y actualiza caches
bool UnassignPatient(pid)                // desasigna y actualiza caches
bool ReassignPatient(pid, room, day, ot) // más eficiente que quitar+poner
bool AssignNurse(nurse, room, day, shift)
bool UnassignNurse(room, day, shift)
```

---

## 4. Serialización JSON

La entrada/salida se gestiona en `src/io/`:

- **`ProblemParser.h`**: parsea el JSON de la instancia usando la librería header-only `nlohmann/json`. Convierte los IDs de string a índices enteros y rellena `ProblemData`.

- **`SolutionIO.h`**: exporta la solución al formato oficial del concurso.

### Formato de salida (compatible IHTC 2024)

**Sección de pacientes:**
```json
{
  "id": "p03",
  "admission_day": 5,
  "room": "r2",
  "operating_theater": "t0"
}
```
Los pacientes no programados solo llevan `"admission_day": "none"` (sin campos `room` ni `operating_theater`).

**Sección de enfermeras** (agrupado por enfermera → asignaciones → lista de habitaciones):
```json
{
  "id": "n01",
  "assignments": [
    { "day": 3, "shift": "early", "rooms": ["r0", "r2"] },
    { "day": 7, "shift": "night", "rooms": ["r1"] }
  ]
}
```

La serialización invierte el índice interno `(room, day, shift) → nurse` agrupando por `nurse → (day, shift) → [rooms]` mediante un `std::map` ordenado.

---

## 5. Gestión de restricciones duras

El módulo `FeasibilityChecker` (`src/evaluator/FeasibilityChecker.h/.cpp`) implementa 13 restricciones duras. Una solución solo es válida si las cumple todas.

| ID | Descripción |
|---|---|
| HC1 | Todos los pacientes obligatorios deben estar programados |
| HC2 | Día de cirugía en `[release_day, due_day]` para obligatorios |
| HC3 | Día de cirugía `≥ release_day` para opcionales (si están programados) |
| HC4 | La estancia completa cabe en el horizonte de planificación |
| HC5 | Ocupación `≤ capacidad` en cada `(habitación, día)` |
| HC6 | Sin mezcla de géneros en ninguna `(habitación, día)` |
| HC7 | Paciente no asignado a habitación incompatible según su historial clínico |
| HC8 | Quirófano abierto (`availability > 0`) el día de la cirugía |
| HC9 | Máximo una enfermera por `(habitación, día, turno)` — implícito en la estructura |
| HC10 | Enfermera disponible en el `(día, turno)` asignado según su horario laboral |
| HC11 | (No aplicable en esta versión) |
| HC12 | Carga del cirujano `≤ max_surgery_time` por día |
| HC13 | Carga del quirófano `≤ availability` por día |

Además se ofrecen dos métodos de comprobación rápida parcial para uso dentro de los operadores de búsqueda local (sin necesidad de verificar la solución completa):

```cpp
FeasibilityChecker::IsFeasiblePatientAssignment(sol, pid, room, day, ot)
FeasibilityChecker::IsFeasibleNurseAssignment(sol, nurse, room, day, shift)
```

Estos son llamados antes de cada tentativa de movimiento, garantizando que la solución **nunca viola restricciones duras**.

---

## 6. Función objetivo — restricciones blandas

El `Evaluator` (`src/evaluator/Evaluator.h`) calcula el coste total como suma de 12 penalizaciones, ponderadas por los pesos de la instancia.

| Componente | Descripción |
|---|---|
| `room_capacity` | Penaliza pacientes en habitación sobre-capacitada |
| `room_gender_mix` | Penaliza mezcla de géneros en una habitación el mismo día |
| `room_mixed_age` | Penaliza mezcla de grupos de edad en una habitación |
| `patient_delay` | Penaliza por cada día de retraso sobre `release_day` |
| `unscheduled_optional` | Penaliza pacientes opcionales no programados |
| `surgeon_overtime` | Penaliza minutos de cirugía por encima del límite diario |
| `ot_overtime` | Penaliza minutos de quirófano por encima de su disponibilidad |
| `open_ot` | Penaliza el número de quirófanos distintos utilizados por día |
| `nurse_skill` | Penaliza cuando el nivel de habilidad de la enfermera es insuficiente |
| `nurse_excessive_workload` | Penaliza carga de trabajo de enfermera que excede su máximo |
| `continuity_of_care` | Penaliza el número de enfermeras distintas que atienden a un paciente |
| `surgeon_transfer` | Penaliza cuando un cirujano usa más de un quirófano en el mismo día |

---

## 7. Generador aleatorio de soluciones

`RandomGenerator` (`src/solver/RandomGenerator.h/.cpp`) construye en cuatro fases una solución **aleatoria y siempre factible**:

**Fase 1 — Obligatorios día a día:**
Ordena los obligatorios por urgencia (menor `due_day` primero) y para cada paciente intenta asignarlo en el primer día de su ventana en el que encuentre habitación y quirófano disponibles (shuffled para aleatorizar).

**Fase 2 — Reparación de obligatorios no colocados:**
Si algún obligatorio no ha podido colocarse en la Fase 1 (por conflictos de capacidad), se intenta **desalojar** opcionales que bloquean recursos para hacer sitio. Garantiza HC1.

**Fase 3 — Opcionales:**
Itera los opcionales en orden aleatorio intentando colocarlos con `TryAssignPatientFeasibly`. Se colocan todos los que quepan sin violar restricciones duras.

**Fase 4 — Enfermeras (greedy):**
Para cada `(habitación, día, turno)` con pacientes, asigna la enfermera disponible que minimice simultáneamente las penalizaciones por nivel de habilidad y maximice la continuidad de cuidado (misma enfermera durante toda la estancia).

---

## 8. Búsqueda local con VNS + ILS

### Pipeline

```
Multi-start (N reinicios, limitado por tiempo global de 600 s):
  └── Generar solución aleatoria factible
  └── ILS: 
        └── Búsqueda local VNS hasta óptimo local
        └── Perturbación (reubicar K=4 pacientes aleatoriamente)
        └── Repetir hasta kMaxPerturbations=15 o tiempo agotado
  └── Guardar mejor solución global si mejora
```

### Operadores (vecindarios VNS)

Cada operador aplica estrategia **first-improvement** exhaustiva: itera todos los candidatos (shuffled) hasta encontrar el primero que mejora el coste.

| # | Nombre | Descripción | Candidatos |
|---|---|---|---|
| 1 | `ChangeRoom` | Mueve un paciente a otra habitación compatible (mismo día y OT) | Todas las habitaciones compatibles |
| 2 | `ChangeDay` | Cambia el día de admisión dentro de la ventana completa | Todos los días de `[release, due]` |
| 3 | `ChangeOT` | Cambia el quirófano (mismo día y habitación) | Todos los OTs abiertos ese día |
| 4 | `Relocate` | Mueve un paciente cambiando simultáneamente día + habitación + OT | Hasta 30 combinaciones (pruned) |
| 5 | `SwapRooms` | Intercambia las habitaciones de dos pacientes | Hasta 200 pares |
| 6 | `SwapDays` | Intercambia los días de admisión de dos pacientes (respetando ventanas) | Hasta 200 pares |
| 7 | `ToggleOpt` | Programa o desprograma un paciente opcional | Todos los opcionales |
| 8 | `ChangeNurse` | Reasigna la enfermera de una posición `(hab, día, turno)` | Hasta 100 posiciones ocupadas |

**Orden de aplicación:** en cada iteración el orden de los operadores se aleatoriza (`std::shuffle`), implementando así el esquema VNS estocástico.

**Perturbación ILS:** reasigna K=4 pacientes a posiciones factibles aleatorias distintas a las actuales, escapando del óptimo local actual. Tras la perturbación se reinicia desde la mejor solución conocida (`best_solution`).

### Bitmask de operadores activos

`LocalSearch::Run` acepta un parámetro `enabled_mask` (8 bits) que activa/desactiva operadores individuales. Esto fue fundamental para ejecutar el ablation test programáticamente.

---

## 9. Ablation test — análisis y resultados

### Metodología

Se ejecutaron **18 configuraciones** sobre **6 instancias** (`i04`, `i08`, `i26`, `m06`, `m11`, `m29`), con 5 seeds × 5 reinicios = 25 runs por instancia-config:

- `all`: todos los operadores activos (configuración completa)
- `no_LS`: solución aleatoria sin búsqueda local (baseline)
- `no_X` (8 configs): leave-one-out — todos menos el operador X
- `only_X` (8 configs): solo el operador X activo

Los resultados se analizan en `ablation_analysis.py` generando 7 figuras en `ablation_results/figures/`.

### Resultados aggregados (6 instancias)

**LOO — Degradación al eliminar cada operador (Δ% coste medio):**

| Operador | Δ% medio | Desv. típ. |
|---|---|---|
| **SwapDays** | +3.35% | 5.25 |
| **Relocate** | +2.32% | 4.10 |
| **ChangeOT** | +2.22% | 3.08 |
| **ChangeDay** | +1.94% | 2.83 |
| **ChangeRoom** | +1.93% | 2.85 |
| **ToggleOpt** | +1.91% | 2.75 |
| **SwapRooms** | +1.88% | 3.17 |
| ChangeNurse | −0.86% | 2.44 |

**Contribución media de cada operador (% de mejoras en config `all`):**

| Operador | % mejoras |
|---|---|
| ChangeRoom | 22.6% |
| ChangeOT | 18.7% |
| SwapRooms | 17.4% |
| Relocate | 14.7% |
| ChangeDay | 13.9% |
| SwapDays | 7.4% |
| ToggleOpt | 5.4% |
| ChangeNurse | 0.0% |

### Conclusiones del ablation

1. **SwapDays es el más crítico** para la calidad final (mayor impacto LOO a pesar de contribuir menos mejoras). Redistribuir días entre pares desbloquea configuraciones que los operadores unarios no alcanzan, especialmente en instancias con ventanas de admisión solapadas.

2. **ChangeRoom y ChangeOT dominan en frecuencia** de mejora (~41% combinado). Son los operadores de "mantenimiento" que en cada iteración ajustan la asignación espacial.

3. **Relocate y ChangeDay** aportan mejoras significativas en instancias con horizontes largos (tipo `m`), donde hay mucha flexibilidad temporal.

4. **ChangeNurse contribuye 0 mejoras** en todas las instancias. Esto indica que la asignación greedy de enfermeras producida por `RandomGenerator` ya es un óptimo local respecto a este vecindario. El operador no es capaz de mejorar el coste en presencia de los demás porque la penalización de enfermeras (skill + continuidad) ya está bien controlada por la construcción. No merece la pena el tiempo de CPU que consume.

5. **ToggleOpt** tiene impacto modesto pero consistente. Su función es principalmente exploratoria: permite salir de soluciones donde un opcional mal ubicado bloquea recursos para otros pacientes.

6. La mejora global de VNS completo respecto a la solución aleatoria es de un **17–20% de media**, con picos del 30% en instancias pequeñas. La calidad de la solución inicial condiciona fuertemente el resultado final.

---

## 10. Conclusiones y siguientes pasos — ACO

### Limitación de la construcción aleatoria

El análisis del ablation revela que el **cuello de botella no está en la búsqueda local** (que es eficiente), sino en la **calidad de la solución inicial**. Construir aleatoriamente genera soluciones con coste inicial de ~3000–47000 según la instancia, dejando mucho margen que la LS recupera parcialmente pero del que nunca se sale del todo.

Una construcción inteligente que coloque los pacientes en posiciones prometedoras desde el principio reduciría el coste inicial y dejaría al VNS explorar una región del espacio de mayor calidad.

### Por qué ACO es adecuado aquí

El problema tiene una estructura natural de **asignación secuencial con restricciones acumulativas**: cada paciente que se coloca afecta la disponibilidad de habitaciones, OTs, carga de cirujanos y estado de género de las habitaciones para los siguientes. Esto encaja directamente con el mecanismo de las colonias de hormigas, donde cada hormiga construye una solución paso a paso guiada por feromonas que codifican la "bondad histórica" de cada asignación.

### Diseño del ACO para IHTC 2024

#### Variables de feromona

La feromona debe capturar la correlación entre un paciente y sus recursos asignados. Propuesta de tres matrices de feromonas:

```
τ_room[patient][room]     — preferencia de asignar 'patient' a 'room'
τ_day[patient][day]       — preferencia del día de admisión
τ_ot[patient][ot]         — preferencia del quirófano
```

Dimensiones representativas: `num_patients × num_rooms` (hasta ~200 × 50 = 10K entradas), perfectamente manejable en memoria.

#### Construcción de una solución (una hormiga)

```
Para cada paciente p (orden heurístico: obligatorios más urgentes primero):
  1. Calcular la heurística η(p, r, d, ot) para cada combinación factible
     η = 1 / (1 + penalización_estimada_de_coste_blando)
  2. Calcular probabilidad de selección:
     P(r, d, ot) ∝ τ_room[p][r]^α · τ_day[p][d]^α · τ_ot[p][ot]^α · η(p,r,d,ot)^β
  3. Seleccionar por ruleta o torneo sesgado
  4. Verificar factibilidad con IsFeasiblePatientAssignment
     Si no factible, excluir candidato y repetir
Asignación de enfermeras: greedy (igual que en RandomGenerator, es óptimo local)
```

La heurística η puede estimarse rápidamente con los caches de `Solution` evaluando parcialmente:
- Penalización por mezcla de género/edad (O(1) con `room_gender_`)
- Penalización por retraso (O(1): `day - release_day`)
- Penalización por apertura de nuevo OT (O(1) con `ot_load_`)

#### Actualización de feromonas (al final de cada iteración)

```
Para la mejor solución encontrada en la iteración:
  τ[p][r] += ρ · (1 / coste_total)   (depósito proporcional a calidad)
  τ[p][d] += ρ · (1 / coste_total)
  τ[p][ot] += ρ · (1 / coste_total)

Evaporación global:
  τ ← (1 - ρ) · τ    (ρ ≈ 0.1)
  Clamp a [τ_min, τ_max] (Ant System Max-Min para evitar convergencia prematura)
```

#### Pipeline ACO + VNS

```
Inicializar feromonas a τ_0 = τ_max (exploración inicial uniforme)
Mientras tiempo < 600 s:
  Para cada hormiga k en [1..n_ants]:
    sol_k = ConstruirSolucion(feromonas, η)
    sol_k = LocalSearch::Run(sol_k, max_iter=1000, mask=0b11111110)  // sin ChangeNurse
    Si sol_k.coste < mejor_global:
      mejor_global = sol_k
  ActualizarFeromonas(mejor_global o mejor_de_iteración)
Exportar mejor_global
```

La combinación ACO (construcción) + VNS (intensificación) es un esquema MMAS (Max-Min Ant System) híbrido estándar para problemas de planificación hospitalaria.

#### Parámetros a ajustar

| Parámetro | Valor inicial sugerido | Efecto |
|---|---|---|
| `n_ants` | 10–20 | Diversidad por iteración |
| `α` | 1.0 | Importancia de la feromona |
| `β` | 2.0–3.0 | Importancia de la heurística |
| `ρ` | 0.1 | Tasa de evaporación |
| `τ_min / τ_max` | 0.01 / 1.0 | Límites MMAS |

#### Ficheros a crear

```
src/solver/ACOSolver.h/.cpp   — clase principal del ACO
src/solver/PheromoneMatrix.h  — encapsula las tres matrices τ con init/update/evaporate
```

`RandomGenerator` queda en el proyecto como fallback de comparación y como base del constructor de hormigas (se puede reutilizar `TryAssignOnDay` con selección probabilística en vez de aleatoria pura).

---

*Última actualización: 2026-04-14*
