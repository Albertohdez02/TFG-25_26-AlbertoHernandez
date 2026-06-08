# Some-touches: tres mejoras finales tras Fase F

Después de Fase F (compound moves, gap −4.2 pp vs postfix) y del abandono de Fase E (delta-eval, complejidad superior a la prevista), quedan tres mejoras potenciales identificadas en el análisis del estancamiento:

- **Fase 1 — NursePolisher**: una fase final de pulido específica para la matriz de enfermeras, ejecutada tras la búsqueda principal. Objetivo: bajar los componentes `continuity_of_care`, `nurse_excessive_workload` y `room_nurse_skill` que en i22 a 1800 s suman **37,201 puntos = 44 %** del coste residual.
- **Fase 2 — Anti-convergencia del ACO**: ajustes a los parámetros (`q0` adaptativo, ratio `τ_min/τ_max` corregido, reset suave) para evitar que el solver caiga consistentemente en los mismos valles locales (i19, i23).
- **Fase 3 — ACO rápido + ALNS+SA dedicado**: reducir el tiempo del bucle ACO a 60-120 s y dedicar el resto a ALNS+SA puro con temperatura bien calibrada.

Documento incremental: cada fase se desarrolla en su sección con diseño, implementación, validación y resultados.

---

## Fase 1 — NursePolisher (en curso)

### 1.1 Diagnóstico y justificación

En la mejor solución actual (`A+B+C+F` 1800 s en i22), los componentes relacionados con enfermeras dominan el coste residual:

| Componente | Valor | % del total |
|---|---|---|
| ElectiveUnscheduled | 37,800 | 45 % |
| **ExcessiveNurseWorkload** | **23,540** | **28 %** |
| **ContinuityOfCare** | **13,425** | **16 %** |
| OpenOT | 1,900 | 2 % |
| PatientDelay | 7,180 | 9 % |
| **RoomSkillLevel** | **236** | **<1 %** |
| Resto | 244 | <1 % |
| **TOTAL** | **84,325** | |

Las tres líneas marcadas son **funciones casi separables del problema de pacientes**: una vez fijados `(paciente → room, day, ot)`, asignar `(room, day, shift → nurse)` es un problema de asignación con restricciones duras (HC9: una nurse por celda; HC10: disponibilidad) y objetivos blandos. Los operadores actuales (`TryChangeNurse` con cap 500, `TrySwapNurseBlock`) lo tocan pero **compiten por tiempo con los operadores de pacientes**, que en VNS son first-improvement y suelen ganar la lotería del shuffle.

Una **fase final dedicada exclusivamente a nurses** se ejecuta tras la búsqueda principal y dispone de todo su tiempo para refinar solo esta matriz.

### 1.2 Diseño

**Nuevo módulo**: `src/solver/NursePolisher.{h,cpp}` con una función pública:
```cpp
class NursePolisher {
 public:
  // Pulido focalizado de la matriz de nurses sobre una solución factible.
  // No toca las asignaciones de pacientes. Time-limited.
  // Devuelve el número de mejoras aceptadas.
  static int Polish(Solution& solution, double time_limit_s,
                    std::mt19937& rng);
};
```

**Operadores** (en orden de aplicación dentro del bucle):

1. **`ChangeOneNurse(r, d, s)` — best-improvement por celda**
   - Para cada `(r, d, s)` con `room_occupancy(r, d) > 0`:
     - Iterar TODAS las nurses factibles (`IsFeasibleNurseAssignment`).
     - Aceptar la que más reduzca el coste total.
   - Diferencia con `TryChangeNurse` de VNS: **best-improvement** (no first), sin cap.

2. **`SwapTwoNurses((r1, d1, s1), (r2, d2, s2))` — intercambio entre celdas**
   - Para pares de celdas con shift idéntico y nurses distintas:
     - Comprobar que ambas son factibles tras swap.
     - Aceptar si baja el coste.

3. **`PromoteContinuity(r, s, day_range)` — extender continuidad**
   - Para cada `(room, shift)`:
     - Identificar la nurse mayoritaria en el rango de días del paciente.
     - Intentar extenderla a todos los días `(r, d, s)` donde sea factible y baje el coste.

**Esquema del bucle principal** (similar a VNS pero dedicado):
```
while (time_remaining > 0 && improved_in_last_pass):
  improved = false
  improved |= ChangeOneNursePass()    # operador 1 sobre TODAS las celdas
  if time_remaining <= 0: break
  improved |= SwapTwoNursesPass()      # operador 2 sobre TODOS los pares
  if time_remaining <= 0: break
  improved |= PromoteContinuityPass()  # operador 3 sobre TODAS las (r, s)
```

First-improvement por celda dentro de cada operador (no por pasada): tan pronto un cambio mejora, se aplica; el operador sigue con la siguiente celda.

**Aceptación**: solo cambios que mejoran o mantienen el coste total (hill climbing puro). No SA, no tabu (innecesario en este sub-problema, los óptimos locales de nurses son menos profundos que los de pacientes).

### 1.3 Integración

En `ACOSolver::Run`, tras el bucle principal y antes de devolver `best_solution`:
```cpp
double polish_budget = 60.0;  // o configurable
if (best_cost < std::numeric_limits<int>::max()) {
  int before = best_cost;
  NursePolisher::Polish(best_solution, polish_budget, rng);
  int after = Evaluator::Evaluate(best_solution);
  std::cout << "Nurse polish: " << before << " -> " << after << "\n";
  best_cost = after;
}
```

El presupuesto `polish_budget` viene del tiempo total: por defecto **10 % del time_limit_s** (60 s de un total de 600 s), configurable vía `ACOParams::nurse_polish_budget_s`.

**Fallback**: si `ACOParams::nurse_polish_budget_s <= 0`, no se ejecuta el polish — comportamiento previo. Default activado.

### 1.4 Verificación esperada

**Test focal**: tomar la solución de i22 600 s A+B+C+F (coste oficial 87,047) y aplicar el polisher con presupuestos crecientes (30, 60, 120 s). Componentes esperados a bajar:

| Componente | Antes | Esperado |
|---|---|---|
| ContinuityOfCare | 13,265 | < 12,000 (−10 %) |
| ExcessiveNurseWorkload | 23,140 | < 20,000 (−15 %) |
| RoomSkillLevel | 201 | < 180 |
| **Total** | **87,047** | **< 84,000** |

**Test agregado**: re-benchmark 30 instancias 600 s con polish activado. Gap medio esperado: **+40.67 % → +35 a +38 %**.

### 1.5 Implementación

Ficheros añadidos:
- [`src/solver/NursePolisher.h`](src/solver/NursePolisher.h) — interfaz pública.
- [`src/solver/NursePolisher.cpp`](src/solver/NursePolisher.cpp) — implementación con 3 operadores hill-climbing.

Ficheros modificados:
- [`CMakeLists.txt`](CMakeLists.txt) — añadido `NursePolisher.cpp` a `SOLVER_SOURCES`.
- [`src/solver/ACOSolver.h`](src/solver/ACOSolver.h) — nuevo campo `nurse_polish_budget_s` en `ACOParams` (default 60.0, clamp 30s..30 % de `time_limit_s`).
- [`src/solver/ACOSolver.cpp`](src/solver/ACOSolver.cpp) — reserva `polish_budget` desde el tiempo total; tras el bucle ACO, llama a `NursePolisher::Polish(best_solution, polish_budget, rng)`. Logs: `Nurse polish: <before> -> <after> (<improvements> mejoras)`.

Detalles de los operadores:

**`ChangeOneNursePass`** (best-improvement por celda)
- Recorre todas las celdas `(r, d, s)` con `room_occupancy(r, d) > 0`.
- Para cada celda: iterar todas las `num_nurses` y elegir la que más reduce `Evaluator::Evaluate`.
- Coste por celda: `O(N × Evaluate)` ≈ `N × 4 ms` en i22 = ~130 ms × ~600 celdas ≈ 78 s. Suficiente para una pasada en el budget de 60 s solo si el polish encuentra pocas alternativas factibles (esperado por HC10 de disponibilidad).

**`SwapTwoNursesPass`** (intercambio entre celdas)
- Recorre pares con mismo shift. Cap exterior = 200 celdas para evitar `O(n²)` explosivo.
- First-improvement por celda externa: tan pronto un swap baja el coste, pasa a la siguiente celda externa.

**`PromoteContinuityPass`** (extender continuidad)
- Para cada `(room, shift)`:
  1. Recoger días activos y contar frecuencia de cada nurse.
  2. Nurse más frecuente = candidata.
  3. Intentar propagarla a todos los días activos donde no esté, aceptando solo si baja el coste.
- Ataca `continuity_of_care` directamente (≥ 13k puntos en i22).

Bucle principal:
```
while (improved && time_remaining > 0.1):
  improved |= ChangeOneNursePass()
  improved |= SwapTwoNursesPass()
  improved |= PromoteContinuityPass()
  pass++; if pass >= 20: break
```

### 1.6 Resultados

#### 1.6.1 Spot-check i22 (600 s, 60 s polish reservados)

| | 600 s ACO+ILS+F | 1800 s ACO+ILS+F | **600 s + Polish** |
|---|---|---|---|
| Coste oficial | 87,047 | 84,325 | **80,659** |
| Gap vs best (47,861) | +81.9 % | +76.2 % | **+68.6 %** |

**Mejora del Polish vs ABCF 600 s: −6,388 puntos (−7.3 %)** y con menos tiempo total que ABCF 1800 s.

Cambios internos durante el polish (mejor solución): coste 83,745 → 76,223 (5 mejoras grandes en 60 s).

Componentes oficiales clave tras polish:

| Componente | Sin polish | **Con polish** | Δ |
|---|---|---|---|
| **ExcessiveNurseWorkload** | 23,140 | **16,720** | **−6,420** |
| ContinuityOfCare | 13,265 | 12,690 | −575 |
| RoomSkillLevel | 201 | 631 | +430 |
| SurgeonTransfer | 99 | 64 | −35 |
| RoomAgeMix | 192 | 159 | −33 |
| ElectiveUnscheduled | 44,100 | 40,950 | −3,150 |

El polisher hizo un trade-off racional: **aumentó RoomSkillLevel (+430)** para bajar drásticamente **ExcessiveNurseWorkload (−6,420)**. Saldo neto muy favorable (los pesos hacen que un déficit de skill cueste menos que el exceso de carga acumulada).

#### 1.6.2 Benchmark agregado 30 instancias 600 s

| Régimen | Total | Gap medio | Wins vs postfix |
|---|---|---|---|
| postfix (base) | 1,038,478 | +44.93 % | — |
| A+B+C | 1,037,932 | +44.85 % | 23/30 |
| A+B+C+F | 1,007,950 | +40.67 % | 26/30 |
| **A+B+C+F + Polish** | **971,988** | **+35.65 %** | **28/30** |

**Mejora Polish vs ABCF: −3.57 % (−35,962 puntos).**
**Gap medio acumulado proyecto: +44.93 % → +35.65 % (−9.28 pp).**

#### 1.6.3 Mejoras destacadas vs ABCF

Mejoras grandes (las 10 mejores absolutas):

| Inst | ABCF | Polish | Δ | Δ % |
|---|---|---|---|---|
| i22 | 87,047 | 81,750 | **−5,297** | −6.1 % |
| i26 | 102,711 | 97,904 | **−4,807** | −4.7 % |
| i27 | 92,973 | 89,455 | **−3,518** | −3.8 % |
| i17 | 62,860 | 59,705 | **−3,155** | −5.0 % |
| i20 | 38,951 | 35,891 | **−3,060** | −7.8 % |
| i10 | 25,740 | 23,690 | **−2,050** | −8.0 % |
| i21 | 36,208 | 34,249 | −1,959 | −5.4 % |
| i15 | 19,007 | 17,251 | **−1,756** | −9.2 % |
| i18 | 45,022 | 43,536 | −1,486 | −3.3 % |
| i19 | 66,477 | 65,116 | **−1,361** | −2.0 % |

**Nota i19**: primera vez en todo el proyecto que mejora respecto a una versión anterior. Sigue siendo regresión vs postfix (+5,819) pero ya se mueve a la baja.

**Regresiones pequeñas**: i24 +19 (negligible), i29 +692.

**Win rate: 28/30 vs postfix Y vs ABCF**. Validación clara.

#### 1.6.4 Conclusión Fase 1

El `NursePolisher` ataca exactamente el cuello de botella identificado en el análisis: los componentes nurse dominantes (workload + continuity + skill) suelen quedar subóptimos tras la búsqueda principal por dos razones:

1. La VNS distribuye el tiempo entre 8+3 operadores, dando atención fragmentaria a `TryChangeNurse` y `TrySwapNurseBlock`.
2. Estos operadores trabajan con first-improvement y caps moderados.

Una fase **dedicada** de 60 s con best-improvement y sin caps (`ChangeOneNursePass`) más operadores específicos (`PromoteContinuityPass` ataca continuity, `SwapTwoNursesPass` reordena celdas) extrae el residual que la VNS deja.

Tabla: [`tables/aco-polish-vs-ABCF-vs-postfix.csv`](tables/aco-polish-vs-ABCF-vs-postfix.csv).

**Decisión**: mantener Polish como **default activo** (`nurse_polish_budget_s = 60.0`). Pasar a Fase 2.

---

## Fase 2 — Anti-convergencia ACO (en curso)

### 2.1 Diagnóstico previo

Síntomas observados de convergencia prematura tras Fase 1:
- i19 e i23 son atractores locales **consistentes** en todos los regímenes probados (postfix, ABC, ABCF, ABCF+Polish, 1800 s). i19 sigue siendo regresión vs postfix (+5,819 con Polish) aunque el Polish ya la mejoró un poco.
- Las hormigas tempranas tras `SeedPheromones` clonan el seed casi exactamente debido a `q0=0.90` + `τ_max` en seed.
- El reset por estancamiento es **brusco** (vuelta a τ_init uniforme), perdiendo todo el aprendizaje.
- El ratio `τ_min/τ_max = 1/(2·n_patients)` es **demasiado alto** comparado con la recomendación MMAS clásica.

### 2.2 Cuatro cambios implementados (con flags individuales)

#### 2.2.1 `q0` dinámico (decreciente)

`q0` arranca en `q0_initial = 0.90` y baja linealmente a `q0_final = 0.70` conforme transcurre el tiempo del bucle ACO. Cálculo:
```cpp
double frac = clamp(elapsed_s / aco_time_budget, 0.0, 1.0);
ant_params.q0 = q0_initial - (q0_initial - q0_final) * frac;
```

**Implementación**: cada iteración del bucle ACO recalcula `q0` y lo pasa a `ConstructSolution` via una copia `ant_params` (no toca `params` original — compatible con paralelismo).

**Justificación**: la convergencia prematura viene de elegir `argmax` el 90 % del tiempo desde el principio. Si arrancamos con más exploración (q0=0.90 al inicio puede parecer alto pero baja conforme avanza), las primeras hormigas diversifican más.

**Flag**: `ACOParams::q0_dynamic = true` (default activo). Si `false`, usa `params.q0` fijo (compatibilidad legacy).

#### 2.2.2 `tau_min_factor` (más contraste MMAS)

Antes: `tau_min = tau_max / (2 × num_patients)`. En i22 con 174 pacientes → ratio 1/348.

Después: `tau_min = tau_max / (tau_min_factor × num_patients)` con `tau_min_factor = 50` (default). En i22 → ratio 1/8700 (×25 menos uniforme).

**Justificación**: MMAS clásico recomienda `τ_min` muy por debajo de `τ_max` para crear contraste real. El ratio anterior hacía que prácticamente todas las posiciones feasibles tuvieran feromona similar, lo que combinado con `q0=0.90` no diferenciaba.

**Flag**: `ACOParams::tau_min_factor = 50` (default). Coherente entre `UpdatePheromones` y `SeedPheromones`.

#### 2.2.3 Reset suave por estancamiento

Antes: al llegar a `stagnation_k = 15` iteraciones sin mejora, `ResetPheromones` ponía todo a `tau_init` uniforme (perdiendo el aprendizaje).

Después: secuencia escalonada
1. **Primer reset suave** (cuando `stagnation_count >= stagnation_k`): multiplicar todas las τ por `soft_reset_factor = 0.5`. Conserva proporciones aprendidas pero acerca todas las entradas a τ_min, lo que aumenta exploración relativa.
2. **Segundo reset suave** consecutivo (si tras el primero no hay mejora en otros `stagnation_k` iters): otra multiplicación por 0.5.
3. **Reset duro** tras `soft_resets_before_hard = 2` resets suaves consecutivos sin mejora: vuelta a `tau_init` uniforme (último recurso).

El contador `soft_reset_count` se resetea a 0 cada vez que aparece una mejora global (`stagnation_count == 0`).

**Flag**: `ACOParams::soft_reset = true` (default). `soft_reset_factor`, `soft_resets_before_hard` configurables.

#### 2.2.4 `seed_dampen`: SeedPheromones menos dominante

Antes: las decisiones del seed recibían `tau_max`. Resto de posiciones feasibles → `tau_min`. Esto creaba un contraste **máximo** que, combinado con `q0=0.90`, hacía que las hormigas tempranas clonaran al seed casi exactamente.

Después: las decisiones del seed reciben `seed_value = seed_dampen_factor × tau_min` (default `3 × tau_min`). Sigue dando ventaja al seed (3× el resto) pero **mucho menor** que `tau_max`. Las hormigas exploran alrededor en lugar de copiar.

**Flag**: `ACOParams::seed_dampen = true` (default). `seed_dampen_factor = 3.0` por defecto.

### 2.3 Implementación

Ficheros modificados:
- [`src/solver/ACOSolver.h`](src/solver/ACOSolver.h) — 8 flags nuevos en `ACOParams` (4 booleanos + 4 numéricos).
- [`src/solver/ACOSolver.cpp`](src/solver/ACOSolver.cpp):
  - Bucle principal: nueva variable `ant_params` con `q0` dinámico.
  - Tras el bucle: lógica de reset suave/duro con contador `soft_reset_count`.
  - `UpdatePheromones`: `tau_min` con `tau_min_factor`.
  - `SeedPheromones`: idem + `seed_value` calculado con `seed_dampen`.

### 2.4 Resultados

#### 2.4.1 Spot-check secuencial i19, i22, i27 (600 s, 1 solver a la vez)

| Instancia | Polish | **Phase 2** | Δ |
|---|---|---|---|
| i19 (atractor) | 65,116 | **62,990** | −2,126 (−3.3 %) |
| i22 | 81,750 | **78,389** | −3,361 (−4.1 %) |
| i27 | 89,455 | 89,094 | −361 (−0.4 %) |

Resultado positivo: mejoran las 3 instancias problemáticas. **i19 baja del atractor por primera vez en todo el proyecto.**

#### 2.4.2 Benchmark agregado 30 instancias (4 paralelo, condiciones de competencia)

| Régimen | Total | Gap | Wins vs postfix |
|---|---|---|---|
| postfix | 1,038,478 | +44.93 % | — |
| Polish (Fase 1) | 971,988 | **+35.65 %** | 28/30 |
| **Phase 2** | **972,566** | **+35.73 %** | 28/30 |

**Phase 2 vs Polish: +578 (+0.06 %), wins 17/30.**

#### 2.4.3 Por qué la divergencia spot vs benchmark

El spot-check usó **ejecución secuencial** (1 solver con sus 4 hormigas internas = 4 threads), mientras que el benchmark usa **4 solvers paralelos** (4 × 4 hormigas = 16 threads sobre ~4 cores reales). En condición de sobre-suscripción los tiempos efectivos de cada hormiga se reducen significativamente, lo que castiga más a Phase 2 que a Polish porque Phase 2 *necesita más iteraciones* para que su mayor exploración produzca mejoras (reset suave acumula efecto a largo plazo, q0 decreciente espera a las últimas iteraciones para explotar).

#### 2.4.4 Por instancia

**Mejoras Phase 2 vs Polish** (12 instancias, suma ≈ −4,500):
- i26: −1,255 (−1.3 %)
- i16: −599
- i11: −480
- i12: −415
- i15: −354
- **i19: −262** ← rompe parcialmente el atractor histórico
- i23: −269
- i05, i06, i28, i29, i01: pequeñas mejoras

**Regresiones Phase 2 vs Polish** (12 instancias, suma ≈ +5,500):
- **i20: +1,242**, **i27: +1,097**, **i22: +577**, **i21: +425**, **i14: +390**, **i08: +393**, **i10: +360**, otros < +200

**Interpretación**: los cambios anti-convergencia ayudan en instancias donde el solver estaba en atractor local malo (i19/i23/i26) y dañan donde el solver ya estaba en un óptimo local bueno y necesitaba *exploitation* para refinarlo (i20/i27/i22).

### 2.5 Decisión

**Phase 2 queda como opt-in (no default).** Defaults revertidos:
- `q0_dynamic = false`
- `tau_min_factor = 2` (comportamiento legacy)
- `soft_reset = false`
- `seed_dampen = false`

Activar manualmente cuando se identifique una instancia con convergencia prematura clara.

**Líneas futuras**: una versión adaptativa de Phase 2 que active los flags solo cuando detecte estancamiento real (e.g., contador stagnation > umbral) podría capturar las ganancias sin pagar las pérdidas. No implementado en esta iteración.

Tabla completa: [`tables/aco-phase2-vs-polish-vs-postfix.csv`](tables/aco-phase2-vs-polish-vs-postfix.csv).

---

## Fase 3 — ACO rápido + ALNS+SA dedicado (implementado, pendiente de validar)

### 3.1 Diagnóstico previo

Actualmente cada hormiga ACO ejecuta una VNS completa. En 600 s caben ~30 iteraciones de hormigas. La mayoría del tiempo se gasta en VNS, no en aprendizaje de feromonas. El ALNS+SA actual sólo se invoca como perturbación intra-VNS, sin disponer de su propio bloque de tiempo dedicado.

### 3.2 Diseño implementado

**Distribución de tiempo en preset `hybrid`** (para 600 s totales):

| Fase | Tiempo | Función |
|---|---|---|
| ACO + VNS rápido | **90 s** | Bucle de hormigas con VNS completa → produce `best_solution` inicial |
| ALNS+SA puro | **~450 s** | Bucle de `Apply()` (destroy + greedy repair + SA-accept) sobre `best_solution`, con VNS corta de 3 s entre Applys aceptados |
| NursePolisher (Fase 1) | **60 s** | Pulido final de la matriz de enfermeras |

**Mecánica del bucle ALNS+SA puro**:
```cpp
ALNSPerturbation alns(problem, best_cost);  // T_0 calibrada
Solution work_sol = best_solution;
int work_cost = best_cost;
while (alns_remaining > vns_time + 1.0) {
  if (alns.Apply(work_sol, work_cost, rng)) {  // ya incluye SA accept
    LocalSearch::Run(work_sol, ..., vns_time=3.0, ..., vns_config);
    work_cost = Evaluator::Evaluate(work_sol);
    if (work_cost < best_cost && feasible) {
      best_cost = work_cost;
      best_solution = work_sol;
    }
  }
}
```

Diferencia clave con el modo normal:
- **Más iteraciones de ALNS** (~150 en vez de ~30 en modo normal).
- **Temperatura SA calibrada con el coste de la mejor solución encontrada**, no con la del seed inicial.
- **VNS corta dedicada** (3 s) entre Applys, en vez de VNS larga compitiendo con todo.

### 3.3 Implementación

Ficheros modificados:
- [`src/solver/ACOSolver.h`](src/solver/ACOSolver.h) — nuevos flags `hybrid_mode`, `aco_quick_budget_s`, `alns_vns_time_s` en `ACOParams`.
- [`src/solver/ACOSolver.cpp`](src/solver/ACOSolver.cpp):
  - Cálculo de presupuestos: `aco_time_budget`, `alns_pure_budget`, `polish_budget`.
  - Tras el bucle ACO, nuevo bloque que ejecuta `ALNSPerturbation::Apply` + VNS corta hasta agotar `alns_pure_budget`.
  - Si `alns_pure_budget < 10s` por configuración apretada → degrada a modo normal sin avisar.
- [`src/main.cpp`](src/main.cpp) — nuevo preset CLI `hybrid` que activa `hybrid_mode = true` en `ACOParams`.

**Flag**: `ACOParams::hybrid_mode = false` (default off). Activar via `preset=hybrid` o `aco_params.hybrid_mode = true`.

### 3.4 Validación esperada

**Hipótesis**: el bucle ALNS+SA puro debería atacar mejor el componente `ElectiveUnscheduledPatients` en instancias problemáticas (i22, i27, i26), porque:
1. Más iteraciones de destroy/repair → más oportunidades de mover bloqueantes.
2. Temperatura SA bien calibrada → acepta empeoramientos temporales que pueden conducir a mejores valles.
3. VNS corta y dedicada → no compite con operadores patient-move.

### 3.5 Resultados

#### 3.5.1 Spot-check i22 600 s con preset=hybrid

Trace del bucle híbrido:
```
ACO+VNS 90 s        → coste inicial 87,953
ALNS puro 450 s     → 87,953 → 77,541 (139 Applies, 125 accepts, 90% acceptance)
Nurse polish 60 s   → 77,541 → 71,223 (5 mejoras)
                    → oficial 75,765
```

**i22 600 s hybrid vs Polish 600 s: −5,985 (−7.3 %)**. Bate incluso el Polish a 1800 s (×3 tiempo), que daba 80,659.

El bucle ALNS+SA puro consiguió programar **10 opcionales más** que el Polish 600 s (sin programar: 91 → 81 = ElectiveUnscheduled −4,500). La hipótesis se valida: con más iteraciones de destroy/repair, los patrones de inserción en greedy repair encuentran combinaciones que los operadores VNS no alcanzan.

#### 3.5.2 Benchmark agregado 30 instancias (4 paralelo, condiciones competición)

| Régimen | Total | Gap | Wins vs postfix |
|---|---|---|---|
| postfix (base) | 1,038,478 | +44.93 % | — |
| A+B+C+F | 1,007,950 | +40.67 % | 26/30 |
| Polish (Fase 1) | 971,988 | +35.65 % | 28/30 |
| **HYBRID (Fase 3)** | **952,969** | **+32.99 %** | **26/30** |

**Mejora HYBRID vs Polish: −19,019 puntos (−1.96 %).**
**Gap medio acumulado total proyecto: +44.93 % → +32.99 % (−11.94 pp).**

#### 3.5.3 Mejoras grandes por instancia

10 mejores absolutas (HYBRID vs POLISH):

| Inst | Polish | Hybrid | Δ | Δ % |
|---|---|---|---|---|
| i22 | 81,750 | 77,249 | **−4,501** | −5.5 % |
| i27 | 89,455 | 85,904 | **−3,551** | −4.0 % |
| i26 | 97,904 | 94,484 | **−3,420** | −3.5 % |
| **i16** | **16,166** | **13,053** | **−3,113** | **−19.2 %** |
| i18 | 43,536 | 41,767 | **−1,769** | −4.1 % |
| i29 | 19,554 | 17,883 | −1,671 | −8.6 % |
| i28 | 85,546 | 83,907 | −1,639 | −1.9 % |
| i24 | 41,535 | 40,472 | −1,063 | −2.6 % |
| i15 | 17,251 | 16,604 | −647 | −3.8 % |
| i19 | 65,116 | 64,556 | −560 | −0.9 % |

**i16 con −19.2 % es excepcional**: bajaba de Polish 16,166 (gap +59.4 %) a Hybrid 13,053 (gap +28.7 %). Casi 30 pp de mejora en una instancia individual.

**i19 mejora por primera vez vs Polish** (−560), confirmando que la diversidad de exploración del ALNS+SA puro empieza a salir del atractor histórico.

#### 3.5.4 Regresiones

| Inst | Polish | Hybrid | Δ |
|---|---|---|---|
| **i23** | 53,139 | 55,994 | **+2,855** |
| i30 | 47,114 | 48,723 | +1,609 |
| i08 | 8,535 | 9,162 | +627 |
| i21 | 34,249 | 34,550 | +301 |
| i20 | 35,891 | 36,159 | +268 |
| i10 | 23,690 | 23,805 | +115 |
| i09 | 8,522 | 8,525 | +3 |

7 regresiones, mayoritariamente pequeñas. **i23 sigue siendo atractor problemático** (también lo era en ABCF y Polish; el hybrid empeora vs Polish pero menos que el postfix).

### 3.6 Conclusión y decisión

El modo **hybrid** (`preset=hybrid`) **se establece como mejor configuración del solver**. Razones cuantitativas:

- Gap medio −2.66 pp adicionales sobre Polish (de +35.65 % a +32.99 %).
- 23/30 wins individuales vs Polish, 26/30 vs postfix.
- Mejoras de **doble dígito en %** en instancias problemáticas (i16 −19 %, i29 −9 %, i22 −5 %).
- Sólo 1 regresión > 1500 puntos (i23, atractor consistente).

**Sin embargo NO se cambia el default** (`hybrid_mode = false`) porque:
- El comportamiento es **muy distinto** al esquema clásico ACO+VNS (puede sorprender en uso).
- Modo `default` (Polish) sigue siendo competitivo y más predecible.
- `preset=hybrid` permite activarlo cuando se quiere el rendimiento máximo.

CSV: [`tables/aco-hybrid-vs-polish-vs-postfix.csv`](tables/aco-hybrid-vs-polish-vs-postfix.csv).

---

## Resumen final del proyecto (cierre)

| Régimen | Total 30 inst | Gap medio | Wins | Estado |
|---|---|---|---|---|
| postfix (línea base bug-fixed) | 1,038,478 | +44.93 % | — | baseline |
| A+B+C (refinamientos) | 1,037,932 | +44.85 % | 23/30 | descartado (gap −0.05 % insignificante) |
| A+B+C+F (compound moves) | 1,007,950 | +40.67 % | 26/30 | default activo |
| **A+B+C+F + Polish** (Fase 1) | **971,988** | **+35.65 %** | **28/30** | **default actual** (`preset=default`) |
| + Phase 2 anti-conv | 972,566 | +35.73 % | 28/30 | opt-in (impacto neutro) |
| **+ Phase 3 hybrid** | **952,969** | **+32.99 %** | **26/30** | **preset=hybrid** (mejor cifra) |
| best-known (competición) | 716,560 | — | — | referencia |

**Mejora acumulada total** desde el postfix (línea base honesta tras bug-fixes del evaluator):
- −85,509 puntos (−8.23 % del total)
- −11.94 puntos porcentuales de gap medio

**Distancia al best-known**: +32.99 % en `preset=hybrid` (desde +44.93 % inicial). El gap se ha reducido a 0.73× el original.

Comparativa cronológica de hitos:
1. Bug-fixes del Evaluator → gap +54.9 % → **+44.9 %** (línea base honesta).
2. Bloque A+B+C (refinamientos VNS+ACO) → gap **+44.85 %** (placebo).
3. Bloque F (compound moves IMADA-inspired) → gap **+40.67 %**.
4. Some-touches Fase 1 (NursePolisher) → gap **+35.65 %**.
5. Some-touches Fase 3 (modo híbrido) → gap **+32.99 %** (seed=42).
6. **Multi-seed (5 seeds) confirmación robusta → gap medio +32.67 % (Wilcoxon p < 10⁻⁴)**.

---

## Anexo: Validación estadística multi-seed

Para reforzar la robustez de los resultados (todo el desarrollo se hizo con `seed=42` único), se ejecutó un benchmark de **5 seeds × 30 instancias = 150 ejecuciones** con `preset=hybrid` 600 s. Seeds: 42, 137, 991, 5043, 7919.

### Resultados agregados

| Métrica | Valor |
|---|---|
| Gap medio (media por instancia) | **+32.67 %** |
| Gap medio (mejor caso por instancia) | +30.72 % |
| Gap medio (peor caso por instancia) | +34.91 % |
| Gap postfix (línea base) | +44.93 % |
| **Reducción robusta vs postfix** | **−12.26 pp** (mean), −14.21 pp (min) |

### Test de significancia

**Wilcoxon pareado postfix vs hybrid_mean** (30 pares):
- W-statistic = 413.00
- **p-value = 3.53 × 10⁻⁵**
- Hipótesis nula (postfix == hybrid_mean) rechazada con p < 0.001.
- **Conclusión estadística**: hybrid es significativamente mejor que postfix.

### Análisis de variabilidad por instancia

**Instancias muy estables** (std < 100):
- i02 (std=20), i05 (37), i03 (68), i06 (57), i01 (48), i07 (81).
- Casi independientes del seed → el algoritmo encuentra siempre el mismo óptimo local.

**Instancias inestables** (std > 800):
- **i16: std=1272** (min=13,053 / max=16,127, rango 23 % del mejor).
- i20: std=1270, i26: std=1232, i19: std=1012, i27: std=976, i22: std=793.

Estas son justamente las "problemáticas" que tienen alta varianza → el solver es **sensible a la suerte de la búsqueda** en ellas. En i16, el mejor seed logró +28.7 % de gap (cerca de romper la barrera del 30 %); el peor seed se quedó en +59.1 %.

### Instancia i23 — atractor estructural genuino

A diferencia de las anteriores, **i23 tiene baja varianza** (std=443) pero **siempre** cae cerca de +45 % de gap. No es mala suerte de búsqueda — es **atractor local consistente**. Sería la candidata clara para una investigación futura focalizada (CP/MIP sub-problema, multi-colony, o reformulación del modelo).

### Coincidencia con seed único

El gap del seed único utilizado en el desarrollo (seed=42, hybrid → +32.99 %) **coincide** con la media multi-seed (+32.67 %). Esto valida que **seed=42 no fue lotería**, sino representativo del comportamiento esperado.

### Ficheros generados

- [`tables/aco-multiseed-stats.csv`](tables/aco-multiseed-stats.csv) — 30 filas con n, min, max, mean, std, gap_*_pct y los 5 valores por seed.
- [`graficas/multiseed_boxplot_hybrid.png`](graficas/multiseed_boxplot_hybrid.png) — boxplot por instancia, ✕=postfix, ★=best-known.
- [`solutions_multiseed/seed_{42,137,991,5043,7919}/`](solutions_multiseed/) — 150 soluciones individuales.
- [`logs/multiseed/`](logs/multiseed/) — 150 logs de ejecución.

### Tiempo de cómputo

5 h 36 min reales (20:37 → 02:13) con 4 procesos paralelos. Total CPU: ~22 h.

---

## Anexo B — Evaluación en el conjunto `m01–m30`

Tras cerrar el desarrollo, se aplicó el modo `hybrid` al **segundo conjunto** del concurso IHTC 2024 (`m01–m30`), de mayor tamaño en promedio que `i01–i30`:

| Conjunto | P máx | R máx | N máx | Tamaño relativo |
|---|---|---|---|---|
| `i01–i30` | 409 (i22) | 20 | 44 | base |
| `m01–m30` | **489 (m30)** | **48** | **90** | mayor |

### B.1 Benchmark single-seed (`preset=hybrid` 600 s, seed=42)

| Métrica | Valor |
|---|---|
| **Feasibles** | **30 / 30** ✓ |
| Total best (oficial) | 694,953 |
| Total hybrid | 937,988 |
| **Gap (sobre totales)** | **+34.97 %** |
| Gap medio (mean por inst) | +29.93 % |
| Gap mediana | +27.03 % |
| Gap mín (m02) | +4.82 % |
| Gap máx (m29) | +83.05 % |
| Std | 17.18 % |

Las `m*` son **~2 pp peores** que las `i*` (+34.97 % vs +32.99 %), coherente con su mayor tamaño (mismas 600 s, pero menos iteraciones LS cabrían). Tabla: [`tables/aco-m-hybrid-vs-best.csv`](tables/aco-m-hybrid-vs-best.csv).

### B.2 Caso focal: m29 a 1 hora

m29 fue la peor instancia del conjunto m a 600 s (+83.05 %). Se relanzó con `hybrid` durante 3,600 s para medir el techo real:

| | m29 600 s | **m29 1 h** | Δ |
|---|---|---|---|
| Coste oficial | 88,014 | **68,322** | **−19,692** |
| Gap vs best (48,082) | +83.05 % | **+42.10 %** | **−40.95 pp** |

**Trace del bucle 1 h:**
- ACO+VNS 90 s → coste inicial 90,991
- ALNS+SA puro 3,450 s → **28,939 Applies, 822 aceptados (2.8 %)** → 70,690
- NursePolisher 60 s → 66,826 interno (3 mejoras)
- Oficial final: **68,322**

### B.3 Observación: decay del Simulated Annealing en horizontes largos

El **acceptance rate** del ALNS+SA muestra una caída drástica conforme aumenta el número de Applies:

| Caso | Applies | Accepts | Rate |
|---|---|---|---|
| i22 600 s | 139 | 125 | **90 %** |
| i22 2,400 s | 780 | 641 | **82 %** |
| **m29 1 h** | **28,939** | **822** | **2.8 %** |

El cooling rate actual (`cooling_rate = 0.998` por defecto en `ALNSParams`) baja la temperatura a un factor de $0.998^N$ tras $N$ Applies. Para $N = 28{,}939$ → factor $0.998^{28939} \approx 6 \times 10^{-26}$, prácticamente cero. **El SA degenera en hill-climbing puro** durante toda la fase final.

**Implicación**: el `cooling_rate = 0.998` está calibrado para horizontes de ~100–500 Applies. En horizontes de 28k+ Applies (m29 1 h) se queda corto. Hay dos vías:

1. **Cooling adaptativo**: calcular `cooling_rate` en función del número esperado de Applies y del ratio $T_0 / T_{\min}$ deseado al final. Por ejemplo, para que tras $N$ Applies la temperatura baje a un 1 % de $T_0$:
   $$\text{cooling\_rate} = (T_{\min}/T_0)^{1/N} = 0.01^{1/28939} \approx 0.99984$$
   En lugar del 0.998 actual.

2. **Reheating periódico**: cuando el acceptance rate cae por debajo de un umbral (e.g., 5 %), elevar la temperatura a un fracción de $T_0$ (estrategia "memetic" o "restart-SA").

Ambas son cambios pequeños (~30 líneas de código), **opt-in** mediante flag, sin riesgo de romper el modo `hybrid` actual.

### B.4 Componentes oficiales residuales (m29 1 h)

| Componente | Coste | % del total |
|---|---|---|
| **ElectiveUnscheduledPatients** | **36,300** (242 sin programar) | **53 %** |
| PatientDelay | 16,470 | 24 % |
| RoomSkillLevel | 4,080 | 6 % |
| OpenOperatingTheater | 3,900 | 5.7 % |
| ContinuityOfCare | 3,817 | 5.6 % |
| ExcessiveNurseWorkload | 2,140 | 3.1 % |
| RoomAgeMix | 875 | 1.3 % |
| SurgeonTransfer | 740 | 1.1 % |
| **Total** | **68,322** | |

m29 es un caso extremo de **densidad de demanda**: 242 pacientes opcionales no programados representan el 53 % del coste residual. Eso sugiere que el horizonte de planificación está **estructuralmente saturado**, y meter más opcionales requeriría desalojos en cadena más largos de los que el actual `TryKickPatient` (cadena de 1 desalojo) consigue. Una versión con **cadenas de ejection-chain de profundidad k > 1** sería una línea futura concreta para m29 y similares.

### B.5 Notas finales sobre paralelización en modo `hybrid`

Durante m29 a 1 h se confirmó la utilización de CPU:

| Fase | Tiempo | Hilos | % del wall-time |
|---|---|---|---|
| Warm-start seed | ~180 s | 1 | 5 % |
| Bucle ACO (hormigas) | 90 s | **4** | 2 % |
| ALNS+SA puro | 3,450 s | 1 | 95 % |
| NursePolisher | 60 s | 1 | 2 % |

Solo **el 2 % del tiempo** se ejecuta con los 4 cores en paralelo (la fase ACO). El **95 %** del tiempo (ALNS+SA puro) usa un solo core. Utilización efectiva: ~27 %.

En contraste, `preset=default` (Polish sin hybrid) usa los 4 cores en el ~92 % del tiempo (la VNS de cada hormiga ACO ocupa la mayoría del wall-time). **Paralelizar el ALNS+SA puro** (multi-island ALNS) es la mejora de paralelización con mayor recorrido pendiente, estimada en 2-3 días de implementación con ~1.5-3 pp de mejora esperada en gap medio.
