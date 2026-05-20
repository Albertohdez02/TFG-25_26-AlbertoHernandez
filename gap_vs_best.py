#!/usr/bin/env python3
"""Valida best-solutions y compara con ACO post-fix."""

import csv, re, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
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
    print(f"{'inst':>4} | {'best':>8} {'b_viol':>6} | {'ACO':>8} {'gap':>6} {'gap%':>6} | {'Rand':>8} {'gap':>6} {'gap%':>6}")
    print("-"*78)
    rows = []
    for inst in INSTANCES:
        bv, bc = validate(inst, ROOT / "best-solutions" / f"sol_{inst}.json")
        if bc is None:
            print(f"{inst:>4} | MISSING BEST"); continue
        ac = post[inst]["aco_cost"]
        rc = post[inst]["random_cost"]
        gap_a = ac - bc
        gap_r = rc - bc
        pa = 100*gap_a/bc if bc else 0
        pr = 100*gap_r/bc if bc else 0
        print(f"{inst:>4} | {bc:>8} {bv:>6} | {ac:>8} {gap_a:>+6} {pa:>+6.1f} | {rc:>8} {gap_r:>+6} {pr:>+6.1f}")
        rows.append((inst, bc, bv, ac, rc))

    tot_best = sum(r[1] for r in rows)
    tot_aco  = sum(r[3] for r in rows)
    tot_rand = sum(r[4] for r in rows)
    print("-"*78)
    print(f"TOTAL   |{tot_best:>9} {'-':>6} |{tot_aco:>9} {tot_aco-tot_best:>+6} {100*(tot_aco-tot_best)/tot_best:>+6.1f} |{tot_rand:>9} {tot_rand-tot_best:>+6} {100*(tot_rand-tot_best)/tot_best:>+6.1f}")

    print(f"\nGap medio ACO  vs best: {100*(tot_aco -tot_best)/tot_best:+.1f}%  ({tot_aco -tot_best:+d} pts)")
    print(f"Gap medio Rand vs best: {100*(tot_rand-tot_best)/tot_best:+.1f}%  ({tot_rand-tot_best:+d} pts)")

    # gap por instancia ordenado de peor a mejor
    print("\nTop-5 instancias con gap mas grande (ACO):")
    sorted_aco = sorted(rows, key=lambda r: (r[3]-r[1])/max(r[1],1), reverse=True)
    for inst, bc, bv, ac, rc in sorted_aco[:5]:
        print(f"  {inst}: best={bc} ACO={ac} gap=+{ac-bc} ({100*(ac-bc)/bc:+.1f}%)")
    print("\nTop-5 instancias con gap mas pequeño (ACO):")
    for inst, bc, bv, ac, rc in sorted_aco[-5:]:
        print(f"  {inst}: best={bc} ACO={ac} gap=+{ac-bc} ({100*(ac-bc)/bc:+.1f}%)")

if __name__ == "__main__":
    main()
