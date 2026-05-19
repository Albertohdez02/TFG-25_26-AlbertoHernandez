# Auditoría del solver IHTC 2024 y hoja de ruta hacia ACO+ALNS híbrido

**Autor:** Alberto Hernández
**Fecha:** 2026-05-08
**Repositorio:** `/home/alberto/TFG-25_26-AlbertoHernandez`
**Rama:** `ACO`

---

## 0. Resumen ejecutivo

El solver actual (ACO+VNS+ILS, post-Fases A+B+C+D) cierra a +52.5 % de gap medio respecto a `best-solutions/` en i01–i30 a 600 s. El equipo Twente, 3.º del IHTC 2024, reporta **2-5 %** con un enfoque MILP+CP+SA descompuesto. La diferencia de un orden de magnitud no se debe a un único bug ni a parámetros mal puestos: es estructural. Este documento descompone el gap en sus causas concretas (codificación, operadores, ACO frente al canon MMAS/ACS), revisa la literatura del problema, y propone una hoja de ruta **ACO+ALNS híbrido en 5 etapas** que mantiene la línea ACO del TFG mientras incorpora el paradigma destroy/repair dominante en patient-admission scheduling.

**Decisión arquitectónica:** preservar el ACO como generador de soluciones con feromona aprendida, y reemplazar la fase de perturbación ILS por un módulo ALNS adaptativo (destroy/repair). El resto del solver (VNS, evaluador, factibilidad) se mantiene y se mejora incrementalmente.

---

## 1. Auditoría del solver actual

### 1.1 Codificación (`Solution`)

La clase `Solution` ([src/solution/Solution.h](src/solution/Solution.h), [src/solution/Solution.cpp](src/solution/Solution.cpp)) es **patient-centric** para pacientes (cachés `patient_room_`, `patient_admission_day_`, `patient_ot_`) pero **room-centric** para enfermeras (`nurse_assignments_[room][day][shift]`). Esa asimetría tiene consecuencias:

- Los operadores VNS mueven pacientes con `Solution::AssignPatient` y `UnassignPatient`. Las cachés delta (`room_occupancy`, `ot_load`, `surgeon_load`, `room_gender`) se actualizan, pero **la asignación de enfermeras no**. El §8 del `history/project-history.md` ya documentó el bug `UncoveredRoom` que esto provocaba; el fix fue post-hoc (`EnsureFullNurseCoverage` tras cada accept y tras `Perturb`). Eso enmascara la ineficiencia: el solver gasta ciclos creando celdas (room, day) sin nurse y reparándolas, en vez de mantener el invariante.
- `UpdateRoomGender` ([src/solution/Solution.cpp:436-467](src/solution/Solution.cpp#L436-L467)) recalcula el género iterando todos los ocupantes y pacientes presentes. Es O(|pacientes en celda|) por movimiento. Con instancias de ~500 pacientes ejecutando miles de `TryX`, esto se acumula.
- `Solution.cpp:407-409` usa `std::erase(std::remove(...))` para eliminar pacientes de listas dinámicas — O(|pacientes_en_celda|) por movimiento.

**Más crítico**: la **evaluación es completa**. `Evaluator::Evaluate` ([src/evaluator/Evaluator.cpp:47](src/evaluator/Evaluator.cpp#L47)) recorre las 12 componentes blandas enteras tras cada movimiento aceptado en `LocalSearch::Run`. No hay evaluación delta. Cada `TryChangeRoom`, `TryChangeDay`, etc. paga el coste completo por aceptación.

### 1.2 Operadores VNS — vecindarios ciegos

`LocalSearch.cpp` define 8 operadores `TryX(Solution&, int& cost, std::mt19937&)`. Auditoría cuantitativa:

| Operador | Límite por iteración LS | Cobertura en i26 (P=500) |
|---|---|---|
| `TryChangeRoom` | `min(60, |scheduled|)` pacientes × todas habitaciones compatibles | **12 % de pacientes** |
| `TryChangeDay` | `min(60, |scheduled|)` × días en ventana × habitaciones | **12 % de pacientes** |
| `TryChangeOT` | `min(60, |scheduled|)` × OTs abiertos | **12 % de pacientes** |
| `TrySwapRooms` | hasta `kMaxPairs=200` pares | **0.16 % de los 124 750 pares posibles** |
| `TrySwapDays` | hasta `kMaxPairs=200` pares | **0.16 % de los pares posibles** |
| `TryToggleOpt` | exhaustivo, pero **1 intento por opcional** (un único `(d, r)` sin reintentar) | falsos negativos sistemáticos |
| `TryChangeNurse` | `min(100, |posiciones_ocupadas|)` | **6 %** de las (room, day, shift) |
| `TryRelocate` | `kMaxCombos=30` combinaciones 3D por paciente | **29 %** de combos teóricos |

Los límites duros se diseñaron para una VNS rápida en instancias pequeñas (test01–test10, P≈30). En las instancias grandes (i17, i22, i26, i27 con P > 300) **se exploran consistentemente <1 % del vecindario**. El ablation test ya mostró que SwapDays tiene LOO Δ% = +3.4 %, pero ese efecto se mide con sus límites actuales — un SwapDays sin tope puede atacar mucho más coste.

### 1.3 Operadores ausentes

Movimientos teóricamente representables con la codificación actual pero que **ningún operador genera**:

- **SurgeonConsolidation (move-3 cluster):** mover hasta k pacientes del mismo cirujano a un mismo día para minimizar `surgeon_overtime` y `open_ot`. El operador `ChangeDay` mueve uno a uno; si la mejora requiere mover 3 simultáneamente para que la suma de duraciones quepa en `surgeon_max_time`, los 3 movimientos individuales son perdidas (cada uno aislado empeora) y first-improvement los descarta.
- **OTShifting (cluster cambia OT):** cuando un OT está sobrecargado en un día, mover varios pacientes a otro OT abierto en el mismo día. `ChangeOT` lo hace uno a uno; el primero típicamente abre nuevo OT (peor `open_ot`) y se rechaza. La cluster-move evalúa el efecto agregado.
- **DayBlock (sala+turno completo):** mover todos los pacientes de una `(sala, día)` a otra. Útil cuando una sala tiene mezcla de género/edad y otra está vacía.
- **NurseSwap entre turnos:** intercambiar dos enfermeras de turnos distintos en el mismo día para balancear `nurse_excessive_workload`. El `ChangeNurse` actual reasigna individuales en una posición.
- **Surgeon-day removal-and-reinsert:** vaciar todos los pacientes asignados a (cirujano s, día d) y re-insertarlos con greedy informado. Es el destroy/repair pero a nivel de operador VNS.

Estos cinco son trivialmente añadibles sobre la codificación actual y **abren una región del espacio de búsqueda hoy invisible**.

### 1.4 ACO frente al canon MMAS/ACS

La implementación se llama "MMAS" pero combina elementos de ACS. Comparación con la teoría:

| Aspecto | Implementación actual | Canon MMAS | Canon ACS | Diagnóstico |
|---|---|---|---|---|
| Feromona | `τ_day[p][d]` + `τ_room[p][r]` (dos matrices marginales) | una matriz sobre arcos `τ[i][j]` | una matriz sobre arcos | **Marginal**: nunca aprende correlaciones día×habitación |
| Heurística η | `η_day = 1/(1 + delay × w_delay)`, `η_room ∈ {0,1}` | depende del problema | depende | **Subdesarrollada**: ignora `surgeon_load`, `ot_load`, `open_ot`, `room_state` |
| Selección | pseudoproporcional (`q0=0.90`) | proporcional (q0=0) | pseudoproporcional + local update | **Híbrido nominal**: toma q0 de ACS pero NO el local update; pierde decorrelación ACS y diversidad MMAS |
| Update τ | siempre `global_best` si existe | `iteration_best` temprano, `global_best` tardío | `global_best` con local update durante construcción | **Sesgo a global**: nunca explora con `iteration_best`; reset por estancamiento es nuclear (solo tras `stagnation_k=15`) |
| Cotas MMAS | `τ_max = 1/(ρ·C_best)`, `τ_min = τ_max/(2P)` | idéntico | (no aplica) | OK |
| Candidate lists | no | sí (top-k) | sí | Construcción evalúa O(D+R) por paciente — aceptable, pero acelera 3-10× |
| Decorrelación entre hormigas paralelas | ninguna; mismas τ, mismo q0 | no contemplado, single-thread | no contemplado | **12 hormigas leyendo τ idéntica con q0=0.9 producen soluciones casi iguales en explotación** |

**Conclusión §1.4:** el ACO es en la práctica un MMAS con regla pseudoproporcional pero sin local update — un compromiso que pierde las ventajas de ambos. El warm-start (Fase B) y la Fase A enmascaran parcialmente la heurística pobre, pero el aprendizaje sigue siendo marginal.

### 1.5 Asimetría feasibility ↔ caches

`FeasibilityChecker::IsFeasiblePatientAssignment` ([src/evaluator/FeasibilityChecker.cpp](src/evaluator/FeasibilityChecker.cpp), líneas 65-180 aprox) chequea HC5 (capacidad), HC6 (género), HC7 (compatibilidad), HC8 (OT abierto), HC12 (cirujano overtime), HC13 (OT overtime). **No chequea HC14 (cobertura enfermera)**. Esta se valida solo en `CheckRoomCoverage` global. Resultado: cualquier movimiento de paciente puede crear una celda `(r, d, shift)` poblada sin enfermera; `LocalSearch::Run` repara con `EnsureFullNurseCoverage` después de cada accept.

Eso funciona — las soluciones salen factibles — pero implica que el evaluador reporta costes inconsistentes durante la búsqueda: la celda descubierta no contribuye a `nurse_skill` mientras está rota, así que el cost reportado es **artificialmente bajo** entre el accept y el `EnsureFullNurseCoverage`. La VNS converge a óptimos locales falsos hasta que la cobertura se restablece.

### 1.6 Perturbación ILS

`Perturb(strength=4)` está hardcoded en [src/solver/LocalSearch.cpp](src/solver/LocalSearch.cpp). Para test01 con P=30 esto reubica el 13 % de los pacientes — perturbación significativa. Para i26 con P≈500, reubica el 0.8 % — esencialmente ruido. **Strength debería escalar con √P o log(P)**.

Además, la perturbación consiste en `relocate-random` × 4. No hay diversificación dirigida (e.g., perturbar específicamente al cirujano más sobrecargado, o al OT más cerca del límite).

### 1.7 Checklist de problemas detectados

| # | Problema | Severidad | Atacable en | 
|---|---|---|---|
| P1 | Evaluación completa por movimiento sin delta | Media-alta | refactor evaluator (no urgente) |
| P2 | Límites duros 60 / 200 / 30 / 100 en operadores | **Alta** | quick win |
| P3 | Operadores cluster ausentes (Surgeon, OT, DayBlock, NurseSwap) | **Alta** | etapa 2 |
| P4 | τ marginal en lugar de tensor 3D | Media | etapa 5 |
| P5 | η solo modela patient_delay | **Alta** | etapa 4 |
| P6 | q0=0.90 fijo, sin schedule, sin local update | Media | quick win (ER-ACO) |
| P7 | Update siempre con global_best, reset solo nuclear | Media | etapa 4 |
| P8 | Sin candidate lists | Baja | etapa 4 |
| P9 | Hormigas paralelas sin decorrelación | Baja | etapa 4 |
| P10 | HC14 fuera de IsFeasiblePatientAssignment, reparación post-hoc | Baja | aceptable como está |
| P11 | Perturbación strength=4 fija | Media | quick win |
| P12 | ToggleOpt con 1 intento por opcional | Media | quick win |
| P13 | Enfermeras totalmente fuera del aprendizaje ACO | Media | etapa 5 |

---

## 2. Revisión de literatura

### 2.1 IHTC 2024 — top finalistas y métodos

Fuente: sitio oficial [ihtc2024.github.io](https://ihtc2024.github.io/), ScienceDirect del journal especial, paper PATAT 2024 [paper 52](https://patatconference.org/patat2024/proceedings/papers/52.pdf).

- **Distribución de equipos:** 26 finalistas. **16 MILP, 2 CP, 8 metaheurísticas**. La mayoría de las metaheurísticas son LS-based; hay una ALNS y una GRASP. **Ningún ACO en los premios**.
- **Equipo Twente (3.º clasificado):** enfoque híbrido en 3 fases — ([arXiv:2511.04685](https://arxiv.org/abs/2511.04685)):
  - **Fase 1 — Master scheduling (MILP):** decide qué staff (cirujanos, enfermeras) trabaja qué turnos. Variables binarias de asignación staff×turno.
  - **Fase 2 — Task assignment (CP):** asigna pacientes a (día, sala, OT) respetando precedencias y compatibilidades. CP solver porque las restricciones tipo "alldifferent" se expresan mejor que en MILP puro.
  - **Fase 3 — Roster refinement (Simulated Annealing):** afina la solución completa con LS metaheurística. Operadores SA: `single-shift moves`, `block moves`, `task reallocation`, `swap`. Acceptance Boltzmann clásica con T schedule geométrico.
  - **Resultado:** gap medio **2-5 %** vs best-known en i01–i30.
  - **Lección:** descomposición jerárquica — los problemas de scheduling con esta estructura responden bien a **resolver primero la asignación de recursos a alto nivel (staff×turnos)** y luego atornillar el detalle. Nuestro solver hace exactamente lo contrario: decide pacientes individuales primero y repara enfermeras después.
- **Equipo SDU-IMADA (Best Open-Source Software Prize):** Othman & Chiarandini. Metaheurística LS-based en C++/Python con interfaz [ROAR-NET API](https://roar-net.eu). Múltiples operadores LS guiados por una metaheurística adaptativa. Es la referencia open-source más cercana a nuestro estilo.
- **Distribución de costes:** los componentes que más distinguen finalistas vs solvers naive son `surgeon_overtime`, `open_ot`, `nurse_skill`, `continuity_of_care`. Son los que requieren razonamiento cluster (no movimientos individuales).

### 2.2 ER-ACO (Eclipse Randomness ACO)

Fuente: [MDPI Algorithms 19/2/102](https://www.mdpi.com/1999-4893/19/2/102). Aplicado a EMS routing y hospital resource scheduling.

**Idea central:** introducir un factor de aleatoriedad en la regla de transición que **decae exponencialmente** con la iteración. Exploración fuerte al principio, explotación al final.

Fórmula conceptual (la forma exacta varía por implementación):

```
q0(t) = q0_min + (q0_max − q0_min) × e^(−λ · t / T)
```

donde `t` es la iteración actual, `T` el horizonte total, `λ` el ritmo de decaimiento (típicamente `λ = 3-5` para decaer al 10 % a mitad de ejecución).

**Ventajas observadas en el paper original:**
- Convergencia más rápida en problemas con mucha varianza inter-instancia (como IHTC).
- Diversidad asegurada al inicio aun con `q0_max` cercano a 1.

**Aplicabilidad directa a nuestro código:** trivial. Solo cambiar el campo `q0` de `ACOParams` por una función `q0(iteration, total_iterations)` y referenciarla en `SelectByScore`. Coste: ~5 líneas.

### 2.3 ALNS para Patient Admission Scheduling

Referencia clásica: **Lusby, Schwierz, Range, Larsen (2016)**, "An Adaptive Large Neighbourhood Search Procedure Applied to the Dynamic Patient Admission Scheduling Problem" ([SSRN 2749619](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=2749619), [PubMed 27964800](https://pubmed.ncbi.nlm.nih.gov/27964800/)).

**Esquema ALNS estándar:**

1. **Estado actual** `s`. Best `s*`.
2. Cada iteración:
   - Selecciona `destroy` y `repair` por roulette ponderado por su historial de éxito.
   - `s' = repair(destroy(s))`.
   - Acceptance criterion (típicamente SA con T descendente).
   - Actualiza pesos: `w_destroy += score(s', s)`, `w_repair += score(s', s)`.
3. Después de N iteraciones, normaliza pesos.

**Operadores típicos en patient-admission:**

| Destroy | Qué hace | Cuándo es eficaz |
|---|---|---|
| `RandomRemoval(k)` | quita k pacientes aleatorios | exploración general |
| `WorstRemoval(k)` | quita los k que más contribuyen al coste | intensifica donde duele más |
| `ShawRemoval(k, r)` | quita un paciente seed y los k más "similares" (mismo cirujano, días cercanos, sala compatible) | rompe estructuras correlacionadas malas |
| `SurgeonCluster(s)` | quita todos los pacientes del cirujano s | ataca surgeon_overtime y surgeon_transfer |
| `DayRemoval(d)` | quita todos los pacientes admitidos el día d | ataca open_ot, ot_overtime de ese día |
| `RoomRemoval(r, [d_lo, d_hi])` | quita pacientes en sala r durante un rango | ataca room_gender_mix, room_mixed_age |

| Repair | Qué hace |
|---|---|
| `GreedyInsertion` | inserta cada paciente en la posición de menor coste delta |
| `RegretKInsertion` | para cada paciente calcula `regret = cost_2nd_best − cost_1st_best`; inserta el de mayor regret primero |
| `ACOGuidedInsertion` | inserta usando la τ aprendida para sesgar la elección (es la conexión natural con nuestro solver) |

**Ventajas frente a VNS+ILS clásica:**
- ALNS reorganiza varios pacientes simultáneamente — captura interdependencias.
- El feedback adaptativo aprende qué combinación destroy/repair funciona mejor en cada instancia.
- SA acceptance permite escapar de óptimos locales sin restart total.

### 2.4 Multi-neighborhood Simulated Annealing

Fuente: [Springer FSMJ 10.1007/s10696-025-09591-z](https://link.springer.com/article/10.1007/s10696-025-09591-z) (2025). "Multi-neighborhood simulated annealing for the integrated patient-to-room and nurse-to-patient assignment problem".

El problema es casi idéntico al IHTC core (sin OTs). Usan **4 vecindarios** (move-patient, swap-patients, move-nurse, swap-nurses) bajo un único framework SA con `cooling_rate ≈ 0.999`. Reportan convergencia y resultados competitivos.

**Observación útil:** este paper trata las decisiones de paciente y enfermera **dentro del mismo bucle SA**, en lugar de greedy post-hoc como nosotros. Esto es la inspiración para Etapa 5 (nurse-aware).

### 2.5 ACO híbridos en hospital scheduling

- **Heliyon 2024** ([cell.com S2405-8440(24)16165-8](https://www.cell.com/heliyon/fulltext/S2405-8440(24)16165-8)). "Improved Ant Colony Optimization Algorithm of Hybrid Strategies for Patient Management". Estrategias añadidas: candidate lists, élite ant set, sub-problem decomposition, mutation operator. Reporta ~10-20 % de mejora sobre ACO básico.
- **Decerle & Grunder, HAL 2021** ([hal.science/hal-03486295v1](https://hal.science/hal-03486295v1)). "Hybrid memetic-ACO for home health care with time windows". Combina ACO (construcción) + GA (cruce entre soluciones élite) + LS (refinamiento). Aplicable a problemas con synchronization constraints — análogo a HC12 (cirujano).
- **Liu et al. 2015** ([ScienceDirect S036083521500159X](https://www.sciencedirect.com/science/article/abs/pii/S036083521500159X)). "ACO for operating room surgery scheduling". Modela el OR scheduling como flexible job-shop. **Sí incluye `τ_ot`**, contradiciendo nuestra decisión de tratar el OT greedy.

**Conclusión §2.5:** la mejora real en ACO viene de **hibridación con LS** (memético), **enriquecimiento de la heurística** y **descomposición**. El ACO puro es un baseline, no un solver competitivo en problemas de esta complejidad.

### 2.6 Síntesis de la literatura

| Hallazgo | Implicación para nuestro solver |
|---|---|
| Top-3 IHTC2024 usa descomposición + SA, no ACO | El gap +52 % es consistente con un ACO sin hibridación profunda. Incorporar SA-acceptance + ALNS destroy/repair es la palanca con mayor relación impacto/coste. |
| ALNS es el paradigma estándar en patient-admission | Reemplazar ILS por ALNS es la mejora arquitectónica más prometedora. |
| ER-ACO mejora la convergencia con schedule de q0 | Quick win: aplicar el schedule sin tocar el resto del ACO. |
| Multi-neighborhood SA integra paciente+enfermera en un solo loop | Justifica hacer la enfermera una decisión modelada (Etapa 5), no greedy post-hoc. |
| ACO híbridos reportan +10-20 % con candidate lists, élite, mutation | Etapa 4 es razonable; no se espera milagro de cada técnica suelta. |
| Liu et al. usan τ_ot en surgery scheduling | Etapa 5 opcional: incluir τ_ot si `m-instances` lo necesitan. |

---

## 3. Análisis del gap +52 % por componente de coste

Mapeo de cada uno de los 12 componentes blandos contra qué decisión/operador del solver actual está fallando. Inferido a partir de las figuras agregadas de [graficas_v2/fig5_componentes_agregados.png](graficas_v2/fig5_componentes_agregados.png) y [graficas_v2/comparison_summary.csv](graficas_v2/comparison_summary.csv).

| Componente | Cubierto hoy por | Limitación | Etapa que lo ataca |
|---|---|---|---|
| `patient_delay` | η_day informada + warm-start | OK; gap pequeño | — |
| `unscheduled_optional` | ToggleOpt + warm-start | ToggleOpt con 1 intento → falsos negativos | Etapa 1 (ToggleOpt completa) |
| `room_capacity` | ChangeRoom + SwapRooms con límites | Solo 12 % de pacientes / 0.16 % de pares explorados | Etapa 1 (límites escalables) |
| `room_gender_mix` | Heurística Fase A construye bien; ChangeRoom corrige | Operador faltante: DayBlock | Etapa 2 |
| `room_mixed_age` | Igual que gender_mix | Igual | Etapa 2 |
| `open_ot` | ChangeOT + greedy en construcción | OTShifting cluster ausente; concentrar 3+ pacientes en mismo OT no se explora | Etapa 2 (OTShifting) + Etapa 3 (ALNS DayRemoval) |
| `surgeon_overtime` | ChangeDay + cascada de fallback | SurgeonConsolidation ausente; movimiento individual no capta interdependencia | Etapa 2 (SurgeonConsolidation) + Etapa 3 (SurgeonCluster removal) |
| `ot_overtime` | ChangeOT + ChangeDay | Igual que open_ot | Etapa 2 + Etapa 3 |
| `nurse_skill` | ChangeNurse + EnsureFullNurseCoverage | Reasignación post-hoc; el aprendizaje ACO no la considera | Etapa 5 (τ_nurse) |
| `nurse_excessive_workload` | ChangeNurse | NurseSwap entre turnos ausente; ChangeNurse cubre solo 6 % | Etapa 1 (límite) + Etapa 2 (NurseSwap) |
| `continuity_of_care` | EnsureFullNurseCoverage bonifica continuidad +50 al construir | No hay operador VNS que reorganice continuidad explícitamente | Etapa 5 |
| `surgeon_transfer` | (ningún operador específico) | Componente fantasma — no atacado | Etapa 3 (SurgeonCluster) + Etapa 4 (η dinámica) |

**Top 3 componentes que más coste suman en el gap (estimado):**
1. `surgeon_overtime` — los movimientos cluster de cirujano son grandes en valor.
2. `open_ot` — abrir OTs de más es caro (peso típico 30).
3. `nurse_excessive_workload` y `nurse_skill` juntos — la enfermera fuera del ACO deja mucho coste sobre la mesa.

Una intervención focalizada en los componentes 1 y 2 (Etapas 2-3) debería capturar la mayor parte del gap. La componente 3 requiere refactor mayor (Etapa 5).

---

## 4. Hoja de ruta — ACO + ALNS híbrido en 5 etapas

Cada etapa es un sprint independiente con benchmark intermedio. Las etapas son **acumulativas**: cada una construye sobre la anterior sin romperla.

### Etapa 1 — Quick wins (1-2 días) — **EJECUTADA Y REVERTIDA (2026-05-08)**

**Resultado empírico:** los cuatro cambios propuestos producen **regresión sistemática** del 2-5 % a iso-tiempo (600 s, mini-benchmark i04/i17/i26). Detalles en [history/ACO.md §15](history/ACO.md). La hipótesis "subir los límites cubre más vecindario y mejora" no resiste la evidencia: cada iteración VNS más profunda gasta más tiempo por aceptación, reduciendo el número de batches del bucle externo ACO y por tanto las actualizaciones de feromona. La perturbación más agresiva destruye más estructura que la VNS recupera en su tiempo asignado. ER-ACO con q0_min=0.5 daña la primera generación de hormigas (el warm-start ya es muy bueno y la feromona aprende solo de él). Cambios revertidos; el solver vuelve al estado v2.

**Lección para la hoja de ruta:** los cuellos de botella detectados en §1 son reales pero **no se atacan subiendo cuotas a iso-tiempo**. Hace falta ampliar cualitativamente el vecindario (operadores cluster — Etapa 2) o sustituir la perturbación ciega por una informada (ALNS — Etapa 3). Esas son las palancas reales.

**Objetivo original:** explotar el código actual al máximo sin tocar arquitectura.

**Cambios:**
1. **ER-ACO schedule** ([src/solver/ACOSolver.cpp](src/solver/ACOSolver.cpp) `SelectByScore` y `Run`): pasar `q0` de constante a función `q0(iter, total_iter)` con decay exponencial. `q0_min=0.5, q0_max=0.95, λ=4`.
2. **Límites VNS escalables** ([src/solver/LocalSearch.cpp](src/solver/LocalSearch.cpp)):
   - `min(60, |scheduled|)` → `min(max(60, ceil(√P × 5)), |scheduled|)`. Para P=500 da 112; para P=30 sigue dando 30. Aplicar a ChangeRoom/Day/OT/Nurse y Relocate.
   - `kMaxPairs=200` → `min(max(200, P × 2), C(P,2))`. Para P=500 da 1000 pares.
   - `kMaxCombos=30` (en Relocate) → `min(60, D × R)`.
3. **Perturbación adaptativa** (`Perturb`): `strength = max(4, round(√P × 0.3))`. Para P=30 sigue siendo 4; para P=500 sube a 7.
4. **ToggleOpt completa**: para cada opcional, en lugar de un único `(d, r)` aleatorio, probar todos los `(d, r)` factibles de su ventana hasta encontrar mejora estricta. Mantener tope `min(P_opt × 5, 500)` evaluaciones por llamada.

**Esfuerzo:** ~150 líneas de cambios, tests en mini-benchmark.

**Ganancia esperada:** -8 a -12 puntos de gap (52 % → **40-44 %**).

**Riesgos:** los límites mayores aumentan tiempo por iteración LS — con 600 s y 12 hormigas paralelas no debe haber regresión, pero monitorizar `time_per_ant`.

### Etapa 2 — Operadores VNS faltantes (2-3 días) — **ABANDONADA (2026-05-08)**

**Hallazgo crítico tras implementar `TrySurgeonConsolidation`:** los operadores cluster bajo `first-improvement` de coste total **no producen mejoras** porque las mejoras agregadas que requieren pasar por estados intermedios peores son rechazadas por la VNS. Diagnóstico empírico: `SurgeonCons` registró **0 mejoras** en 300 s sobre i17, mientras introducía regresión sistemática (+2-5 %) por gastar tiempo y robar slot del shuffle. Detalles en [history/ACO.md §16](history/ACO.md).

Los demás operadores cluster propuestos (`TryOTShifting`, `TryDayBlock`, `TryNurseSwap`) sufren la misma limitación. **Etapa 2 entera se considera invalidada**: no es un problema de qué operadores, sino del *acceptance criterion*. La VNS first-improvement rechaza la única vía por la que estos operadores aportarían (movimientos cluster con saldo agregado positivo pero componentes individuales negativos).

**Implicación:** los operadores cluster deben implementarse **dentro del módulo ALNS** (Etapa 3), donde la *acceptance* es Simulated Annealing y permite aceptar `Δcost > 0` con probabilidad. Las "Etapas 2.b–2.d" originales quedan **subsumidas como destroy operators de Etapa 3**.

**Objetivo original:** abrir vecindarios cluster que hoy son invisibles.

**Cambios en [src/solver/LocalSearch.cpp](src/solver/LocalSearch.cpp):**

1. **`TrySurgeonConsolidation(Solution&, int& cost, std::mt19937&)`** (operador #8):
   - Para cada cirujano `s`, recoger sus pacientes programados.
   - Si `surgeon_overtime[s]` > 0 en algún día: intentar mover hasta 3 pacientes de los días sobrecargados a un día con margen.
   - First-improvement.
2. **`TryOTShifting`** (operador #9):
   - Para cada par (OT_a, OT_b, día d) con OT_a sobrecargado: mover hasta 3 pacientes de OT_a a OT_b en el mismo día.
3. **`TryNurseSwap`** (operador #10):
   - Para cada (sala r, día d, turno s1, turno s2): si las dos enfermeras n1, n2 son distintas e intercambiarlas reduce `nurse_excessive_workload`, hacerlo.
4. **`TryDayBlock`** (operador #11):
   - Para cada (sala r, día d): si los pacientes de esa sala-día caben en otra sala libre que mejora gender/age, mover el bloque.
5. **Actualizar `kOperatorNames`** y el bitmask `enabled_mask` a 11 operadores.

**Riesgo de regresión en factibilidad:** cada nuevo operador termina con `EnsureFullNurseCoverage` y verificación de `IsFeasiblePatientAssignment` por movimiento. Tests en i08 y i20 (las más restringidas).

**Ganancia esperada adicional:** -5 a -8 puntos (40-44 % → **32-39 %**).

### Etapa 3 — Módulo ALNS sustituye al ILS (4-5 días, absorbe Etapa 2) — **EJECUTADA (2026-05-09)**

**Resultado empírico — benchmark completo i01–i30, 600 s, 30/30 factibles:**

| Métrica | v2 baseline | v3 ALNS+SA | Mejora |
|---|---:|---:|---:|
| Coste agregado | 1 080 218 | 1 069 872 | **−0.96 %** |
| Gap medio vs best | 52.5 % | **52.0 %** | −0.53 pp |
| Mediana gap | 49.7 % | 49.4 % | −0.3 pp |
| ACO mejor que Random | 27/30 | 25/30 | −2 |
| v3 mejor que v2 | — | 20/30 | — |

ALNS+SA aporta **−0.5 pp de gap medio**, frente a la proyección de −10 a −15 pp del plan. Top mejoras en grandes (i27 −5.4 %, i05 −3.2 %, i03 −2.6 %); empeoramientos en pequeñas/saturadas (i04 +4.8 %, i08 +2.97 %). Detalles en [history/ACO.md §17](history/ACO.md).

**Lectura:** la hipótesis "SA-acceptance es la palanca clave" se valida en sentido cualitativo (el SA escapa de óptimos locales y ayuda en grandes) pero la magnitud está muy por debajo de lo esperado. Hipótesis del por qué (a profundizar en Etapa 3b/4):

1. **GreedyRepair conservador**: re-asigna sin sesgar por feromona ni `Δcost` mínimo.
2. **SA mal calibrado**: cooling 0.998 sobre 30 Apply → T apenas decae → tasa de aceptación demasiado alta, búsqueda desorientada.
3. **k = √P × 0.5 muy bajo**: P=500 → k=11 (2.4 %). Literatura ALNS usa 10-30 %.
4. **Sin adaptive weights**: los 3 destroys (Random/Surgeon/Day) se eligen uniformemente sin priorizar los útiles.
5. **τ marginal y η pobre**: el cuello de botella mayor probablemente está en la construcción ACO (Etapas 4-5), no en la perturbación.

**Objetivo original:** reemplazar la perturbación ciega del ILS por un módulo destroy/repair adaptativo con **Simulated Annealing acceptance**. **Es el cambio arquitectónico mayor** y la mayor palanca esperada. El SA es lo que desbloquea los operadores cluster (que en Etapa 2 fallaron porque la VNS rechazaba degradaciones).

**Cambios:**

1. **Nuevo módulo** [src/solver/ALNSPerturbation.h](src/solver/ALNSPerturbation.h) y [.cpp](src/solver/ALNSPerturbation.cpp):
   - Clase `ALNSPerturbation` con estado `weights_destroy[6]`, `weights_repair[3]`, contadores de éxito.
   - Método `Apply(Solution&, ProblemData&, std::mt19937&, double T_current)` que ejecuta una iteración destroy+repair+SA-accept.
2. **Destroy operators implementados:**
   - `RandomRemoval(k)` con `k = round(√P × 0.5)`.
   - `WorstRemoval(k)`: ranquear pacientes por su contribución al coste blando.
   - `ShawRemoval(k)`: dado un seed, calcular distancia `d(p, q) = w1·|day_p−day_q| + w2·1[surgeon_p≠surgeon_q] + w3·1[room_p≠room_q]` y quitar los k más cercanos.
   - `SurgeonCluster(s)`: quitar todos los pacientes del cirujano `s` (elegido por probabilidad ∝ overtime).
   - `DayRemoval(d)`: quitar todos los del día `d` (elegido por probabilidad ∝ ot_load + open_ot).
   - `RoomRemoval(r, [d_lo, d_hi])`: quitar pacientes en sala r durante un rango.
3. **Repair operators:**
   - `GreedyInsertion`: por cada paciente removido, encontrar el `(d, r, ot)` factible con menor `Δcost`.
   - `RegretKInsertion(k=3)`: ordenar pacientes removidos por regret descendente y greedy-insert.
   - `ACOGuidedInsertion`: usar `τ_day · η_day` y `τ_room · η_room` para sesgar la elección — la conexión natural con el ACO.
4. **Adaptive weights:**
   - `score = ψ_1 × hits + ψ_2 × accepts + ψ_3 × improvements` con ψ = (1, 4, 9).
   - Roulette por pesos normalizados; reset cada N=50 iteraciones.
5. **SA acceptance:**
   - `T_0 = 0.05 × C_initial`, `cooling_rate = 0.998`.
   - Aceptar si `Δcost ≤ 0` o con prob `exp(−Δcost / T)`.
6. **Integración en `LocalSearch::Run`:**
   - Cuando ningún operador VNS mejora (óptimo local) → `ALNSPerturbation::Apply(...)` en lugar de `Perturb(strength=4)`.
   - Tras `Apply` → re-evaluar coste y volver al loop VNS.
   - Mantener el contador de `kMaxPerturbations` pero subirlo a 30 (las iteraciones ALNS son más informadas).
7. **Flag de activación:** parámetro CLI nuevo `--use-alns` (o argumento posicional 8) para poder comparar lado a lado con la versión solo VNS+ILS.

**Esfuerzo:** ~600 líneas (módulo + integración + tests).

**Ganancia esperada adicional:** -10 a -15 puntos (32-39 % → **22-29 %**). **Es el sprint con mayor impacto.**

**Riesgos:**
- Compatibilidad con multi-threading: el ALNS se ejecuta dentro de cada hormiga, en su propio thread, con su propio RNG y solución local — no debe haber estado global compartido.
- Coste por iteración: una iteración ALNS es más cara que una perturbación simple; ajustar `kMaxPerturbations` y `time_budget`.
- Aceptación SA: con T mal calibrado puede degradar más de lo que mejora. Tests en mini-benchmark obligatorios.

### Etapa 4 — ACO informado (2-3 días)

**Objetivo:** elevar la calidad de las hormigas individuales antes de que entren en LS+ALNS.

**Cambios en [src/solver/ACOSolver.cpp](src/solver/ACOSolver.cpp):**

1. **Heurística η dinámica:** recalcular `η_day` y `η_room` al inicio de **cada hormiga**, no una vez antes del bucle.
   - `η_day[p][d] *= 1 / (1 + surgeon_load[surgeon_p][d] / max_load)`
   - `η_day[p][d] *= 1 / (1 + ot_load_estimate[d])`
   - `η_room[p][r] = (compatible) × (1 / (1 + gender_pen + age_pen))`
   - Donde las cargas se toman de la solución parcial que la hormiga ha construido hasta el momento (incrementalmente).
2. **Candidate lists top-5:** precalcular para cada paciente sus 5 mejores `(d, r)` por `τ × η` y restringir `SelectByScore` a esa lista.
3. **q0 heterogéneo entre hormigas paralelas:** `q0_k = q0_base + Gaussian(0, 0.05)`. Decorrelaciona soluciones del batch.
4. **Update soft-reset:** contador `global_age`. Si `global_age > 5` → forzar 2-3 iteraciones con `iteration_best` antes de volver a `global_best`. Reset por `stagnation_k=15` se mantiene como red de seguridad.

**Esfuerzo:** ~250 líneas.

**Ganancia esperada adicional:** -3 a -6 puntos (22-29 % → **18-24 %**).

### Etapa 5 — τ 3D y nurse-aware (3-5 días, opcional)

**Objetivo:** poner el solver al nivel de finalistas top.

**Cambios:**

1. **Refactor τ a tensor 3D:** `τ[p][d][r]` aplanado. Para `P=500, D=21, R=10` son ~100 K doubles → 800 KB. Para `P=2000, D=42, R=20` (m-instances) ~17 M doubles → 130 MB. **Requiere medir RAM**; si excede, usar dict ralo o seguir factorizado pero con τ_pair = τ_day + τ_room + τ_dr donde τ_dr captura solo correlaciones (matriz pequeña).
2. **Selección atómica `(d, r)`:** un único `SelectByScore` sobre el cartesiano `D × R` factible por paciente. Coste evita la cascada actual.
3. **τ_nurse[r][n][d][s]:** matriz pesada pero factorizable; 4ª dimensión de aprendizaje. Cambia el orden de construcción a:
   - Asignar pacientes (como ahora).
   - Para cada (r, d, s) poblada, `SelectByScore(τ_nurse[r], scores)` para elegir enfermera.
   - Update τ_nurse con mejores soluciones.
4. **τ_ot opcional:** introducir `τ_ot[p][ot]` solo si el benchmark m-instances lo justifica.

**Esfuerzo:** ~800-1000 líneas (refactor profundo).

**Ganancia esperada adicional:** -3 a -8 puntos (18-24 % → **12-18 %**).

**Riesgos:** RAM en m-instances; coherencia de cotas MMAS sobre tensor 3D (cotas marginales vs cotas conjuntas); aprendizaje más lento por dispersión de la feromona.

### Resumen de proyección (actualizado tras Etapas 1, 2, 3)

| Etapa | Esfuerzo | Gap esperado | Gap real | Estado |
|---|---|---|---|---|
| Inicio (post v2) | — | +52.5 % | +52.5 % | base |
| ~~Etapa 1 (quick wins)~~ | 1-2 días | ~~40-44 %~~ | revertida | regresión a iso-tiempo |
| ~~Etapa 2 (op cluster en VNS)~~ | 2-3 días | ~~38-46 %~~ | abandonada | first-improvement rechaza degradaciones |
| Etapa 3 (ALNS+SA, MVP+3 destroys) | 4-5 días | 22-30 % | **52.0 %** | **ejecutada — mejora marginal (−0.5 pp)** |
| **Etapa 3b (recalibración ALNS)** | 1-2 días | 47-50 % | — | **siguiente — barata** |
| Etapa 4 (ACO informado: η dinámica, candidate lists, q0 heterogéneo) | 2-3 días | 42-46 % | — | crítica |
| Etapa 5 (τ 3D + nurse-aware) | 3-5 días | 35-42 % | — | opcional |

**Lección consolidada de Etapas 1, 2, 3:** las mejoras puramente locales (cuotas, operadores nuevos, acceptance) tienen un techo bajo en este problema. El gap a `best-solutions/` está dominado por la **calidad de la solución generada por la construcción ACO**, no por la perturbación. La heurística η solo modela `patient_delay`, la τ es marginal `(p,d) + (p,r)` y la enfermera está fuera del aprendizaje. Estos tres factores limitan la cuenca de atracción de la VNS — y ningún módulo de perturbación va a sacar a la solución de la cuenca incorrecta.

**Reajuste de proyecciones:** las proyecciones originales de Etapas 4-5 (gap esperado tras Etapa 5: 12-18 %) eran optimistas. Con la evidencia de Etapas 1-3, una proyección más realista es **35-45 %** tras Etapa 5 — sigue siendo una mejora sustancial pero lejos del 2-5 % de finalistas top. Cerrar más gap requeriría refactor mayor de la codificación (e.g., descomposición tipo Twente con MILP/CP, fuera del alcance del TFG).

Una entrega "academicamente respetable" se logra tras Etapa 3 (sub-30 % de gap). Una entrega "competitiva con finalistas top" requiere todas las etapas (sub-20 %).

---

## 5. Criterios de éxito y plan de validación

### 5.1 Por etapa

Tras cada etapa:

1. **Mini-benchmark** (300 s, semilla 42): i04 (pequeña, ACO actualmente pierde), i08 (caso patológico), i17 (mediana, ACO gana), i26 (grande, ACO gana), m06 (m-instance pequeña).
2. **Validador oficial:** las 5 instancias deben dar 0 violaciones.
3. **Métricas a reportar:**
   - Coste interno (Evaluator) y oficial (validador).
   - Tiempo wall-clock.
   - Gap a `best-solutions/` por instancia.
   - Diff vs etapa anterior (si mejora ≥ -2 % por instancia o ≥ -1 % agregado, etapa OK).

### 5.2 Tras Etapa 3 (ALNS)

Benchmark **completo i01-i30** a 600 s, secuencial con 4 hilos por solver. Regenerar gráficas en [graficas_v3/](graficas_v3/). Documentar resultados en `history/ACO.md` §15.

### 5.3 Tras Etapa 5

Benchmark completo **i01-i30 + m01-m30** a 600 s. Regenerar gráficas en [graficas_v4/](graficas_v4/). Documentar §16. Es la versión candidata a publicación / TFG final.

### 5.4 Métricas primarias y secundarias

| Métrica | Cómo medirla | Objetivo etapa 3 | Objetivo etapa 5 |
|---|---|---|---|
| **Gap medio vs best** | media de `(cost_aco − cost_best) / cost_best × 100` | ≤ 30 % | ≤ 18 % |
| **Mediana del gap** | mediana de la misma serie | ≤ 25 % | ≤ 15 % |
| **Instancias con gap < 30 %** | conteo | ≥ 18/30 | ≥ 28/30 |
| **Instancias con gap < 10 %** | conteo | ≥ 3/30 | ≥ 12/30 |
| **Soluciones factibles (validador)** | conteo | 30/30 | 60/60 (i+m) |
| **i08 patológica** | gap individual | ≤ 80 % | ≤ 30 % |

---

## 6. Riesgos y mitigaciones

| Riesgo | Probabilidad | Impacto | Mitigación |
|---|---|---|---|
| Etapa 3 ALNS introduce regresión por SA mal calibrado | Media | Alto | Empezar con T_0 conservador y `cooling_rate=0.998`; mini-benchmark obligatorio; flag CLI para A/B con ILS clásico |
| Compartir τ entre ACO e inserción ALNS crea ciclos de feedback raros | Baja | Medio | Separar fases: ACO actualiza τ solo en `UpdatePheromones`; ALNS solo lee, no escribe |
| Operadores cluster (Etapa 2) crean infactibilidades silenciosas | Media | Alto | Cada accept termina con `EnsureFullNurseCoverage` + `FeasibilityChecker::Check`; tests en i08 e i20 (las más restringidas) |
| RAM en Etapa 5 con m-instances | Media | Medio | Medir antes de comprometerse; alternativa factorizada `τ_day + τ_room + τ_dr` |
| Sobre-ingeniería del ALNS (demasiados destroy/repair) | Media | Bajo | Empezar con 3 destroy + 2 repair; añadir más solo si aportan en ablation |
| Regresión de tiempo: cada etapa añade overhead computacional | Alta | Medio | Monitorizar `time_per_ant` y `iteraciones VNS / segundo`; subir `n_ants` solo si el batch sigue siendo barato |
| El benchmark completo se vuelve demasiado caro (etapa 5 + m-instances = horas) | Alta | Bajo | Programar como background tasks con notification; documentar en `history/` |

---

## 7. Referencias

### Sitio oficial y problema
- [IHTC 2024 — sitio oficial](https://ihtc2024.github.io/) — descripción del problema, instancias, validador.
- [PATAT 2024 paper 52](https://patatconference.org/patat2024/proceedings/papers/52.pdf) — "The Integrated Healthcare Timetabling Competition 2024", Ceschia et al. Especificación oficial del problema.
- [The Integrated Healthcare Timetabling Competition 2024 — ScienceDirect](https://www.sciencedirect.com/science/article/pii/S3050784725000157) — overview en journal especial.
- [PATAT KU Leuven news](https://patat.cs.kuleuven.be/news-post/integrated-healthcare-timetabling-competition-2024) — anuncios de la organización.

### Top finalistas IHTC 2024
- **Equipo Twente (3.º), arXiv:2511.04685** — [A hybrid solution approach for the IHTC 2024](https://arxiv.org/abs/2511.04685). Descomposición MILP+CP+SA en 3 fases. Gap medio 2-5 %.
- **SDU-IMADA (Best OSS Prize)** — [Anuncio ROAR-NET](https://roar-net.eu/news/ihtc-2024-best-oss-prize/). Solver C++/Python LS-based con interfaz ROAR-NET API.

### ACO híbridos y variantes
- **ER-ACO** — [Real-Time Ant Colony Optimization Framework, MDPI Algorithms 19/2/102 (2026)](https://www.mdpi.com/1999-4893/19/2/102). Eclipse Randomness con decay exponencial.
- **Improved ACO con estrategias híbridas** — [Heliyon 2024, S2405-8440(24)16165-8](https://www.cell.com/heliyon/fulltext/S2405-8440(24)16165-8). Candidate lists, élite, sub-problemas.
- **Memetic-ACO para home health care** — [Decerle & Grunder, HAL 2021](https://hal.science/hal-03486295v1).
- **ACO para OR scheduling** — [Liu et al. 2015, ScienceDirect S036083521500159X](https://www.sciencedirect.com/science/article/abs/pii/S036083521500159X). Incluye τ_ot.
- **ACO para nurse scheduling regional** — [An ACO algorithm for a dynamic regional nurse-scheduling problem in Austria, Academia.edu](https://www.academia.edu/31625035/).

### ALNS y multi-neighborhood
- **Lusby et al. 2016** — [An Adaptive Large Neighbourhood Search Procedure Applied to the Dynamic Patient Admission Scheduling Problem, SSRN 2749619](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=2749619) y [PubMed 27964800](https://pubmed.ncbi.nlm.nih.gov/27964800/). Referencia clásica del paradigma ALNS aplicado a PAS.
- **Multi-neighborhood SA** — [Springer FSMJ 10.1007/s10696-025-09591-z (2025)](https://link.springer.com/article/10.1007/s10696-025-09591-z). Patient-to-room + nurse-to-patient.
- **ALNS overview** — [ScienceDirect — Adaptive Large Neighborhood Search overview](https://www.sciencedirect.com/topics/computer-science/adaptive-large-neighborhood-search). Operadores destroy/repair canónicos.

### Surveys y reviews
- [Healthcare scheduling in optimization context: a review, PMC8035616](https://pmc.ncbi.nlm.nih.gov/articles/PMC8035616/).
- [Role of metaheuristic algorithms in healthcare, OUP JCDE 2024](https://academic.oup.com/jcde/article/11/3/223/7682402).

### Implementación local del solver actual
- [src/solver/ACOSolver.cpp](src/solver/ACOSolver.cpp) — ACO+VNS+ILS con Fases A+B+C+D.
- [src/solver/LocalSearch.cpp](src/solver/LocalSearch.cpp) — VNS de 8 vecindarios + ILS.
- [history/ACO.md](history/ACO.md) §13-§14 — propuestas previas y resultados de v1/v2.
- [tables/aco-random-comparison-v2.csv](tables/aco-random-comparison-v2.csv) — datos del benchmark post-mejoras.
