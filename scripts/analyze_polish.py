#!/usr/bin/env python3
"""Compara A+B+C+F+Polish (Some-touches Fase 1) vs A+B+C+F y postfix."""

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
    p = ROOT / "tables" / "aco-random-comparison-postfix.csv"
    d = {}
    with p.open() as f:
        for row in csv.DictReader(f):
            d[row["instance"]] = int(row["aco_cost"])
    return d

def main():
    post = read_postfix()
    rows = []
    print(f"{'inst':>4} | {'best':>8} {'post':>8} {'ABCF':>8} {'POLISH':>8} | "
          f"{'g_ABCF':>7} {'g_POL':>7} | {'P-ABCF':>7} {'P-post':>7}")
    print("-"*92)
    tot_best=tot_post=tot_abcf=tot_pol=0
    pol_wins_vs_post = pol_wins_vs_abcf = 0
    for inst in INSTANCES:
        _, bc = validate(inst, ROOT / "best-solutions" / f"sol_{inst}.json")
        _, abcf = validate(inst, ROOT / "solutions_blockF_default" / f"{inst}_solution.json")
        _, pol = validate(inst, ROOT / "solutions_polish_default" / f"{inst}_solution.json")
        if pol is None or bc is None: print(f"{inst:>4} | MISSING"); continue
        po = post[inst]
        gap_abcf = 100.0*(abcf-bc)/bc if bc else 0
        gap_pol = 100.0*(pol-bc)/bc if bc else 0
        d_abcf = pol - abcf
        d_post = pol - po
        mark = ""
        if pol < po:   pol_wins_vs_post += 1; mark += " P+"
        if pol < abcf: pol_wins_vs_abcf += 1; mark += " F+"
        elif pol > abcf:                              mark += " F-"
        print(f"{inst:>4} | {bc:>8} {po:>8} {abcf:>8} {pol:>8} | "
              f"{gap_abcf:>+6.1f}% {gap_pol:>+6.1f}% | {d_abcf:>+7} {d_post:>+7}{mark}")
        tot_best += bc; tot_post += po; tot_abcf += abcf; tot_pol += pol
        rows.append((inst, bc, po, abcf, pol))

    print("-"*92)
    print(f"{'TOTAL':>4} | {tot_best:>8} {tot_post:>8} {tot_abcf:>8} {tot_pol:>8} | "
          f"{100*(tot_abcf-tot_best)/tot_best:>+6.1f}% "
          f"{100*(tot_pol-tot_best)/tot_best:>+6.1f}% | "
          f"{tot_pol-tot_abcf:>+7} {tot_pol-tot_post:>+7}")
    n = len(rows)
    print(f"\nWins POLISH vs postfix: {pol_wins_vs_post}/{n}")
    print(f"Wins POLISH vs ABCF:    {pol_wins_vs_abcf}/{n}")
    print()
    print(f"Gap medio postfix vs best:        {100*(tot_post -tot_best)/tot_best:+.2f}%")
    print(f"Gap medio ABCF    vs best:        {100*(tot_abcf-tot_best)/tot_best:+.2f}%")
    print(f"Gap medio POLISH  vs best:        {100*(tot_pol -tot_best)/tot_best:+.2f}%")
    print(f"Mejora POLISH vs ABCF:            {100*(tot_pol-tot_abcf)/tot_abcf:+.2f}%")
    print(f"Mejora POLISH vs postfix:         {100*(tot_pol-tot_post)/tot_post:+.2f}%")

    out = ROOT / "tables" / "aco-polish-vs-ABCF-vs-postfix.csv"
    with out.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["instance", "best", "postfix", "ABCF", "POLISH",
                    "gap_ABCF_pct", "gap_POLISH_pct",
                    "POLISH_minus_ABCF", "POLISH_minus_postfix"])
        for inst, bc, po, abcf, pol in rows:
            w.writerow([inst, bc, po, abcf, pol,
                        round(100*(abcf-bc)/bc, 2),
                        round(100*(pol-bc)/bc, 2),
                        pol-abcf, pol-po])
    print(f"\nCSV: {out}")

if __name__ == "__main__":
    main()
