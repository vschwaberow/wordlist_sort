#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT}/build-cuda"
BINARY="${BUILD_DIR}/wordlist_sort"

echo "==> Configure (CUDA)"
cmake -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DWORDLIST_SORT_CUDA=ON \
  -DCMAKE_CUDA_ARCHITECTURES=native

echo "==> Build"
cmake --build "${BUILD_DIR}" -j

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

cat > "${WORK}/in.txt" << 'INPUT'
banana
apple
cherry
apple
INPUT

EXPECTED=$'apple\nbanana\ncherry\n'

echo "==> Test: CPU fallback (threshold not reached)"
"${BINARY}" --sort --deduplicate "${WORK}/out_cpu.txt" "${WORK}/in.txt"
if [[ "$(cat "${WORK}/out_cpu.txt")" != "${EXPECTED}" ]]; then
  echo "FAIL: CPU fallback output mismatch"
  exit 1
fi

echo "==> Test: CUDA sort + deduplicate (--cuda-threshold 1)"
"${BINARY}" --sort --deduplicate --cuda --cuda-threshold 1 "${WORK}/out_cuda.txt" "${WORK}/in.txt"
if [[ "$(cat "${WORK}/out_cuda.txt")" != "${EXPECTED}" ]]; then
  echo "FAIL: CUDA output mismatch"
  cat "${WORK}/out_cuda.txt"
  exit 1
fi

echo "==> Test: CUDA matches CPU output"
if ! cmp -s "${WORK}/out_cpu.txt" "${WORK}/out_cuda.txt"; then
  echo "FAIL: CPU and CUDA outputs differ"
  diff -u "${WORK}/out_cpu.txt" "${WORK}/out_cuda.txt" || true
  exit 1
fi

echo "==> Test: deduplicate-only on CUDA (implicit sort)"
OUTPUT="$("${BINARY}" --deduplicate --cuda --cuda-threshold 1 "${WORK}/out_dedup.txt" "${WORK}/in.txt" 2>&1)" || true
if ! grep -q 'Deduplication requires sorting' <<< "${OUTPUT}"; then
  echo "FAIL: missing implicit-sort note on stdout"
  exit 1
fi
if [[ "$(cat "${WORK}/out_dedup.txt")" != "${EXPECTED}" ]]; then
  echo "FAIL: CUDA deduplicate-only output mismatch"
  exit 1
fi

echo "==> CTest"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -LE integration

echo "PASS: CUDA build_and_test"
