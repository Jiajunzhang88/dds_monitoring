#!/usr/bin/env bash
# 端到端联调：启动三域模拟节点 → 采集快照 / 生成报告 / QoS 检查 / 守护进程写回
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BIN="${BIN:-$ROOT/build}"
OUT="${OUT:-$ROOT/build/e2e}"
mkdir -p "$OUT" "$ROOT/reports"

"$BIN/ddsmon-sim" -c config/ddsmon.json --node all --duration 40 > "$OUT/sim.log" 2>&1 &
SIM=$!
trap 'kill $SIM 2>/dev/null || true' EXIT

sleep 3

echo "== snapshot =="
"$BIN/ddsmon" snapshot -c config/ddsmon.json -d 6 --no-color > "$OUT/snapshot.txt" 2>&1
echo "exit=$?" >> "$OUT/snapshot.txt"

echo "== report =="
"$BIN/ddsmon" report -c config/ddsmon.json -d 8 -o reports --prefix E2E_basic > "$OUT/report.log" 2>&1
echo "exit=$?" >> "$OUT/report.log"

echo "== qos =="
"$BIN/ddsmon" qos -c config/ddsmon.json -d 4 > "$OUT/qos.txt" 2>&1
echo "exit=$?" >> "$OUT/qos.txt"

echo "== daemon (写回监控诊断域) =="
"$BIN/ddsmon" daemon -c config/ddsmon.json -d 6 --report-interval 2 > "$OUT/daemon.log" 2>&1
echo "exit=$?" >> "$OUT/daemon.log"

kill $SIM 2>/dev/null || true
wait $SIM 2>/dev/null || true

echo "输出目录: $OUT"
grep -h "收到诊断报告" "$OUT/sim.log" | head -5 || true
