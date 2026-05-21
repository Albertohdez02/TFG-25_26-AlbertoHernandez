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

(Pendiente: completar tras escribir el código.)

### 1.6 Resultados

(Pendiente.)

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
