#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build (if needed) and run the sort/dedup CPU baseline harness.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${WORDLIST_SORT_BENCH_BUILD:-$ROOT/build}"
CONFIGURE_ARGS=("-DWORDLIST_SORT_BUILD_BENCHMARKS=ON")

if [[ "${WORDLIST_SORT_CUDA:-OFF}" == "ON" ]]; then
  BUILD_DIR="${WORDLIST_SORT_BENCH_BUILD:-$ROOT/build-cuda}"
  CONFIGURE_ARGS+=("-DWORDLIST_SORT_CUDA=ON" "-DCMAKE_CUDA_ARCHITECTURES=native")
fi

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "${CONFIGURE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j --target sort_dedup_bench

BENCH="$BUILD_DIR/benchmarks/sort_dedup_bench"
if [[ ! -x "$BENCH" ]]; then
  # Fallback when CMAKE puts the binary in the build root.
  BENCH="$BUILD_DIR/sort_dedup_bench"
fi

exec "$BENCH" "$@"
