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

## 5. Construcción de una solución (una hormiga)

```
1. Ordenar pacientes:
   - Obligatorios: por slack ascendente (due_day − release_day)
     → los más urgentes se asignan primero, cuando los recursos están libres
   - Opcionales: orden aleatorio (mezcla en cada iteración)

2. Para cada paciente p:
   a. Calcular scores de día: score(d) = τ_day[p][d]^α × η_day[p][d]^β
      (solo días dentro de la ventana y con al menos un quirófano abierto)
   
   b. Elegir día d* con regla pseudoproporcional (q0/1-q0)
   
   c. Calcular scores de habitación: score(r) = τ_room[p][r]^α × η_room[p][r]^β
      (solo habitaciones compatibles con p)
   
   d. Elegir habitación r* con regla pseudoproporcional
   
   e. Para el par (d*, r*): intentar quirófanos en orden de carga descendente
      (preferir el más cargado para concentrar cirugías → minimiza open_ot)
      → si es factible (HC2,HC5,HC6,HC7,HC8,HC12,HC13): asignar
   
   f. Si (d*, r*) falla: probar otras habitaciones del mismo día d*
   
   g. Si todo falla en d*: probar otros días en orden de score descendente
   
   h. Si no hay posición factible:
      - Paciente obligatorio: activar reparación (TryAssignPatientFeasibly → ForceAssignMandatory)
      - Paciente opcional: dejarlo sin programar (coste soft unscheduled_optional)

3. Asignación de enfermeras: greedy idéntico a RandomGenerator
   (reutiliza RandomGenerator::GenerateNurseAssignments directamente)
```

La verificación de factibilidad en cada intento usa `FeasibilityChecker::IsFeasiblePatientAssignment`, que consulta los cachés incrementales de `Solution` en O(1).

---

## 6. Pipeline completo (ACOSolver::Run)

```
Inicializar τ_day, τ_room con tau_init en posiciones feasibles, 0 en infeasibles
Precomputar η_day, η_room (una sola vez)

Mientras quede tiempo (> 2s):
    Para cada hormiga k en 1..n_ants:
        1. sol_k = ConstructSolution(τ, η)       ← construcción ACO
        2. sol_k = LocalSearch::Run(sol_k, ...)   ← VNS mejora la solución
        3. Actualizar mejor_iteración si cost(sol_k) < mejor_iteración
        4. Actualizar mejor_global si cost(sol_k) < mejor_global AND factible(sol_k)

    Actualizar τ con la mejor solución (mejor_global si existe, si no mejor_iteración)
    Si no hay mejora global en stagnation_k iteraciones: reinicializar τ
```

Cada hormiga recibe un presupuesto de tiempo igual a `tiempo_restante / (n_ants + 1)` para su VNS, repartiéndolo equitativamente entre las hormigas de la iteración.

---

## 7. Ficheros creados/modificados

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

## 8. Parámetros por defecto y cómo usarlo

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

## 9. Resultados de validación inicial

Comparación en instancias pequeñas con **30 segundos** de tiempo (misma semilla):

| Instancia | Modo random | Modo ACO | Mejora |
|---|---|---|---|
| test01 | 2339 | 2109 | **−9.8%** |
| i01 (60s) | 3787 | 3537 | **−6.6%** |

Ambas soluciones son **100% factibles** (0 violaciones de restricciones duras).

---

## 10. Notas técnicas y decisiones de diseño

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

## 11. Líneas futuras sobre este módulo

- [ ] Tuning de parámetros ACO (α, β, ρ, q0, n_ants) con los resultados del ablation existente
- [ ] Añadir `τ_ot` para instancias con muchos quirófanos (m-series)
- [ ] Explorar MMAS con actualización de la mejor global vs. mejor de iteración (actualmente alterna)
- [ ] Paralelismo: cada hilo gestiona su propia hormiga con feromona compartida (mutex en update)
- [ ] Benchmark completo i01–i30 comparando ACO vs random con 600s
