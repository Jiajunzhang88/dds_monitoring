#!/usr/bin/env bash
# 一键构建：cmake 配置 + 编译 + 单元测试
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"

echo "== 依赖检查 =="
pkg-config --exists CycloneDDS || { echo "缺少 CycloneDDS 开发包: sudo apt-get install cyclonedds-dev cyclonedds-tools"; exit 1; }
command -v idlc >/dev/null || { echo "缺少 idlc（cyclonedds-tools）"; exit 1; }
echo "CycloneDDS $(pkg-config --modversion CycloneDDS), idlc $(idlc -v 2>&1 | head -1)"

echo "== 配置 =="
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release

echo "== 编译 =="
cmake --build "$BUILD" -j"$(nproc)"

echo "== 单元测试 =="
ctest --test-dir "$BUILD" --output-on-failure

echo "完成: $BUILD/ddsmon  $BUILD/ddsmon-sim"
