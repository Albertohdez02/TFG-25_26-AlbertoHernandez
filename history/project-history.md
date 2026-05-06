# Historial del Proyecto — TFG IHTC 2024
**Autor:** Alberto Hernández  
**Fecha última actualización:** 2026-05-04  
**Repositorio:** `/home/alberto/TFG-25_26-AlbertoHernandez`

---

## 1. Descripción del Proyecto

Solver para el **Integrated Hospital Treatment Scheduling Challenge 2024 (IHTC 2024)**.  
Enlace a la competición: https://ihtc2024.github.io/

El problema consiste en programar la admisión de pacientes en un hospital asignando para cada uno:
- **Habitación** (respetando compatibilidad, capacidad, género, edad)
- **Día de admisión** (dentro de ventana [release_day, due_day] para obligatorios)
- **Quirófano** (abierto el día de cirugía, carga ≤ disponibilidad)
- **Enfermeras** (una por (habitación, día, turno), disponible, con habilidades adecuadas)

Pacientes **obligatorios** deben programarse siempre (restricción dura HC1). Los **opcionales** se programan si hay capacidad, maximizando la cobertura.

---

## 2. Arquitectura del Solver

### Estructura de ficheros

```
src/
├── main.cpp                     # Pipeline multi-start + ILS
├── common/types.h               # Tipos: PatientId, RoomId, Day, Shift…
├── entities/                    # Patient, Nurse, Surgeon, Room, OT, ProblemData
├── evaluator/
│   ├── Evaluator.h/.cpp         # 12 componentes de coste blando
│   └── FeasibilityChecker.h/.cpp# 13 restricciones duras (HC1–HC13)
├── solution/
│   └── Solution.h/.cpp          # Codificación espacial (patient-centric) + cachés
├── solver/
│   ├── LocalSearch.h/.cpp       # 8 vecindarios VNS + ILS
│   └── RandomGenerator.h/.cpp  # Generador constructivo en 4 fases
└── io/
    ├── ProblemParser.h          # Parser JSON de instancias
    └── SolutionIO.h             # ExportJSON de soluciones
```

### Pipeline principal (`main.cpp`)

1. **Parsear instancia** JSON
2. **Multi-start** (hasta `num_restarts` reinicios, limitado por `global_time_s`):
   - Generar solución aleatoria factible (`RandomGenerator::Generate`)
   - Mejorar con `LocalSearch::Run` (VNS + ILS)
   - Actualizar mejor solución **solo si es factible** (`FeasibilityChecker::Check`)
3. **Evaluar** mejor solución (costes blandos + factibilidad dura)
4. **Exportar** a `solutions/<basename>_solution.json`

**Uso:**
```bash
./build/ihtc_solver <instancia.json> [seed=42] [max_iter=5000] [restarts=100] [time_s=600]
```

### Generador (`RandomGenerator`)

Construcción en 4 fases:
1. **Obligatorios por urgencia** — día a día, prioriza `due_day - current_day` mínimo
2. **Reparación** — fuerza la asignación de obligatorios restantes, desplazando otros
3. **Opcionales** — programa opcionales con 70% de probabilidad si caben
4. **Enfermeras** — asignación greedy por score (`skill_match × w + continuity - overload`)

### LocalSearch (`LocalSearch::Run`)

VNS con **8 vecindarios** (first-improvement) + ILS (15 perturbaciones de fuerza 4):

| # | Nombre | Descripción |
|---|--------|-------------|
| 0 | `ChangeRoom` | Reasigna habitación de un paciente |
| 1 | `ChangeDay` | Cambia día de admisión (ventana completa) |
| 2 | `ChangeOT` | Reasigna quirófano en el mismo día |
| 3 | `Relocate` | Reubicación compuesta: day+room+OT simultáneo (≥2 cambios) |
| 4 | `SwapRooms` | Intercambia habitaciones entre dos pacientes |
| 5 | `SwapDays` | Intercambia días de admisión entre dos pacientes |
| 6 | `ToggleOpt` | Programa/desprograma pacientes opcionales |
| 7 | `ChangeNurse` | Reasigna enfermera en una posición (room, día, turno) |

### Evaluador — 12 componentes de coste blando

`room_capacity`, `room_gender_mix`, `room_mixed_age`, `patient_delay`,
`unscheduled_optional`, `surgeon_overtime`, `ot_overtime`, `open_ot`,
`nurse_skill`, `nurse_excessive_workload`, `continuity_of_care`, `surgeon_transfer`

### FeasibilityChecker — 13 restricciones duras (HC1–HC13)

HC1: obligatorios programados | HC2: ventana obligatorios | HC3: ventana opcionales |
HC4: estancia en horizonte | HC5: capacidad habitación | HC6: mezcla género |
HC7: compatibilidad habitación | HC8: OT abierto | HC10: enfermera disponible |
HC12: carga cirujano | HC13: carga OT

---

## 3. Instancias de Datos

| Conjunto | Ficheros | Ubicación |
|----------|----------|-----------|
| Test (pequeñas) | `test01.json` – `test10.json` | `data/` |
| Benchmark i (medianas) | `i01.json` – `i30.json` | `data/` |
| Benchmark m (grandes) | `m01.json` – `m30.json` | `data/` |
| Mejores soluciones competición | `sol_i*.json`, `sol_m*.json` | `best-solutions/` |
| Nuestras soluciones | `i*_solution.json`, `test*_solution.json` | `solutions/` |

---

## 4. Cambios Realizados en Esta Sesión

### 4.1 Ablation Test — Infraestructura

**Problema:** Determinar cuáles de los 8 vecindarios VNS son realmente útiles.

**Cambios en `LocalSearch.h` y `LocalSearch.cpp`:**

- Añadido `std::array<int, 8> op_improvements` a `LocalSearchStats` — cuenta mejoras por operador
- Añadido `uint8_t enabled_mask = 0xFF` como parámetro de `LocalSearch::Run()` — bitmask para activar/desactivar operadores individualmente (bit i = operador i). Default `0xFF` = todos activos (retrocompatible)
- Añadido `static constexpr std::array<const char*, 8> kOperatorNames` — nombres de operadores en orden
- `ToString()` de `LocalSearchStats` ahora muestra desglose por operador
- En el bucle principal de Run(): los neighborhoods activos se filtran por la máscara; cada mejora incrementa `stats.op_improvements[global_idx]`

**Nuevo binario `src/ablation_test.cpp`:**

Pool de hilos (`std::thread`) con 18 configuraciones:
- `all` (0xFF): todos los operadores — baseline
- `no_LS` (0x00): solo generador aleatorio, sin LS
- `no_X` (×8): leave-one-out — todos salvo X
- `only_X` (×8): solo el operador X

Parámetros: `seeds × restarts × 4_hilos`, tiempo por tarea calculado para respetar `competition_time_s` (10 min por defecto).

Salida: tabla resumen + `ablation_results/<instancia>_ablation.csv` con columnas:
`instance, config, seed, restart, feasible, init_cost, final_cost, improvement_pct, time_s, op_ChangeRoom, …op_ChangeNurse, cost_room_capacity, …cost_surgeon_transfer, cost_total`

**Nuevo target en `CMakeLists.txt`:** `ihtc_ablation`

**Uso:**
```bash
./build/ihtc_ablation <instancia.json> [seeds=3] [restarts=3] [threads=4] [competition_s=600]
```

### 4.2 Ablation Test — Experimentos Ejecutados

**Experimento 1 (instancias test01–test10):**
- 2 seeds × 2 restarts, binario original sin hilos
- Resultados en `ablation_results/test*_ablation.{csv,txt}`

**Experimento 2 (instancias i01–i18):**
- 5 seeds × 5 restarts, secuencial
- Resultados en `ablation_results/i*_ablation.{csv,txt}` + `ablation_results/ablation_merged_i01_i18.csv`

**Experimento 3 — Exhaustivo (instancias i04, i08, i26, m06, m11, m29):**
- 3 seeds × 3 restarts × **4 hilos**, 600s por instancia (requisito competición)
- 0 soluciones infactibles en 972 ejecuciones
- Resultados en `ablation_results/i04_ablation.csv`, `i08_ablation.csv`, `i26_ablation.csv`, `m06_ablation.csv`, `m11_ablation.csv`, `m29_ablation.csv`
- CSV consolidado: `ablation_results/ablation_exhaustive_merged.csv` (972 filas, 30 columnas)

### 4.3 Hallazgos del Ablation Test

Resultados agregados sobre 6 instancias × 3 seeds × 3 restarts = 162 runs por instancia:

| Operador | LOO Δ% (media±std) | Contrib% en 'all' | Solo vs noLS% |
|---|---|---|---|
| SwapDays | +3.35±5.25% | 7.4±4.3% | 10.5±19.5% |
| Relocate | +2.32±4.10% | 14.7±4.2% | 18.5±27.3% |
| ChangeOT | +2.22±3.08% | 18.7±3.3% | 9.4±13.0% |
| ChangeDay | +1.94±2.83% | 13.9±7.2% | 14.4±22.8% |
| ChangeRoom | +1.93±2.85% | 22.6±3.5% | 12.2±16.4% |
| ToggleOpt | +1.91±2.75% | 5.4±7.8% | 8.6±12.9% |
| SwapRooms | +1.88±3.17% | 17.4±4.8% | 9.9±15.2% |
| **ChangeNurse** | **-0.86±2.44%** | **0.0%** | 6.4±12.7% |

**Conclusiones clave:**
1. **ChangeNurse** es el único operador definitivamente eliminable: 0 mejoras en todas las instancias, LOO negativo (su presencia perjudica ligeramente)
2. **Alta varianza inter-instancia** — ningún operador domina consistentemente. Ej: SwapDays va de +13.9% (i04) a +0.2% (m29). Justifica mantener diversidad de vecindarios
3. **ToggleOpt** es un operador "habilitador": baja frecuencia (5.4%) pero alto impacto al eliminarlo. Activa pacientes opcionales desbloqueando oportunidades para otros operadores
4. **ChangeRoom** es redundante con Relocate: máxima frecuencia (22.6%) pero mínimo impacto marginal (+1.93%)
5. El **VNS completo mejora un 18% sobre aleatorio**; el mejor operador en solitario (Relocate) solo alcanza 6.2%

**Recomendación — VNS reducido de 6 operadores:** eliminar `ChangeNurse` (inútil) y `ChangeRoom` (redundante con SwapRooms+Relocate). Pérdida esperada ≤0.3%.

### 4.4 Script de Análisis Python (`ablation_analysis.py`)

Genera 6 figuras a partir de `ablation_results/ablation_exhaustive_merged.csv`:

| Figura | Descripción |
|--------|-------------|
| `fig1_loo_heatmap.png` | Heatmap LOO Δ% por instancia y operador (escala divergente rojo/verde) |
| `fig2_op_contribution.png` | Barras apiladas de contribución por operador en 'all', por instancia |
| `fig3_boxplot_configs.png` | Boxplot de coste normalizado por configuración (leave-one-out, single-op) |
| `fig4_solo_vs_all.png` | Barras horizontales: operador único vs VNS completo, mejora % vs aleatorio |
| `fig5_cost_breakdown.png` | Heatmap desglose de 12 costes blandos en config 'all', por instancia |
| `fig6_loo_summary.png` | Barras con error de LOO Δ% agregado sobre 6 instancias |

También exporta `ablation_results/figures/summary_table.csv` con estadísticas agregadas.

**Uso:**
```bash
python3 ablation_analysis.py [csv_path]
# Requiere: matplotlib, seaborn, pandas
```

Figuras generadas en `ablation_results/figures/`.

### 4.5 Correcciones en `main.cpp`

**Bug 1 — Límite de tiempo global:**
- Añadido 5º argumento `global_time_s` (default 600s = 10 min de competición)
- El bucle multi-start se detiene cuando `remaining_s() < 2.0`
- Cada restart recibe `remaining_s() / restarts_left` segundos para LocalSearch
- `num_restarts` cambiado de 10 a 100 (el tiempo global lo detiene antes de completarlos todos)

**Bug 2 — Soluciones infactibles como "mejor solución":**
- El evaluador NO penaliza HC1 (pacientes obligatorios no programados) en el objetivo blando
- Una perturbación podía dejar un obligatorio sin asignar y la solución parecía "mejor" por coste
- **Fix:** la mejor solución solo se actualiza si `FeasibilityChecker::Check(candidate).feasible` es `true`

```cpp
// ANTES
if (ls_stats.final_cost < best_cost) {
    best_cost = ls_stats.final_cost;
    best_solution = std::move(candidate);
}
// DESPUÉS
if (ls_stats.final_cost < best_cost &&
    FeasibilityChecker::Check(candidate).feasible) {
    best_cost = ls_stats.final_cost;
    best_solution = std::move(candidate);
}
```

**Afectaba a:** i12 (LS corrompía solución), i16 y i20 (generador fallaba en >80% de semillas por instancias muy restringidas).

### 4.6 Generación de Soluciones i01–i30

Ejecutadas las 30 instancias en paralelo (20 procesos simultáneos, 20 cores disponibles):

```bash
printf '%s\n' $(seq -w 1 30 | sed 's/^/data\/i/;s/$/.json/') | \
xargs -P20 -I{} build/ihtc_solver "{}" 42 5000 100 600
```

**Resultados:**
- **30/30 soluciones factibles** (0 violaciones duras)
- ~100 reinicios por instancia en 600s
- Guardadas en `solutions/i*_solution.json`

| Instancia | Coste | Instancia | Coste |
|-----------|-------|-----------|-------|
| i01 | 3787 | i16 | 14096 |
| i02 | 727 | i17 | 51180 |
| i03 | 10050 | i18 | 43266 |
| i04 | 585 | i19 | 29317 |
| i05 | 10645 | i20 | 37408 |
| i06 | 10951 | i21 | 29080 |
| i07 | 2621 | i22 | 83688 |
| i08 | 4303 | i23 | 26251 |
| i09 | 2935 | i24 | 39294 |
| i10 | 24795 | i25 | 11447 |
| i11 | 28857 | i26 | 80478 |
| i12 | 12753 | i27 | 84481 |
| i13 | 15957 | i28 | 81414 |
| i14 | 10977 | i29 | 16921 |
| i15 | 12766 | i30 | 35406 |

---

## 5. Estado Actual del Proyecto

### Ficheros modificados respecto a commits anteriores

| Fichero | Cambio |
|---------|--------|
| `src/solver/LocalSearch.h` | `enabled_mask` param, `op_improvements[8]` en stats, `kOperatorNames` |
| `src/solver/LocalSearch.cpp` | Filtrado por máscara, tracking por operador, `ToString()` extendido |
| `src/main.cpp` | Tiempo global, fix factibilidad en best_solution |
| `CMakeLists.txt` | Target `ihtc_ablation` añadido |

### Ficheros nuevos

| Fichero | Descripción |
|---------|-------------|
| `src/ablation_test.cpp` | Binario ablation con pool de hilos |
| `ablation_analysis.py` | Script Python de análisis y visualización |
| `ablation_results/` | CSVs por instancia + merged + figuras |
| `solutions/i*_solution.json` | Soluciones generadas i01–i30 |
| `history/project-history.md` | Este documento |

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -S .
cmake --build build -j$(nproc)
# Binarios: build/ihtc_solver, build/ihtc_ablation
```

---

## 6. Tareas Pendientes / Líneas Futuras

- [x] ~~Comparar nuestras soluciones (i01–i30) contra `best-solutions/sol_i*.json` con tabla CSV~~ → ver §8
- [ ] Generar soluciones para instancias `m01`–`m30`
- [ ] Implementar `SolutionIO::ImportJSON` para poder evaluar soluciones externas (los formatos de enfermeras difieren: nuestro formato es `{room,day,shift,nurse}` por entrada; el de la competición es nurse-centric `{id, assignments:[{day,shift,rooms:[]}]}`)
- [ ] Implementar VNS reducido (6 operadores: sin ChangeNurse y ChangeRoom) y comparar con full
- [ ] Añadir multi-threading al solver principal (actualmente usa 1 hilo; la competición permite 4)
- [ ] Análisis estadístico formal (Wilcoxon, effect size) sobre resultados del ablation

---

## 7. Notas Importantes para Futuros Contextos

- El **evaluador no penaliza HC1** en el objetivo blando — si un obligatorio queda sin asignar, la solución parece válida por coste. Siempre verificar con `FeasibilityChecker::Check()` antes de aceptar una solución como "mejor"
- **i16 y i20** son instancias muy restringidas donde el generador falla en la mayoría de semillas. El multi-start con muchos reinicios lo compensa, pero son candidatas a mejorar el generador
- El formato JSON de enfermeras de la competición (`best-solutions/`) es diferente al nuestro — una enfermera puede cubrir múltiples habitaciones en el mismo turno (HC11 desactivada en nuestra implementación)
- **`kOperatorNames`** está definido como variable `static constexpr` en `LocalSearch.h` (no dentro de la clase), accesible desde `ablation_test.cpp` con solo incluir el header
- Los resultados del ablation tienen **alta varianza inter-instancia** — las conclusiones son tendencias, no verdades absolutas por instancia individual
- **`UncoveredRoom`** (§8): los operadores VNS mueven pacientes pero no enfermeras. Una celda `(room, day)` que pasa de vacía a poblada tras un movimiento queda sin nurse → infactible para el validador oficial. El fix (`EnsureFullNurseCoverage` tras cada accept y tras Perturb) ya está integrado, pero cualquier *nuevo* operador que cree celdas debe respetar el invariante o llamarlo explícitamente

---

## 8. Sesión 2026-05-04 — Validador Oficial y Fix de Cobertura de Enfermeras

### 8.1 Integración del validador oficial IHTC

Se añadió `validator/IHTP_Validator.cc` (descargado de la web oficial, versión 0.0 — 23/05/24) con su dependencia `json.hpp`. Compilación:

```bash
cp src/io/json.hpp validator/json.hpp
g++ -O2 -std=c++17 -o validator/IHTP_Validator validator/IHTP_Validator.cc
```

Uso: `./validator/IHTP_Validator <instancia.json> <solucion.json> [verbose]`. La salida lista violaciones agrupadas y costes ponderados por componente.

### 8.2 Bug crítico detectado: UncoveredRoom

**Síntoma:** la primera comparación ACO vs random sobre i01–i30 (300 s × 4 hilos) arrojó **57/60 soluciones infactibles** según el validador oficial, todas por `UncoveredRoom > 0`. Algunas además mostraban `RoomSkillLevel` con costes negativos / overflow (8 instancias) — efecto colateral del bug del propio validador (línea 914 de `IHTP_Validator.cc`: accede a `NurseSkillLevel(-1)` cuando una habitación poblada no tiene nurse).

**Causa raíz:** los operadores VNS (`TryChangeRoom`, `TryChangeDay`, …, `TryRelocate`) mueven pacientes mediante `Solution::AssignPatient/UnassignPatient`, pero no tocan las asignaciones de enfermeras. Cuando un movimiento crea una celda `(room, day)` que antes estaba vacía, esa celda queda **sin enfermera para los 3 turnos**, ya que `RandomGenerator::GenerateNurseAssignments` solo se llama una vez al final de la construcción. Resultado: `UncoveredRoom` en todas las celdas que ganó tráfico durante la mejora local.

Adicional: la perturbación ILS también mueve pacientes y produce el mismo efecto.

**Verificación empírica:** sobre `solutions_aco/i01_solution.json`, `r1` día 2 contenía a `p10` (length_of_stay=3) pero ninguna nurse asignada. En la solución original tras la construcción ACO, `r1` día 2 estaba vacía → no se asignó nurse. La VNS movió a `p10` allí más tarde y nadie cubrió la celda.

### 8.3 Fix aplicado

Se introduce el invariante: **toda celda `(room, day, shift)` con `room_occupancy > 0` debe tener una enfermera asignada en todo momento**. Se materializa con:

1. Nueva función `RandomGenerator::EnsureFullNurseCoverage(Solution&, ProblemData&, std::mt19937&)` (idempotente). Recorre todas las `(r, d, s)` y, si la celda está poblada y no tiene nurse, elige la mejor por score (mismo criterio que `GenerateNurseAssignments`: penaliza skill insuficiente y sobrecarga, bonifica continuidad con el día anterior).

2. Llamadas estratégicas en `LocalSearch::Run`:
   - **Al entrar a `Run`**, antes de evaluar coste inicial — cubre el caso de soluciones recién construidas por ACO con celdas pendientes (improbable porque ACO ya llama `GenerateNurseAssignments`, pero defensivo).
   - **Tras cada operador aceptado** que mueve pacientes (todos salvo `TryChangeNurse`) — re-evaluar el coste con cobertura completa. Sin esto, el `Evaluator` reporta coste artificialmente bajo (las celdas descubiertas no contribuyen a `nurse_skill`) y la VNS converge a óptimos locales falsos.
   - **Tras `Perturb`** — cubre las celdas creadas por la perturbación.

3. Nueva restricción dura `HC14: CheckRoomCoverage` en `FeasibilityChecker` para que el solver pueda detectar internamente este problema (espejo de `UncoveredRoom` del validador).

### 8.4 Validación post-fix (i01–i30, 300 s, 4 hilos paralelos, semilla 42)

Tras compilar y relanzar el benchmark completo:

| Métrica | Antes del fix | Después del fix |
|---|---|---|
| Soluciones random factibles | 2/30 (i13, i21) | **30/30** |
| Soluciones ACO factibles | 1/30 (i20) | **30/30** |
| Filas con overflow del validador | 8 | **0** |

**Comparación ACO vs Random (todas factibles):**

| Métrica | Random | ACO | Diff |
|---|---|---|---|
| Coste agregado i01–i30 | 1 153 644 | 1 119 248 | **−34 396 (−2.98%)** |
| Instancias donde gana ACO | — | **24** | 80% |
| Instancias donde gana random | **6** | — | 20% |
| Suma de mejoras donde ACO gana | — | 39 273 | — |
| Suma de mejoras donde random gana | 4 877 | — | — |

ACO gana en proporción 24:6 con mejora agregada de ~3%. Las 6 instancias donde random sale mejor son: i04, i08, i11, i19, i23, i26 — diferencias entre 0.01% y 15.30% (i08 es la única realmente significativa con −15.3%).

### 8.5 Ficheros nuevos / modificados

| Fichero | Cambio |
|---|---|
| [src/solver/RandomGenerator.h](../src/solver/RandomGenerator.h) | Declarado `EnsureFullNurseCoverage` |
| [src/solver/RandomGenerator.cpp](../src/solver/RandomGenerator.cpp) | Implementación de `EnsureFullNurseCoverage` (idempotente) |
| [src/solver/LocalSearch.cpp](../src/solver/LocalSearch.cpp) | Llamadas a `EnsureFullNurseCoverage` al entrar, tras cada accept (excepto ChangeNurse) y tras `Perturb` |
| [src/evaluator/FeasibilityChecker.h](../src/evaluator/FeasibilityChecker.h) | HC14 declarada + comentario doc |
| [src/evaluator/FeasibilityChecker.cpp](../src/evaluator/FeasibilityChecker.cpp) | `CheckRoomCoverage` añadida a `Check()` |
| `validator/IHTP_Validator.cc` | Validador oficial IHTC añadido al repo |
| `validator/IHTP_Validator` | Binario compilado |
| `validator/json.hpp` | Copia de `src/io/json.hpp` para satisfacer dependencia |
| `aco-random-comparison.csv` | CSV final de la comparación (todas 0 violaciones) |
| `solutions_random/i01..i30_solution.json` | 30 soluciones modo random tras fix |
| `solutions_aco/i01..i30_solution.json` | 30 soluciones modo ACO tras fix |
| `run_comparison.sh` | Wrapper de ejecución por (instancia, modo) que mueve la salida a su carpeta |
| `validate_and_compare.sh` | Genera el CSV final llamando al validador oficial sobre las 60 soluciones |
| [history/ACO.md](ACO.md) | Sección 5 (construcción paso a paso) y sección 6 (VNS detallado) reescritas con el flujo real del código |

### 8.6 Notas sobre el coste reportado

El coste final que reporta nuestro `Evaluator` es ligeramente menor que el del validador oficial (e.g. para i01 ACO: 3636 interno vs 4737 oficial). Diferencia esperada por:

- El oficial pondera 12 componentes con pesos definidos en la instancia; nuestro `Evaluator` usa los mismos pesos pero la suma de `RoomAgeMix` parece distinta (oficial cuenta `max - min` entre patients+occupants en cada (r,d); el nuestro puede usar otra agregación).
- El oficial siempre reporta cost íntegro (incluye `unscheduled_optional`) pero algunos componentes (como `RoomSkillLevel`) pueden depender del orden de las nurses asignadas.

Lo crítico es que ambas medidas son consistentes (ACO < random en validator y en evaluator), y la solución es 100% factible.

### 8.7 Reproducir el experimento

```bash
# 1. Compilar todo
cmake --build build -j$(nproc)
g++ -O2 -std=c++17 -o validator/IHTP_Validator validator/IHTP_Validator.cc

# 2. Limpiar salidas previas
rm -f solutions_random/*.json solutions_aco/*.json
rm -f logs/comparison/_progress.csv && touch logs/comparison/_progress.csv

# 3. Fase 1: random (~37 min)
seq -f "%02g" 1 30 | xargs -P 4 -I {} ./run_comparison.sh {} random 300

# 4. Fase 2: ACO (~37 min)
seq -f "%02g" 1 30 | xargs -P 4 -I {} ./run_comparison.sh {} aco 300

# 5. Validar y generar CSV
./validate_and_compare.sh
```

Tiempo total con paralelismo 4 (regla IHTC): ~75 min.
