"""
ablation_analysis.py - Análisis y visualización del Ablation Test IHTC 2024
TFG Alberto Hernandez

Genera las siguientes figuras a partir de ablation_results/ablation_exhaustive_merged.csv:
  1. Heatmap: Δ% leave-one-out por instancia y operador
  2. Barras apiladas: contribución de operadores en config 'all' por instancia
  3. Boxplot: distribución de coste final por configuración (normalizado)
  4. Barras agrupadas: mejora vs aleatoria (%) por config y por instancia
  5. Heatmap: desglose de costes blandos por instancia (config 'all')
  6. Tabla resumen: estadísticas agregadas por config
  7. Barras: único operador activo vs VNS completo (% reducción)

Uso:
    python3 ablation_analysis.py [csv_path]

    csv_path por defecto: ablation_results/ablation_exhaustive_merged.csv
"""

import sys
import warnings
warnings.filterwarnings("ignore")

import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mtick
from matplotlib.colors import TwoSlopeNorm
import seaborn as sns
from pathlib import Path

# ---------------------------------------------------------------------------
# Configuración
# ---------------------------------------------------------------------------

CSV_PATH    = sys.argv[1] if len(sys.argv) > 1 else "ablation_results/ablation_exhaustive_merged.csv"
OUTPUT_DIR  = Path("ablation_results/figures")
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

OP_NAMES = ["ChangeRoom", "ChangeDay", "ChangeOT", "Relocate",
            "SwapRooms",  "SwapDays",  "ToggleOpt", "ChangeNurse"]

COST_COLS = [
    "cost_room_capacity", "cost_room_gender_mix", "cost_room_mixed_age",
    "cost_patient_delay", "cost_unscheduled_optional",
    "cost_surgeon_overtime", "cost_ot_overtime", "cost_open_ot",
    "cost_nurse_skill", "cost_nurse_excessive_workload",
    "cost_continuity_of_care", "cost_surgeon_transfer",
]
COST_LABELS = [
    "RoomCap", "GenderMix", "MixedAge",
    "Delay", "Unscheduled",
    "SurgOT", "OT_OT", "OpenOT",
    "NurseSkill", "NurseWork",
    "Continuity", "SurgTransf",
]

PALETTE = sns.color_palette("tab10")
INSTANCES = None  # se rellena al cargar

# ---------------------------------------------------------------------------
# Carga de datos
# ---------------------------------------------------------------------------

def load_data(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)
    # Conversiones de tipo
    num_cols = ["init_cost", "final_cost", "improvement_pct", "time_s"] + \
               [f"op_{n}" for n in OP_NAMES] + COST_COLS + ["cost_total"]
    for c in num_cols:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")
    df["feasible"] = df["feasible"].astype(str).str.lower() == "true"
    return df


def normalized_cost(df: pd.DataFrame) -> pd.DataFrame:
    """Coste final normalizado por la media de no_LS de cada instancia."""
    ref = df[df["config"] == "no_LS"].groupby("instance")["final_cost"].mean().rename("ref_cost")
    df = df.merge(ref, on="instance")
    df["norm_cost"] = df["final_cost"] / df["ref_cost"]
    return df

# ---------------------------------------------------------------------------
# Helpers estadísticos
# ---------------------------------------------------------------------------

def summary_by_config(df: pd.DataFrame) -> pd.DataFrame:
    g = df.groupby("config")["final_cost"]
    s = pd.DataFrame({
        "mean":   g.mean(),
        "std":    g.std(),
        "min":    g.min(),
        "max":    g.max(),
        "median": g.median(),
    })
    # vs no_LS (usando medias por instancia para evitar sesgo de escala)
    no_ls_mean = df[df["config"] == "no_LS"].groupby("instance")["final_cost"].mean()
    all_mean   = df[df["config"] == "all"  ].groupby("instance")["final_cost"].mean()

    def vs_ref(cfg_name, ref):
        cfg_mean = df[df["config"] == cfg_name].groupby("instance")["final_cost"].mean()
        pct = ((ref - cfg_mean) / ref * 100).mean()
        return pct

    s["vs_noLS_pct"] = [vs_ref(c, no_ls_mean) for c in s.index]
    s["vs_all_delta"] = [vs_ref(c, all_mean) for c in s.index]
    return s


def loo_table(df: pd.DataFrame) -> pd.DataFrame:
    """Tabla leave-one-out: Δ% coste al eliminar cada operador, por instancia."""
    rows = []
    for inst in df["instance"].unique():
        sub = df[df["instance"] == inst]
        all_mean = sub[sub["config"] == "all"]["final_cost"].mean()
        for op in OP_NAMES:
            cfg = f"no_{op}"
            cfg_mean = sub[sub["config"] == cfg]["final_cost"].mean()
            delta_pct = (cfg_mean - all_mean) / all_mean * 100 if all_mean > 0 else 0.0
            rows.append({"instance": inst, "operator": op, "delta_pct": delta_pct})
    return pd.DataFrame(rows)


def op_contribution(df: pd.DataFrame) -> pd.DataFrame:
    """% de mejoras por operador en config 'all', por instancia."""
    sub = df[df["config"] == "all"].copy()
    rows = []
    for inst in sub["instance"].unique():
        s = sub[sub["instance"] == inst]
        total = sum(s[f"op_{n}"].sum() for n in OP_NAMES)
        for op in OP_NAMES:
            pct = s[f"op_{op}"].sum() / total * 100 if total > 0 else 0.0
            rows.append({"instance": inst, "operator": op, "pct": pct})
    return pd.DataFrame(rows)


def solo_vs_all(df: pd.DataFrame) -> pd.DataFrame:
    """Mejora % vs no_LS para 'only_X' y para 'all', por instancia."""
    rows = []
    for inst in df["instance"].unique():
        sub = df[df["instance"] == inst]
        no_ls = sub[sub["config"] == "no_LS"]["final_cost"].mean()
        all_v = sub[sub["config"] == "all"]["final_cost"].mean()
        all_pct = (no_ls - all_v) / no_ls * 100 if no_ls > 0 else 0.0
        for op in OP_NAMES:
            only_v = sub[sub["config"] == f"only_{op}"]["final_cost"].mean()
            only_pct = (no_ls - only_v) / no_ls * 100 if no_ls > 0 else 0.0
            rows.append({"instance": inst, "operator": op,
                         "only_pct": only_pct, "all_pct": all_pct})
    return pd.DataFrame(rows)

# ---------------------------------------------------------------------------
# Figura 1: Heatmap leave-one-out
# ---------------------------------------------------------------------------

def fig_loo_heatmap(df: pd.DataFrame):
    loo = loo_table(df)
    pivot = loo.pivot(index="operator", columns="instance", values="delta_pct")
    # Ordenar operadores por media descendente
    pivot = pivot.loc[pivot.mean(axis=1).sort_values(ascending=False).index]

    fig, ax = plt.subplots(figsize=(9, 5))
    vmax = max(abs(pivot.values.max()), abs(pivot.values.min()), 1)
    norm = TwoSlopeNorm(vmin=-vmax, vcenter=0, vmax=vmax)
    sns.heatmap(pivot, ax=ax, cmap="RdYlGn_r", norm=norm,
                annot=True, fmt=".1f", linewidths=0.5,
                cbar_kws={"label": "Δ% coste (+ = degradación al eliminar)"})
    ax.set_title("Leave-One-Out: Δ% coste al eliminar cada operador", fontsize=13, pad=12)
    ax.set_xlabel("Instancia"); ax.set_ylabel("Operador eliminado")
    plt.tight_layout()
    path = OUTPUT_DIR / "fig1_loo_heatmap.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"  Guardado: {path}")


# ---------------------------------------------------------------------------
# Figura 2: Contribución por operador (barras apiladas)
# ---------------------------------------------------------------------------

def fig_op_contribution(df: pd.DataFrame):
    contrib = op_contribution(df)
    pivot = contrib.pivot(index="instance", columns="operator", values="pct")
    pivot = pivot[OP_NAMES]  # orden fijo

    fig, ax = plt.subplots(figsize=(10, 5))
    bottom = np.zeros(len(pivot))
    colors = plt.cm.tab10(np.linspace(0, 1, len(OP_NAMES)))
    for i, op in enumerate(OP_NAMES):
        ax.bar(pivot.index, pivot[op], bottom=bottom, label=op,
               color=colors[i], edgecolor="white", linewidth=0.5)
        bottom += pivot[op].values

    ax.set_ylabel("% de mejoras atribuidas")
    ax.set_title("Contribución de cada operador en config 'all'", fontsize=13)
    ax.legend(loc="upper right", fontsize=8, ncol=2)
    ax.yaxis.set_major_formatter(mtick.PercentFormatter())
    plt.tight_layout()
    path = OUTPUT_DIR / "fig2_op_contribution.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"  Guardado: {path}")


# ---------------------------------------------------------------------------
# Figura 3: Boxplot coste normalizado por configuración
# ---------------------------------------------------------------------------

def fig_boxplot_configs(df: pd.DataFrame):
    dft = normalized_cost(df)

    # Separar en grupos
    lo_groups  = {"all": "all", "no_LS": "no_LS"}
    loo_cfgs   = [f"no_{op}"   for op in OP_NAMES]
    solo_cfgs  = [f"only_{op}" for op in OP_NAMES]

    fig, axes = plt.subplots(1, 3, figsize=(16, 5), sharey=False)

    def boxplot_group(ax, cfgs, title, palette=None):
        sub = dft[dft["config"].isin(cfgs)].copy()
        order = [c for c in cfgs if c in sub["config"].unique()]
        # Ordenar por mediana
        medians = sub.groupby("config")["norm_cost"].median()
        order = medians.loc[order].sort_values().index.tolist()
        colors = sns.color_palette("RdYlGn_r", len(order)) if palette is None else palette
        sns.boxplot(data=sub, x="config", y="norm_cost", order=order,
                    palette=colors, ax=ax, linewidth=0.8, fliersize=3)
        ax.set_title(title, fontsize=11)
        ax.set_xlabel("")
        ax.set_ylabel("Coste normalizado (1 = no_LS)")
        ax.tick_params(axis="x", rotation=45)
        ax.axhline(1.0, color="red", linestyle="--", linewidth=0.8, alpha=0.7)

    boxplot_group(axes[0], list(lo_groups.keys()) + loo_cfgs, "Leave-one-out")
    boxplot_group(axes[1], solo_cfgs, "Operador único activo")
    boxplot_group(axes[2], ["all"] + loo_cfgs, "Leave-one-out (zoom)")

    plt.suptitle("Distribución del coste normalizado por configuración", fontsize=13, y=1.01)
    plt.tight_layout()
    path = OUTPUT_DIR / "fig3_boxplot_configs.png"
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Guardado: {path}")


# ---------------------------------------------------------------------------
# Figura 4: Mejora vs aleatoria — solo vs all, por operador
# ---------------------------------------------------------------------------

def fig_solo_vs_all(df: pd.DataFrame):
    data = solo_vs_all(df)
    # Agregar por operador (media sobre instancias)
    agg = data.groupby("operator")[["only_pct", "all_pct"]].mean().reset_index()
    agg = agg.sort_values("only_pct", ascending=True)

    fig, ax = plt.subplots(figsize=(9, 5))
    y = np.arange(len(agg))
    h = 0.35
    bars1 = ax.barh(y - h/2, agg["only_pct"], h, label="Solo este operador",
                    color=PALETTE[0], alpha=0.85)
    bars2 = ax.barh(y + h/2, agg["all_pct"],  h, label="VNS completo (all)",
                    color=PALETTE[2], alpha=0.85)
    ax.set_yticks(y); ax.set_yticklabels(agg["operator"])
    ax.set_xlabel("Mejora % respecto a solución aleatoria (no_LS)")
    ax.set_title("Operador único vs VNS completo: mejora sobre aleatorio", fontsize=13)
    ax.xaxis.set_major_formatter(mtick.PercentFormatter())
    ax.legend()
    ax.axvline(0, color="black", linewidth=0.5)
    # Etiquetas
    for bar in bars1:
        ax.text(bar.get_width() + 0.2, bar.get_y() + bar.get_height()/2,
                f"{bar.get_width():.1f}%", va="center", fontsize=8)
    plt.tight_layout()
    path = OUTPUT_DIR / "fig4_solo_vs_all.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"  Guardado: {path}")


# ---------------------------------------------------------------------------
# Figura 5: Heatmap desglose de costes blandos (config 'all')
# ---------------------------------------------------------------------------

def fig_cost_breakdown(df: pd.DataFrame):
    sub = df[df["config"] == "all"].copy()
    # Media por instancia, como % del coste total
    rows = []
    for inst in sub["instance"].unique():
        s = sub[sub["instance"] == inst]
        total_mean = s["cost_total"].mean()
        for col, label in zip(COST_COLS, COST_LABELS):
            pct = s[col].mean() / total_mean * 100 if total_mean > 0 else 0.0
            rows.append({"instance": inst, "component": label, "pct": pct})
    data = pd.DataFrame(rows)
    pivot = data.pivot(index="component", columns="instance", values="pct")
    # Ordenar por media descendente
    pivot = pivot.loc[pivot.mean(axis=1).sort_values(ascending=False).index]

    fig, ax = plt.subplots(figsize=(9, 6))
    sns.heatmap(pivot, ax=ax, cmap="YlOrRd", annot=True, fmt=".1f",
                linewidths=0.5, cbar_kws={"label": "% del coste total"})
    ax.set_title("Desglose de costes blandos — config 'all' (% del total)", fontsize=13, pad=12)
    ax.set_xlabel("Instancia"); ax.set_ylabel("Componente de coste")
    plt.tight_layout()
    path = OUTPUT_DIR / "fig5_cost_breakdown.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"  Guardado: {path}")


# ---------------------------------------------------------------------------
# Figura 6: LOO Δ% agregado con barras de error
# ---------------------------------------------------------------------------

def fig_loo_summary(df: pd.DataFrame):
    loo = loo_table(df)
    agg = loo.groupby("operator")["delta_pct"].agg(["mean", "std"]).reset_index()
    agg = agg.sort_values("mean", ascending=False)

    colors = ["#d62728" if m > 0 else "#2ca02c" for m in agg["mean"]]
    fig, ax = plt.subplots(figsize=(9, 4))
    bars = ax.bar(agg["operator"], agg["mean"], yerr=agg["std"],
                  color=colors, edgecolor="black", linewidth=0.6,
                  capsize=5, alpha=0.85, error_kw={"linewidth": 1.2})
    ax.axhline(0, color="black", linewidth=0.8)
    ax.set_ylabel("Δ% coste (+ = empeora al eliminarlo)")
    ax.set_title("Impacto marginal de cada operador — leave-one-out\n(media ± desv.típ. sobre 6 instancias)", fontsize=12)
    ax.yaxis.set_major_formatter(mtick.PercentFormatter())
    # Etiquetas
    for bar, (_, row) in zip(bars, agg.iterrows()):
        ypos = row["mean"] + (row["std"] if row["mean"] >= 0 else -row["std"]) + 0.05
        ax.text(bar.get_x() + bar.get_width()/2, ypos + 0.2,
                f"{row['mean']:+.1f}%", ha="center", va="bottom", fontsize=9)
    plt.tight_layout()
    path = OUTPUT_DIR / "fig6_loo_summary.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"  Guardado: {path}")


# ---------------------------------------------------------------------------
# Tabla resumen CSV (para incluir en la memoria del TFG)
# ---------------------------------------------------------------------------

def export_summary_table(df: pd.DataFrame):
    loo  = loo_table(df)
    contrib = op_contribution(df)

    # LOO: media y std por operador
    loo_agg = loo.groupby("operator")["delta_pct"].agg(
        loo_mean="mean", loo_std="std").reset_index()

    # Contribución: media y std por operador
    contrib_agg = contrib.groupby("operator")["pct"].agg(
        contrib_mean="mean", contrib_std="std").reset_index()

    # Solo: mejora vs no_LS por operador
    solo = solo_vs_all(df).groupby("operator")["only_pct"].agg(
        solo_mean="mean", solo_std="std").reset_index()

    merged = loo_agg.merge(contrib_agg, on="operator").merge(solo, on="operator")
    merged = merged.sort_values("loo_mean", ascending=False)

    for col in ["loo_mean", "loo_std", "contrib_mean", "contrib_std", "solo_mean", "solo_std"]:
        merged[col] = merged[col].round(2)

    path = OUTPUT_DIR / "summary_table.csv"
    merged.to_csv(path, index=False)
    print(f"  Guardado: {path}")

    # También imprimir en consola
    print("\n=== TABLA RESUMEN AGREGADA (6 instancias) ===\n")
    print(f"{'Operador':<14} {'LOO Δ%':>8} {'±':>6} {'Contrib%':>9} {'±':>6} {'Solo%':>8} {'±':>6}")
    print("-" * 62)
    for _, r in merged.iterrows():
        print(f"{r['operator']:<14} {r['loo_mean']:>+7.2f}% {r['loo_std']:>5.2f}  "
              f"{r['contrib_mean']:>8.1f}% {r['contrib_std']:>5.1f}  "
              f"{r['solo_mean']:>7.1f}% {r['solo_std']:>5.1f}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    print(f"Cargando datos de: {CSV_PATH}\n")
    df = load_data(CSV_PATH)
    df = normalized_cost(df)
    INSTANCES = sorted(df["instance"].unique())
    print(f"Instancias: {INSTANCES}")
    print(f"Filas totales: {len(df)}")
    print(f"Soluciones infactibles: {(~df['feasible']).sum()}\n")

    print("Generando figuras...\n")
    fig_loo_heatmap(df)
    fig_op_contribution(df)
    fig_boxplot_configs(df)
    fig_solo_vs_all(df)
    fig_cost_breakdown(df)
    fig_loo_summary(df)
    export_summary_table(df)

    print(f"\nTodas las figuras guardadas en: {OUTPUT_DIR}/")
