#!/bin/bash
# uso: ./scripts/run_blockAB.sh <instance_num> <preset>
# ej:  ./scripts/run_blockAB.sh 01 default
#      ./scripts/run_blockAB.sh 01 legacy
# Ejecuta una instancia 600s ACO+ILS con preset dado, mueve la solucion
# a solutions_blockAB_<preset>/ y deja log en logs/blockAB/.

set -e
INST=$1
PRESET=${2:-default}
TIME_S=600
SEED=42

DATA="data/i${INST}.json"
LOG_DIR="logs/blockAB"
OUT_DIR="solutions_blockAB_${PRESET}"
TMP_OUT="solutions/i${INST}_solution.json"
FINAL_OUT="${OUT_DIR}/i${INST}_solution.json"
LOG="${LOG_DIR}/i${INST}_${PRESET}.log"

mkdir -p "$LOG_DIR" "$OUT_DIR"

START=$(date +%s)
echo "[$(date '+%H:%M:%S')] START i${INST} preset=${PRESET}" > "$LOG"
./build/ihtc_solver "$DATA" $SEED 999999 999 $TIME_S aco 12 ils "$PRESET" >> "$LOG" 2>&1
RC=$?
END=$(date +%s)
echo "[$(date '+%H:%M:%S')] END i${INST} rc=${RC} elapsed=$((END-START))s" >> "$LOG"

if [ -f "$TMP_OUT" ]; then
  mv "$TMP_OUT" "$FINAL_OUT"
fi

echo "i${INST} ${PRESET}: $((END-START))s rc=${RC}"
