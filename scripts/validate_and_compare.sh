#!/bin/bash
# Validates all 60 solutions with the official IHTC validator and produces
# aco-random-comparison.csv with both modes side-by-side.
set -e

VALIDATOR=./validator/IHTP_Validator
DATA_DIR=data
PROGRESS=logs/comparison/_progress.csv
OUT_CSV=aco-random-comparison.csv
VAL_LOG_DIR=logs/comparison/validator
mkdir -p "$VAL_LOG_DIR"

extract_violations() {
  awk '/Total violations =/{print $NF; exit}' "$1"
}
extract_cost() {
  awk '/Total cost =/{print $NF; exit}' "$1"
}
get_elapsed() {
  # extract the elapsed seconds for a given (instance, mode) from progress log
  local inst=$1 mode=$2
  awk -F, -v i="$inst" -v m="$mode" '$1==i && $2==m {print $3; exit}' "$PROGRESS"
}

echo "instance,random_violations,random_cost,random_time_s,aco_violations,aco_cost,aco_time_s,improvement_abs,improvement_pct" > "$OUT_CSV"

for n in $(seq -f "%02g" 1 30); do
  INST_FILE="${DATA_DIR}/i${n}.json"
  RND_SOL="solutions_random/i${n}_solution.json"
  ACO_SOL="solutions_aco/i${n}_solution.json"

  RND_LOG="${VAL_LOG_DIR}/i${n}_random.log"
  ACO_LOG="${VAL_LOG_DIR}/i${n}_aco.log"

  if [ -f "$RND_SOL" ]; then
    "$VALIDATOR" "$INST_FILE" "$RND_SOL" > "$RND_LOG" 2>&1 || true
    RV=$(extract_violations "$RND_LOG")
    RC=$(extract_cost "$RND_LOG")
  else
    RV="NA"; RC="NA"
  fi

  if [ -f "$ACO_SOL" ]; then
    "$VALIDATOR" "$INST_FILE" "$ACO_SOL" > "$ACO_LOG" 2>&1 || true
    AV=$(extract_violations "$ACO_LOG")
    AC=$(extract_cost "$ACO_LOG")
  else
    AV="NA"; AC="NA"
  fi

  RT=$(get_elapsed "$n" random); RT=${RT:-NA}
  AT=$(get_elapsed "$n" aco);    AT=${AT:-NA}

  # improvement = random_cost - aco_cost (positive = ACO better)
  if [[ "$RC" =~ ^[0-9]+$ ]] && [[ "$AC" =~ ^[0-9]+$ ]]; then
    DIFF=$((RC - AC))
    if [ "$RC" -gt 0 ]; then
      PCT=$(awk -v d="$DIFF" -v r="$RC" 'BEGIN{printf "%.2f", (d/r)*100}')
    else
      PCT="NA"
    fi
  else
    DIFF="NA"; PCT="NA"
  fi

  echo "i${n},${RV},${RC},${RT},${AV},${AC},${AT},${DIFF},${PCT}" >> "$OUT_CSV"
done

echo "[done] CSV written to $OUT_CSV"
column -s, -t "$OUT_CSV" | head -35
