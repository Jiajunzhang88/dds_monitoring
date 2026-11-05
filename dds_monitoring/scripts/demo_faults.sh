#!/usr/bin/env bash
# 故障注入演示：一次启动带故障的模拟器，跑诊断报告，展示各类问题的检出效果
# 用法: scripts/demo_faults.sh <loss|stale|stall|qos|type|kill|all>
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BIN="${BIN:-$ROOT/build}"
FAULT="${1:-stale}"
OUT="${OUT:-$ROOT/build/fault_$FAULT}"
mkdir -p "$OUT" "$ROOT/reports"

"$BIN/ddsmon-sim" -c config/ddsmon.json --node all --duration 30 --fault "$FAULT" > "$OUT/sim.log" 2>&1 &
SIM=$!
trap 'kill $SIM 2>/dev/null || true' EXIT

sleep 3
"$BIN/ddsmon" report -c config/ddsmon.json -d 10 -o reports --prefix "FAULT_$FAULT" > "$OUT/report.log" 2>&1
echo "exit=$?" >> "$OUT/report.log"

kill $SIM 2>/dev/null || true
wait $SIM 2>/dev/null || true

echo "故障[$FAULT] 报告: $OUT/report.log"
grep -E "DDS\." "$OUT/report.log" | head -20 || true
