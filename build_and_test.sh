#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT}/build"
BINARY="${BUILD_DIR}/wordlist_sort"

echo "==> Configure (CPU)"
cmake -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DWORDLIST_SORT_CUDA=OFF

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

echo "==> Test: sort + deduplicate (CPU)"
"${BINARY}" --sort --deduplicate "${WORK}/out.txt" "${WORK}/in.txt"

EXPECTED=$'apple\nbanana\ncherry\n'
ACTUAL="$(cat "${WORK}/out.txt")"
if [[ "${ACTUAL}" != "${EXPECTED}" ]]; then
  echo "FAIL: unexpected output"
  printf '%s\n' "${ACTUAL}"
  exit 1
fi

echo "==> Test: deduplicate without --sort (implicit sort note)"
OUTPUT="$("${BINARY}" --deduplicate "${WORK}/out2.txt" "${WORK}/in.txt" 2>&1)" || true
if ! grep -q 'Deduplication requires sorting' <<< "${OUTPUT}"; then
  echo "FAIL: missing implicit-sort note on stdout"
  exit 1
fi
if [[ "$(cat "${WORK}/out2.txt")" != "${EXPECTED}" ]]; then
  echo "FAIL: deduplicate-only output mismatch"
  exit 1
fi

echo "==> Test: --cuda ignored without CUDA build"
if ! "${BINARY}" --sort --deduplicate --cuda "${WORK}/out3.txt" "${WORK}/in.txt" 2>&1 | grep -q 'built without CUDA support'; then
  echo "FAIL: expected --cuda ignored note"
  exit 1
fi

echo "==> CTest"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -LE integration

echo "PASS: CPU build_and_test"
