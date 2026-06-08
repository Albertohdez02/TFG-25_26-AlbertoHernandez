#!/usr/bin/env python3
"""Analisis estadistico de un barrido (CSV de sweep.py).

Compara cada valor del parametro contra el valor baseline (default actual),
emparejando por (instancia, seed). Reporta:
  - mean/median del cambio relativo por celda (%) [+ peor; - mejor]
  - wins (celdas donde el candidato mejora) sobre el total
  - Wilcoxon pareado (one-sample sobre diferencias relativas) vs baseline

El cambio relativo por celda normaliza las distintas escalas de coste entre
instancias (i04~2500 vs m26~100000), evitando que las grandes dominen.

Uso: python3 tuning/analyze.py tuning/runs/beta.csv --baseline 2
"""
import argparse, sys
import numpy as np, pandas as pd
from scipy.stats import wilcoxon

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--baseline", required=True, help="valor de referencia (default actual)")
    args = ap.parse_args()

    df = pd.read_csv(args.csv)
    df["value"] = df["value"].astype(str)
    base = str(args.baseline)
    bad = df[df.cost < 0]
    if len(bad):
        print(f"[WARN] {len(bad)} runs fallidos (cost<0):")
        print(bad.to_string(index=False))
        df = df[df.cost >= 0]

    param = df["param"].iloc[0]
    pivot = df.pivot_table(index=["instance", "seed"], columns="value",
                           values="cost")
    if base not in pivot.columns:
        print(f"[ERR] baseline '{base}' no esta entre los valores: {list(pivot.columns)}")
        sys.exit(1)

    values = [v for v in pivot.columns]
    print(f"\n=== Barrido {param} | baseline={base} | "
          f"{pivot.shape[0]} celdas (instancia x seed) ===")
    print(f"{'value':>8} {'sum_cost':>10} {'mean_rel%':>10} {'med_rel%':>9} "
          f"{'wins':>7} {'wilcoxon_p':>11} {'veredicto':>22}")
    print("-" * 86)

    results = []
    for v in values:
        # celdas con ambos valores presentes (evita columnas duplicadas si v==base)
        both = pd.concat([pivot[v].rename("cand"), pivot[base].rename("ref")],
                         axis=1).dropna()
        cand = both["cand"].values.astype(float)
        ref = both["ref"].values.astype(float)
        sum_cost = float(cand.sum())
        rel = cand / ref - 1.0            # <0 mejora, >0 empeora
        mean_rel = 100 * rel.mean()
        med_rel = 100 * np.median(rel)
        wins = int((cand < ref).sum())
        n = len(both)
        if v == base:
            p = np.nan
            verd = "(baseline)"
        else:
            nz = rel[rel != 0]
            if len(nz) >= 1 and not np.allclose(nz, 0):
                try:
                    _, p = wilcoxon(rel)
                except ValueError:
                    p = np.nan
            else:
                p = np.nan
            if np.isnan(p):
                verd = "sin senal"
            elif p < 0.05 and mean_rel < 0:
                verd = "MEJORA signif."
            elif p < 0.05 and mean_rel > 0:
                verd = "EMPEORA signif."
            else:
                verd = "no signif. (ruido)"
        results.append((v, sum_cost, mean_rel, med_rel, wins, n, p, verd))

    # ordenar por mean_rel ascendente (mejores primero), baseline aparte
    results.sort(key=lambda r: (r[0] != base, r[2]))
    for (v, s, mr, md, w, n, p, verd) in results:
        ps = f"{p:.4f}" if not np.isnan(p) else "   -"
        star = " *" if v == base else "  "
        print(f"{v:>8}{star}{int(s):>9} {mr:>+9.2f} {md:>+8.2f} {w:>4}/{n} "
              f"{ps:>11} {verd:>22}")

    print("\nLectura: mean_rel% = cambio medio de coste por celda vs baseline "
          "(negativo = mejor).")
    print("Wilcoxon p<0.05 => la diferencia vs baseline no es ruido.")

if __name__ == "__main__":
    main()
