#!/usr/bin/env python3
"""Compara Phase 2 (anti-convergencia) vs Polish, ABCF y postfix."""

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
    print(f"{'inst':>4} | {'best':>8} {'post':>8} {'POLISH':>8} {'PHASE2':>8} | "
          f"{'g_POL':>6} {'g_P2':>6} | {'P2-POL':>7} {'P2-post':>8}")
    print("-"*92)
    tot_best=tot_post=tot_pol=tot_p2=0
    p2_wins_vs_post = p2_wins_vs_pol = 0
    for inst in INSTANCES:
        _, bc = validate(inst, ROOT / "best-solutions" / f"sol_{inst}.json")
        _, pol = validate(inst, ROOT / "solutions_polish_default" / f"{inst}_solution.json")
        _, p2 = validate(inst, ROOT / "solutions_phase2_default" / f"{inst}_solution.json")
        if p2 is None or bc is None: print(f"{inst:>4} | MISSING"); continue
        po = post[inst]
        gp = 100.0*(pol-bc)/bc if bc else 0
        g2 = 100.0*(p2-bc)/bc if bc else 0
        d_pol = p2 - pol
        d_post = p2 - po
        mark = ""
        if p2 < po:  p2_wins_vs_post += 1; mark += " P+"
        if p2 < pol: p2_wins_vs_pol  += 1; mark += " PL+"
        elif p2 > pol:                              mark += " PL-"
        print(f"{inst:>4} | {bc:>8} {po:>8} {pol:>8} {p2:>8} | "
              f"{gp:>+5.1f}% {g2:>+5.1f}% | {d_pol:>+7} {d_post:>+8}{mark}")
        tot_best += bc; tot_post += po; tot_pol += pol; tot_p2 += p2
        rows.append((inst, bc, po, pol, p2))

    print("-"*92)
    print(f"{'TOTAL':>4} | {tot_best:>8} {tot_post:>8} {tot_pol:>8} {tot_p2:>8} | "
          f"{100*(tot_pol-tot_best)/tot_best:>+5.1f}% "
          f"{100*(tot_p2 -tot_best)/tot_best:>+5.1f}% | "
          f"{tot_p2-tot_pol:>+7} {tot_p2-tot_post:>+8}")
    n = len(rows)
    print(f"\nWins PHASE2 vs postfix: {p2_wins_vs_post}/{n}")
    print(f"Wins PHASE2 vs POLISH:  {p2_wins_vs_pol}/{n}")
    print()
    print(f"Gap medio postfix:  {100*(tot_post-tot_best)/tot_best:+.2f}%")
    print(f"Gap medio POLISH:   {100*(tot_pol -tot_best)/tot_best:+.2f}%")
    print(f"Gap medio PHASE2:   {100*(tot_p2  -tot_best)/tot_best:+.2f}%")
    print(f"Mejora PHASE2 vs POLISH:  {100*(tot_p2-tot_pol)/tot_pol:+.2f}%")
    print(f"Mejora PHASE2 vs postfix: {100*(tot_p2-tot_post)/tot_post:+.2f}%")

    out = ROOT / "tables" / "aco-phase2-vs-polish-vs-postfix.csv"
    with out.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["instance", "best", "postfix", "POLISH", "PHASE2",
                    "gap_POLISH_pct", "gap_PHASE2_pct",
                    "PHASE2_minus_POLISH", "PHASE2_minus_postfix"])
        for inst, bc, po, pol, p2 in rows:
            w.writerow([inst, bc, po, pol, p2,
                        round(100*(pol-bc)/bc, 2),
                        round(100*(p2-bc)/bc, 2),
                        p2-pol, p2-po])
    print(f"\nCSV: {out}")

if __name__ == "__main__":
    main()
