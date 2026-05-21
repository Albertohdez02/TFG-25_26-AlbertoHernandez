#!/usr/bin/env python3
"""Valida con IHTP_Validator las 30 soluciones de Bloque A+B+C y compara
contra postfix, best-solutions."""

import csv, re, subprocess, sys
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
            d[row["instance"]] = {
                "aco_cost": int(row["aco_cost"]),
                "random_cost": int(row["random_cost"]),
            }
    return d

def main():
    post = read_postfix()
    rows = []
    print(f"{'inst':>4} | {'best':>8} {'postfix':>8} {'ABC':>8} | {'gap_post':>8} {'gap_ABC':>7} | {'delta':>6} {'delta%':>6}")
    print("-"*78)
    total_best = total_post = total_abc = 0
    abc_wins = 0
    for inst in INSTANCES:
        bv, bc = validate(inst, ROOT / "best-solutions" / f"sol_{inst}.json")
        av, ac = validate(inst, ROOT / "solutions_blockABC_default" / f"{inst}_solution.json")
        if ac is None or bc is None:
            print(f"{inst:>4} | MISSING")
            continue
        po = post[inst]["aco_cost"]
        delta = ac - po
        delta_pct = 100.0 * delta / po if po else 0.0
        gap_post = 100.0 * (po - bc) / bc if bc else 0.0
        gap_abc  = 100.0 * (ac - bc) / bc if bc else 0.0
        win = " *" if ac < po else ""
        print(f"{inst:>4} | {bc:>8} {po:>8} {ac:>8} | {gap_post:>+7.1f}% {gap_abc:>+6.1f}% | {delta:>+6} {delta_pct:>+6.1f}%{win}")
        if ac < po:
            abc_wins += 1
        total_best += bc
        total_post += po
        total_abc  += ac
        rows.append((inst, bc, po, ac))

    print("-"*78)
    print(f"{'TOTAL':>4} | {total_best:>8} {total_post:>8} {total_abc:>8} | "
          f"{100*(total_post-total_best)/total_best:>+7.1f}% "
          f"{100*(total_abc-total_best)/total_best:>+6.1f}% | "
          f"{total_abc-total_post:>+6} "
          f"{100*(total_abc-total_post)/total_post:>+6.1f}%")
    print(f"\nWins ABC vs postfix: {abc_wins}/{len(rows)}")
    print(f"Gap medio postfix vs best: {100*(total_post-total_best)/total_best:+.1f}%")
    print(f"Gap medio ABC     vs best: {100*(total_abc-total_best)/total_best:+.1f}%")
    print(f"Mejora ABC vs postfix:     {100*(total_abc-total_post)/total_post:+.1f}%")

    out = ROOT / "tables" / "aco-blockABC-vs-postfix.csv"
    with out.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["instance", "best_cost", "postfix_cost", "abc_cost",
                    "gap_post_pct", "gap_abc_pct", "delta", "delta_pct"])
        for inst, bc, po, ac in rows:
            w.writerow([inst, bc, po, ac,
                        round(100*(po-bc)/bc, 2) if bc else 0,
                        round(100*(ac-bc)/bc, 2) if bc else 0,
                        ac-po,
                        round(100*(ac-po)/po, 2) if po else 0])
    print(f"\nCSV: {out}")

if __name__ == "__main__":
    main()
