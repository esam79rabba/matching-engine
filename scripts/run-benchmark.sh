#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../build"

echo "=== Matching Engine Benchmark ==="
echo "Date: $(date -u)"
echo ""

# Build if needed
cmake .. -DCMAKE_BUILD_TYPE=Release 2>/dev/null
make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" matching-benchmark 2>&1 | tail -3

# Run benchmark
echo ""
echo "=== Results ==="
./benchmark/matching-benchmark

echo ""
echo "=== Test Suite ==="
ctest --timeout 30 2>&1 | grep -E "tests passed|Total" || true
