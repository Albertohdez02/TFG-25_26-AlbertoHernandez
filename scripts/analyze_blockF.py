#!/usr/bin/env python3
"""Compara A+B+C+F (Fase F del Plan II) vs A+B+C, postfix y best-known."""

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
    print(f"{'inst':>4} | {'best':>8} {'post':>8} {'ABC':>8} {'ABCF':>8} | "
          f"{'gap_ABC':>7} {'gap_ABCF':>8} | {'F-ABC':>7} {'F-post':>7}")
    print("-"*88)
    tot_best=tot_post=tot_abc=tot_abcf=0
    f_wins_vs_post = f_wins_vs_abc = 0
    for inst in INSTANCES:
        bv, bc   = validate(inst, ROOT / "best-solutions" / f"sol_{inst}.json")
        av1, abc = validate(inst, ROOT / "solutions_blockABC_default" / f"{inst}_solution.json")
        avf, abcf = validate(inst, ROOT / "solutions_blockF_default" / f"{inst}_solution.json")
        if abcf is None or bc is None:
            print(f"{inst:>4} | MISSING"); continue
        po = post[inst]
        gap_abc = 100.0 * (abc - bc) / bc if bc else 0
        gap_abcf = 100.0 * (abcf - bc) / bc if bc else 0
        d_abc = abcf - abc
        d_post = abcf - po
        mark = ""
        if abcf < post[inst]: f_wins_vs_post += 1; mark += " P+"
        if abcf < abc:        f_wins_vs_abc  += 1; mark += " C+"
        elif abcf > abc:                                mark += " C-"
        print(f"{inst:>4} | {bc:>8} {po:>8} {abc:>8} {abcf:>8} | "
              f"{gap_abc:>+6.1f}% {gap_abcf:>+7.1f}% | {d_abc:>+7} {d_post:>+7}{mark}")
        tot_best += bc; tot_post += po; tot_abc += abc; tot_abcf += abcf
        rows.append((inst, bc, po, abc, abcf))

    print("-"*88)
    print(f"{'TOTAL':>4} | {tot_best:>8} {tot_post:>8} {tot_abc:>8} {tot_abcf:>8} | "
          f"{100*(tot_abc-tot_best)/tot_best:>+6.1f}% "
          f"{100*(tot_abcf-tot_best)/tot_best:>+7.1f}% | "
          f"{tot_abcf-tot_abc:>+7} {tot_abcf-tot_post:>+7}")
    n = len(rows)
    print(f"\nWins F vs postfix: {f_wins_vs_post}/{n}")
    print(f"Wins F vs A+B+C:   {f_wins_vs_abc}/{n}")
    print()
    print(f"Gap medio postfix vs best:  {100*(tot_post -tot_best)/tot_best:+.2f}%")
    print(f"Gap medio A+B+C vs best:    {100*(tot_abc  -tot_best)/tot_best:+.2f}%")
    print(f"Gap medio A+B+C+F vs best:  {100*(tot_abcf -tot_best)/tot_best:+.2f}%")
    print(f"Mejora F vs A+B+C:          {100*(tot_abcf-tot_abc)/tot_abc:+.2f}%")
    print(f"Mejora F vs postfix:        {100*(tot_abcf-tot_post)/tot_post:+.2f}%")

    out = ROOT / "tables" / "aco-blockF-vs-ABC-vs-postfix.csv"
    with out.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["instance", "best", "postfix", "ABC", "ABCF",
                    "gap_ABC_pct", "gap_ABCF_pct", "F_minus_ABC", "F_minus_post"])
        for inst, bc, po, abc, abcf in rows:
            w.writerow([inst, bc, po, abc, abcf,
                        round(100*(abc-bc)/bc, 2),
                        round(100*(abcf-bc)/bc, 2),
                        abcf-abc, abcf-po])
    print(f"\nCSV: {out}")

if __name__ == "__main__":
    main()
