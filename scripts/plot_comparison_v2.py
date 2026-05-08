#!/usr/bin/env python3
# Script temporal: genera graficas comparativas de las soluciones de
# best-solutions/, solutions_aco/ y solutions_random/ usando el validador
# oficial IHTC para extraer el coste total real de cada solucion.
#
# Salida: graficas/ (figuras PNG) y graficas/comparison_summary.csv

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
SOL_ACO = ROOT / "solutions_aco"          # ACO post-Fases A+B+C+D
SOL_ACO_V1 = ROOT / "solutions_aco_v1"    # ACO pre-mejoras (referencia)
SOL_RND = ROOT / "solutions_random"
OUT_DIR = ROOT / "graficas_v2"
OUT_DIR.mkdir(exist_ok=True)

INSTANCES = [f"i{n:02d}" for n in range(1, 31)]

# ---------------------------------------------------------------------------
# Validar y extraer (violations, total_cost) de cada solucion
# ---------------------------------------------------------------------------

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


print("Validando soluciones (90 ejecuciones)...")
results = {}
for inst in INSTANCES:
    inst_file = DATA_DIR / f"{inst}.json"
    results[inst] = {
        "best":   validate(inst_file, SOL_BEST / f"sol_{inst}.json"),
        "aco":    validate(inst_file, SOL_ACO  / f"{inst}_solution.json"),
        "random": validate(inst_file, SOL_RND  / f"{inst}_solution.json"),
    }
    line = [f"{inst}"]
    for k in ("best", "aco", "random"):
        r = results[inst][k]
        line.append(f"{k}={r['cost'] if r else 'NA'}/{r['violations'] if r else '?'}")
    print("  " + "  ".join(line))


# ---------------------------------------------------------------------------
# CSV resumen
# ---------------------------------------------------------------------------

csv_path = OUT_DIR / "comparison_summary.csv"
with csv_path.open("w", newline="") as f:
    w = csv.writer(f)
    w.writerow([
        "instance",
        "best_violations", "best_cost",
        "aco_violations",  "aco_cost",
        "random_violations", "random_cost",
        "aco_gap_pct", "random_gap_pct",
        "aco_minus_random",
    ])
    for inst in INSTANCES:
        r = results[inst]
        b, a, rd = r["best"], r["aco"], r["random"]
        bcost = b["cost"] if b else None
        acost = a["cost"] if a else None
        rcost = rd["cost"] if rd else None
        agap = (acost - bcost) / bcost * 100 if (bcost and acost is not None and bcost > 0) else ""
        rgap = (rcost - bcost) / bcost * 100 if (bcost and rcost is not None and bcost > 0) else ""
        diff = (acost - rcost) if (acost is not None and rcost is not None) else ""
        w.writerow([
            inst,
            b["violations"] if b else "NA", bcost if bcost is not None else "NA",
            a["violations"] if a else "NA", acost if acost is not None else "NA",
            rd["violations"] if rd else "NA", rcost if rcost is not None else "NA",
            f"{agap:.2f}" if agap != "" else "NA",
            f"{rgap:.2f}" if rgap != "" else "NA",
            diff if diff != "" else "NA",
        ])
print(f"\nCSV resumen: {csv_path}")


# ---------------------------------------------------------------------------
# Datos numericos
# ---------------------------------------------------------------------------

inst_idx = []
best_costs, aco_costs, rnd_costs = [], [], []
for inst in INSTANCES:
    r = results[inst]
    if r["best"] and r["aco"] and r["random"]:
        inst_idx.append(inst)
        best_costs.append(r["best"]["cost"])
        aco_costs.append(r["aco"]["cost"])
        rnd_costs.append(r["random"]["cost"])

best = np.array(best_costs)
aco  = np.array(aco_costs)
rnd  = np.array(rnd_costs)
aco_gap = (aco - best) / best * 100
rnd_gap = (rnd - best) / best * 100


# Figura 1: barras agrupadas
fig, ax = plt.subplots(figsize=(16, 6))
x = np.arange(len(inst_idx))
w = 0.27
ax.bar(x - w, best, width=w, label="best (oficial)",  color="#2ca02c")
ax.bar(x,     aco,  width=w, label="ACO+VNS",          color="#1f77b4")
ax.bar(x + w, rnd,  width=w, label="Random+VNS",       color="#d62728")
ax.set_xticks(x); ax.set_xticklabels(inst_idx, rotation=45, ha="right")
ax.set_ylabel("Coste total (validador oficial)")
ax.set_title("Coste por instancia — best vs ACO (post-mejoras) vs Random (i01–i30, 600s/inst)")
ax.legend(); ax.grid(axis="y", alpha=0.3)
fig.tight_layout()
fig.savefig(OUT_DIR / "fig1_costes_absolutos.png", dpi=120)
plt.close(fig)


# Figura 2: gap relativo
fig, ax = plt.subplots(figsize=(16, 6))
ax.bar(x - 0.2, aco_gap, width=0.4, label="ACO+VNS",    color="#1f77b4")
ax.bar(x + 0.2, rnd_gap, width=0.4, label="Random+VNS", color="#d62728")
ax.axhline(0, color="black", linewidth=0.8)
ax.set_xticks(x); ax.set_xticklabels(inst_idx, rotation=45, ha="right")
ax.set_ylabel("Gap respecto a best (%)")
ax.set_title("Gap respecto a best — ACO post-mejoras (Fases A+B+C+D) vs Random (600s/inst)")
ax.legend(); ax.grid(axis="y", alpha=0.3)
fig.tight_layout()
fig.savefig(OUT_DIR / "fig2_gap_relativo.png", dpi=120)
plt.close(fig)


# Figura 3: scatter ACO vs Random
fig, ax = plt.subplots(figsize=(8, 8))
mx = max(rnd.max(), aco.max()) * 1.05
ax.plot([0, mx], [0, mx], "--", color="gray", linewidth=1)
ax.scatter(rnd, aco, s=40, alpha=0.85, color="#1f77b4", edgecolor="black", linewidth=0.5)
for i, name in enumerate(inst_idx):
    if abs(aco[i] - rnd[i]) / max(rnd[i], 1) > 0.04:
        ax.annotate(name, (rnd[i], aco[i]), fontsize=7, alpha=0.7,
                    xytext=(3, 2), textcoords="offset points")
ax.set_xlabel("Coste Random+VNS")
ax.set_ylabel("Coste ACO+VNS")
ax.set_title("ACO post-mejoras vs Random — puntos bajo y=x significan que ACO es mejor")
ax.set_xlim(0, mx); ax.set_ylim(0, mx); ax.grid(alpha=0.3)
fig.tight_layout()
fig.savefig(OUT_DIR / "fig3_aco_vs_random_scatter.png", dpi=120)
plt.close(fig)


# Figura 4: boxplot del gap
fig, ax = plt.subplots(figsize=(7, 6))
bp = ax.boxplot([aco_gap, rnd_gap], tick_labels=["ACO+VNS", "Random+VNS"],
                patch_artist=True, widths=0.5)
for patch, c in zip(bp["boxes"], ["#1f77b4", "#d62728"]):
    patch.set_facecolor(c); patch.set_alpha(0.6)
ax.set_ylabel("Gap respecto a best (%)")
ax.set_title("Gap a best i01–i30 — ACO post-mejoras vs Random (600s/inst)")
ax.grid(axis="y", alpha=0.3)
ax.axhline(0, color="black", linewidth=0.8)
for i, data in enumerate([aco_gap, rnd_gap], start=1):
    ax.scatter([i], [data.mean()], color="black", marker="D", s=40, zorder=5)
    ax.text(i + 0.07, data.mean(), f"μ={data.mean():.1f}%", va="center")
fig.tight_layout()
fig.savefig(OUT_DIR / "fig4_boxplot_gap.png", dpi=120)
plt.close(fig)


# Figura 5: descomposicion de coste por componente blando
import json as _json
agg = {"best": {c: 0 for c in SOFT_COMPONENTS},
       "aco":  {c: 0 for c in SOFT_COMPONENTS},
       "random": {c: 0 for c in SOFT_COMPONENTS}}
with (DATA_DIR / "i01.json").open() as f:
    weights_dict = _json.load(f)["weights"]
W = {
    "RoomAgeMix":            weights_dict["room_mixed_age"],
    "RoomSkillLevel":        weights_dict["room_nurse_skill"],
    "ContinuityOfCare":      weights_dict["continuity_of_care"],
    "ExcessiveNurseWorkload": weights_dict["nurse_eccessive_workload"],
    "OpenOperatingTheater":  weights_dict["open_operating_theater"],
    "SurgeonTransfer":       weights_dict["surgeon_transfer"],
    "PatientDelay":          weights_dict["patient_delay"],
    "ElectiveUnscheduledPatients": weights_dict["unscheduled_optional"],
}
for inst in inst_idx:
    for kind in ("best", "aco", "random"):
        comps = results[inst][kind]["components"]
        for c, raw in comps.items():
            agg[kind][c] += raw * W.get(c, 1)

fig, ax = plt.subplots(figsize=(11, 6))
labels = SOFT_COMPONENTS
xs = np.arange(len(labels))
ax.bar(xs - 0.27, [agg["best"][c]   for c in labels], width=0.27, label="best",   color="#2ca02c")
ax.bar(xs,        [agg["aco"][c]    for c in labels], width=0.27, label="ACO",    color="#1f77b4")
ax.bar(xs + 0.27, [agg["random"][c] for c in labels], width=0.27, label="Random", color="#d62728")
ax.set_xticks(xs); ax.set_xticklabels(labels, rotation=30, ha="right")
ax.set_ylabel("Coste agregado i01–i30 (con pesos)")
ax.set_title("Coste agregado por componente — ACO post-mejoras vs Random vs best (600s/inst)")
ax.legend(); ax.grid(axis="y", alpha=0.3)
fig.tight_layout()
fig.savefig(OUT_DIR / "fig5_componentes_agregados.png", dpi=120)
plt.close(fig)


# Resumen consola
print("\n=== Resumen agregado (i01–i30, 600s/inst, validador oficial) ===")
print(f"  Instancias completas : {len(inst_idx)}/30")
print(f"  Coste agregado best   : {best.sum():>10,d}")
print(f"  Coste agregado ACO    : {aco.sum():>10,d}  gap medio {aco_gap.mean():+.2f}%  mediana {np.median(aco_gap):+.2f}%")
print(f"  Coste agregado random : {rnd.sum():>10,d}  gap medio {rnd_gap.mean():+.2f}%  mediana {np.median(rnd_gap):+.2f}%")
print(f"  ACO mejor que random  : {(aco < rnd).sum()}/{len(inst_idx)} instancias")
print(f"\nFiguras en {OUT_DIR}/")
