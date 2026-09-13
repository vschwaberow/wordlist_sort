# AGENTS.md

Guide for AI agents working in this repository. Captures non-obvious knowledge that saves trial-and-error discovery.

## Project Overview

`wordlist_sort` is a C++26 CLI tool for processing, filtering, and sorting wordlists (commonly used for subdomain enumeration / security wordlist prep). Source layout:

- `src/main.cc` — CLI parsing and `main`
- `src/word_pipeline.{hpp,cc}` — per-word/file filter pipeline
- `src/sort_dedup_*.{hpp,cc,cu}` — optional CUDA sort/dedup (`WORDLIST_SORT_CUDA`)
- `src/export_format.*` / `cdb_export.cc` / `fst_export.cc` — `--format text|cdb|fst` writers
- `src/membership_filter.*` — `--exclude` / `--intersect` with `hash|fst|pthash` engines

Hand-rolled CLI parser (no CLI11).

## Essential Commands

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWORDLIST_SORT_BUILD_TESTS=ON

# Build
cmake --build build -j

# Unit + E2E tests (GoogleTest + CTest)
ctest --test-dir build --output-on-failure -LE integration

# Full smoke (build + shell checks + ctest)
./build_and_test.sh

# Run
./build/wordlist_sort [OPTIONS] <output_file> <input_file1> [input_file2 ...]

# Regenerate compile_commands.json for clangd after structural CMake changes
cmake -B build
ln -sf build/compile_commands.json .   # only needed once
```

Tests use **GoogleTest + CTest** (`tests/`, enabled via `WORDLIST_SORT_BUILD_TESTS=ON`, default ON):

| Target | Scope |
|--------|-------|
| `sort_dedup_test` | `SortDedupPlan`, CPU sort/dedup, dispatch |
| `sort_dedup_cuda_test` | GPU/CPU parity (CUDA build only, label `cuda`) |
| `word_pipeline_test` | `process_word`, filters, `process_file` |
| `e2e_cli_test` | Subprocess CLI tests against `wordlist_sort` |

CUDA tests call `GTEST_SKIP()` when no GPU is available. Shell wrappers `build_and_test*.sh` are CTest fixtures with label `integration`.


## Benchmarks (P0 CPU baseline)

Build and run the sort/dedup harness (synthetic wordlists; measures sort, isolated unique/erase, and sort+dedup total):

```bash
cmake -B build -DWORDLIST_SORT_BUILD_BENCHMARKS=ON
cmake --build build -j --target sort_dedup_bench
./build/benchmarks/sort_dedup_bench --sizes 100000,1000000 --iters 3
# or
./run_benchmark.sh --sizes 100000,1000000 --csv
```

Optional CUDA comparison (requires `-DWORDLIST_SORT_CUDA=ON`): pass `--cuda` to the harness / `WORDLIST_SORT_CUDA=ON ./run_benchmark.sh --cuda`.
Use median timings across sizes to calibrate `--cuda-threshold` (default 10_000_000).

## Build & Runtime Facts

These match the current code and README (keep them aligned):

1. **Binary name is `wordlist_sort`.** `add_executable(wordlist_sort …)` and `PROJECT_NAME` / the version banner all use the same name. Output path: `build/wordlist_sort`.
2. **Language standard is C++26.** `CMAKE_CXX_STANDARD 26` with `STANDARD_REQUIRED ON`; `cmake_minimum_required(VERSION 3.18)`.
3. **I/O uses large user-space buffers (~1 MiB) + `memchr` record scans** (`BufferedRecordReader` / `BufferedRecordWriter` in `src/io_buffer.hpp`). There is no `mmap` path; peak RAM is survivors (+ optional B membership filter + I/O slabs), not the full input file.
4. **Dedup is sort + unique.** `--deduplicate` runs `std::ranges::sort` then `std::unique` + erase. No `unordered_set` / `unordered_map` for deduplication.
5. **Release optimization is portable by default.** GCC/Clang get `-O3` (MSVC `/O2`) only for `CMAKE_BUILD_TYPE=Release`. `-march=native` is **opt-in** via `-DWORDLIST_SORT_NATIVE_ARCH=ON` (GCC/Clang only; non-portable).

### Optional CUDA sort/dedup (`WORDLIST_SORT_CUDA` CMake option)

GPU acceleration applies **only** to `--sort` and `--deduplicate`, not to the filter pipeline.

```bash
# Default build (no CUDA toolkit required)
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

# CUDA build (NVIDIA toolkit required)
cmake -B build-cuda -DCMAKE_BUILD_TYPE=Release -DWORDLIST_SORT_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=native
cmake --build build-cuda -j
```

- **`--cuda`**: enable GPU sort/dedup when compiled with CUDA and word count ≥ threshold
- **`--no-cuda`**: force CPU path
- **`--cuda-threshold N`**: minimum words for GPU (default: 10_000_000; `0` = auto ≈ `kCudaHeuristicMinWords` / 100k)
- **`--cuda-timing`**: print H2D/sort/dedup/D2H timings on stderr
- GPU path uses pinned host buffers; oversized working sets use chunked GPU sort + host k-way merge
- Without CUDA build, `--cuda` prints a note and uses CPU
- CUDA failure at runtime falls back to CPU with a warning


## Non-Obvious Flag Behavior

These are behaviors not clearly documented and easy to get wrong:

- **Positional order defaults to OUTPUT first**, then inputs (`wordlist_sort <out> <in...>`). Prefer `-o`/`--output` so all positionals are inputs (`wordlist_sort -o out in1 in2`).
- **Compressed inputs**: transparent inflate for `.gz` (zlib), `.zst`/`.zstd` (libzstd), `.xz` (liblzma), and `.lz4` (liblz4) on ingest and text `--exclude`/`--intersect` sources (`WORDLIST_SORT_ZLIB` / `WORDLIST_SORT_ZSTD` / `WORDLIST_SORT_LZMA` / `WORDLIST_SORT_LZ4`, default ON). Detected by extension or magic; stdin stays raw.
- **Compressed text output**: writing `--format=text` to a `.gz` / `.zst` (`.zstd`) / `.xz` / `.lz4` path compresses with zlib / libzstd / liblzma / liblz4. `--append` adds another member/frame/stream. Binary formats reject compressed outputs.
- **`-` means stdio**: input `-` reads stdin (at most once); output `-` writes `--format=text` to stdout and forces quiet. Non-text formats refuse stdout.
- **`--noutf8`** strips bytes `>127` on every input line (independent of `--dewebify`).
- **`-0` / `--null`**: NUL-separated text I/O for ingest and `--format=text` output (external-sort merge included). Rejected for binary formats. Internal spill runs stay newline-based.
- **`--append`**: text-only; open output with `app` instead of `trunc`. Existing files do not require `--force`. Rejected for `cdb`/`fst`/`pthash`.
- **`--skip-comments`**: drop lines whose first non-space/tab character is `#` (before other line transforms).
- **`-f` / `--force`**: required to overwrite an existing output path; stdout `-` is always allowed. Directories as output are always rejected.
- **`--limit N`**: cap accepted survivors during ingest (`0` = off). Parallel-safe; stops reading further lines once full. Applies before sort/dedup (output may be smaller after dedup).
- **`--progress`**: ingest ticker on stderr (works with `-q` / stdout `-`; does not write to stdout).
- **Ingest failure exits 1**: if any input file fails to open/read, the process exits non-zero (no silent partial success).
- **`-q` / `--quiet`**: suppress banners and status lines on stdout (and non-fatal notes); errors/warnings stay on stderr.
- **`--deduplicate` implies `--sort`**: if you pass `--deduplicate` alone, the CLI auto-enables `--sort` and prints a one-line note to stderr (global dedup needs a sorted pass).
- **Threading is one `std::async` task per input file**, gated by `--jobs` (default: `hardware_concurrency` via counting semaphore; `0` = unlimited). Each task streams its file line-by-line.
- **`--jobs`**: omit = auto CPU count (default), `0` unlimited, `>0` cap.
- **`--sort-chunk N`**: when `N>0` (CPU path), flush sorted temp runs during ingest every `N` words, then k-way merge (`ExternalSortBuilder`); text format merges directly to the output stream.
- **`--cuda` / `--no-cuda` / `--cuda-threshold`**: GPU sort/dedup (compile-time optional via `-DWORDLIST_SORT_CUDA=ON`).
- **`--tmp-dir`**: override system temp for external-sort run files and text→FST/PTHash filter build scratch.
- **`--exclude` / `--intersect` / `--filter-engine`**: build/open B filter first, then stream A with early drop; text B uses `hash|fst|pthash`; WLTRIE1/`*.cdb` opened as in-memory indexes (`open_membership_filter`).
- **`--format text|cdb|fst|pthash`**: output encoder after sort/dedup (`cdb` = DJB CDB, `fst` = WLTRIE1, `pthash` = WLPTH1 MPHF+keys). Duplicate keys keep the first occurrence.
- **`--dup-sense N` (0–100)** rejects a word if *any single byte* exceeds `N%` of the word's length (uses a 256-bucket `std::array<unsigned int, 256>` char histogram).

## Architecture & Data Flow

1. **`process_file`** — streams each input record via `BufferedRecordReader` (no full-file buffer); optional membership gate drops during ingest; optional text stream sink batches survivors under a shared mutex.
2. **`process_word`** — transform/filter pipeline (`strip_html_tags`, trims, dup-sense, email-sort, min/max len).
3. **`process_multiple_files_parallel`** — one `std::async` task per input file.
4. **`--exclude`/`--intersect`** — build membership from B before reading A (`membership_filter.*`).
5. **`sort_and_deduplicate_words`** — CPU or optional CUDA sort/dedup (`sort_dedup_*.cc`).
6. **`write_export` / `write_lines`** — text / cdb / fst writers.
7. **`main`** — CLI in `main.cc`, orchestrates the above.
8. **`read_file`** — still available for bulk reads (helpers/tests); ingest path no longer uses it.

## Coding Conventions

- **SPDX header** at the top of every source file:
  ```
  // SPDX-License-Identifier: MIT
  // Project: wordlist_sort
  // File: <path>
  // Author: Volker Schwaberow <volker@schwaberow.de>
  // Copyright (c) <year> Volker Schwaberow
  ```
- **Resource classes use a static `Create(path)` factory** returning `std::unique_ptr<T>` (or `nullptr` on failure), with a private/defaulted constructor. Follow this pattern for any new file/resource-owning type. (`FileBuffer`, `BufferedFile`, `OutputFile`.)
- **Prefer `constexpr noexcept` free functions** for byte-classification (`is_digit_char`, `is_alpha_char`, `is_alnum_char`) rather than `<cctype>` — keeps the checks locale-independent and branchless-friendly.
- **Transformations mutate in place** (`*_inplace` suffix) where possible.
- **`inline constexpr const char*`** is used (not `constexpr std::string_view`) for the build-metadata globals derived from CMake macros.
- **Do not bump `PROJECT_VERSION` unless the user explicitly asks.** Version numbers are owner-controlled; feature PRs leave `CMakeLists.txt` `PROJECT_VERSION` unchanged.
- **Project metadata is injected via `add_definitions(...)`** in `CMakeLists.txt` (the older mechanism), not `target_compile_definitions`. `PROJECT_NAME`/`PROJECT_VERSION`/`PROJECT_AUTHOR`/`PROJECT_COPYRIGHT`/`BUILD_PLATFORM_INFO`/`COMPILER_INFO_STRING` become preprocessor macros. If you add new metadata, follow the same `add_definitions` pattern for consistency.

## clangd / LSP Setup

Out of the box, clangd may report spurious errors on `src/main.cc` (missing `PROJECT_*` macros / standard library features) when no compile database is present. These are **not real compile errors**. To fix:

```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ln -sf build/compile_commands.json .
```

After this, clangd resolves the `PROJECT_*` macros and the C++26 standard library. Do not "fix" these diagnostics by editing the source — they vanish once `compile_commands.json` exists.

## Dependencies & Build Internals

- Hand-rolled CLI parser — **no CLI11 / CPM** dependency for the main binary.
- GoogleTest is fetched via CMake `FetchContent` when `-DWORDLIST_SORT_BUILD_TESTS=ON` (default ON); first configure needs network access.
- `build/` and `build-cuda/` are gitignored. `CMakeUserPresets.json` and `.vscode/` are also gitignored.

## Git / Contribution Conventions

- **Default integration branch is `master`** (`origin/HEAD -> origin/master`). PRs merge into `master`.
- **Conventional-commit-style prefixes** are used: `chore:`, `refactor:`, `feat:`-ish freeform ("Improvements", "Code optimizations"). PRs are squash-merged and referenced as `(##N)` in the log.
- Commits and PRs are the unit of change; there is no separate CHANGELOG.

## Working in This Repo — Quick Checklist

- Building/testing: `cmake -B build -DWORDLIST_SORT_BUILD_TESTS=ON && cmake --build build -j && ctest --test-dir build --output-on-failure -LE integration`
- Before editing `src/main.cc`, ensure `compile_commands.json` exists so diagnostics are trustworthy.
- When changing CLI flags or build options, update the parser/`CMakeLists.txt` **and** `README.md` in the same change. Trust the code if they disagree, then fix the README.
- **README sync (mandatory):** Any user-facing or functional change (CLI flags, build options, defaults, new binaries/harnesses) must update `README.md` in the same commit/PR. Do not leave README catch-up for later.
- Do not reintroduce `mmap`, `unordered_set` dedup, a C++17 baseline, or unconditional `-march=native` without updating README/`AGENTS.md`.
