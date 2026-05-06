#!/bin/bash
# Runs a single (instance, mode) experiment and moves the solution to its mode dir.
# Usage: run_comparison.sh <instance_num> <mode> <time_s>
set -e
INST=$1
MODE=$2
TIME_S=$3
SEED=42

INST_FILE="data/i${INST}.json"
LOG_FILE="logs/comparison/i${INST}_${MODE}.log"
SOL_SRC="solutions/i${INST}_solution.json"
SOL_DST="solutions_${MODE}/i${INST}_solution.json"

START=$(date +%s.%N)
./build/ihtc_solver "$INST_FILE" "$SEED" 999999 999 "$TIME_S" "$MODE" > "$LOG_FILE" 2>&1
RC=$?
END=$(date +%s.%N)
ELAPSED=$(awk "BEGIN{printf \"%.3f\", $END - $START}")

if [ -f "$SOL_SRC" ]; then
  mv "$SOL_SRC" "$SOL_DST"
fi

echo "${INST},${MODE},${ELAPSED},${RC}" >> logs/comparison/_progress.csv
echo "[done] i${INST} ${MODE} (${ELAPSED}s rc=${RC})"
