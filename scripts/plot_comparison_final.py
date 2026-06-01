#!/usr/bin/env python3
# Graficas comparativas finales: Random+VNS vs Hybrid (algoritmo final) vs best-known.
#
# Fuentes de datos:
#   - best:    validacion de best-solutions/sol_iXX.json con IHTP_Validator
#   - Hybrid:  validacion de solutions_hybrid_default/iXX_solution.json (seed=42)
#   - Random:  tabla historica tables/aco-random-comparison-postfix.csv
#              (campo random_cost, ejecutado en su momento a 600s con
#               postfix-evaluator)
#
# Salida: graficas/ (5 figuras PNG + comparison_summary_final.csv)

import csv
import re
import subprocess
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parent.parent
VALIDATOR = ROOT / "validator" / "IHTP_Validator"
DATA_DIR = ROOT / "data"
SOL_BEST = ROOT / "best-solutions"
SOL_FINAL = ROOT / "solutions_hybrid_default"
RANDOM_CSV = ROOT / "tables" / "aco-random-comparison-postfix.csv"
OUT_DIR = ROOT / "graficas"
OUT_DIR.mkdir(exist_ok=True)

INSTANCES = [f"i{n:02d}" for n in range(1, 31)]

FINAL_LABEL = "Hybrid (ACO+ALNS+Polish)"
RANDOM_LABEL = "Random+VNS"
BEST_LABEL = "best (oficial)"

RX_VIOL = re.compile(r"Total violations\s*=\s*(\d+)")
RX_COST = re.compile(r"Total cost\s*=\s*(-?\d+)")
RX_BREAKDOWN = re.compile(
    r"^([A-Za-z]+)\.+\s*(-?\d+)\s*\(\s*(\d+)\s*X\s*(-?\d+)\s*\)$"
)

SOFT_COMPONENTS = [
    "RoomAgeMix", "RoomSkillLevel", "ContinuityOfCare",
    "ExcessiveNurseWorkload", "OpenOperatingTheater", "SurgeonTransfer",
    "PatientDelay", "ElectiveUnscheduledPatients",
]


def validate(instance_file: Path, solution_file: Path):
    if not solution_file.exists():
        return None
    try:
        out = subprocess.run(
            [str(VALIDATOR), str(instance_file), str(solution_file)],
            capture_output=True, text=True, timeout=60,
        )
    except Exception as e:
        print(f"[error] {solution_file.name}: {e}", file=sys.stderr)
        return None
    text = out.stdout
    mv = RX_VIOL.search(text)
    mc = RX_COST.search(text)
    if not mv or not mc:
        return None
    components = {}
    for line in text.splitlines():
        m = RX_BREAKDOWN.match(line.strip())
        if m and m.group(1) in SOFT_COMPONENTS:
            components[m.group(1)] = int(m.group(2))
    return {
        "violations": int(mv.group(1)),
        "cost": int(mc.group(1)),
        "components": components,
    }


def read_random_costs():
    d = {}
    with RANDOM_CSV.open() as f:
        for row in csv.DictReader(f):
            d[row["instance"]] = {
                "cost": int(row["random_cost"]),
                "violations": int(row["random_violations"]),
            }
    return d


print(f"Validando best + hybrid (60 ejecuciones)...")
print(f"Random: leyendo de {RANDOM_CSV.name}")
random_data = read_random_costs()

results = {}
for inst in INSTANCES:
    inst_file = DATA_DIR / f"{inst}.json"
    results[inst] = {
        "best":   validate(inst_file, SOL_BEST / f"sol_{inst}.json"),
        "final":  validate(inst_file, SOL_FINAL / f"{inst}_solution.json"),
        "random": random_data.get(inst),
    }
    line = [f"{inst}"]
    for k in ("best", "final", "random"):
        r = results[inst][k]
        line.append(f"{k}={r['cost'] if r else 'NA'}")
    print("  " + "  ".join(line))


# CSV resumen
csv_path = OUT_DIR / "comparison_summary_final.csv"
with csv_path.open("w", newline="") as f:
    w = csv.writer(f)
    w.writerow([
        "instance", "best_cost", "final_cost", "random_cost",
        "final_gap_pct", "random_gap_pct", "final_minus_random",
    ])
    for inst in INSTANCES:
        r = results[inst]
        b, a, rd = r["best"], r["final"], r["random"]
        bcost = b["cost"] if b else None
        acost = a["cost"] if a else None
        rcost = rd["cost"] if rd else None
        agap = (acost - bcost) / bcost * 100 if (bcost and acost is not None) else ""
        rgap = (rcost - bcost) / bcost * 100 if (bcost and rcost is not None) else ""
        diff = (acost - rcost) if (acost is not None and rcost is not None) else ""
        w.writerow([
            inst,
            bcost if bcost is not None else "NA",
            acost if acost is not None else "NA",
            rcost if rcost is not None else "NA",
            f"{agap:.2f}" if agap != "" else "NA",
            f"{rgap:.2f}" if rgap != "" else "NA",
            diff if diff != "" else "NA",
        ])
print(f"\nCSV resumen: {csv_path}")


# Datos numericos
inst_idx, best_costs, final_costs, rnd_costs = [], [], [], []
for inst in INSTANCES:
    r = results[inst]
    if r["best"] and r["final"] and r["random"]:
        inst_idx.append(inst)
        best_costs.append(r["best"]["cost"])
        final_costs.append(r["final"]["cost"])
        rnd_costs.append(r["random"]["cost"])

best = np.array(best_costs)
final = np.array(final_costs)
rnd = np.array(rnd_costs)
final_gap = (final - best) / best * 100
rnd_gap = (rnd - best) / best * 100


# Figura 1: barras agrupadas — coste absoluto
fig, ax = plt.subplots(figsize=(16, 6))
x = np.arange(len(inst_idx))
w = 0.27
ax.bar(x - w, best, width=w, label=BEST_LABEL, color="#2ca02c")
ax.bar(x, final, width=w, label=FINAL_LABEL, color="#1f77b4")
ax.bar(x + w, rnd, width=w, label=RANDOM_LABEL, color="#d62728")
ax.set_xticks(x)
ax.set_xticklabels(inst_idx, rotation=45, ha="right")
ax.set_ylabel("Coste total (validador oficial)")
ax.set_title("Coste por instancia — best vs Hybrid vs Random (i01–i30, 600s/inst)")
ax.legend()
ax.grid(axis="y", alpha=0.3)
fig.tight_layout()
fig.savefig(OUT_DIR / "fig1_final_costes_absolutos.png", dpi=120)
plt.close(fig)


# Figura 2: gap relativo
fig, ax = plt.subplots(figsize=(16, 6))
ax.bar(x - 0.2, final_gap, width=0.4, label=FINAL_LABEL, color="#1f77b4")
ax.bar(x + 0.2, rnd_gap, width=0.4, label=RANDOM_LABEL, color="#d62728")
ax.axhline(0, color="black", linewidth=0.8)
ax.set_xticks(x)
ax.set_xticklabels(inst_idx, rotation=45, ha="right")
ax.set_ylabel("Gap respecto a best (%)")
ax.set_title("Distancia porcentual respecto a la mejor solución oficial (600s/inst)")
ax.legend()
ax.grid(axis="y", alpha=0.3)
fig.tight_layout()
fig.savefig(OUT_DIR / "fig2_final_gap_relativo.png", dpi=120)
plt.close(fig)


# Figura 3: scatter Hybrid vs Random
fig, ax = plt.subplots(figsize=(8, 8))
mx = max(rnd.max(), final.max()) * 1.05
ax.plot([0, mx], [0, mx], "--", color="gray", linewidth=1, label="y = x")
ax.scatter(rnd, final, s=40, alpha=0.85, color="#1f77b4",
            edgecolor="black", linewidth=0.5)
for i, name in enumerate(inst_idx):
    if abs(final[i] - rnd[i]) / max(rnd[i], 1) > 0.05:
        ax.annotate(name, (rnd[i], final[i]), fontsize=7, alpha=0.7,
                    xytext=(3, 2), textcoords="offset points")
ax.set_xlabel("Coste " + RANDOM_LABEL)
ax.set_ylabel("Coste " + FINAL_LABEL)
ax.set_title(f"{FINAL_LABEL} vs {RANDOM_LABEL}\n"
              "puntos bajo y=x => Hybrid mejor que Random")
ax.set_xlim(0, mx)
ax.set_ylim(0, mx)
ax.grid(alpha=0.3)
ax.legend()
fig.tight_layout()
fig.savefig(OUT_DIR / "fig3_final_vs_random_scatter.png", dpi=120)
plt.close(fig)


# Figura 4: boxplot del gap
fig, ax = plt.subplots(figsize=(7, 6))
bp = ax.boxplot([final_gap, rnd_gap],
                tick_labels=[FINAL_LABEL, RANDOM_LABEL],
                patch_artist=True, widths=0.5)
for patch, c in zip(bp["boxes"], ["#1f77b4", "#d62728"]):
    patch.set_facecolor(c)
    patch.set_alpha(0.6)
ax.set_ylabel("Gap respecto a best (%)")
ax.set_title("Distribución del gap a best en i01–i30 (600s/inst)")
ax.grid(axis="y", alpha=0.3)
ax.axhline(0, color="black", linewidth=0.8)
for i, data in enumerate([final_gap, rnd_gap], start=1):
    ax.scatter([i], [data.mean()], color="black", marker="D", s=40, zorder=5)
    ax.text(i + 0.07, data.mean(), f"μ={data.mean():.1f}%", va="center")
fig.tight_layout()
fig.savefig(OUT_DIR / "fig4_final_boxplot_gap.png", dpi=120)
plt.close(fig)


# Figura 5: componentes agregados de best y hybrid (random no se valida por
# falta de soluciones fisicas; se omite del breakdown)
import json as _json
agg = {"best": {c: 0 for c in SOFT_COMPONENTS},
        "final": {c: 0 for c in SOFT_COMPONENTS}}
# Cada instancia tiene sus propios pesos; sumamos coste*peso por instancia
for inst in inst_idx:
    with (DATA_DIR / f"{inst}.json").open() as f:
        weights_dict = _json.load(f)["weights"]
    W = {
        "RoomAgeMix": weights_dict["room_mixed_age"],
        "RoomSkillLevel": weights_dict["room_nurse_skill"],
        "ContinuityOfCare": weights_dict["continuity_of_care"],
        "ExcessiveNurseWorkload": weights_dict["nurse_eccessive_workload"],
        "OpenOperatingTheater": weights_dict["open_operating_theater"],
        "SurgeonTransfer": weights_dict["surgeon_transfer"],
        "PatientDelay": weights_dict["patient_delay"],
        "ElectiveUnscheduledPatients": weights_dict["unscheduled_optional"],
    }
    for kind in ("best", "final"):
        comps = results[inst][kind]["components"]
        for c, raw in comps.items():
            agg[kind][c] += raw * W.get(c, 1)

fig, ax = plt.subplots(figsize=(11, 6))
labels = SOFT_COMPONENTS
xs = np.arange(len(labels))
ax.bar(xs - 0.2, [agg["best"][c] for c in labels], width=0.4, label=BEST_LABEL, color="#2ca02c")
ax.bar(xs + 0.2, [agg["final"][c] for c in labels], width=0.4, label=FINAL_LABEL, color="#1f77b4")
ax.set_xticks(xs)
ax.set_xticklabels(labels, rotation=30, ha="right")
ax.set_ylabel("Coste agregado i01–i30 (con pesos)")
ax.set_title("Coste agregado por componente blando — sumatorio sobre i01–i30 (600s/inst)\n"
              "(Random omitido: no quedan las soluciones físicas; ver fig 1-4)")
ax.legend()
ax.grid(axis="y", alpha=0.3)
fig.tight_layout()
fig.savefig(OUT_DIR / "fig5_final_componentes_agregados.png", dpi=120)
plt.close(fig)


# Resumen consola
print(f"\n=== Resumen agregado (i01–i30, 600s/inst, validador oficial) ===")
print(f"  Instancias completas      : {len(inst_idx)}/30")
print(f"  Coste agregado best        : {best.sum():>10,d}")
print(f"  Coste agregado {FINAL_LABEL:21s}: {final.sum():>10,d}  "
      f"gap medio {final_gap.mean():+.2f}%  mediana {np.median(final_gap):+.2f}%")
print(f"  Coste agregado {RANDOM_LABEL:21s}: {rnd.sum():>10,d}  "
      f"gap medio {rnd_gap.mean():+.2f}%  mediana {np.median(rnd_gap):+.2f}%")
print(f"  Hybrid mejor que Random   : {(final < rnd).sum()}/{len(inst_idx)} instancias")
print(f"\nFiguras en {OUT_DIR}/:")
for p in sorted(OUT_DIR.glob("fig*_final_*.png")):
    print(f"  {p.relative_to(ROOT)}")
