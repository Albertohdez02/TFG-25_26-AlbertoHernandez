#!/usr/bin/env python3
"""Analisis estadistico multi-seed (5 seeds x 30 instancias) con preset=hybrid.

Genera:
  - tables/aco-multiseed-stats.csv: por instancia con media, std, min, max, IQR
  - tables/aco-multiseed-wilcoxon.csv: test pareado vs postfix
  - graficas/multiseed_boxplot.png: distribucion por instancia
  - resumen agregado en stdout
"""

import csv, re, subprocess, statistics
from pathlib import Path

try:
    from scipy import stats as scipy_stats
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAS_MPL = True
except ImportError:
    HAS_MPL = False

ROOT = Path(__file__).resolve().parent.parent
VALIDATOR = ROOT / "validator" / "IHTP_Validator"
DATA_DIR = ROOT / "data"
INSTANCES = [f"i{n:02d}" for n in range(1, 31)]
SEEDS = [42, 137, 991, 5043, 7919]
COST_RE = re.compile(r"^Total cost = (\d+)$", re.MULTILINE)
VIOL_RE = re.compile(r"^Total violations = (\d+)$", re.MULTILINE)


def validate(inst, sol):
    if not sol.exists():
        return None, None
    r = subprocess.run([str(VALIDATOR), str(DATA_DIR / f"{inst}.json"), str(sol)],
                       capture_output=True, text=True, timeout=120)
    cm = COST_RE.search(r.stdout)
    vm = VIOL_RE.search(r.stdout)
    return (int(vm.group(1)) if vm else None,
            int(cm.group(1)) if cm else None)


def read_postfix():
    d = {}
    with (ROOT / "tables" / "aco-random-comparison-postfix.csv").open() as f:
        for row in csv.DictReader(f):
            d[row["instance"]] = int(row["aco_cost"])
    return d


def main():
    post = read_postfix()
    bests = {}
    for inst in INSTANCES:
        _, bc = validate(inst, ROOT / "best-solutions" / f"sol_{inst}.json")
        bests[inst] = bc

    # Recolectar costos por (inst, seed)
    costs = {inst: [] for inst in INSTANCES}
    missing = []
    for inst in INSTANCES:
        for seed in SEEDS:
            sol = ROOT / f"solutions_multiseed/seed_{seed}/{inst}_solution.json"
            _, c = validate(inst, sol)
            if c is None:
                missing.append((inst, seed))
            else:
                costs[inst].append(c)

    if missing:
        print(f"WARN: {len(missing)} ejecuciones faltantes: {missing[:5]}...")

    # Estadisticas por instancia
    print(f"\n{'inst':>4} {'best':>7} {'post':>7} {'mean':>8} {'std':>6} "
          f"{'min':>7} {'max':>7} {'gap_min':>7} {'gap_mean':>8} {'gap_max':>7}")
    print("-" * 88)
    stats_rows = []
    for inst in INSTANCES:
        cs = costs[inst]
        if not cs:
            print(f"{inst:>4} MISSING")
            continue
        bc = bests[inst]
        po = post[inst]
        mn, mx = min(cs), max(cs)
        avg = statistics.mean(cs)
        sd = statistics.stdev(cs) if len(cs) > 1 else 0.0
        gap_min = 100 * (mn - bc) / bc if bc else 0
        gap_avg = 100 * (avg - bc) / bc if bc else 0
        gap_max = 100 * (mx - bc) / bc if bc else 0
        print(f"{inst:>4} {bc:>7} {po:>7} {avg:>8.0f} {sd:>6.0f} "
              f"{mn:>7} {mx:>7} {gap_min:>+6.1f}% {gap_avg:>+7.1f}% {gap_max:>+6.1f}%")
        stats_rows.append({
            "instance": inst, "best": bc, "postfix": po,
            "n": len(cs), "min": mn, "max": mx, "mean": round(avg, 2),
            "std": round(sd, 2),
            "gap_min_pct": round(gap_min, 2),
            "gap_mean_pct": round(gap_avg, 2),
            "gap_max_pct": round(gap_max, 2),
            **{f"seed_{s}": cs[i] if i < len(cs) else None
               for i, s in enumerate(SEEDS)}
        })

    # Agregados
    tot_best = sum(bests[i] for i in INSTANCES if costs[i])
    tot_post = sum(post[i] for i in INSTANCES if costs[i])
    tot_mean = sum(statistics.mean(costs[i]) for i in INSTANCES if costs[i])
    tot_min = sum(min(costs[i]) for i in INSTANCES if costs[i])
    print("-" * 88)
    print(f"TOTAL  {tot_best:>7} {tot_post:>7} {tot_mean:>8.0f}        {tot_min:>7}")
    print(f"\nGap medio (mean por inst):  {100 * (tot_mean - tot_best) / tot_best:+.2f}%")
    print(f"Gap medio (min por inst):   {100 * (tot_min - tot_best) / tot_best:+.2f}%")
    print(f"Gap medio postfix vs best:  {100 * (tot_post - tot_best) / tot_best:+.2f}%")

    # Wilcoxon: por instancia, los 5 seeds de hybrid vs el valor postfix
    # (test de hipotesis: hybrid es significativamente mejor que postfix)
    if HAS_SCIPY:
        # Wilcoxon agregado sobre las 30 instancias (post[inst] vs mean(costs[inst]))
        post_vec = [post[i] for i in INSTANCES if costs[i]]
        mean_vec = [statistics.mean(costs[i]) for i in INSTANCES if costs[i]]
        try:
            res = scipy_stats.wilcoxon(post_vec, mean_vec, alternative="greater")
            print(f"\nWilcoxon pareado (postfix vs hybrid_mean), 30 instancias:")
            print(f"  H0: postfix == hybrid_mean")
            print(f"  H1: postfix > hybrid_mean (hybrid es mejor)")
            print(f"  W-statistic: {res.statistic:.2f}, p-value: {res.pvalue:.6g}")
        except Exception as e:
            print(f"Wilcoxon falló: {e}")

    # CSV
    out_csv = ROOT / "tables" / "aco-multiseed-stats.csv"
    with out_csv.open("w", newline="") as f:
        if stats_rows:
            w = csv.DictWriter(f, fieldnames=list(stats_rows[0].keys()))
            w.writeheader()
            w.writerows(stats_rows)
    print(f"\nCSV: {out_csv}")

    # Boxplot
    if HAS_MPL:
        outpng = ROOT / "graficas" / "multiseed_boxplot_hybrid.png"
        outpng.parent.mkdir(exist_ok=True)
        instances_with_data = [i for i in INSTANCES if costs[i]]
        data = [costs[i] for i in instances_with_data]
        fig, ax = plt.subplots(figsize=(14, 6))
        ax.boxplot(data, labels=instances_with_data, showmeans=True)
        # marcar postfix y best
        x = list(range(1, len(instances_with_data) + 1))
        ax.scatter(x, [post[i] for i in instances_with_data], marker="x",
                    color="red", label="postfix", zorder=3)
        ax.scatter(x, [bests[i] for i in instances_with_data], marker="*",
                    color="green", s=80, label="best-known", zorder=3)
        ax.set_xlabel("Instancia")
        ax.set_ylabel("Coste oficial")
        ax.set_title("Distribución multi-seed (5 seeds × 30 inst, preset=hybrid 600s)\n"
                      "Box=cuartiles, ◯=media, ✕=postfix, ★=best-known")
        ax.legend()
        ax.tick_params(axis="x", rotation=45)
        ax.set_yscale("log")
        plt.tight_layout()
        plt.savefig(outpng, dpi=110)
        print(f"PNG: {outpng}")
    else:
        print("(matplotlib no disponible — sin boxplot)")


if __name__ == "__main__":
    main()
