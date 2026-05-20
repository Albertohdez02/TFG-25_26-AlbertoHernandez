#!/bin/bash
# uso: ./run_one.sh <instance_num> <mode>
# ej:   ./run_one.sh 01 aco

set -e

INST=$1
MODE=$2
TIME_S=600
SEED=42
DATA="data/i${INST}.json"
LOG="logs/competition_postfix/i${INST}_${MODE}.log"
OUT_DIR="solutions_${MODE}"
TMP_OUT="solutions/i${INST}_solution.json"
FINAL_OUT="${OUT_DIR}/i${INST}_solution.json"

mkdir -p "$OUT_DIR" "$(dirname "$LOG")"

START=$(date +%s)
echo "[$(date '+%H:%M:%S')] START i${INST} ${MODE}" > "$LOG"

./build/ihtc_solver "$DATA" $SEED 999999 999 $TIME_S "$MODE" >> "$LOG" 2>&1
RC=$?

END=$(date +%s)
ELAPSED=$((END-START))
echo "[$(date '+%H:%M:%S')] END i${INST} ${MODE} rc=$RC elapsed=${ELAPSED}s" >> "$LOG"

if [ -f "$TMP_OUT" ]; then
  mv "$TMP_OUT" "$FINAL_OUT"
fi

echo "i${INST} ${MODE}: ${ELAPSED}s rc=${RC}"
