#!/usr/bin/env python3
"""Compara las 3 versiones (postfix, A+B, A+B+C) en las 30 instancias."""

import csv, re, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VALIDATOR = ROOT / "validator" / "IHTP_Validator"
DATA_DIR = ROOT / "data"
INSTANCES = [f"i{n:02d}" for n in range(1, 31)]
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
    p = ROOT / "tables" / "aco-random-comparison-postfix.csv"
    d = {}
    with p.open() as f:
        for row in csv.DictReader(f):
            d[row["instance"]] = int(row["aco_cost"])
    return d

def main():
    post = read_postfix()
    rows = []
    print(f"{'inst':>4} | {'best':>8} {'post':>8} {'A+B':>8} {'ABC':>8} | "
          f"{'gap_AB':>7} {'gap_ABC':>7} | {'AB-post':>7} {'ABC-AB':>7}")
    print("-"*86)
    tot_best = tot_post = tot_ab = tot_abc = 0
    ab_wins_vs_post = abc_wins_vs_post = abc_wins_vs_ab = 0
    for inst in INSTANCES:
        bv, bc = validate(inst, ROOT / "best-solutions" / f"sol_{inst}.json")
        avb, ab = validate(inst, ROOT / "solutions_blockAB_only" / f"{inst}_solution.json")
        avc, abc = validate(inst, ROOT / "solutions_blockABC_default" / f"{inst}_solution.json")
        if ab is None or abc is None or bc is None:
            print(f"{inst:>4} | MISSING")
            continue
        po = post[inst]
        gap_ab  = 100.0 * (ab - bc) / bc if bc else 0
        gap_abc = 100.0 * (abc - bc) / bc if bc else 0
        ab_d_post  = ab - po
        abc_d_ab   = abc - ab
        mark = ""
        if ab < po:  ab_wins_vs_post += 1
        if abc < po: abc_wins_vs_post += 1
        if abc < ab: abc_wins_vs_ab += 1; mark = " C+"
        elif abc > ab: mark = " C-"
        print(f"{inst:>4} | {bc:>8} {po:>8} {ab:>8} {abc:>8} | "
              f"{gap_ab:>+6.1f}% {gap_abc:>+6.1f}% | {ab_d_post:>+7} {abc_d_ab:>+7}{mark}")
        tot_best += bc
        tot_post += po
        tot_ab   += ab
        tot_abc  += abc
        rows.append((inst, bc, po, ab, abc))

    print("-"*86)
    print(f"{'TOTAL':>4} | {tot_best:>8} {tot_post:>8} {tot_ab:>8} {tot_abc:>8} | "
          f"{100*(tot_ab-tot_best)/tot_best:>+6.1f}% "
          f"{100*(tot_abc-tot_best)/tot_best:>+6.1f}% | "
          f"{tot_ab-tot_post:>+7} {tot_abc-tot_ab:>+7}")
    n = len(rows)
    print(f"\nWins A+B  vs postfix: {ab_wins_vs_post}/{n}")
    print(f"Wins ABC  vs postfix: {abc_wins_vs_post}/{n}")
    print(f"Wins ABC  vs A+B:     {abc_wins_vs_ab}/{n}")
    print()
    print(f"Gap medio postfix vs best: {100*(tot_post-tot_best)/tot_best:+.2f}%")
    print(f"Gap medio A+B     vs best: {100*(tot_ab  -tot_best)/tot_best:+.2f}%")
    print(f"Gap medio A+B+C   vs best: {100*(tot_abc -tot_best)/tot_best:+.2f}%")
    print(f"Mejora A+B   vs postfix:   {100*(tot_ab -tot_post)/tot_post:+.2f}%")
    print(f"Mejora A+B+C vs postfix:   {100*(tot_abc-tot_post)/tot_post:+.2f}%")
    print(f"Mejora A+B+C vs A+B:       {100*(tot_abc-tot_ab)/tot_ab:+.2f}%")

    out = ROOT / "tables" / "aco-AB-vs-ABC-vs-postfix.csv"
    with out.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["instance", "best", "postfix", "AB", "ABC",
                    "gap_AB_pct", "gap_ABC_pct", "AB_minus_post", "ABC_minus_AB"])
        for inst, bc, po, ab, abc in rows:
            w.writerow([inst, bc, po, ab, abc,
                        round(100*(ab-bc)/bc, 2),
                        round(100*(abc-bc)/bc, 2),
                        ab-po,
                        abc-ab])
    print(f"\nCSV: {out}")

if __name__ == "__main__":
    main()
