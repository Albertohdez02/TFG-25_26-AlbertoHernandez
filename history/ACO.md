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
- [x] ~~Implementar warm-start (§13.1), n_ants=12 (§13.2), multi-threading (§13.3) y heurística informada en RandomGenerator~~ → ver §14
- [ ] Re-lanzar benchmark completo i01–i30 con Fases A+B+C+D y 600 s/instancia para regenerar las figuras de `graficas/`

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

---

## 14. Sesión 2026-05-06 — Implementación de las mejoras §13

Implementación incremental de las cuatro mejoras propuestas en §13, validando cada fase con el validador oficial antes de seguir. El plan está en `/home/alberto/.claude/plans/ahora-mismo-tengo-un-swirling-axolotl.md`.

### 14.1 Fase A — Heurística informada en `RandomGenerator::TryAssignOnDay`

**Objetivo.** El `RandomGenerator` se usa en dos contextos: (1) generador del modo `random` y (2) — tras esta sesión — generador del *seed* del warm-start del ACO (Fase B). Si la primera elección de habitación/OT ya fuera "razonablemente buena" en lugar de puramente aleatoria, la VNS partiría de soluciones más cercanas al óptimo y el seed del warm-start tendría más calidad para sembrar la feromona.

**Cambio.** En [src/solver/RandomGenerator.cpp](../src/solver/RandomGenerator.cpp) `TryAssignOnDay`, antes del bucle `for (room) for (ot)`:

1. **Habitaciones** ahora se ordenan por una clave de tres niveles que refleja directamente los costes blandos `room_gender_mix`, `room_mixed_age` y `open_room` (en `Evaluator`):
   - Nivel 1 — `gender_pen`: para cada día de la estancia `[day, day+stay-1]`, +2 si `GetRoomGender(r,d) == -2` (mezcla ya existente, peor opción) y +1 si el género del paciente introduciría mezcla nueva.
   - Nivel 2 — `age_pen`: +1 por cada paciente u ocupante presente en `(r, d)` cuyo `AgeGroup` difiere del paciente (espejo del cálculo en `Evaluator::room_mixed_age`, [src/evaluator/Evaluator.cpp:128-155](../src/evaluator/Evaluator.cpp#L128-L155)).
   - Nivel 3 — `−occupancy`: a igualdad, preferir habitaciones más llenas (concentrar pacientes evita abrir extras).
   - Tras ordenar, se baraja el top-3 con `std::shuffle` para mantener diversidad entre llamadas a `Generate`.
2. **Quirófanos** ahora se ordenan por `GetOTLoad(ot, day)` descendente (preferir los más cargados, igual criterio que ACO en [src/solver/ACOSolver.cpp:297-306](../src/solver/ACOSolver.cpp#L297-L306)). Sin shuffle: el espacio es muy pequeño (2–5 OTs en serie i).

La firma pública no cambia. Solo afecta a las llamadas a `TryAssignOnDay` desde `GeneratePatientAssignments` (fase 1 obligatorios y fase 3 opcionales) y desde `TryAssignPatientFeasibly` (reparación). No introduce cambios en cobertura de enfermeras ni en factibilidad — solo cambia el orden de exploración.

**Validación.** Build limpio. Sanity con `./build/ihtc_solver data/i04.json 42 2000 5 30 random`:
- Coste interno: 920 (vs 585 con 600 s, dentro del rango esperado para 30 s).
- Validador oficial sobre `solutions/i04_solution.json`: **0 violaciones**, coste 4248.

Mini-benchmark formal (i04, i17, i26 con 300 s × semilla 42, modo `aco`) se hará al final de las cuatro fases para no triplicar el coste de cómputo entre cambios pequeños.

**Ficheros**: [src/solver/RandomGenerator.cpp](../src/solver/RandomGenerator.cpp) (función `TryAssignOnDay`).

### 14.2 Fase B — Warm-start de feromonas desde un seed Random+VNS

**Objetivo.** Sembrar la feromona inicial con las decisiones reales de una solución factible producida por `RandomGenerator::Generate + LocalSearch::Run`, para que las hormigas de la primera iteración del bucle ACO ya muestreen un punto del espacio de búsqueda donde la VNS demostró que se puede llegar a calidad razonable.

**Cambio.**

1. **Header** [src/solver/ACOSolver.h](../src/solver/ACOSolver.h): nueva función privada `SeedPheromones(tau_day, tau_room, seed, seed_cost, problem, params)`.

2. **Implementación de `SeedPheromones`** ([src/solver/ACOSolver.cpp](../src/solver/ACOSolver.cpp)): tras `InitPheromones`, recibe una solución factible y:
   - Calcula `tau_max = 1/(rho × seed_cost)` y `tau_min = tau_max/(2 × num_patients)` con la misma fórmula que `UpdatePheromones` para mantener la coherencia MMAS.
   - Pone todas las posiciones feasibles a `tau_min`.
   - Pone los arcs del seed (cada paciente programado → su día y su habitación) a `tau_max`.
   - Las posiciones infeasibles permanecen en 0.0 (siguen sin ser elegibles).

3. **Modificación de `Run`** ([src/solver/ACOSolver.cpp](../src/solver/ACOSolver.cpp), tras `PrecomputeHeuristics`):
   - `warm_budget = min(30 s, 5% × time_limit_s)`.
   - Solo se ejecuta si quedan al menos `warm_budget + 5 s` y el budget es ≥ 1 s.
   - Genera `seed = RandomGenerator::Generate(problem, seed_rng)` con un RNG derivado (`std::mt19937(rng())`) para no agotar el RNG principal.
   - Llama a `LocalSearch::Run(seed, max_ls_iter, seed_rng, warm_budget)` (la VNS ya invoca `EnsureFullNurseCoverage` internamente, así que el seed sale con cobertura completa de enfermeras).
   - Si `FeasibilityChecker::Check(seed).feasible == true` y `seed_cost > 0`:
     - Llama `SeedPheromones(...)`.
     - Inicializa `best_solution = seed` y `best_cost = seed_cost`. Esto sirve como suelo de calidad: todas las hormigas posteriores compiten contra el seed.
   - Si el seed sale infactible (i16/i20 muy restringidas), se cae al esquema `tau_init` uniforme original sin tocar nada (defensivo).

**Detalle de diseño crítico.** El seed NO se inyecta como hormiga 0 de la primera iteración. Si lo hiciéramos, su coste tendería a dominar `iteration_best` y la feromona convergería prematuramente sobre él, perdiendo el efecto exploratorio de la colonia. Solo se usa para sesgar la inicialización y como primer `best_solution` global.

**Impacto sobre el bucle de pheromonas.** Tras Fase B, la primera iteración construye con un contraste τ_max/τ_min ya activo (en lugar de τ_init uniforme), por lo que la regla ACS pseudoproporcional (`q0 = 0.90`) explota desde el inicio las decisiones del seed. Las decisiones distintas siguen siendo elegibles (τ_min > 0) pero con probabilidad reducida.

**Validación.** Build limpio. Sanity con `./build/ihtc_solver data/i04.json 42 2000 5 60 aco`:
- Coste interno: 855 (vs 920 modo `random` 30 s en Fase A; vs 585 modo `aco` 600 s antes de las mejoras — pendiente comparación a iso-tiempo).
- Validador oficial sobre `solutions/i04_solution.json`: **0 violaciones**.

**Ficheros**:
- [src/solver/ACOSolver.h](../src/solver/ACOSolver.h) (declaración `SeedPheromones`).
- [src/solver/ACOSolver.cpp](../src/solver/ACOSolver.cpp) (implementación `SeedPheromones` + bloque warm-start en `Run`).

### 14.3 Fase C — `n_ants` 5 → 12 + override por CLI

**Cambio.**

1. [src/solver/ACOSolver.h](../src/solver/ACOSolver.h): `ACOParams::n_ants` default 5 → **12**. Comentario actualizado.

2. [src/main.cpp](../src/main.cpp):
   - Nuevo argumento CLI opcional, 7º en posición: `n_ants`. Si se pasa con valor positivo, sobrescribe el default de `ACOParams`.
   - Mensaje de uso ampliado: `<instancia.json> [seed] [max_iter] [restarts] [time_s] [mode] [n_ants]`.
   - El log imprime `Hormigas por iteracion: N` para verificar que el override se aplica.

**Por qué subir a 12.** En 600 s con `n_ants = 5`, el bucle externo de ACO completa solo 3–5 iteraciones porque cada hormiga consume ~50 s en VNS. La feromona apenas tiene tiempo de aprender. Con `n_ants = 12` y `time_per_ant ≈ remaining_s() / 13`, cada hormiga corre ~46 s al inicio y va decreciendo a medida que avanza el tiempo, pero el número total de actualizaciones de feromona pasa de 3–5 a 10–15 — 3× más oportunidades de aprendizaje. A iso-tiempo total, la VNS individual hace marginalmente menos trabajo (rendimientos decrecientes en sus últimos segundos) pero el aprendizaje colectivo de la colonia es mucho mejor.

**No tocado.** `ACOSolver::Run` ya reparte el tiempo correctamente con `time_per_ant = remaining_s() / (n_ants + 1.0)` ([src/solver/ACOSolver.cpp:69](../src/solver/ACOSolver.cpp#L69)), así que no requiere ningún cambio estructural.

**Validación.** Build limpio. Sanity con `./build/ihtc_solver data/i04.json 42 2000 5 60 aco`:
- Log muestra `Hormigas por iteracion: 12`.
- Coste interno: 918 (más alto que 855 con `n_ants=5` a 60 s — esperado: a 60 s totales `n_ants=12` da solo ~4.6 s por hormiga, insuficiente para una VNS profunda).
- Validador oficial: **0 violaciones**.

> A 60 s, `n_ants=5` saca más rendimiento porque cada hormiga tiene ~12 s para una VNS más completa. La ganancia real de `n_ants=12` se materializa a 600 s (presupuesto de competición), donde 4–5 s/hormiga vs 12 s/hormiga sigue siendo VNS productiva pero la diversidad y las actualizaciones de feromona compensan con creces.

**Ficheros**: [src/solver/ACOSolver.h](../src/solver/ACOSolver.h), [src/main.cpp](../src/main.cpp).

### 14.4 Fase D — Multi-threading: hormigas en paralelo dentro de cada iteración

**Objetivo.** La regla del concurso permite 4 hilos por instancia. Hasta ahora `ACOSolver::Run` era 100 % single-threaded y la pauta de ejecución compensaba lanzando 4 instancias en paralelo con `xargs -P4`. Esto no acelera el aprendizaje del ACO **dentro** de cada instancia. Paralelizando las hormigas a nivel de iteración, cada actualización de feromona se basa en 4× más muestras por unidad de tiempo wall-clock.

**Diseño.** Las matrices `tau_day` y `tau_room` son **solo de lectura** durante `ConstructSolution`. Las únicas escrituras a feromona ocurren en `UpdatePheromones` y `ResetPheromones`, ambas fuera del bloque paralelo. Por tanto no hay condiciones de carrera reales y no se necesitan mutex durante la construcción.

**Implementación.**

1. **`ACOParams::pool_size`** ([src/solver/ACOSolver.h](../src/solver/ACOSolver.h)): nuevo campo, default 4 (regla IHTC). No se expone por CLI; se usa el default por ahora.

2. **`CMakeLists.txt`**: añadido `find_package(Threads REQUIRED)` y `target_link_libraries(ihtc_core PUBLIC Threads::Threads)` para garantizar el linkado de pthread cuando se use `std::thread`.

3. **`ACOSolver.cpp`** (`Run`):
   - Sustituido el bucle `for (int k = 0; k < n_ants; ++k)` por **paralelismo por lotes**: las hormigas se procesan en batches de hasta `pool_size` simultáneas, lanzando un `std::thread` por hormiga del batch y haciendo `join` antes de pasar al siguiente.
   - Cada hormiga recibe su propio `std::mt19937` derivado de una semilla generada con el RNG principal *antes* del paralelo (`ant_seeds[k] = rng()`), así que el RNG principal se consume de forma determinista y cada hormiga es reproducible aisladamente.
   - Cada hormiga construye una `Solution` local, ejecuta su VNS, y deja `(solution, cost)` en `results[k]`.
   - Tras los `join` de todos los batches, **una pasada secuencial** procesa los resultados en orden de hormiga: actualiza `iteration_best` y `best_solution` global. Mantener este orden estable evita una fuente extra de no-determinismo (los empates se resuelven por índice de hormiga, no por orden de finalización del hilo).
   - El reparto de tiempo se ajusta a `time_per_batch = remaining_s() / (num_batches + 1)` con `num_batches = ceil(n_ants / pool_size)`. Con `n_ants=12, pool_size=4` se obtienen 3 batches: cada batch corre ~`remaining/4` segundos en paralelo, así que el "tiempo lineal por hormiga" sigue siendo del mismo orden que single-threaded a `n_ants=5`, pero con 12 hormigas en lugar de 5.

4. **Headers añadidos**: `<thread>`, `<utility>` para `std::move`.

**Trade-off de determinismo.** Con paralelismo, dos ejecuciones con la misma semilla producen las mismas hormigas individuales (porque cada una tiene su sub-RNG derivado de forma determinista del RNG principal), y la recolección secuencial mantiene el orden de empates. La única fuente residual de no-determinismo viene de `LocalSearch::Run` si llamara a alguna primitiva del SO con timing-dependiente, pero por código no parece ser el caso. El usuario ya documentó (§13.3) que la mejora estadística supera la pérdida de reproducibilidad bit a bit.

**Pauta de ejecución del benchmark final.** Pasamos de **4 instancias × 1 hilo en paralelo** (`xargs -P4`) a **1 instancia × 4 hilos secuencial**. El tiempo total de pared se mantiene aproximadamente igual (4 × 600 s × 1 hilo ≈ 1 × 600 s × 4 hilos × 4 instancias) pero ahora cada instancia individual aprovecha el paralelismo permitido por el reglamento.

**Validación.** Build limpio. Sanity con `time ./build/ihtc_solver data/i04.json 42 2000 5 60 aco`:
- `Hormigas por iteracion: 12`
- `real 0m54s, user 3m14s` → speedup efectivo ~3.5× (límite teórico 4×, overhead esperado por VNS de duración variable y join sincrónico al final del batch).
- Coste interno: 788 (mejora respecto a Fase C 918 single-thread con el mismo presupuesto).
- Validador oficial: **0 violaciones**.

**Ficheros**:
- [src/solver/ACOSolver.h](../src/solver/ACOSolver.h) (`pool_size` en `ACOParams`).
- [src/solver/ACOSolver.cpp](../src/solver/ACOSolver.cpp) (paralelización por lotes en `Run`, headers `<thread>` y `<utility>`).
- [CMakeLists.txt](../CMakeLists.txt) (`find_package(Threads)` + linkado a `ihtc_core`).

### 14.5 Mini-benchmark consolidado tras Fases A+B+C+D

Las cuatro mejoras se validan a iso-tiempo (300 s, semilla 42) en tres instancias representativas: `i04` (donde el ACO original perdía contra Random), `i17` y `i26` (instancias grandes donde el ACO ya ganaba pero seguía lejos del óptimo). Cada ejecución usa 4 hilos dentro del solver (Fase D); las instancias se lanzan secuencialmente para no superar el límite de la competición.

**Coste oficial del validador IHTC** — comparativa pre-mejoras (datos de `tables/aco-random-comparison.csv`, ACO con `n_ants=5`, single-thread, sin warm-start) vs post-mejoras (Fases A+B+C+D, `n_ants=12`, pool 4):

| Instancia | ACO pre | ACO post | Δ absoluto | Δ % | Best (oficial) | Gap pre → post |
|---|---:|---:|---:|---:|---:|---:|
| i04 | 4 822 | **4 658** | −164 | −3.4 % | 1 884 | 156 % → 147 % |
| i17 | 69 980 | **65 875** | −4 105 | −5.9 % | 40 535 | 73 % → 63 % |
| i26 | 107 079 | **104 554** | −2 525 | −2.4 % | 64 613 | 66 % → 62 % |
| **Total** | **181 881** | **175 087** | **−6 794** | **−3.7 %** | 107 032 | 70 % → 64 % |

**Validación oficial:** 3/3 instancias **0 violaciones** según `IHTP_Validator`.

**Lectura de los resultados.**

- Mejora consistente en las tres instancias (no hay ningún caso donde post-mejoras empeore).
- La mejora porcentual mayor está en `i17` (−5.9 %), instancia mediana donde el bottleneck antes era el número bajo de iteraciones del bucle externo de ACO. Encaja con la teoría de §13: las Fases C+D multiplican las actualizaciones de feromona y la Fase B siembra el aprendizaje informado desde el inicio.
- En `i04` la mejora es más modesta (−3.4 %). Esta instancia es pequeña y ya saturada por la VNS antes de las mejoras; la Fase A (heurística informada) y la Fase B (warm-start) son las que más aportan aquí.
- El gap agregado contra `best-solutions/` cae de 70 % a 64 % a iso-tiempo de 300 s. Con el presupuesto completo de competición (600 s) y el benchmark `i01–i30` se espera ampliar la mejora porque las Fases C+D se benefician de tiempos más largos (más iteraciones del bucle externo).

**Pendientes para sesiones siguientes.**

1. Lanzar el benchmark completo `i01–i30` con 600 s/instancia (regla del concurso) para producir el CSV equivalente a `tables/aco-random-comparison.csv` y regenerar las figuras de [graficas/](../graficas/).
2. Comparar también modo `random` post-Fase A (la Fase A afecta tanto a `RandomGenerator` como al seed del warm-start). Esperable: el modo `random` también baja un poco su gap por la heurística informada en `TryAssignOnDay`.
3. Si tras el benchmark completo persiste un subconjunto de instancias donde ACO+VNS no se acerca a `best-solutions/`, considerar las propuestas de §13.5 (extensión de `η_room` con penalty de género, `τ_ot`, backtracking en construcción).

### 14.6 Marcado de tareas

Se actualizan los checks de §13: las tres mejoras propuestas están implementadas y validadas en mini-benchmark.

### 14.7 Benchmark completo i01–i30 (600 s) — Fases A+B+C+D

**Setup.** 30 instancias × 600 s × 4 hilos por solver. 5 instancias en paralelo (20 cores disponibles, cada solver consume su cuota de 4). Wall-clock total: ~60 min. Semilla 42. Las soluciones de modo `random` son las mismas que el benchmark §8.4 (`solutions_random/`, sin regenerar) — las soluciones ACO antiguas se preservan en `solutions_aco_v1/` y las nuevas se escriben en `solutions_aco/`. Tabla CSV en [tables/aco-random-comparison-v2.csv](../tables/aco-random-comparison-v2.csv) y figuras en [graficas_v2/](../graficas_v2/).

**Resultados agregados (validador oficial IHTC, 30/30 factibles, 0 violaciones).**

| Métrica | ACO v1 (pre-mejoras) | ACO v2 (post-mejoras) | Random (referencia) | Best (oficial) |
|---|---:|---:|---:|---:|
| Coste agregado i01–i30 | 1 119 248 | **1 080 218** | 1 146 853 | 716 560 |
| Gap medio vs best | +56.4 % | **+52.5 %** | +60.3 % | — |
| Mediana gap vs best | — | **+49.7 %** | +57.3 % | — |
| ACO mejor que Random | 24/30 | **27/30** | — | — |

**Mejora ACO v1 → v2:** −39 030 puntos absolutos (−3.5 %). Mejora del ratio de victoria contra Random de 80 % a 90 %. La mediana del gap baja de ~55 % a ~50 %, indicando que la mejora es consistente en la mitad central de las instancias, no concentrada en outliers.

**Top mejoras absolutas ACO v2 vs Random** (de [tables/aco-random-comparison-v2.csv](../tables/aco-random-comparison-v2.csv)):

| Instancia | ACO v2 | Random | Δ abs | Δ % |
|---|---:|---:|---:|---:|
| i22 | 85 405 | 95 334 | −9 929 | −10.4 % |
| i27 | 97 948 | 106 586 | −8 638 | −8.1 % |
| i17 | 65 765 | 74 330 | −8 565 | −11.5 % |
| i21 | 37 236 | 42 533 | −5 297 | −12.5 % |
| i26 | 103 732 | 107 646 | −3 914 | −3.6 % |
| i28 | 86 910 | 90 065 | −3 155 | −3.5 % |

**Las 3 instancias donde Random sigue ganando a ACO v2** (i04, i08, i09):

| Instancia | ACO v2 | Random | Best | Δ ACO−Random |
|---|---:|---:|---:|---:|
| i04 | 4 658 | 4 590 | 1 884 | +68 (+1.5 %) |
| i08 | 16 877 | 13 199 | 6 291 | +3 678 (+27.9 %) |
| i09 | 10 275 | 10 212 | 6 682 | +63 (+0.6 %) |

`i08` sigue siendo el caso patológico (era el peor en v1 también, con −15.3 %; ahora es −27.9 %). Es probable que el seed de warm-start esté aterrizando en una cuenca de atracción mala para esta instancia y la feromona refuerce esa decisión durante el resto del run; lo investigamos más adelante.

**Lectura general.**

- La hipótesis del bottleneck (§13: pocas iteraciones del bucle externo de ACO) se confirma. Con multi-threading el bucle externo completa ~10–15 actualizaciones de feromona en lugar de 3–5, y eso es lo que mueve el gap.
- El warm-start (Fase B) ayuda especialmente a instancias grandes (i17, i22, i27) donde la feromona uniforme costaba arrancar.
- En instancias pequeñas (i04, i09) la VNS ya era casi óptima; las Fases A+B no consiguen mejorar lo que la búsqueda local ya estaba capturando.

**Líneas siguientes para reducir más el gap.**

1. **Caso i08**: hacer un análisis dirigido. Posibles causas: warm-start con i08 cae en óptimo local malo y el contraste τ_max/τ_min creado lo bloquea. Una mitigación es lanzar varios seeds en warm-start (random restarts) y elegir el mejor.
2. **`τ_ot`** (§5.4 / §13.5) podría ayudar en m-series; se mantiene como pendiente.
3. **Análisis de componentes** en `graficas_v2/fig5_componentes_agregados.png` para identificar qué coste blando concentra el gap a best.

**Ficheros nuevos / preservados en esta sesión.**

| Path | Descripción |
|---|---|
| [solutions_aco_v1/](../solutions_aco_v1/) | 30 soluciones ACO v1 (pre-mejoras) preservadas |
| [solutions_aco/](../solutions_aco/) | 30 soluciones ACO v2 (Fases A+B+C+D, 600 s) |
| [tables/aco-random-comparison-v1.csv](../tables/aco-random-comparison-v1.csv) | CSV del benchmark §8.4 preservado |
| [tables/aco-random-comparison-v2.csv](../tables/aco-random-comparison-v2.csv) | CSV nuevo (post-mejoras) |
| [graficas_v1/](../graficas_v1/) | Figuras anteriores preservadas |
| [graficas_v2/](../graficas_v2/) | Figuras nuevas (5 PNG + comparison_summary.csv) |
| [scripts/plot_comparison_v2.py](../scripts/plot_comparison_v2.py) | Script ajustado: lee `solutions_aco/`, escribe en `graficas_v2/` |
| [logs/comparison_v2/](../logs/comparison_v2/) | Logs por instancia + `_progress.csv` |

---

## 15. Sesión 2026-05-08 — Etapa 1 (Quick wins) — implementada y revertida

Se intentó la Etapa 1 propuesta en [ACO-limitation-research.md §4](../ACO-limitation-research.md): cuatro mejoras "quick wins" sin cambiar la arquitectura, para subir la cobertura de los vecindarios y diversificar la regla ACS. Resultado neto: **regresión sistemática** a iso-tiempo. Etapa revertida tras los experimentos. Los aprendizajes se reportan completos para guiar la siguiente etapa.

### 15.1 Cambios aplicados (en orden)

1. **Límites VNS escalables con √P** en [src/solver/LocalSearch.cpp](../src/solver/LocalSearch.cpp):
   - `GetShuffledScheduled` cap: `max(60, ceil(√P × 5))`. Para P=500 ⇒ 112 vs 60 anterior.
   - `kMaxPairs` (SwapRooms/SwapDays): `min(max(200, 2·n), C(n,2))`.
   - `kMaxPositions` (ChangeNurse): `max(100, ceil(0.3 × |posiciones|))`.
   - `kMaxCombos` (Relocate): `min(60, |days|·|rooms|)`.
2. **Perturbación adaptativa**: `strength = max(4, round(√P × 0.3))`. P=500 ⇒ 7 vs 4.
3. **ToggleOptional completa**: para cada opcional, probar todos los `(d, r, ot)` factibles hasta mejora estricta (no descartar tras el primer intento). Tope global por llamada `min(500, max(100, 5·|opt|))`.
4. **ER-ACO schedule** en [src/solver/ACOSolver.cpp](../src/solver/ACOSolver.cpp): `q0(t) = q0_max − (q0_max − q0_min) × exp(−λ·t/T)` con `q0_min=0.5, q0_max=params.q0=0.95, λ=4`. Exploración temprana (q0 bajo), explotación tardía (q0 alto). Aplicado por iteración del bucle externo, propagado al `ConstructSolution` vía copia local de `ACOParams`.

### 15.2 Resultados — mini-benchmark a 600 s (semilla 42, 4 hilos paralelos)

Comparación contra el ACO v2 (post-Fases A+B+C+D):

| Instancia | v2 (600 s) | Etapa 1 completa | Etapa 1 sin ER-ACO | Etapa 1 sin VNS-scaling |
|---|---:|---:|---:|---:|
| i04 | 4 658 | 4 881 (**+4.8 %**) | 4 815 (+3.4 %) | 5 029 (+8.0 %) |
| i17 | 65 765 | 67 830 (**+3.1 %**) | 67 600 (+2.8 %) | 69 570 (+5.8 %) |
| i26 | 103 732 | 105 853 (**+2.0 %**) | 105 663 (+1.9 %) | 105 345 (+1.6 %) |

Las tres variantes están sistemáticamente **peor** que v2. La regresión está fuera del rango de varianza inter-seed (medido aparte: ±3-6 %, similar al efecto observado).

Test cruzado seed=7 (300 s) para verificar varianza:

| Instancia | seed=42 (300 s) | seed=7 (300 s) | varianza |
|---|---:|---:|---:|
| i04 | 4 876 | 5 175 | ±6.1 % |
| i17 | 69 530 | 66 975 | ±3.7 % |
| i26 | 106 258 | 106 982 | ±0.7 % |

La regresión a 600 s se mantiene incluso considerando varianza.

### 15.3 Análisis de la causa

La hipótesis del plan era que **subir los límites de exploración mejora porque cubre más vecindario**. Lo que muestra el experimento es que en realidad, a iso-tiempo:

- Cada iteración VNS más profunda **gasta más segundos por aceptación**. Con 4 hilos y 12 hormigas, cada batch ACO dura más → menos batches en 600 s → menos actualizaciones de feromona → la colonia aprende menos.
- La perturbación más agresiva **destruye más estructura** que la VNS recupera en su tiempo asignado: la perturbación de strength 7 mueve el doble de pacientes que strength 4, pero la VNS no tiene el doble de tiempo para repararlo.
- ToggleOptional completa hace muchas más evaluaciones por llamada (hasta 500), gasto que no se traduce proporcionalmente en mejoras.
- ER-ACO con `q0_min=0.5` al inicio destruye la calidad de las primeras hormigas: el warm-start ya es muy bueno, las hormigas iniciales con q0 bajo exploran ruleta y no superan al seed → la feromona aprende solo del seed → las hormigas siguientes con q0 ya creciente repiten variantes del seed.

Resumen: **las mejoras de Etapa 1 amplían el espacio explorado pero no aprovechan el presupuesto computacional fijo**. Solo serían beneficiosas con presupuestos mucho mayores (varios miles de segundos), donde la VNS tiene tiempo de explotar el vecindario ampliado.

### 15.4 Decisión

Tras los experimentos, **se revierten todos los cambios** dejando el solver en estado v2 limpio (`git diff` vacío). Las opciones plausibles para la siguiente sesión son:

- **(Recomendado)** Saltar a **Etapa 2** del plan: añadir operadores cluster (`SurgeonConsolidation`, `OTShifting`, `NurseSwap`, `DayBlock`). Estos NO amplían el espacio explorado en regiones ya cubiertas, sino que abren regiones hoy invisibles a los movimientos individuales. Es un cambio **cualitativo** del vecindario, no cuantitativo.
- Re-intentar Etapa 1 con presupuestos mucho mayores (1800 s+) — coste experimental alto y poca relación con la regla del concurso.
- Re-calibrar Etapa 1 con valores mucho más conservadores (e.g., `kMaxPairs=300`, `strength=5`, `q0_min=0.85`, `λ=8`) — mejora marginal esperada, no merece la pena antes de Etapa 2.

### 15.5 Ficheros modificados (todos revertidos)

| Fichero | Estado |
|---|---|
| `src/solver/LocalSearch.cpp` | Revertido (idéntico a v2) |
| `src/solver/ACOSolver.cpp` | Revertido (idéntico a v2) |

Logs y soluciones experimentales preservadas para análisis posterior:

| Path | Descripción |
|---|---|
| `logs/etapa1/`, `logs/etapa1_seed7/`, `logs/etapa1_600s/`, `logs/etapa1_no_eraco/`, `logs/etapa1_partial/` | Logs por experimento |
| `solutions_etapa1/`, `solutions_etapa1_seed7/`, `solutions_etapa1_600s/`, `solutions_etapa1_no_eraco/`, `solutions_etapa1_partial/` | Soluciones generadas |

### 15.6 Implicación para la hoja de ruta

[ACO-limitation-research.md](../ACO-limitation-research.md) §4 estimaba que Etapa 1 daría -8 a -12 puntos de gap. La evidencia empírica refuta esa estimación. Se ha actualizado el documento de research con los hallazgos y la nueva proyección esperada para Etapa 2 (operadores cluster), que ahora es la principal apuesta para acercarse al gap objetivo de Etapa 3 (~22-29 %).

---

## 16. Sesión 2026-05-08 — Etapa 2 (operadores cluster) — abandonada con hallazgo crítico

Se intentó la primera sub-fase de Etapa 2: añadir `TrySurgeonConsolidation` como 9.º operador VNS, refactorizando el bitmask de `uint8_t` a `uint16_t` y los arrays de `8` a `kNumOperators` constexpr. El operador, propuesto en [ACO-limitation-research.md §4 Etapa 2](../ACO-limitation-research.md), busca mover hasta 3 pacientes del mismo cirujano del día más sobrecargado al de mayor slack en bloque atómico. Implementado limpio, compila sin avisos.

### 16.1 Diagnóstico instrumentado

Se añadió un contador atómico `op_total_improvements[kNumOperators]` en `ACOSolver::Run` que agrega las `op_improvements` de todas las hormigas y se imprime al final. Resultado en i17 con 300 s, 12 hormigas, 4 hilos, semilla 42:

```
Mejoras por operador (agregado todas las hormigas):
  ChangeRoom : 855  (23.3 %)
  ChangeDay  : 643  (17.5 %)
  ChangeOT   : 1101 (30.0 %)
  Relocate   : 295  ( 8.0 %)
  SwapRooms  : 321  ( 8.8 %)
  SwapDays   :  76  ( 2.1 %)
  ToggleOpt  : 374  (10.2 %)
  ChangeNurse:   0  ( 0.0 %)
  SurgeonCons:   0  ( 0.0 %)
```

**SurgeonCons no produjo ni una sola mejora** en ~120 ejecuciones de `LocalSearch::Run`. Mini-benchmark a 600 s confirmó regresión sistemática vs v2: i04 +2.2 %, i17 +4.9 %, i26 +2.7 %. La causa de la regresión es doble: (1) el operador, aunque devuelva `false` rápido, gasta tiempo recorriendo cirujanos y (2) ocupa un slot del shuffle order, restando turno a operadores productivos como ChangeOT.

### 16.2 Hallazgo crítico — los operadores cluster son ciegos bajo first-improvement

El experimento revela una limitación fundamental del framework VNS actual que la auditoría de §1.3 no anticipó:

> **Cualquier operador cluster que mueva múltiples pacientes simultáneamente bajo `first-improvement` de coste TOTAL solo puede aceptar movimientos donde la suma agregada baja en términos absolutos.**
>
> **Si moviendo 1 paciente individual ya sería mejora, los operadores individuales (ChangeDay, ChangeOT) ya la habrán hecho** antes de que el operador cluster tenga turno.
>
> **Si la mejora agregada cluster requiere pasar por estados intermedios donde algún componente empeora**, la VNS individual NO la encuentra (porque mover 1 empeora). El operador cluster sí podría encontrarla, pero solo si el saldo TOTAL es positivo. En la práctica, mover 2-3 pacientes del cirujano `s` del día `d_alto` al `d_bajo` reduce `surgeon_overtime` pero suele aumentar `patient_delay`, `room_capacity` o `surgeon_transfer` — el saldo neto rara vez es estrictamente positivo.

Esto explica por qué `SurgeonCons` registra **cero mejoras**: las únicas combinaciones donde funcionaría requieren acceptance criterion no estricto, y la VNS de la implementación actual no lo permite.

### 16.3 Consecuencia para Etapas 2.b–2.d

Si el patrón se confirma para los demás operadores cluster propuestos, **ninguno aportará bajo first-improvement de coste total**:

| Operador | Por qué probablemente fallará igual |
|---|---|
| `TryOTShifting` | Mover N pacientes de OT_a→OT_b tiene mismo trade-off entre `open_ot` (mejora) y otros (empeoran). ChangeOT individual ya cubre las mejoras estrictamente positivas. |
| `TryDayBlock` | Bloque (sala, día) → otra sala. Si ya cabe sin perder coste, ChangeRoom individual lo hace. |
| `TryNurseSwap` | Intercambiar enfermeras de turnos del mismo día. ChangeNurse ya prueba reasignaciones individuales y no encuentra mejoras (0 % en el log). El intercambio agregado tiene la misma limitación. |

La auditoría asumía que los operadores cluster atacaban "vecindarios ciegos" en sentido geométrico (movimientos no representables). En realidad **sí son representables**, lo que es ciego es el **acceptance criterion**: la VNS rechaza degradaciones intermedias.

### 16.4 Decisión — saltar a Etapa 3 (ALNS+SA)

Implicación inmediata para la hoja de ruta:

- **Abandonar Etapa 2.** Ningún operador cluster va a aportar dentro del marco VNS+ILS actual. Continuar implementándolos solo introduciría más regresión.
- **Saltar directo a Etapa 3 (módulo ALNS).** El ALNS canónico de Lusby et al. usa **Simulated Annealing como acceptance criterion**, lo que permite aceptar movimientos `Δcost > 0` con probabilidad `exp(−Δcost / T)`. Esto es **exactamente** lo que falta hoy: una vía para aceptar degradaciones intermedias y desbloquear las regiones del espacio de búsqueda hoy invisibles.
- Los operadores cluster (SurgeonRemoval, DayRemoval, etc.) deben implementarse **dentro del módulo ALNS**, no en la VNS. El destroy-repair sobre clusters de 5-15 pacientes con SA acceptance es donde la palanca real está según la literatura ([ACO-limitation-research.md §2.3](../ACO-limitation-research.md)).
- Las "Etapas 2.b–2.d" del plan original quedan **subsumidas en Etapa 3** con otra forma: como destroy operators, no como vecindarios VNS. La estructura del plan se simplifica.

### 16.5 Ficheros modificados (todos revertidos)

| Fichero | Estado |
|---|---|
| `src/solver/LocalSearch.h` | Revertido a v2 (8 operadores, uint8_t mask) |
| `src/solver/LocalSearch.cpp` | Revertido a v2 |
| `src/solver/ACOSolver.cpp` | Revertido a v2 (eliminado contador atómico de diagnóstico) |
| `src/ablation_test.cpp` | Revertido a v2 |

`git diff --stat src/` vacío. Logs y soluciones experimentales preservadas en `logs/etapa2a/` y `solutions_etapa2a/` para análisis posterior.

### 16.6 Insights consolidados para el plan de Etapa 3

A partir de los datos del diagnóstico, refinamos las prioridades de Etapa 3:

1. **ChangeOT y ChangeRoom dominan las mejoras** (53 % combinado) — son el núcleo productivo de la VNS actual. ALNS debe **complementarlos**, no reemplazarlos.
2. **ChangeNurse aporta 0 %** consistentemente (también lo veía el ablation original §4.3). Es candidato a desactivar permanentemente — libera un slot del shuffle. Validar antes en otra instancia.
3. **SwapDays solo 2.1 %** — operador débil; muchos pares se prueban para pocas mejoras. Considerar reducir su frecuencia en el shuffle (o moverlo dentro de ALNS como destroy/repair).
4. La distribución sugiere que el ALNS debería tener **destroy operators dirigidos a (cirujano, día) y (OT, día)** — exactamente las dimensiones donde ChangeOT+ChangeDay ya producen mejoras pero no en bloque.
5. La **acceptance SA** es el cambio crítico, no los operadores nuevos. Sin acceptance no estricta, los operadores cluster son inútiles.

---

## 17. Sesión 2026-05-09 — Etapa 3 (ALNS+SA): MVP, ampliación, benchmark completo

Se implementa la **Etapa 3** del plan de [ACO-limitation-research.md §4](../ACO-limitation-research.md): un módulo ALNS (Adaptive Large Neighborhood Search) con **Simulated Annealing acceptance** que sustituye la perturbación ILS clásica de [LocalSearch::Run](../src/solver/LocalSearch.cpp). Aprendiendo de las regresiones de Etapas 1 y 2, se siguió un enfoque **incremental**: MVP estricto → ampliación dirigida → benchmark completo, con decisión de parar/continuar tras cada hito.

### 17.1 Diseño del MVP

Nuevo módulo `src/solver/ALNSPerturbation.{h,cpp}` con:

- **`ALNSParams`**: `initial_temp_factor=0.05` (T₀ = 5% del coste inicial), `cooling_rate=0.998` (decay por Apply), `min_temp=0.5` (suelo), `destroy_factor=0.5` (k = √P × 0.5 acotado [4, 30]).
- **`ALNSPerturbation::Apply`**: snapshot completo de la solución → destroy → repair → cobertura de enfermeras → SA-acceptance. Si rechaza, restaura el snapshot.
- **MVP destroy**: solo `RandomRemoval(k)`. **Repair**: re-asignación greedy con `RandomGenerator::TryAssignPatientFeasibly` (con `ForceAssignMandatory` como fallback para obligatorios).
- **SA-acceptance**: `Δcost ≤ 0` siempre acepta; `Δcost > 0` acepta con probabilidad `exp(−Δcost / T)`. Cool-down geométrico tras cada Apply.

**Integración** ([LocalSearch::Run](../src/solver/LocalSearch.cpp)): nuevo parámetro opcional `bool use_alns = false`. Cuando es true, sustituye el bloque `Perturb(strength=4)` por `alns->Apply(...)`. `kMaxPerturbations` sube de 15 a 30 (cada Apply es más informativo). El ILS clásico se conserva exactamente como en v2 cuando `use_alns=false` — retrocompatibilidad total.

**Propagación**: `ACOParams::use_alns` (default false) controla el flag desde `main.cpp`. Nuevo argumento CLI 8º: `perturb` ∈ {`ils`, `alns`}, default `ils`.

### 17.2 MVP — mini-benchmark (i04, i08, i17, i26)

| Instancia | seed=42 vs v2 | seed=7 vs v2 | Promedio |
|---|---:|---:|---:|
| i04 | +3.65 % | −3.46 % | +0.10 % |
| i08 | +1.45 % | — | +1.45 % |
| i17 | −0.41 % | −0.81 % | −0.61 % |
| i26 | −0.61 % | −1.81 % | −1.21 % |

ALNS MVP **mejora ligeramente** en instancias grandes (i17, i26), neutral en pequeñas (i04, i08). La varianza inter-seed (~5-6 %) domina el efecto. Decisión: ampliar con destroy operators dirigidos antes de descartar.

### 17.3 Ampliación — ALNS+ con 3 destroy operators

Añadidos `SurgeonRemoval` y `DayRemoval` a `ALNSPerturbation`:

- **`SurgeonRemoval`**: identifica cirujanos con overtime, muestrea uno por probabilidad ∝ overtime, retira todos sus pacientes (acotado por `k_cap`). Si ningún cirujano tiene overtime, fallback a `RandomRemoval`.
- **`DayRemoval`**: muestrea día por probabilidad ∝ número de pacientes admitidos, retira hasta `k_cap` de ese día.

En cada `Apply`, elige uniformemente entre los 3 destroys. Sin adaptive weights aún (se añaden si el experimento confirma palanca).

**Mini-benchmark ALNS+ (3 destroys):**

| Instancia | seed=42 | seed=7 |
|---|---:|---:|
| i04 | +4.81 % | −5.24 % |
| i08 | +2.97 % | — |
| i17 | −0.19 % | −0.05 % |
| i26 | −0.66 % | −2.66 % |

Resultado **muy similar al MVP solo con RandomRemoval**. El destroy dirigido no añade valor visible. Hipótesis: (a) k = √P × 0.5 muy conservador (P=500 → solo 12 pacientes ≈ 2.4 %); (b) GreedyRepair re-asigna en posiciones casi idénticas; (c) cooling 0.998 → T casi constante → SA acepta casi todo, tasa de aceptación demasiado alta.

Decisión metódica: **lanzar benchmark completo i01–i30 con ALNS+** (semilla 42). 30 instancias compensan la varianza por número de muestras. Si el agregado mejora consistentemente → Etapa 3a OK; replantear Etapa 3b. Si no → diagnóstico profundo.

### 17.4 Benchmark completo i01–i30 (600 s, semilla 42)

Mismo setup que v2: 30 instancias × 600 s × 4 hilos por solver, 5 instancias en paralelo (20 cores), wall-clock ~60 min. Datos en [tables/aco-random-comparison-v3.csv](../tables/aco-random-comparison-v3.csv); figuras en [graficas_v3/](../graficas_v3/).

**Resultados agregados (validador oficial IHTC):**

| Métrica | v1 (pre-mejoras) | v2 (Fases A+B+C+D) | **v3 (ALNS+SA)** | Best (oficial) |
|---|---:|---:|---:|---:|
| Coste agregado i01–i30 | 1 119 248 | 1 080 218 | **1 069 872** | 716 560 |
| Gap medio vs best | +56.4 % | +52.5 % | **+52.0 %** | — |
| Mediana gap vs best | — | +49.7 % | **+49.4 %** | — |
| ACO gana a Random | 24/30 | 27/30 | **25/30** | — |
| Soluciones factibles | 30/30 | 30/30 | **30/30** | — |

**Mejora v2 → v3**: −10 346 puntos absolutos (**−0.96 %**). **Gap medio cae 0.53 pp**. 20/30 instancias mejoran respecto a v2; 10/30 empeoran.

**Top mejoras v3 vs v2:**

| Instancia | v2 | v3 | Δ % |
|---|---:|---:|---:|
| i27 | 97 948 | 92 631 | **−5.43 %** |
| i05 | 15 904 | 15 396 | −3.19 % |
| i03 | 11 150 | 10 860 | −2.60 % |
| i11 | 29 900 | 29 125 | −2.59 % |
| i15 | 20 280 | 19 762 | −2.55 % |
| i24 | 42 477 | 41 397 | −2.54 % |
| i22 | 85 405 | 83 617 | −2.09 % |
| i13 | 23 856 | 23 362 | −2.07 % |

**Empeoramientos v3 vs v2:**

| Instancia | v2 | v3 | Δ % |
|---|---:|---:|---:|
| i04 | 4 658 | 4 882 | +4.81 % |
| i01 | 4 330 | 4 465 | +3.12 % |
| i08 | 16 877 | 17 378 | +2.97 % |
| i19 | 79 081 | 80 363 | +1.62 % |
| i25 | 16 722 | 16 970 | +1.48 % |

### 17.5 Lectura de los resultados

**Patrón claro:**

- ALNS+SA aporta más en instancias **medianas y grandes** (i05, i11, i13, i15, i17, i22, i24, i27 con mejoras 2-5 %).
- En instancias **pequeñas o ya saturadas** (i01, i04, i08, i19, i25) introduce más ruido del que recupera.
- El SA, con cooling 0.998 sobre 30 perturbaciones, mantiene T cerca del inicial — acepta demasiado. Esto explica por qué ayuda en grandes (donde se necesita escapar de óptimos locales) y daña en pequeñas (donde el óptimo local ya estaba bien).

**i08 sigue patológica:** v1 = 13 199, v2 = 16 877, v3 = 17 378. Mientras Random saca 13 199 (mejor que ACO de cualquier versión), el ACO no encuentra esa cuenca. La feromona se está sesgando a un atractor diferente — fenómeno consistente con la auditoría §1.4 sobre la factorización marginal de τ.

**Comparación con la proyección de [ACO-limitation-research.md §4](../ACO-limitation-research.md):**

- Plan original Etapa 3: gap esperado **22-29 %**.
- Resultado real: **52 %**.
- Diferencia: el plan asumía que el ALNS+SA generaba un salto cualitativo. La realidad es que solo añade −0.5 pp de gap.

La hipótesis de "el SA-acceptance es la palanca clave" se valida solo parcialmente: sí cambia el comportamiento (acepta degradaciones, escapa óptimos locales), pero no en la magnitud esperada. La **factorización marginal de la feromona** y la **heurística η pobre** (Etapas 4-5) parecen ser el cuello de botella mayor.

### 17.6 Análisis: ¿por qué el ALNS no genera el salto esperado?

Hipótesis a profundizar tras los datos del benchmark:

1. **GreedyRepair es demasiado conservador**: re-asigna por orden de obligatoriedad/slack y aleatorio, sin sesgar por feromona ni por `Δcost` mínimo. Un `RegretKInsertion` o un `ACOGuidedInsertion` puede dar más palanca.
2. **Tasa de aceptación SA muy alta**: cooling 0.998 sobre 30 Apply → T_final ≈ T_inicial × 0.94. Casi todo se acepta. Eso desorienta la búsqueda en lugar de dirigirla. Un `cooling_rate=0.93` daría T_final ≈ 12 % de T_inicial — schedule realista.
3. **k = √P × 0.5 muy bajo en grandes**: para P=500, k=11. Removemos 2.4 % de pacientes. La literatura ALNS típica usa 10-30 % (k=50-150 para P=500). Con k tan bajo, el destroy es esencialmente equivalente a una micro-perturbación.
4. **Sin adaptive weights**: los 3 destroys se eligen uniformemente; un destroy peor se ejecuta tantas veces como el mejor. Adaptive weights con roulette priorizarían los más útiles.
5. **Sin diversificación tras estancamiento**: si ALNS no mejora durante muchas Apply, no hay reset. Un mecanismo de "kick the can" cuando se detecta estancamiento (e.g., ralentizar T fija a un valor alto durante 3 Apply) podría ayudar.

### 17.7 Decisión y siguiente paso

El benchmark v3 es **una mejora marginal pero consistente** sobre v2. Las cifras quedan lejos del objetivo de la hoja de ruta. Tres caminos viables:

1. **Etapa 3b — Recalibración del ALNS** (1-2 días): subir `destroy_factor` a 1.0-1.5, bajar `cooling_rate` a 0.93, añadir `RegretKInsertion`, añadir adaptive weights con roulette. Ganancia esperada: −2 a −5 pp adicionales.
2. **Etapa 4 — ACO informado** (2-3 días, según plan original): η dinámica con surgeon_load/ot_load/room_state, candidate lists, q0 heterogéneo, soft-reset por edad de global_best. Ataca la **causa raíz** identificada en la auditoría §1.4 (η pobre, τ marginal). Ganancia esperada por el plan: −3 a −6 pp.
3. **Mantener v3 como estado actual** y pasar al análisis estadístico/redacción.

Recomendación: **Etapa 3b primero** (recalibración barata) y luego Etapa 4 (cambio cualitativo). Aplicarlas en este orden porque la 3b mejora el módulo ALNS recién implementado mientras la lógica está fresca, y la 4 ataca componentes ortogonales (la construcción ACO).

### 17.8 Ficheros nuevos / modificados

| Path | Acción |
|---|---|
| [src/solver/ALNSPerturbation.h](../src/solver/ALNSPerturbation.h) | **Nuevo**: header del módulo ALNS+SA |
| [src/solver/ALNSPerturbation.cpp](../src/solver/ALNSPerturbation.cpp) | **Nuevo**: 3 destroy ops + GreedyRepair + SA-accept |
| [src/solver/LocalSearch.h](../src/solver/LocalSearch.h) | Añadido parámetro `bool use_alns = false` a `Run` |
| [src/solver/LocalSearch.cpp](../src/solver/LocalSearch.cpp) | Bloque ILS clásico vs ALNS según `use_alns` |
| [src/solver/ACOSolver.h](../src/solver/ACOSolver.h) | Añadido `bool use_alns = false` a `ACOParams` |
| [src/solver/ACOSolver.cpp](../src/solver/ACOSolver.cpp) | Propaga `params.use_alns` a `LocalSearch::Run` (warm-start y hormigas paralelas) |
| [src/main.cpp](../src/main.cpp) | 8º argumento CLI: `perturb` ∈ {ils, alns} |
| [CMakeLists.txt](../CMakeLists.txt) | Añadido `ALNSPerturbation.cpp` a `SOLVER_SOURCES` |
| [scripts/plot_comparison_v3.py](../scripts/plot_comparison_v3.py) | Lee `solutions_aco_v3/`, escribe en `graficas_v3/` |
| [solutions_aco_v3/](../solutions_aco_v3/) | 30 soluciones ALNS+SA (semilla 42, 600 s) |
| [tables/aco-random-comparison-v3.csv](../tables/aco-random-comparison-v3.csv) | Tabla con costes y gaps de v3 |
| [graficas_v3/](../graficas_v3/) | 5 PNG + comparison_summary.csv |
| [logs/comparison_v3/](../logs/comparison_v3/) | Logs por instancia + `_progress.csv` |

---

## 18. Sesión 2026-05-20 — Bug-fix del Evaluator y Bloques A+B de mejoras

### 18.1 Bug-fix del evaluator interno (Sesión previa, día 19)

Al ejecutar el validador oficial `IHTP_Validator` sobre las soluciones del solver descubrimos que el **evaluador interno subestimaba el coste en un ~65 %** debido a tres bugs:

1. **PatientDelay calculado desde `due_day` en lugar de `release_day`**: `Patient::GetDelayDays` devolvía `max(0, admission - surgery_due_day_)` (cero para casi todos los pacientes) cuando el validador oficial usa `admission - surgery_release_day`. Para i08, el coste real de delay era 10560, el solver veía 0.
2. **`GetSkillLevelForShift` y `GetWorkloadForShift` usaban solo el primer día de la matriz** `skill_level_required[LOS × shifts_per_day]`. Resultado: el día 4 de la estancia de un paciente leía las skills del día 0. Reemplazado por `GetSkillLevelAt(day_in_stay, shift)` y `GetWorkloadAt(day_in_stay, shift)` que indexan correctamente. Igual en `Occupant.h` (donde `day_in_stay == day` porque entran el día 0).
3. **`Evaluator::CalcNurseSkillCost` añadía `weight` por violación en lugar del déficit real**: el validador oficial usa `(required - nurse_skill) * weight`. Ajustado.

Impacto medido en i08 a 1200 s con el mismo seed:
- Antes del fix: coste oficial **16,220** (interno reportaba 3,212).
- Después del fix: coste oficial **9,070** (interno 8,616 — la pequeña discrepancia restante es por `ContinuityOfCare` y `RoomAgeMix` que difieren en fórmula pero el usuario indicó no replicar exactamente el validador oficial).

Benchmark completo 30 instancias 600 s (validador oficial):
- ACO pre-fix: 1,110,465 (gap +54.9 % vs `best-solutions/`)
- ACO post-fix: **1,038,478** (gap **+44.9 %** vs `best-solutions/`)
- ACO gana a Random en **24/30** instancias (vs 21/30 antes).

Ficheros: [src/entities/Patient.h](../src/entities/Patient.h), [src/entities/Occupant.h](../src/entities/Occupant.h), [src/evaluator/Evaluator.cpp](../src/evaluator/Evaluator.cpp), [src/solution/Solution.cpp](../src/solution/Solution.cpp), [src/solver/RandomGenerator.cpp](../src/solver/RandomGenerator.cpp). Commit `[9260404]`.

### 18.2 Auditoría general post-fix

Con el evaluador honesto, una nueva auditoría completa del solver identificó tres familias de problemas que limitan el algoritmo:

**Crítico (VNS)**:
- **H-1**: `GetShuffledScheduled` recorta a **60 pacientes** todos los operadores patient-based. En i22 (174 pacientes) la VNS solo ve 35 % de la solución por iteración. Probable causa del estancamiento al ampliar tiempo.
- **H-2**: caps duros en `TrySwapRooms/SwapDays` (200 pares), `TryRelocate` (30 combos), `TryChangeNurse` (100 celdas). En i22, `TryChangeNurse` ve 100 de ~1176 posibles = 8.5 %.
- **H-3**: sin delta-eval. Cada movimiento llama a `Evaluator::Evaluate` que recalcula los 12 componentes.

**Importante (ACO)**:
- **H-4**: `η_room ∈ {0,1}`. No diferencia habitaciones compatibles entre sí.
- **H-5**: `η_day` solo modela PatientDelay (8 % del coste en i22). Ignora OT-open, surgeon-load.
- **H-6/H-7**: Nurses fuera del modelo ACO. `GenerateNurseAssignments` es greedy idéntico a Random, solo mira día anterior. En i22, `continuity + nurse_workload = 41 % del coste`.

**Medio (VNS/Random)**:
- **H-10**: `TryToggleOptional` descarta opcionales tras un único intento fallido. Causa probable de los **92 opcionales sin programar en i22** (+41,400 puntos de coste).
- **H-11**: `kPerturbStrength = 4` fijo, demasiado débil en instancias grandes.

### 18.3 Bloque A — Quick wins en VNS

Cinco cambios incrementales con flag de fallback (struct `VNSConfig` en `LocalSearch.h`):

| # | Cambio | Default agresivo | Legacy |
|---|---|---|---|
| A1 | Cap pacientes en `GetShuffledScheduled` | 0 (sin cap) | 60 |
| A2 | `TryToggleOptional` exhaustivo (hasta N posiciones) | 50 | 1 (legacy) |
| A3 | `kPerturbStrength` proporcional `(factor × scheduled, base, max)` | (0.10, 4, 25) | (0, 4, 4) |
| A4 | Caps de operadores `Relocate`/`ChangeNurse` | 200 / 500 | 30 / 100 |
| A5 | Refresh nurse cada N iter LS (snapshot + tol) | cada 50 iter, 2 % | desactivado |

Cada cambio mira `g_vns_config` (thread_local). El binario acepta nuevo arg CLI `preset` ∈ {`default`, `legacy`}: `default` activa Bloque A, `legacy` lo desactiva.

### 18.4 Bloque B — Heurísticas ACO informadas

| # | Cambio | Default | Legacy |
|---|---|---|---|
| B1 | `η_room` enriquecida (penalty por género/edad de ocupantes preexistentes + bonus por capacidad) | activo | binaria {0,1} |
| B2 | `η_day` enriquecida (penalty por OT no abierto, surgeon-load del día) | activo | solo delay |
| B3 | Warm-start budget = clamp(8 % × time, 30s, 180s) | adaptativo | min(30s, 5%) |

Flags: `ACOParams::rich_eta_room`, `rich_eta_day`, `adaptive_warm_start`. Por defecto activos; `preset=legacy` los desactiva.

### 18.5 Ficheros modificados / nuevos en esta sesión

| Path | Acción |
|---|---|
| [src/solver/LocalSearch.h](../src/solver/LocalSearch.h) | Nuevo `struct VNSConfig` + parámetro `const VNSConfig&` en `Run` |
| [src/solver/LocalSearch.cpp](../src/solver/LocalSearch.cpp) | A1-A5: cap configurable, exhaustive optional, perturb proporcional, refresh nurses |
| [src/solver/ACOSolver.h](../src/solver/ACOSolver.h) | Nuevos flags B1/B2/B3 + miembro `vns_config` |
| [src/solver/ACOSolver.cpp](../src/solver/ACOSolver.cpp) | B1: `η_room` rica; B2: `η_day` rica; B3: warm-budget adaptativo; propaga `vns_config` a `LocalSearch::Run` |
| [src/solver/RandomGenerator.h/.cpp](../src/solver/RandomGenerator.cpp) | A5: nueva `RegenerateNurses(sol, problem, rng)` |
| [src/main.cpp](../src/main.cpp) | D1: 9º arg CLI `preset` ∈ {default, legacy}; helper `MakeLegacyVNSConfig` |

### 18.6 Política de fallback

- Toda la configuración pasa por `VNSConfig` y `ACOParams`. Default = agresivo (Bloque A+B activos).
- CLI: `./ihtc_solver ... aco 12 ils default` (todos los flags A+B activos) vs `./ihtc_solver ... aco 12 ils legacy` (comportamiento pre-Bloque-A).
- Snapshot pre-mejoras: `solutions_aco_postfix/`, `tables/aco-random-comparison-postfix.csv`.

### 18.7 Bloque C — Pendiente de evaluación

Según criterios del plan: el Bloque C (`τ_nurse` + `GenerateNurseAssignmentsACO`) se activa solo si tras A+B:
- Gap medio sigue > 35 %, **o**
- Componentes `continuity_of_care + nurse_workload` siguen > 30 % del coste total en i22.

Pendiente medir benchmark A+B para decidir.
