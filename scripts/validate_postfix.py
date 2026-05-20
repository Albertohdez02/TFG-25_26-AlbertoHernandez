#!/usr/bin/env python3
"""Valida con IHTP_Validator las 60 soluciones postfix y genera la tabla
comparativa contra los datos prefix."""

import csv
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VALIDATOR = ROOT / "validator" / "IHTP_Validator"
DATA_DIR = ROOT / "data"
INSTANCES = [f"i{n:02d}" for n in range(1, 31)]

COST_RE = re.compile(r"^Total cost = (\d+)$", re.MULTILINE)
VIOL_RE = re.compile(r"^Total violations = (\d+)$", re.MULTILINE)

def validate(instance: str, sol_path: Path):
    if not sol_path.exists():
        return None, None
    res = subprocess.run(
        [str(VALIDATOR), str(DATA_DIR / f"{instance}.json"), str(sol_path)],
        capture_output=True, text=True, timeout=120,
    )
    out = res.stdout
    cm = COST_RE.search(out)
    vm = VIOL_RE.search(out)
    cost = int(cm.group(1)) if cm else None
    viols = int(vm.group(1)) if vm else None
    return viols, cost

def read_prefix_csv():
    """Lee tables/aco-random-comparison-prefix.csv si existe."""
    prefix = {}
    p = ROOT / "tables" / "aco-random-comparison-prefix.csv"
    if not p.exists():
        return prefix
    with p.open() as f:
        r = csv.DictReader(f)
        for row in r:
            prefix[row["instance"]] = {
                "random_cost": int(row["random_cost"]),
                "aco_cost": int(row["aco_cost"]),
                "random_viols": int(row["random_violations"]),
                "aco_viols": int(row["aco_violations"]),
            }
    return prefix

def main():
    prefix = read_prefix_csv()
    rows = []
    print(f"{'inst':>4} | {'rand_v':>6} {'rand_$':>8} | {'aco_v':>5} {'aco_$':>8} | {'winner':>8}")
    print("-" * 64)
    for inst in INSTANCES:
        rv, rc = validate(inst, ROOT / "solutions_random" / f"{inst}_solution.json")
        av, ac = validate(inst, ROOT / "solutions_aco"    / f"{inst}_solution.json")
        if rc is None or ac is None:
            print(f"{inst:>4} | MISSING")
            continue
        winner = "ACO" if ac < rc else ("Random" if rc < ac else "tie")
        if rv > 0 or av > 0:
            winner += "*"  # con violaciones
        print(f"{inst:>4} | {rv:>6} {rc:>8} | {av:>5} {ac:>8} | {winner:>8}")
        rows.append({"instance": inst,
                     "random_violations": rv, "random_cost": rc,
                     "aco_violations": av, "aco_cost": ac})

    # CSV
    out_csv = ROOT / "tables" / "aco-random-comparison-postfix.csv"
    with out_csv.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["instance", "random_violations", "random_cost",
                    "aco_violations", "aco_cost",
                    "random_cost_prefix", "aco_cost_prefix",
                    "random_delta", "aco_delta"])
        for row in rows:
            inst = row["instance"]
            pre = prefix.get(inst, {})
            rc_pre = pre.get("random_cost", "")
            ac_pre = pre.get("aco_cost", "")
            r_delta = (row["random_cost"] - rc_pre) if rc_pre != "" else ""
            a_delta = (row["aco_cost"] - ac_pre) if ac_pre != "" else ""
            w.writerow([inst,
                        row["random_violations"], row["random_cost"],
                        row["aco_violations"], row["aco_cost"],
                        rc_pre, ac_pre, r_delta, a_delta])
    print(f"\nGuardado: {out_csv}")

    # Agregados
    total_r_post = sum(r["random_cost"] for r in rows)
    total_a_post = sum(r["aco_cost"] for r in rows)
    aco_wins = sum(1 for r in rows if r["aco_cost"] < r["random_cost"])
    rand_wins = sum(1 for r in rows if r["random_cost"] < r["aco_cost"])
    ties = len(rows) - aco_wins - rand_wins
    feasible_r = sum(1 for r in rows if r["random_violations"] == 0)
    feasible_a = sum(1 for r in rows if r["aco_violations"] == 0)
    print(f"\nAGREGADO postfix (30 instancias, 600s):")
    print(f"  Total Random: {total_r_post}")
    print(f"  Total ACO:    {total_a_post}")
    print(f"  Wins ACO/Random/ties: {aco_wins}/{rand_wins}/{ties}")
    print(f"  Factibles (oficial): Random {feasible_r}/30, ACO {feasible_a}/30")

    if prefix:
        total_r_pre = sum(prefix[r["instance"]]["random_cost"] for r in rows if r["instance"] in prefix)
        total_a_pre = sum(prefix[r["instance"]]["aco_cost"]    for r in rows if r["instance"] in prefix)
        print(f"\nAGREGADO prefix (mismas 30 instancias):")
        print(f"  Total Random: {total_r_pre}")
        print(f"  Total ACO:    {total_a_pre}")
        print(f"\nMejora absoluta tras fix:")
        print(f"  Random: {total_r_pre - total_r_post:+d}  ({100*(total_r_post-total_r_pre)/total_r_pre:+.1f}%)")
        print(f"  ACO:    {total_a_pre - total_a_post:+d}  ({100*(total_a_post-total_a_pre)/total_a_pre:+.1f}%)")

if __name__ == "__main__":
    main()
