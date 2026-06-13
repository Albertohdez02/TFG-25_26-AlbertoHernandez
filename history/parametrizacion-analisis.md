# Análisis de parametrización del solver: ACO, ALNS y VNS — ¿qué parámetros son palanca?

**Autor:** Alberto Hernández
**Fecha:** 2026-06-09
**Rama git:** `ACO`
**Artefactos:** harness en [tuning/](../tuning/), resultados en [tuning/RESULTS.md](../tuning/RESULTS.md), figuras en [tuning/figs/](../tuning/figs/).

---

## 0. Resumen ejecutivo

Campaña experimental para fijar, con respaldo estadístico, los valores por
defecto de los parámetros ajustables de los tres algoritmos del solver (ACO,
ALNS+SA, VNS/ILS). Se barrieron **10 parámetros** multi-seed sobre un
subconjunto representativo de 6 instancias (pequeñas/medianas/grandes × series
`i`/`m`), con comparación pareada por celda y test de Wilcoxon contra el default
actual, y confirmación a budget de despliegue (600 s) + validación oficial
(IHTP_Validator).

**Resultado:** **ningún parámetro tuneable mejora los defaults de forma
estadísticamente significativa al budget de despliegue.** Los defaults actuales
son robustos. La palanca de mejora es **estructural** (elección de preset
`hybrid`, operadores compound), no paramétrica — confirmando cuantitativamente
lo que el histórico del proyecto ([ACO.md](ACO.md),
[ACO-limitation-research.md](ACO-limitation-research.md)) ya venía mostrando.

Tres hallazgos transversales sostienen la conclusión:
1. **Suelo de ruido ~1 %**: validado con un control positivo (quitar los
   compound moves produce +1.24 % agregado, +2–4 % por instancia). El harness
   detecta efectos ≥2 %; los parámetros (todos <1 %) caen por debajo.
2. **Horizonte-dependencia**: el óptimo del `cooling_rate` del SA depende del
   nº de Applies; una ventaja medida a 240 s no transfiere a 600 s.
3. **Estructural ≫ paramétrico**: el mismo subconjunto, validado oficialmente,
   muestra que el preset `hybrid` mueve −3.6 pp de gap agregado, mientras
   ningún parámetro lo mueve.

---

## 1. Motivación y objetivo

El solver expone parámetros en tres structs: `ACOParams`
([ACOSolver.h](../src/solver/ACOSolver.h)), `VNSConfig`
([LocalSearch.h](../src/solver/LocalSearch.h)) y `ALNSParams`
([ALNSPerturbation.h](../src/solver/ALNSPerturbation.h)). El histórico documentó
que la mejora del gap vino de cambios **estructurales** (compound moves −4.2 pp,
nurse polish −5 pp, modo hybrid −2.66 pp), pero **los exponentes clásicos del
ACO (α, β, ρ, q0) nunca se sintonizaron empíricamente** — se fijaron en valores
de libro. El objetivo es cerrar ese hueco: medir con rigor estadístico qué
parámetros (si alguno) merecen un default distinto.

---

## 2. Metodología

### 2.1 Subconjunto representativo

No se usan las 30+30 instancias (coste prohibitivo). Se eligen 6 que cubren el
espectro de tallas en ambas series:

| Talla | `i` | tallas | `m` | tallas |
|---|---|---|---|---|
| Pequeña | **i04** | 54 pac, 9 hab | **m01** | 56 pac, 4 hab |
| Mediana | **i13** | 129 pac, 10 hab | **m08** | 200 pac, 13 hab |
| Grande | **i22** | 409 pac, 20 hab | **m26** | 570 pac, 41 hab |

### 2.2 Diseño estadístico

- **Métrica**: coste interno (`Evaluator::Evaluate`, lo que el solver optimiza)
  como surrogate para comparación relativa — válido porque la pequeña
  discrepancia sistemática con el validador oficial se cancela en las
  diferencias. Los ganadores se reconfirman con coste oficial.
- **Pareo**: para cada parámetro, cada (instancia × seed) es una celda; se
  compara el **cambio relativo por celda** `cost_cand/cost_base − 1` (normaliza
  las escalas dispares: i04~2.5k vs m26~100k) y se agrega.
- **Significancia**: Wilcoxon pareado de las diferencias relativas vs el default.
- **Budget**: 120 s en exploración, 240 s para ALNS (más Applies), **600 s
  (despliegue) en confirmación**, con 3–5 seeds.

### 2.3 El solver es *time-bounded* — implicación clave

Cada run cuesta ≈ budget, **independiente de la talla** (las loops chequean
tiempo de pared, no iteraciones). Esto permite presupuestar el cómputo, pero
también significa que cualquier parámetro que profundice la búsqueda por
iteración roba tiempo al bucle externo: a iso-tiempo, "explorar más" suele
significar "aprender menos". Además, el overhead fijo (warm-start ~30 s + nurse
polish ~36 s) es insensible a los parámetros de construcción y **diluye su
señal** a budgets cortos.

### 2.4 Infraestructura y fallback

Cambio de código **mínimo y aditivo**: hooks de override por variable de entorno
(`IHTC_*`) en [main.cpp](../src/main.cpp) (ACO + VNS) y en el constructor de
[ALNSPerturbation.cpp](../src/solver/ALNSPerturbation.cpp) (ALNS). **Si no se
exporta ninguna variable, el binario es idéntico al baseline** — fallback
garantizado, anclado además en `git tag baseline-pre-tuning`. No se cambió
ningún default algorítmico (`git diff baseline...` sobre los structs = vacío).

Harness reutilizable: [tuning/sweep.py](../tuning/sweep.py) (barrido genérico),
[sweep_alns.py](../tuning/sweep_alns.py), [sweep_vns.py](../tuning/sweep_vns.py),
[confirm.py](../tuning/confirm.py) (600 s), [analyze.py](../tuning/analyze.py)
(Wilcoxon), [validate_config.py](../tuning/validate_config.py) (oficial),
[plot.py](../tuning/plot.py) (figuras).

---

## 3. Resultados por familia

Cambio relativo por celda vs default; negativo = mejor. p = Wilcoxon pareado.

### 3.1 ACO — regla de transición y schedule de feromona

| Paso | Param | Default | Valores | Mejor | Δ mean | p | Veredicto |
|---|---|---|---|---|---|---|---|
| 1 | β | 2 | 1,2,3,4 | β=1 | −0.57 % | 0.068 | mantener 2 |
| 2 | q0 | 0.90 | .80,.90,.95,.98 | — | −0.16 % | 0.41 | mantener 0.90 |
| 3a | ρ | 0.10 | .05,.10,.20,.30 | — | −0.08 % | 0.81 | mantener 0.10 (**inerte**, mediana 0.00) |
| 3b | τ_min_factor | 2 | 2,10,50 | 50 | −0.46 % | 0.31 | mantener 2 (τ50 ayuda 5/6 pero **daña i22**) |

Los cuatro metaparámetros ACO son **no significativos**. ρ es literalmente
inerte (mediana de cambio 0.00 %: en la mayoría de celdas no altera el
resultado — con warm-start dominante y pocas iteraciones externas, la
evaporación apenas actúa). τ_min=50 es el único con tendencia favorable, pero
reproduce el patrón de la Fase 2 ([ACO.md §16](ACO.md), some-touches §2):
ayuda a la mayoría y daña la instancia grande → no es default seguro.

### 3.2 ALNS+SA — schedule de temperatura y tamaño de destroy

Factorial 2×2 (cooling × destroy) en preset `hybrid` a 240 s:

| Config | cambio | wins | p |
|---|---|---|---|
| C0 base (cooling .998, destroy .5/30) | — | — | — |
| C1 cooling=0.995 | **−0.53 %** | 12/18 | 0.167 |
| C2 destroy 2.0/80 | **+0.74 %** | 6/18 | 0.26 |
| C3 ambos | +0.40 % | 6/18 | 0.30 |

Dos hallazgos:
- **`destroy_factor` mayor DAÑA** (+0.74 %, sobre todo en grandes: m26 +4 %,
  i22 +2 %). Esto **refuta la hipótesis de la auditoría**
  ([ACO-limitation-research.md §1, §17.6](ACO-limitation-research.md)) de que el
  destroy estaba "demasiado bajo": el greedy repair no recupera la disrupción de
  un destroy grande dentro del budget. **Mantener 0.5/30.**
- **`cooling_rate=0.995`** es el único candidato positivo de toda la campaña
  (−0.53 %, no daña ninguna instancia), pero p=0.167 → confirmar a 600 s.

**Confirmación a 600 s** (5 seeds, hybrid): la ventaja **se evapora**.

| cooling | mean (600 s) | wins | p |
|---|---|---|---|
| 0.998 (default) | — | — | — |
| 0.995 | +0.17 % | 12/30 | 0.63 |
| 0.993 | +0.27 % | 18/30 | 0.82 |

Restringiendo a las **mismas seeds** de exploración, 0.995 pasa de −0.53 %
(240 s) a −0.01 % (600 s): es **horizonte-dependencia** pura, no varianza de
seed. El cooling fijo no tiene un óptimo transferible (ver §4.2). **Mantener
0.998.**

### 3.3 VNS/ILS — caps, perturbación y refresco

OFAT en preset `default` (donde la VNS es el motor) a 120 s:

| Param | Default | Mejor | Δ mean | p | Veredicto |
|---|---|---|---|---|---|
| perturb_strength_factor | 0.10 | 0.30 | −0.67 % | 0.80 | mantener 0.10 |
| max_patients_per_op (cap) | 0 (sin cap) | cap60 | −0.37 % | 0.35 | mantener 0 |
| nurse_refresh_every | 50 | 100 | −0.39 % | 0.16 | mantener 50 |
| **enable_compound** | ON | — | +1.24 % si OFF | 0.21 | **mantener ON** |

Ningún parámetro VNS es palanca (todos ±0.7 %, ruido) — coincide con los docs
(Bloque A "placebo", Etapa 1 √P "regresión revertida"). `enable_compound` es el
**control positivo** (§4.1).

---

## 4. Hallazgos transversales

![Resumen](../tuning/figs/fig1_overview.png)

*Fig 1 — Efecto de cada parámetro. Todos caen en la banda de ruido ±1 %; solo el
control estructural (compound OFF, borde rojo) sobresale.*

### 4.1 Suelo de ruido y control positivo

`enable_compound=off` (quitar los compound moves, palanca estructural conocida
de −4.2 pp en los docs) es el mayor efecto de la campaña: +1.24 % agregado, y
**+2 a +4 % por instancia en medianas/grandes** (i13 +3.94 %, m08 +2.29 %,
m26 +2.12 %), neutro/negativo en triviales (m01 −1.52 %). El harness **sí**
detecta efectos ≥2 %, leídos por instancia. Esto fija el suelo de ruido del
montaje en ~1 %: los parámetros, todos por debajo, **no merecen tunearse**.

![Compound](../tuning/figs/fig3_compound_byinstance.png)

*Fig 3 — Los compound moves ayudan exactamente donde se diseñaron (instancias
con opcionales que reubicar y días que reorganizar), no en las triviales.*

### 4.2 Horizonte-dependencia del cooling

![Cooling](../tuning/figs/fig2_cooling_horizon.png)

*Fig 2 — La ventaja de cooling=0.995 a 240 s desaparece a 600 s.*

El factor de enfriamiento óptimo depende del nº de Applies `N` (que escala con
budget y velocidad de evaluación). Un `cooling` fijo bueno para un horizonte es
malo para otro — exactamente lo que [some-touches §B.3](some-touches.md)
diagnosticó (a 28.939 Applies, 0.998 degenera en hill-climbing). La solución
correcta **no es otra constante**, sino un cooling adaptativo
`cooling=(T_min/T₀)^(1/N)` (cambio de código, fuera del alcance "solo
parámetros").

### 4.3 Falsos positivos por poco rigor

Dos candidatos parecían buenos con menos rigor y se cayeron al confirmar:
- **β=3** ganaba 5/6 en el smoke de 1 seed → ruido con 3 seeds.
- **cooling=0.995** ganaba a 240 s → no transfiere a 600 s.

Lección metodológica para el TFG: tunear este solver exige **multi-seed +
budget de despliegue**; los efectos son <1 % (por debajo de la varianza
inter-seed ~3–6 %) y horizonte-dependientes.

---

## 5. Capstone: validación oficial (estructural vs paramétrico)

Config actual SIN overrides, preset `default` vs `hybrid`, IHTP_Validator,
600 s, seed 42. **12/12 factibles (0 violaciones)** — confirma que todo el
harness midió soluciones reales.

| preset | i04 | i13 | i22 | m01 | m08 | m26 | agregado |
|---|---|---|---|---|---|---|---|
| default | +38.2 % | +31.8 % | +69.5 % | +17.4 % | +25.5 % | +47.6 % | **+49.7 %** |
| hybrid | +40.9 % | +26.8 % | +58.9 % | +21.1 % | +28.0 % | +47.4 % | **+46.1 %** |

![Oficial](../tuning/figs/fig4_official_gap.png)

*Fig 4 — El cambio estructural (preset hybrid) mueve −3.6 pp agregado
(−10.6 pp en i22), frente a 0 de cualquier parámetro.* (Los gaps son más altos
que el +35.65 % del full-30 porque el subconjunto sobrepondera instancias
difíciles para maximizar la discriminación; no es comparable al agregado de 30.)

---

## 6. Conclusión y recomendaciones

**Los defaults actuales son robustos y están bien elegidos.** Es un resultado
*positivo* para el TFG: confirma que el trabajo previo de calibración
estructural ya exprimió el espacio paramétrico, y que el gap residual no se
cierra tocando constantes.

Defaults confirmados (sin cambios):

| | β | q0 | ρ | τ_min_factor | cooling_rate | destroy | perturb | refresh | compound |
|---|---|---|---|---|---|---|---|---|---|
| valor | 2 | 0.90 | 0.10 | 2 | 0.998 | 0.5/30 | 0.10 | 50 | ON |

Si se quiere seguir mejorando, la vía **no es paramétrica**:
1. **Cooling adaptativo** `(T_min/T₀)^(1/N)` — la única "miscalibración" real,
   pero requiere ~30 líneas de código.
2. **Adoptar `hybrid` como preset por defecto** — ya documentado como mejor;
   esta validación oficial lo reconfirma en el subconjunto.

---

## 7. Reproducibilidad

```bash
# barridos (preset default salvo ALNS=hybrid)
python3 tuning/sweep.py --param IHTC_BETA --values 1,2,3,4 --out tuning/runs/beta.csv
python3 tuning/sweep_alns.py     # factorial cooling x destroy, 240s hybrid
python3 tuning/sweep_vns.py      # VNS OFAT, 120s default
python3 tuning/confirm.py        # cooling a 600s, 5 seeds
# analisis y figuras
python3 tuning/analyze.py tuning/runs/beta.csv --baseline 2
python3 tuning/plot.py
# validacion oficial
python3 tuning/validate_config.py --label base_hybrid --preset hybrid
```

Variables de entorno disponibles (no-op si no se exportan):
`IHTC_{ALPHA,BETA,RHO,Q0,TAU_MIN_FACTOR,STAGNATION_K,N_ANTS,POLISH_BUDGET}`,
`IHTC_ALNS_{COOLING,TEMP_FACTOR,DESTROY_FACTOR,MIN_DESTROY,MAX_DESTROY}`,
`IHTC_VNS_{MAX_PATIENTS,PERTURB_FACTOR,PERTURB_MAX,SWAP_PAIRS,RELOCATE,NURSE_POS,REFRESH_EVERY,REFRESH,COMPOUND,...}`.
