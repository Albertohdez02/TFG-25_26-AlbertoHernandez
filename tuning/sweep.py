#!/usr/bin/env python3
"""Barrido de un parametro ACO/ALNS sobre un subconjunto representativo.

Corre ./build/ihtc_solver para cada (valor, instancia, seed), parsea el coste
interno ("Coste final"), y vuelca un CSV. Pensado para fijar defaults con
respaldo estadistico (ver analyze.py).

Uso:
  python3 tuning/sweep.py --param IHTC_BETA --values 1,2,3,4 \
      --budget 120 --seeds 42,7,123 --preset default --out tuning/runs/beta.csv \
      [--fixed IHTC_RHO=0.1 ...] [--perturb ils]

El solver es time-bounded: cada run cuesta ~budget. Concurrencia=5 (20 cores / 4
hilos por solver). Sin variables de entorno el binario reproduce el baseline.
"""
import argparse, csv, re, subprocess, sys, time, os
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOLVER = os.path.join(ROOT, "build", "ihtc_solver")
DATA = os.path.join(ROOT, "data")
INSTANCES = ["i04", "i13", "i22", "m01", "m08", "m26"]  # small/med/large x i/m
COST_RE = re.compile(r"Coste final:\s+(\d+)")

def run_one(inst, seed, budget, env_over, preset, perturb):
    env = dict(os.environ)
    env.update(env_over)
    cmd = [SOLVER, os.path.join(DATA, f"{inst}.json"), str(seed),
           "999999", "999", str(budget), "aco", "12", perturb, preset]
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, env=env,
                           timeout=budget + 120)
        m = COST_RE.search(r.stdout)
        cost = int(m.group(1)) if m else -1
    except subprocess.TimeoutExpired:
        cost = -1
    return inst, seed, cost, round(time.time() - t0, 1)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--param", required=True, help="env var a barrer, p.ej. IHTC_BETA")
    ap.add_argument("--values", required=True, help="lista separada por comas")
    ap.add_argument("--budget", type=int, default=120)
    ap.add_argument("--seeds", default="42,7,123")
    ap.add_argument("--preset", default="default")
    ap.add_argument("--perturb", default="ils")
    ap.add_argument("--fixed", nargs="*", default=[], help="KEY=VAL fijos en todos los runs")
    ap.add_argument("--instances", default=",".join(INSTANCES))
    ap.add_argument("--out", required=True)
    ap.add_argument("--workers", type=int, default=5)
    args = ap.parse_args()

    values = args.values.split(",")
    seeds = [int(s) for s in args.seeds.split(",")]
    instances = args.instances.split(",")
    fixed_env = {}
    for kv in args.fixed:
        k, v = kv.split("=", 1)
        fixed_env[k] = v

    jobs = []
    for val in values:
        for inst in instances:
            for seed in seeds:
                env_over = dict(fixed_env)
                if args.param.upper() != "NONE":
                    env_over[args.param] = val
                jobs.append((val, inst, seed, env_over))

    total = len(jobs)
    print(f"[sweep] param={args.param} values={values} budget={args.budget}s "
          f"instances={instances} seeds={seeds} preset={args.preset} "
          f"perturb={args.perturb} fixed={fixed_env}")
    print(f"[sweep] {total} runs, ~{args.budget}s c/u, workers={args.workers} -> "
          f"ETA ~{(total/args.workers)*(args.budget+8)/60:.0f} min")

    rows = []
    done = 0
    t_start = time.time()
    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        fut = {}
        for (val, inst, seed, env_over) in jobs:
            f = ex.submit(run_one, inst, seed, args.budget, env_over,
                          args.preset, args.perturb)
            fut[f] = (val, inst, seed)
        for f in as_completed(fut):
            val, inst, seed = fut[f]
            _, _, cost, wall = f.result()
            done += 1
            rows.append({"param": args.param, "value": val, "instance": inst,
                         "seed": seed, "cost": cost, "wall_s": wall})
            flag = " !!" if cost < 0 else ""
            print(f"  [{done}/{total}] {args.param}={val} {inst} s{seed} "
                  f"-> cost={cost} ({wall}s){flag}", flush=True)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["param", "value", "instance",
                                            "seed", "cost", "wall_s"])
        w.writeheader()
        w.writerows(rows)
    print(f"[sweep] escrito {args.out} ({len(rows)} filas) en "
          f"{(time.time()-t_start)/60:.1f} min")

if __name__ == "__main__":
    main()
