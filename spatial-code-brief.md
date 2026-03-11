# Codificación Espacial y Búsqueda Local — IHTC 2024

## 1. Codificación de la solución

### 1.1 Enfoque: codificación espacial (patient-centric)

La solución se organiza **por paciente** como eje principal. La pregunta que responde la codificación es "¿dónde y cuándo va cada paciente?". Cada paciente programado tiene asociados tres recursos: una habitación, un día de admisión y un quirófano.

La solución tiene dos bloques:

```
Solución S = {
  BLOQUE 1 — Asignación de pacientes:
    Para cada paciente p:
      patient_room[p]          = habitación asignada
      patient_admission_day[p] = día de ingreso
      patient_ot[p]            = quirófano asignado

  BLOQUE 2 — Enfermeras:
    Para cada (habitación, día, turno):
      nurse[habitación][día][turno] = id_enfermera
}
```

### 1.2 Estructura primaria: tres vectores paralelos

A diferencia de la codificación temporal (que usa una lista de admisiones por día), aquí la información se almacena directamente en tres vectores indexados por `PatientId`:

```cpp
vector<RoomId>              patient_room_;          // habitación de cada paciente
vector<Day>                 patient_admission_day_;  // día de ingreso
vector<OperatingTheaterId>  patient_ot_;             // quirófano asignado
unordered_set<PatientId>    scheduled_patients_;     // quiénes están programados
```

Por ejemplo, si `patient_room_[5] = 2`, `patient_admission_day_[5] = 3` y `patient_ot_[5] = 1`, significa que el paciente 5 se ingresa el día 3 en la habitación 2 y se opera en el quirófano 1.

Un paciente **no programado** tiene `kInvalidId` en los tres vectores y no aparece en `scheduled_patients_`.

### 1.3 Ventaja frente a la codificación temporal

En la codificación temporal, la estructura primaria es `day_admissions_[día] = [(paciente, habitación, quirófano), ...]`. Para consultar dónde está un paciente hay que buscar en todas las listas de admisiones, por lo que se necesitan índices inversos adicionales (`patient_day_`, `patient_room_`, `patient_ot_`).

En la codificación **espacial**, esos vectores **son** la estructura primaria — no hay redundancia ni necesidad de mantener índices inversos sincronizados. Esto simplifica la implementación y reduce el riesgo de inconsistencias.

### 1.4 Cachés delta

Para no recalcular datos caros desde cero ante cada movimiento, la solución mantiene cachés que se actualizan incrementalmente:

| Caché | Qué almacena | Indexado por |
|-------|-------------|--------------|
| `room_occupancy_` | Nº pacientes en cada habitación-día | (habitación, día) |
| `ot_load_` | Minutos de cirugía acumulados | (quirófano, día) |
| `surgeon_load_` | Minutos de cirugía por cirujano | (cirujano, día) |
| `nurse_workload_` | Carga de trabajo de cada enfermera | (enfermera, día, turno) |
| `room_gender_` | Género actual de cada habitación-día | (habitación, día) |
| `room_day_patients_` | Lista de pacientes en cada habitación-día | (habitación, día) |

Todos usan **arrays planos** con índices calculados mediante funciones helper (`RoomDayIndex`, `OtDayIndex`, etc.). Este diseño orientado a datos (Data-Oriented Design) maximiza la eficiencia de caché de la CPU.

Cada vez que se asigna o desasigna un paciente, los métodos internos `AddPatientToCaches` y `RemovePatientFromCaches` actualizan todas estas cachés de forma incremental en O(estancia del paciente).

### 1.5 API espacial

La solución expone operaciones centradas en el paciente:

- `AssignPatient(paciente, habitación, día, quirófano)` — programa un paciente
- `UnassignPatient(paciente)` — quita del planning
- `ReassignPatient(paciente, nueva_hab, nuevo_día, nuevo_qt)` — reubicación completa (atómica)
- `AssignNurse(enfermera, habitación, día, turno)` — asigna enfermera a un turno
- `UnassignNurse(habitación, día, turno)` — libera un turno

---

## 2. Construcción de la solución inicial

Se usa un **generador constructivo greedy** que construye la solución en cuatro fases:

### Fase 1 — Obligatorios (ordenados por urgencia)

Para cada día d = 0..D-1, se recogen los pacientes obligatorios cuya ventana de admisión incluye ese día. Se ordenan por urgencia: primero los que tienen `due_day` (fecha límite) más cercana. Para cada uno, se busca una combinación factible de (habitación, quirófano) y se programa con `AssignPatient`.

### Fase 2 — Reparación

Los obligatorios que no se pudieron colocar se intentan forzar desalojando y reubicando pacientes que estén bloqueando recursos (habitación o quirófano). Esto garantiza que todos los obligatorios queden programados.

### Fase 3 — Opcionales (probabilísticos)

Por cada día, se intenta programar pacientes opcionales con un 70% de probabilidad cada uno. Solo se programan si se encuentra una ubicación factible.

### Fase 4 — Enfermeras (asignación greedy)

Para cada (habitación, día, turno) con ocupación > 0, se elige la enfermera factible que **minimiza** violaciones de skill y sobrecarga, y que **maximiza** la continuidad de cuidado (reutilizar la enfermera del día anterior en la misma habitación). Se usa una función de puntuación:

```
score = skill_match * w_skill + continuity_bonus * w_cont - overload_penalty * w_load
```

y se selecciona la enfermera con mayor score.

---

## 3. Búsqueda local

### 3.1 Estrategia general: ILS (Iterated Local Search)

La búsqueda local utiliza **ILS** (Iterated Local Search), que combina tres componentes:

1. **Búsqueda local first-improvement** hasta llegar a un óptimo local
2. **Perturbación** para escapar del óptimo local
3. **Reinicio** de la búsqueda local desde la solución perturbada

Se guarda siempre la **mejor solución encontrada** a lo largo de todo el proceso. El ciclo ILS se repite hasta un máximo de 15 perturbaciones o hasta agotar el tiempo límite (30 segundos por defecto).

### 3.2 Framework multi-start

Todo lo anterior se envuelve en un bucle **multi-start**: se ejecutan N reinicios independientes (por defecto 10), cada uno generando una solución aleatoria nueva y aplicándole ILS. Al final se devuelve la mejor solución de todos los reinicios.

```
Para cada reinicio r = 1..N:
  1. Generar solución aleatoria factible (greedy)
  2. Aplicar ILS (LS + perturbación + LS + ...)
  3. Si el coste es el mejor visto, guardarla
Devolver la mejor solución
```

### 3.3 Vecindarios

La búsqueda local usa **8 vecindarios** que se prueban en orden aleatorio en cada iteración. Cada vecindario itera una muestra aleatoria de pacientes programados (hasta 60) y acepta el **primer movimiento que mejore el coste** (first-improvement):

| # | Vecindario | Qué hace | Dimensión |
|---|-----------|----------|-----------|
| 1 | **TryChangeRoom** | Cambia la habitación de un paciente, manteniendo día y quirófano | Espacial |
| 2 | **TryChangeDay** | Cambia el día de admisión dentro de la ventana `[release_day, due_day]` | Temporal |
| 3 | **TryChangeOT** | Cambia el quirófano, manteniendo día y habitación | Recursos |
| 4 | **TryRelocate** | Cambia día + habitación + quirófano simultáneamente (mínimo 2 de 3) | Compuesto |
| 5 | **TrySwapRooms** | Intercambia las habitaciones de dos pacientes | Espacial |
| 6 | **TrySwapDays** | Intercambia los días de admisión de dos pacientes | Temporal |
| 7 | **TryToggleOptional** | Programa o desprograma un paciente opcional | Decisión |
| 8 | **TryChangeNurse** | Cambia la enfermera asignada a un (habitación, día, turno) | Personal |

### 3.4 Mecánica de un movimiento

Todos los vecindarios siguen el mismo patrón usando la API espacial:

```
Para cada paciente p (shuffled, máx. 60):
  1. Guardar estado actual: (room, day, ot) ← consultas directas O(1)
  2. UnassignPatient(p)                     ← quitar de la solución
  3. Para cada candidato alternativo:
     a. Comprobar factibilidad con IsFeasiblePatientAssignment(p, room', day', ot')
     b. Si factible: AssignPatient(p, room', day', ot')
     c. Evaluar coste total con Evaluator::Evaluate()
     d. Si coste < coste_actual → aceptar (return true)
     e. Si no mejora → UnassignPatient(p) y probar siguiente candidato
  4. Si ningún candidato mejoró → AssignPatient(p, room, day, ot) (restaurar)
```

**Diferencia clave con la codificación temporal:** en la versión temporal el patrón era `DischargePatient → IsFeasibleAdmission → AdmitPatient`. Aquí el patrón equivalente es `UnassignPatient → IsFeasiblePatientAssignment → AssignPatient`. La ventaja es que no hay necesidad de comprobar auto-referencias (si el paciente ya estaba asignado), porque `UnassignPatient` siempre se llama antes — el paciente nunca está asignado al momento de validar.

### 3.5 Verificación de factibilidad

`IsFeasiblePatientAssignment(solución, paciente, habitación, día, quirófano)` comprueba las restricciones duras relevantes de forma rápida usando los cachés:

- **HC2:** Capacidad de habitación — `room_occupancy[hab][d] < capacity` para cada día de estancia
- **HC3:** Género — el género del paciente es compatible con `room_gender[hab][d]`
- **HC4:** Departamento — la especialidad de la habitación es compatible con el departamento del paciente
- **HC5:** Ventana de admisión — `release_day ≤ día ≤ due_day` (para obligatorios)
- **HC6:** Estancia dentro de horizonte — `día + duración ≤ num_days`
- **HC7:** Capacidad del quirófano — `ot_load[qt][día] + duración_cirugía ≤ disponibilidad`
- **HC8:** Capacidad del cirujano — `surgeon_load[cir][día] + duración_cirugía ≤ max_cirujano`
- **HC12:** Habitaciones incompatibles — la habitación no está en la lista de incompatibles del paciente
- **HC13:** Quirófano abierto — el quirófano tiene disponibilidad > 0 ese día

Para enfermeras, `IsFeasibleNurseAssignment` comprueba HC10 (la enfermera trabaja ese turno) y HC11 (no supera su carga máxima).

### 3.6 Perturbación ILS

Cuando la búsqueda local se estanca, se aplica una **perturbación** para escapar del óptimo local:

1. Se seleccionan **4 pacientes** programados al azar
2. Para cada uno, se desasigna con `UnassignPatient`
3. Se intenta reubicar en una posición factible aleatoria **diferente** a la original
4. Si no se encuentra alternativa factible, se restaura la posición original

Esto desplaza la solución a una zona distinta del espacio de búsqueda, permitiendo que la siguiente ronda de LS encuentre nuevos caminos de mejora.

### 3.7 Controles de rendimiento

Para que la búsqueda escale a instancias grandes (500+ pacientes), se aplican límites:

- **Pacientes por vecindario:** máximo 60 pacientes shuffled por llamada
- **Pares en swaps:** máximo 200 pares evaluados en TrySwapRooms/TrySwapDays
- **Combinaciones en TryRelocate:** máximo 30 combinaciones (día × habitación × quirófano) por paciente
- **Posiciones de enfermera:** máximo 100 posiciones (habitación, día, turno) por llamada
- **Perturbaciones ILS:** máximo 15 por ejecución
- **Tiempo límite:** 30 segundos por ejecución de ILS

---

## 4. Mejoras obtenidas

### 4.1 Punto de partida

La rama `main` originalmente solo contenía las entidades, el parser, la clase `Solution` y un `main.cpp` de demostración. No existía ni evaluador, ni verificador de factibilidad, ni búsqueda local — solo se podían crear soluciones a mano.

### 4.2 Infraestructura creada

Se han creado desde cero los siguientes componentes:

| Componente | Ficheros | Función |
|-----------|---------|---------|
| **FeasibilityChecker** | `evaluator/FeasibilityChecker.h/.cpp` | Verifica 13 restricciones duras (HC1-HC13) |
| **Evaluator** | `evaluator/Evaluator.h/.cpp` | Calcula coste blando ponderado (12 componentes) |
| **RandomGenerator** | `solver/RandomGenerator.h/.cpp` | Genera soluciones factibles (heurístico greedy) |
| **LocalSearch** | `solver/LocalSearch.h/.cpp` | ILS con 8 vecindarios exhaustivos |
| **SolutionIO** | `io/SolutionIO.h` | Exportación JSON + resumen en consola |
| **main.cpp** | `main.cpp` | Pipeline completo multi-start + ILS |

### 4.3 Resultados

Todas las instancias producen soluciones **FACTIBLES** (0 violaciones duras). La mejora del ILS sobre la solución greedy inicial:

| Instancia | Coste Inicial (avg) | Coste Final | Mejora | Tiempo |
|-----------|--------------------:|------------:|-------:|-------:|
| test01 | 2 964 | 2 144 | 27.7% | 0.3s |
| test02 | 2 359 | 726 | 69.2% | 0.7s |
| test03 | 10 130 | 9 536 | 5.9% | 0.2s |
| test04 | 1 713 | 798 | 53.4% | 1.9s |
| test05 | 16 076 | 15 164 | 5.7% | 0.3s |
| test06 | 24 467 | 18 889 | 22.8% | 1.0s |
| test07 | 17 319 | 14 876 | 14.1% | 1.5s |
| test08 | 27 246 | 25 719 | 5.6% | 2.1s |
| test09 | 19 625 | 17 126 | 12.7% | 2.9s |
| test10 | 45 621 | 34 326 | 24.8% | 137s |

**Mejora media global: ~16.8%** con 10 reinicios, seed=42, 5000 iteraciones máximas.

Las instancias con más pacientes opcionales (test02, test04) muestran la mayor mejora porque el vecindario `TryToggleOptional` puede programar/desprogramar pacientes de forma inteligente.

---

## 5. Resumen del pipeline completo

```
Entrada: instancia JSON + seed + max_iteraciones + N_reinicios

Para cada reinicio:
  ┌─────────────────────────────────────────────┐
  │  1. Generador constructivo greedy           │
  │     (obligatorios → reparación → opcionales │
  │      → enfermeras greedy)                   │
  ├─────────────────────────────────────────────┤
  │  2. ILS (Iterated Local Search)             │
  │     ┌───────────────────────────┐           │
  │     │ LS first-improvement      │           │
  │     │ (8 vecindarios × 60 pacs) │◄──┐       │
  │     └───────────┬───────────────┘   │       │
  │                 │ estancado         │       │
  │                 ▼                   │       │
  │     ┌───────────────────────────┐   │       │
  │     │ Perturbación (4 pacs)     │───┘       │
  │     └───────────────────────────┘           │
  │     (repetir hasta 15 veces o 30s)          │
  └─────────────────────────────────────────────┘

Salida: mejor solución de los N reinicios → JSON
```
