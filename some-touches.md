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

## Fase 2 — Anti-convergencia ACO (pendiente)

### 2.1 Diagnóstico previo

Síntomas observados de convergencia prematura:
- i19 e i23 son atractores locales **consistentes** en todos los regímenes probados (postfix, ABC, ABCF, 1800 s).
- Las hormigas tempranas tras `SeedPheromones` clonan el seed casi exactamente debido a `q0=0.90` + `τ_max` en seed.
- El reset por estancamiento es **brusco** (vuelta a τ_init uniforme), perdiendo todo el aprendizaje.

### 2.2 Cambios propuestos

| # | Cambio | Justificación |
|---|---|---|
| 2.1 | `q0` decreciente: arrancar en 0.90 y bajar linealmente a 0.70 conforme avanza el tiempo | Más exploración temprana, exploitation final |
| 2.2 | `τ_min = τ_max / (n × avg_branching)` con avg_branching estimado de (días × habs feasibles) | Más contraste, evita uniformidad |
| 2.3 | Reset suave: en stagnation, multiplicar τ por 0.5 en vez de resetear a τ_init | Preserva aprendizaje parcial |
| 2.4 | Eliminar τ_max en seed; usar 3× τ_min en su lugar | Reduce dominancia del seed en las primeras iteraciones |

(Diseño detallado tras Fase 1.)

---

## Fase 3 — ACO rápido + ALNS+SA dedicado (pendiente)

### 3.1 Diagnóstico previo

Actualmente cada hormiga ACO ejecuta una VNS completa. En 600 s caben ~30 iteraciones de hormigas. La mayoría del tiempo se gasta en VNS, no en aprendizaje de feromonas.

### 3.2 Cambios propuestos

- Reducir el tiempo del bucle ACO a **60-120 s** (3-5 iteraciones, 4 hormigas paralelas).
- Dedicar **el resto** (480-540 s) a ALNS+SA puro sobre `best_solution` encontrada:
  - Temperatura SA recalibrada con el coste real de `best_solution`.
  - Más iteraciones de destroy/repair (~300 en vez de ~30).
  - Sin VNS entre Apply()s (acelera el bucle).

Modo CLI: nuevo argumento `mode=hybrid` que activa este esquema.

(Diseño detallado tras Fase 1 y 2.)
