#!/usr/bin/env python3
"""Compara HYBRID (Fase 3) vs Polish, postfix y best-known."""

import csv, re, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VALIDATOR = ROOT / "validator" / "IHTP_Validator"
DATA_DIR = ROOT / "data"
INSTANCES = [f"i{n:02d}" for n in range(1, 31)]
COST_RE = re.compile(r"^Total cost = (\d+)$", re.MULTILINE)
VIOL_RE = re.compile(r"^Total violations = (\d+)$", re.MULTILINE)

def validate(inst, sol):
    if not sol.exists(): return None, None
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
    rows = []
    print(f"{'inst':>4} | {'best':>8} {'post':>8} {'POLISH':>8} {'HYBRID':>8} | "
          f"{'g_POL':>6} {'g_HY':>6} | {'HY-POL':>7} {'HY-post':>8}")
    print("-"*92)
    tot_best=tot_post=tot_pol=tot_hy=0
    hy_wins_vs_post = hy_wins_vs_pol = 0
    for inst in INSTANCES:
        _, bc = validate(inst, ROOT / "best-solutions" / f"sol_{inst}.json")
        _, pol = validate(inst, ROOT / "solutions_polish_default" / f"{inst}_solution.json")
        _, hy = validate(inst, ROOT / "solutions_hybrid_default" / f"{inst}_solution.json")
        if hy is None or bc is None: print(f"{inst:>4} | MISSING"); continue
        po = post[inst]
        gp = 100.0*(pol-bc)/bc if bc else 0
        gh = 100.0*(hy-bc)/bc if bc else 0
        d_pol = hy - pol
        d_post = hy - po
        mark = ""
        if hy < po:  hy_wins_vs_post += 1; mark += " P+"
        if hy < pol: hy_wins_vs_pol  += 1; mark += " PL+"
        elif hy > pol:                              mark += " PL-"
        print(f"{inst:>4} | {bc:>8} {po:>8} {pol:>8} {hy:>8} | "
              f"{gp:>+5.1f}% {gh:>+5.1f}% | {d_pol:>+7} {d_post:>+8}{mark}")
        tot_best += bc; tot_post += po; tot_pol += pol; tot_hy += hy
        rows.append((inst, bc, po, pol, hy))

    print("-"*92)
    print(f"{'TOTAL':>4} | {tot_best:>8} {tot_post:>8} {tot_pol:>8} {tot_hy:>8} | "
          f"{100*(tot_pol-tot_best)/tot_best:>+5.1f}% "
          f"{100*(tot_hy -tot_best)/tot_best:>+5.1f}% | "
          f"{tot_hy-tot_pol:>+7} {tot_hy-tot_post:>+8}")
    n = len(rows)
    print(f"\nWins HYBRID vs postfix: {hy_wins_vs_post}/{n}")
    print(f"Wins HYBRID vs POLISH:  {hy_wins_vs_pol}/{n}")
    print()
    print(f"Gap medio postfix:  {100*(tot_post-tot_best)/tot_best:+.2f}%")
    print(f"Gap medio POLISH:   {100*(tot_pol -tot_best)/tot_best:+.2f}%")
    print(f"Gap medio HYBRID:   {100*(tot_hy  -tot_best)/tot_best:+.2f}%")
    print(f"Mejora HYBRID vs POLISH:  {100*(tot_hy-tot_pol)/tot_pol:+.2f}%")
    print(f"Mejora HYBRID vs postfix: {100*(tot_hy-tot_post)/tot_post:+.2f}%")

    out = ROOT / "tables" / "aco-hybrid-vs-polish-vs-postfix.csv"
    with out.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["instance", "best", "postfix", "POLISH", "HYBRID",
                    "gap_POLISH_pct", "gap_HYBRID_pct",
                    "HYBRID_minus_POLISH", "HYBRID_minus_postfix"])
        for inst, bc, po, pol, hy in rows:
            w.writerow([inst, bc, po, pol, hy,
                        round(100*(pol-bc)/bc, 2),
                        round(100*(hy-bc)/bc, 2),
                        hy-pol, hy-po])
    print(f"\nCSV: {out}")

if __name__ == "__main__":
    main()
