# Implementación del Algoritmo de Colonia de Hormigas (ACO) — IHTC 2024
**Autor:** Alberto Hernández  
**Fecha:** 2026-04-29  
**Rama git:** `ACO`

---

## 1. Motivación

El solver anterior generaba soluciones iniciales de forma aleatoria con heurística greedy (`RandomGenerator`) y luego las mejoraba con VNS+ILS. Cada reinicio en el multi-start empezaba desde cero, sin aprovechar el conocimiento de las iteraciones anteriores.

El objetivo de este módulo es sustituir esa generación aleatoria por un **Algoritmo de Colonia de Hormigas (ACO)** que aprenda, a lo largo del tiempo, qué asignaciones de día y habitación tienden a producir mejores soluciones, guiando así la construcción de cada nueva solución.

---

## 2. Idea central del ACO

En ACO, un grupo de **hormigas artificiales** construye soluciones de forma probabilística. Después de cada iteración, las hormigas que encontraron buenas soluciones depositan **feromona** en las decisiones que tomaron. Esa feromona actúa como memoria colectiva: en iteraciones futuras, otras hormigas tienden a repetir las mismas decisiones (explotar lo aprendido), aunque siempre con algo de aleatoriedad (explorar nuevas posibilidades).

Formalmente, la probabilidad de que una hormiga elija la opción `j` para el paciente `p` es proporcional a:

```
score(p, j) = τ(p, j)^α × η(p, j)^β
```

donde:
- `τ(p, j)` — **feromona**: cuánto conocimiento acumulado indica que `j` es buena para `p`
- `η(p, j)` — **heurística**: información estática del problema (p. ej., evitar días tardíos)
- `α`, `β` — exponentes que controlan el peso relativo de feromona vs heurística

---

## 3. Adaptación al problema IHTC 2024

### 3.1 Componentes de solución (qué aprende la hormiga)

Para el IHTC 2024, cada paciente necesita una asignación de **(día, habitación, quirófano)**. El quirófano se elige de forma greedy (se prefiere concentrar cirugías en los quirófanos ya en uso, para minimizar el coste `open_ot`). El ACO aprende sobre las dos decisiones principales:

| Matriz de feromona | Índice | Significado |
|---|---|---|
| `tau_day[P][D]` | `[paciente][día]` | Qué tan bueno es operar al paciente `p` el día `d` |
| `tau_room[P][R]` | `[paciente][habitación]` | Qué tan buena es la habitación `r` para el paciente `p` |

Ambas matrices se almacenan como vectores 1D aplanados: `tau_day[pid * num_days + day]`. Esto sigue el patrón Data-Oriented ya establecido en `Solution.h`.

**Tamaño de las matrices** (instancias grandes con ~500 pacientes, 21 días, 100 habitaciones):
- `tau_day`: 500 × 21 = 10.500 doubles → 84 KB
- `tau_room`: 500 × 100 = 50.000 doubles → 400 KB

### 3.2 Heurística η (conocimiento estático del problema)

La heurística se precomputa **una sola vez** antes del bucle ACO y no cambia durante la ejecución:

- **`eta_day[p][d]`**: favorece días tempranos para minimizar el coste de retraso (`patient_delay`).  
  Fórmula: `1 / (1 + delay_days × w_patient_delay)`.  
  Para días dentro del plazo (`d ≤ due_day`): delay = 0 → η = 1.0.  
  Para días tardíos: η decrece suavemente.

- **`eta_room[p][r]`**: distingue solo entre habitaciones compatibles (η = 1.0) e incompatibles con el paciente (η = 0.0), aplicando la restricción dura HC7.

### 3.3 Regla de selección (ACS pseudoproporcional)

Para cada decisión, la hormiga usa la **regla pseudoproporcional del Ant Colony System**:

- Con probabilidad `q0`: elige la opción con mayor score (argmax) → **explotación**
- Con probabilidad `1-q0`: elige por ruleta proporcional a los scores → **exploración**

Con `q0 = 0.90`, el 90% del tiempo la hormiga explota el mejor conocimiento disponible, y el 10% explora opciones alternativas.

---

## 4. Variante MMAS (Max-Min Ant System)

Se implementa la variante **MMAS**, la más robusta para problemas con alta varianza entre instancias como el IHTC:

1. **Solo la mejor solución deposita feromona** (no todas las hormigas).
2. **Cotas `[τ_min, τ_max]`** sobre todas las feromonas, que evitan que ninguna decisión quede bloqueada permanentemente ni domine completamente.
3. **Reinicialización automática** si no se mejora la mejor solución global en `stagnation_k` iteraciones consecutivas.

### Fórmulas de actualización

```
τ_max = 1 / (ρ × C_best)          [donde C_best es el mejor coste conocido]
τ_min = τ_max / (2 × num_pacientes)

Evaporación (todos):   τ(p,j) ← (1 - ρ) × τ(p,j)
Depósito (mejor sol.): τ(p,d) += 1/C_best   [para cada paciente p → día d asignado]
                        τ(p,r) += 1/C_best   [para cada paciente p → habitación r asignada]
Cotas:                 τ(p,j) ← clamp(τ(p,j), τ_min, τ_max)
```

Con el tiempo, los componentes de la mejor solución convergen a `τ_max` y los no usados a `τ_min`, creando un contraste fuerte que guía la construcción.

---

## 5. Construcción de una solución (paso a paso)

Cada hormiga ejecuta `ACOSolver::ConstructSolution` y produce una solución completa: pacientes asignados a `(habitación, día, quirófano)` y enfermeras a `(habitación, día, turno)`. Se descompone en cinco etapas:

### 5.1 Ordenación de pacientes (estructura el orden de toma de decisión)

Antes de empezar a colocar pacientes se decide en qué orden tratarlos, porque un orden malo (e.g. opcionales antes que obligatorios) dispara la fase de reparación y degrada la solución:

- **Obligatorios → ordenados por *slack* ascendente.** El slack se define como `due_day − release_day`. Los pacientes con ventana más estrecha (slack pequeño) se intentan primero, cuando los recursos —camas, quirófanos, cirujanos— aún están libres. Esta ordenación es determinista: igual semilla → igual orden.

- **Opcionales → orden aleatorio.** Se barajan en cada hormiga con `std::shuffle(rng)`. Como su asignación es opcional (su no-asignación se paga con `unscheduled_optional`), introducir aleatoriedad evita sesgos sistemáticos y aporta diversidad entre hormigas.

### 5.2 Decisión del día — regla ACS pseudoproporcional sobre `tau_day × eta_day`

Para cada paciente `p` se construye el conjunto de días candidatos `D_p`:

1. Filtrar a `[release_day, due_day]` para obligatorios; `[release_day, num_days−1]` para opcionales.
2. Descartar días sin ningún quirófano abierto (`OperatingTheater::IsOpenOnDay(d)`).
3. Para cada día `d ∈ D_p` calcular el *score*:

```
score_day(p, d) = τ_day[p][d]^α × η_day[p][d]^β
```

con `α = 1.0` y `β = 2.0`. La feromona `τ_day` representa "cuántas veces se ha demostrado bueno operar `p` el día `d`" y la heurística `η_day = 1/(1 + delay·w_delay)` favorece días tempranos para evitar el coste blando `patient_delay`.

4. Aplicar la regla **pseudoproporcional ACS**:

```
si  rand() < q0 :  d* = argmax_d score_day(p, d)        // explotación (90%)
si  rand() ≥ q0 :  d* ~ Categorical(score_day / Σ)      // ruleta proporcional (10%)
```

Con `q0 = 0.90`, la hormiga sigue mayoritariamente el "mejor día" aprendido por la colonia, pero el 10% de las veces explora alternativas. Si `D_p` está vacío (p. ej. todos los días infactibles), el paciente queda sin programar y se delega a la fase de reparación.

### 5.3 Decisión de la habitación — regla ACS sobre `tau_room × eta_room`

Análoga al paso 5.2, pero sobre habitaciones:

1. Filtrar `R_p` a habitaciones compatibles (HC7): `Patient::IsCompatibleWithRoom(r)`.
2. Para cada `r ∈ R_p`: `score_room(p, r) = τ_room[p][r]^α × η_room[p][r]^β`. La heurística `η_room` es binaria `{0, 1}` (compatible o no), así que la decisión la dirige fundamentalmente la feromona aprendida.
3. Aplicar regla ACS pseudoproporcional → habitación elegida `r*`.

> Importante: día y habitación se eligen por **separado**. La factorización `τ_day + τ_room` aprende dos distribuciones marginales en lugar de una conjunta `τ[p][d×r]`. Es un compromiso explícito: menos parámetros = aprendizaje más rápido y robusto, a costa de no capturar correlaciones día-habitación. Para IHTC esto funciona bien porque las correlaciones fuertes están más entre `paciente-cirujano` y `paciente-quirófano` que entre `paciente-(día,habitación)`.

### 5.4 Elección del quirófano — greedy por carga descendente

Aquí no hay feromona. Para `(p, d*, r*)` ya elegidos, se construye la lista de quirófanos abiertos el día `d*` y se ordena por carga actual de mayor a menor (los ya en uso tienen prioridad). Razones:

- Concentrar cirugías minimiza el coste blando `open_ot` (cada quirófano que se abre paga 30·`w_open_ot`).
- HC13 (capacidad de quirófano) y HC12 (carga del cirujano) se chequean en `IsFeasiblePatientAssignment`, así que la concentración nunca puede romper restricciones duras.
- Añadir una tercera matriz `τ_ot` daría poco valor: típicamente hay 2–5 quirófanos por instancia en la serie `i`, decisión casi determinista.

Se prueban los OTs en ese orden hasta encontrar el primero que satisface todas las restricciones duras. Si ninguno la satisface, se entra en el fallback (5.5).

### 5.5 Cascada de fallback (3 niveles)

Si la primera elección `(d*, r*, OT_greedy)` falla por factibilidad:

| Nivel | Estrategia | Coste |
|---|---|---|
| **1** | Mantener `d*`, probar otras habitaciones de `R_p` en orden barajado | O(|R_p| · |OTs|) |
| **2** | Probar otros días de `D_p` ordenados por score descendente, con todas las habitaciones | O(|D_p| · |R_p| · |OTs|) |
| **3** (solo obligatorios) | `TryAssignPatientFeasibly` (orden aleatorio en la ventana) → si falla, `ForceAssignMandatory` (desaloja pacientes bloqueantes) | reutilizado de `RandomGenerator` |

Para opcionales solo hay nivel 1 y 2; si todo falla, el paciente queda sin programar (coste blando aceptable).

### 5.6 Asignación de enfermeras (greedy, idéntica al RandomGenerator)

Tras colocar TODOS los pacientes, se llama `RandomGenerator::GenerateNurseAssignments(sol, problem, rng)`:

- Para cada `(habitación r, día d)` con ocupación > 0 (paciente o ocupante fijo) y para cada turno `s`:
  - Conjunto candidato = `{n : n trabaja en (d,s) AND no hay ya nurse en (r,d,s)}` (HC9, HC10).
  - Score por candidata: penalización `+100` por cada paciente cuyo `skill_required` excede su `skill_level`, penalización lineal por exceso de `workload`, bonificación `−50` si es la nurse del día anterior (continuidad).
  - Se elige la nurse con menor score y se asigna.

Si nadie satisface HC9+HC10 (turno sin nurses trabajando), la celda queda descubierta y será cubierta más adelante por `EnsureFullNurseCoverage` durante la VNS o el final del solver.

### Verificación incremental

Cada intento de asignación usa `FeasibilityChecker::IsFeasiblePatientAssignment`, que consulta los cachés delta de `Solution` (room_occupancy, ot_load, surgeon_load, room_gender) en O(1). Esto hace que la construcción completa sea aproximadamente O(P · D · R), no O(P · evaluación_completa).

---

## 6. Mejora con VNS — qué operador, en qué orden, y por qué

Tras la construcción ACO, la solución pasa a `LocalSearch::Run`, que aplica una **VNS de 8 vecindarios + ILS**. El usuario pidió aclarar: *qué criterio elige un operador, si se aplican aislados, y cómo encaja todo*.

### 6.1 Los 8 vecindarios disponibles

| # | Operador | Modifica | Espacio de movimientos |
|---|---|---|---|
| 0 | `ChangeRoom` | habitación de un paciente | mismo (día, OT), nueva habitación compatible |
| 1 | `ChangeDay` | día de admisión | misma (habitación, OT), nuevo día en la ventana |
| 2 | `ChangeOT` | quirófano | misma (habitación, día), nuevo OT abierto |
| 3 | `Relocate` | (día, habitación, OT) en bloque | reubicación compuesta — al menos 2 atributos cambian |
| 4 | `SwapRooms` | habitaciones entre dos pacientes | requiere compatibilidad cruzada |
| 5 | `SwapDays` | días entre dos pacientes | ambos respetan ventanas |
| 6 | `ToggleOpt` | desprograma o reprograma un opcional | activa/desactiva pacientes opcionales |
| 7 | `ChangeNurse` | enfermera en un (habitación, día, turno) | sin tocar pacientes |

Los vecindarios 0–6 mueven pacientes; el 7 solo mueve enfermeras. Cada operador se implementa como una función `bool TryX(Solution&, int& cost, std::mt19937&)` que devuelve `true` si encuentra una mejora estricta.

### 6.2 Estrategia *first-improvement* dentro de cada operador

Cada `TryX` recorre `min(60, |pacientes_programados|)` pacientes en orden barajado. Para el primer paciente con un movimiento estricto-mejorante, lo aplica y retorna `true` inmediatamente — **no busca el mejor movimiento**, solo el primero que reduzca coste. Razones:

- VNS clásica usa first-improvement para evitar trampas de mejor-mejorante (que tiende a hacer movimientos grandes y converger a malos óptimos).
- Es mucho más rápido: O(promedio) en lugar de O(neighborhood completo) por iteración.

### 6.3 Selección del operador — barajado, no rotación fija

```cpp
std::vector<int> order(active.size());
std::iota(order.begin(), order.end(), 0);
// ...
while (ls_improved && tiempo_restante) {
  std::shuffle(order.begin(), order.end(), rng);  // ← orden NUEVO cada iteración
  for (int local_idx : order) {
    if (active[local_idx].second(solution, current_cost, rng)) {
      ls_improved = true;
      break;  // first-improvement entre operadores
    }
  }
}
```

**Criterio de elección:** orden aleatorio en cada iteración. **Aplicación: aislada.** En cada iteración LS:

1. Se baraja la lista de operadores.
2. Se ejecuta el primer operador del orden actual. Si encuentra mejora → se aplica, se rompe el bucle (`break`), comienza una nueva iteración con un orden diferente.
3. Si NO encuentra mejora → se prueba el siguiente operador del orden.
4. Si **ningún** operador del orden encuentra mejora en esa iteración → fin de la fase de búsqueda local (óptimo local alcanzado para esta combinación).

Por tanto **en una iteración solo se aplica un operador**. No hay composición. La diversificación viene de:
- El orden aleatorio (un operador puede tener turno antes en una iteración y después en la siguiente).
- El propio barajado interno de cada operador sobre los pacientes a probar.

Este patrón es el VNS de "vecindarios alternados con orden aleatorio", una variante de la VND (Variable Neighborhood Descent) que evita el sesgo del orden fijo.

### 6.4 Fase ILS (Iterated Local Search) — escapar del óptimo local

Cuando ningún operador mejora (óptimo local), el LS no se rinde. Aplica una **perturbación**:

```
1. Restaurar solución a best_solution conocida
2. Perturb(strength=4):  reubicar 4 pacientes aleatorios a posiciones factibles aleatorias
3. EnsureFullNurseCoverage  (los movimientos pueden haber descubierto celdas)
4. Re-evaluar coste
5. Volver a la fase LS con esta solución perturbada
```

Se permiten hasta `kMaxPerturbations = 15` perturbaciones. La perturbación es **destructiva** (no first-improvement): mueve pacientes sin importar si empeora el coste, justamente para sacar a la solución del valle del óptimo local.

### 6.5 Bucle externo completo

```
EnsureFullNurseCoverage(solution)               # cubrir celdas tras construcción ACO
current_cost = Evaluate(solution)
best_solution = solution; best_cost = current_cost
perturbation_count = 0

while (iter < max_iter AND perturbation_count ≤ 15 AND tiempo_restante):
    # ─── Fase LS (búsqueda local) ───
    repeat:
        shuffle(order)
        for local_idx in order:
            if operator[local_idx].apply(solution, current_cost):
                EnsureFullNurseCoverage(solution)        # rellenar celdas
                current_cost = Evaluate(solution)         # re-evaluar honesto
                ls_improved = true; break
        else:
            ls_improved = false
    until not ls_improved

    if current_cost < best_cost:
        best_solution = solution; best_cost = current_cost

    # ─── Fase ILS (perturbación) ───
    if perturbation_count >= 15: break
    perturbation_count++
    solution = best_solution
    Perturb(solution, strength=4)
    EnsureFullNurseCoverage(solution)
    current_cost = Evaluate(solution)

return best_solution
```

### 6.6 Resumen del rol de cada operador (datos del ablation test)

| Operador | LOO Δ% | Rol |
|---|---|---|
| `SwapDays` | +3.4% | Diversificador de día sin desplazar pacientes (intercambio = neutral en delay total) |
| `Relocate` | +2.3% | Movimiento compuesto, escapa óptimos locales pequeños |
| `ChangeOT` | +2.2% | Reduce `open_ot` y `surgeon_transfer` |
| `ChangeDay` | +1.9% | Mejora `patient_delay`, `surgeon_overtime`, `room_capacity` |
| `ChangeRoom` | +1.9% | Mejora gender mix, age mix, room capacity |
| `ToggleOpt` | +1.9% | Habilitador: programa/desprograma opcionales para liberar/usar capacidad |
| `SwapRooms` | +1.9% | Intercambia roomings sin tocar día (útil tras compatibilizar género) |
| `ChangeNurse` | −0.9% | Operador débil: cuando el VNS está cerca del óptimo, raramente mejora |

(Datos sobre 6 instancias × 3 seeds × 3 restarts = 162 corridas; ver `history/project-history.md` §4.3.)

---

## 7. Pipeline completo (ACOSolver::Run)

```
Inicializar τ_day, τ_room con tau_init en posiciones feasibles, 0 en infeasibles
Precomputar η_day, η_room (una sola vez)

Mientras quede tiempo (> 2s):
    Para cada hormiga k en 1..n_ants:
        1. sol_k = ConstructSolution(τ, η)       ← construcción ACO (5.1–5.6)
        2. sol_k = LocalSearch::Run(sol_k, ...)   ← VNS+ILS (6)
        3. Actualizar mejor_iteración si cost(sol_k) < mejor_iteración
        4. Actualizar mejor_global si cost(sol_k) < mejor_global AND factible(sol_k)

    Actualizar τ con la mejor solución (mejor_global si existe, si no mejor_iteración)
    Si no hay mejora global en stagnation_k iteraciones: reinicializar τ
```

Cada hormiga recibe un presupuesto de tiempo igual a `tiempo_restante / (n_ants + 1)` para su VNS, repartiéndolo equitativamente entre las hormigas de la iteración.

---

## 8. Ficheros creados/modificados

### Ficheros nuevos

| Fichero | Descripción |
|---|---|
| [src/solver/ACOSolver.h](../src/solver/ACOSolver.h) | Header: `ACOParams` struct + declaración de `ACOSolver` |
| [src/solver/ACOSolver.cpp](../src/solver/ACOSolver.cpp) | Implementación completa (~250 líneas) |

### Ficheros modificados

| Fichero | Cambio |
|---|---|
| [src/solver/RandomGenerator.h](../src/solver/RandomGenerator.h) | `TryAssignPatientFeasibly` y `ForceAssignMandatory` pasados de `private` a `public` para que ACO pueda usarlos en la reparación de obligatorios |
| [src/main.cpp](../src/main.cpp) | Añadido 6º argumento `mode` ("aco" o "random"). Modo ACO llama a `ACOSolver::Run`; modo random mantiene el bucle multi-start original |
| [CMakeLists.txt](../CMakeLists.txt) | Añadido `src/solver/ACOSolver.cpp` a `SOLVER_SOURCES` |

---

## 9. Parámetros por defecto y cómo usarlo

```cpp
struct ACOParams {
  int    n_ants       = 5;    // hormigas por iteración
  double alpha        = 1.0;  // peso de la feromona
  double beta         = 2.0;  // peso de la heurística
  double rho          = 0.10; // tasa de evaporación
  double q0           = 0.90; // prob. explotación vs exploración
  double tau_init     = 1.0;  // feromona inicial
  int    stagnation_k = 15;   // iters sin mejora para reinicializar
};
```

**Uso desde línea de comandos:**

```bash
# Modo ACO (por defecto, 600s)
./build/ihtc_solver data/i01.json 42 5000 100 600 aco

# Modo random (comportamiento original)
./build/ihtc_solver data/i01.json 42 5000 100 600 random

# Si no se especifica modo, usa ACO
./build/ihtc_solver data/i01.json
```

---

## 10. Resultados de validación inicial

Comparación en instancias pequeñas con **30 segundos** de tiempo (misma semilla):

| Instancia | Modo random | Modo ACO | Mejora |
|---|---|---|---|
| test01 | 2339 | 2109 | **−9.8%** |
| i01 (60s) | 3787 | 3537 | **−6.6%** |

Ambas soluciones son **100% factibles** (0 violaciones de restricciones duras).

---

## 11. Notas técnicas y decisiones de diseño

### Por qué factorizar día y habitación por separado

Usar una matriz conjunta `τ[P][D×R]` capturaría correlaciones entre día y habitación, pero tendría tamaño P×D×R ≈ 1M doubles (8 MB), y el aprendizaje sería mucho más lento al dispersarse la feromona. La factorización `τ_day + τ_room` tiene 60K doubles en total y aprende de forma más robusta.

### Por qué MMAS y no AS básico

El IHTC tiene alta varianza inter-instancia (lo que sirve para i04 no sirve para i26). MMAS con sus cotas `[τ_min, τ_max]` impide que el algoritmo converja prematuramente a una solución de mala calidad, y la reinicialización por estancamiento permite explorar nuevas regiones del espacio de búsqueda.

### Por qué los OTs se eligen de forma greedy y no con feromona

Los quirófanos tienen muy pocas opciones por día (2-5 en las instancias benchmark) y su asignación está fuertemente determinada por la capacidad disponible. Añadir una tercera matriz `τ_ot` añadiría complejidad con poco beneficio esperado dado el pequeño espacio de decisión.

### Reparación de obligatorios

Si ACO no puede colocar un paciente obligatorio en ninguna posición factible (ocurre en instancias muy restringidas como i16, i20), se activa el mecanismo de reparación heredado de `RandomGenerator`: primero `TryAssignPatientFeasibly` (prueba todos los días de la ventana aleatoriamente) y si falla, `ForceAssignMandatory` (desaloja pacientes que bloquean y los reubica).

### Compatibilidad retrocompatible

El modo "random" en `main.cpp` reproduce exactamente el comportamiento anterior. Todos los binarios existentes (`ihtc_ablation`, `ihtc_eval`) no se ven afectados. El ablation test sigue funcionando igual.

---

## 12. Líneas futuras sobre este módulo

- [ ] Tuning de parámetros ACO (α, β, ρ, q0, n_ants) con los resultados del ablation existente
- [ ] Añadir `τ_ot` para instancias con muchos quirófanos (m-series)
- [ ] Explorar MMAS con actualización de la mejor global vs. mejor de iteración (actualmente alterna)
- [x] ~~Benchmark completo i01–i30 comparando ACO vs random con 600s~~ → ver `history/project-history.md` §8

---

## 13. Mejoras prioritarias propuestas (post-benchmark 600 s)

Tras el benchmark completo i01–i30 con 600 s/instancia, ACO+VNS gana 26/30 a Random+VNS pero solo por un **3.17% agregado**, y ambas variantes quedan ~57% por encima de las soluciones oficiales. Esto revela un patrón claro:

> **El bottleneck NO es el tiempo de la VNS, son las pocas iteraciones de ACO.** Con `n_ants=5` y VNS larga (~50 s/hormiga) en 600 s el bucle externo solo completa 3–5 iteraciones — la feromona apenas alcanza a aprender. Las tres mejoras siguientes atacan exactamente esa raíz, ordenadas por ratio impacto/coste.

### 13.1 Warm-start desde una solución random+VNS (mejora prioritaria)

**Idea.** En vez de inicializar la feromona uniforme (`τ_init = 1.0` en todas las posiciones factibles), gastar los primeros ~30 s en producir una solución decente con `RandomGenerator::Generate + LocalSearch::Run` y depositar feromona elevada sobre las decisiones de **esa** solución. La colonia parte así de un punto del espacio de búsqueda donde la VNS ya demostró que se puede llegar a calidad razonable, y el aprendizaje arranca informado en lugar de desde ruido.

**Por qué encaja con los datos.** En las instancias donde Random+VNS le gana a ACO+VNS (i04, i08, i11, i23) la VNS aterrizó en una cuenca de atracción mejor que la que sugería la feromona uniforme. Warm-start convierte ese hallazgo en sesgo inicial para todas las hormigas posteriores.

**Cómo plantearlo en el código.**

1. **Sustituir** la inicialización actual:
   ```cpp
   // ACTUAL (ACOSolver::Run, líneas 51–53)
   PheromoneMatrix tau_day(num_patients * num_days,  0.0);
   PheromoneMatrix tau_room(num_patients * num_rooms, 0.0);
   InitPheromones(tau_day, tau_room, problem, params.tau_init);
   ```

2. **Por** una nueva fase preliminar antes del bucle ACO:
   ```cpp
   // PROPUESTA — tras InitPheromones uniforme
   double warm_budget = std::min(30.0, time_limit_s * 0.05);  // 5% o 30s
   Solution seed = RandomGenerator::Generate(problem, rng);
   LocalSearch::Run(seed, max_ls_iter, rng, warm_budget);
   int seed_cost = Evaluator::Evaluate(seed);

   if (FeasibilityChecker::Check(seed).feasible) {
     // sembrar la feromona inicial con las decisiones de seed
     SeedPheromones(tau_day, tau_room, seed, problem, params, seed_cost);
     // arrancar best_solution global con seed
     best_solution = seed;
     best_cost     = seed_cost;
   }
   ```

3. **Implementar** `SeedPheromones` (función nueva, ~20 líneas). Para cada paciente programado en `seed`, depositar una cantidad significativa de feromona — del orden de `τ_max = 1/(ρ·C_seed)` — en sus decisiones reales de día y habitación, manteniendo `τ_init` mínimo en el resto. Las cotas `[τ_min, τ_max]` se aplican igual que en `UpdatePheromones`.

**Detalles de diseño.**

- **No usar la solución de seed como hormiga 0.** Si la usamos como una hormiga más en la primera iteración, su coste tiende a dominar el `iteration_best` y la feromona converge prematuramente. Usarla SOLO para sesgar la inicialización y como primer `best_solution` global.
- **Si `seed` es infactible** (improbable, pero `i16`/`i20` son restrictivas): saltar el seed y caer al esquema actual de `τ_init` uniforme. Defensivo.
- **Presupuesto de warm-up.** 30 s es un 5% de 600 s. Suficiente para que la VNS haga ~10–20 iteraciones de mejora desde una solución aleatoria. No tiene sentido invertir más (rendimientos decrecientes de la VNS sin guía).

**Impacto esperado.** En i04/i08/i23 (donde ACO actualmente pierde 5–18%) el warm-start cierra la mayor parte del gap con random porque la feromona inicial ya recoge las decisiones que random+VNS encontró buenas. Mejora estimada: **3–5% agregado adicional**.

### 13.2 Más hormigas con VNS más corta

**Idea.** Cambiar `n_ants` de 5 a ~12 y reducir `time_per_ant` proporcionalmente (~25 s en vez de ~50 s). En 600 s pasamos de ~3–5 iteraciones externas de ACO a ~10–15 — la feromona ve el triple de actualizaciones y aprende más patrones.

**Trade-off explícito.** Cada hormiga mejora menos su construcción individual (la VNS hace más trabajo en los primeros segundos por rendimientos decrecientes), pero la diversidad de muestras alimenta mejor el aprendizaje colectivo. Es una decisión de **explorar más amplio** vs **explotar más profundo**.

**Cómo plantearlo en el código.**

Solo cambia `ACOParams` por defecto:
```cpp
struct ACOParams {
  int n_ants = 12;          // antes 5
  // resto igual
};
```

Y nada más, porque `ACOSolver::Run` ya reparte el tiempo: `time_per_ant = remaining_s() / (n_ants + 1.0)`. No requiere cambios estructurales.

**Detalle.** Convendría exponer `n_ants` como argumento CLI para sintonizarlo por instancia (las grandes pueden beneficiarse de más hormigas que las pequeñas).

**Impacto esperado.** **2–4% agregado**, con mayor mejora en instancias grandes (i19, i22, i26, i27) donde el estancamiento del aprendizaje es más visible.

### 13.3 Multi-threading (paralelizar las hormigas)

**Idea.** La competición permite 4 hilos; el solver actual usa 1. Lanzar las `n_ants` hormigas en paralelo (4 simultáneas) con feromona compartida. Sin tocar el algoritmo, cuadruplicamos el rendimiento computacional.

**Cómo plantearlo en el código.**

1. **Estructura de datos.** Las matrices `tau_day` y `tau_room` se vuelven datos compartidos protegidos por `std::shared_mutex` (lectura múltiple durante construcción, escritura exclusiva en `UpdatePheromones`).

2. **Bucle principal.** En cada iteración externa:
   ```cpp
   std::vector<std::future<Result>> futures;
   for (int k = 0; k < params.n_ants; ++k) {
     futures.push_back(std::async(std::launch::async, [&, k]() {
       std::mt19937 local_rng(rng() + k);  // semilla derivada por hormiga
       Solution sol = ConstructSolution(/* lectura de tau bajo shared_lock */);
       LocalSearch::Run(sol, max_ls_iter, local_rng, ls_time);
       return Result{sol, Evaluator::Evaluate(sol)};
     }));
   }
   // recolectar resultados, elegir iteration_best/global_best
   // UpdatePheromones bajo unique_lock
   ```

3. **Limitar el pool a 4 hilos** con `std::counting_semaphore` o un pool propio para respetar la regla del concurso aunque `n_ants > 4`.

**Detalles importantes.**

- **Las feromonas son solo de lectura durante la construcción** — las hormigas no modifican `tau_day`/`tau_room` mientras construyen. Así que el `shared_lock` casi nunca contiende. Solo el `UpdatePheromones` al final de cada iteración necesita exclusividad (microsegundos).
- **Determinismo se rompe.** Con threading, dos ejecuciones con la misma semilla pueden dar resultados ligeramente distintos (orden de finalización de hormigas → orden de empates en `iteration_best`). Aceptable porque la mejora estadística supera la pérdida de reproducibilidad bit a bit.
- **No paralelizar la VNS internamente.** La VNS tiene mucho estado mutable y poco paralelismo natural; mejor mantenerla single-threaded por hormiga y paralelizar al nivel de hormigas.

**Impacto esperado.** Multiplicador **3.5–4×** sobre la velocidad efectiva de aprendizaje. Combinado con (13.2): de ~10–15 iteraciones externas a 30–60 en el mismo presupuesto de 600 s. Es la mejora con mayor impacto absoluto.

### 13.4 Combinación recomendada y proyección

Ejecutar las tres juntas (no son excluyentes):

| Configuración | Iteraciones externas en 600 s | Gap medio a best esperado |
|---|---|---|
| Actual (n=5, secuencial, sin warm-start) | 3–5 | +56.4% |
| + Warm-start (13.1) | 3–5 (mismas, pero arrancan informadas) | +52% |
| + Más hormigas (13.2) | 10–15 | +48% |
| + Multi-threading (13.3) | 30–60 | **+35–40%** |

La proyección de +35–40% es heurística (basada en cómo el gap medio escala con el número de iteraciones externas en runs intermedios), pero el orden de magnitud es razonable: con suficientes actualizaciones de feromona, MMAS converge cerca de los óptimos locales encontrados, y la VNS cierra el resto.

### 13.5 Lo que NO recomendaría tocar primero

- **Heurística η** (sec. 3.2): mejorarla teóricamente da impacto, pero requiere calibrar pesos por componente y es fácil empeorar las cosas si están mal sintonizados. Dejar para después de las tres anteriores.
- **`τ_ot`** (sec. 5.4): solo aporta en m-series; en i01–i30 no merece la pena.
- **Backtracking en construcción**: ataca i16/i20 (donde se dispara `ForceAssignMandatory`) pero esas no son las instancias más caras. Coste/beneficio bajo.
