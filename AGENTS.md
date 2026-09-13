# AGENTS.md

Guide for AI agents working in this repository. Captures non-obvious knowledge that saves trial-and-error discovery.

## Project Overview

`wordlist_sort` is a C++26 CLI tool for processing, filtering, and sorting wordlists (commonly used for subdomain enumeration / security wordlist prep). Source layout:

- `src/main.cc` — CLI parsing and `main`
- `src/word_pipeline.{hpp,cc}` — per-word/file filter pipeline
- `src/sort_dedup_*.{hpp,cc,cu}` — optional CUDA sort/dedup (`WORDLIST_SORT_CUDA`)

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
3. **I/O is bulk `ifstream` → `vector<char>`.** There is no `mmap` / memory-mapped path; large inputs are fully resident in RAM.
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

- **Positional order: OUTPUT first, then inputs.** Positional parsing registers output before inputs. Usage is `wordlist_sort <out> <in1> [in2 ...]`, opposite of most CLIs.
- **`--noutf8` only does anything when combined with `--dewebify`.** The non-ASCII (>127) stripping lives inside the `if (options.dewebify)` block in `process_file`. Alone, `--noutf8` has no effect.
- **`--deduplicate` silently forces a sort** even without `--sort`, and prints a note to stdout. Dedup requires sorted input (`std::unique` only removes *consecutive* duplicates).
- **Threading is one `std::async` task per input file** (`std::launch::async`), with no thread pool or concurrency cap. Passing hundreds of files spawns hundreds of threads. Each task reads its file fully into memory, so peak RAM scales with concurrent file sizes.
- **`--cuda` / `--no-cuda` / `--cuda-threshold`**: GPU sort/dedup (compile-time optional via `-DWORDLIST_SORT_CUDA=ON`). See gotcha §6.
- **`--dup-sense N` (0–100)** rejects a word if *any single byte* exceeds `N%` of the word's length (uses a 256-bucket `std::array<unsigned int, 256>` char histogram).

## Architecture & Data Flow

1. **`read_file`** — loads a file fully into `std::vector<char>` via `std::ifstream` (binary).
2. **`process_word`** / **`process_file`** — filter pipeline in `word_pipeline.cc` (`strip_html_tags`, trims, dup-sense, email-sort, min/max len).
3. **`process_multiple_files_parallel`** — one `std::async` task per input file.
4. **`sort_and_deduplicate_words`** — CPU or optional CUDA sort/dedup (`sort_dedup_*.cc`).
5. **`write_lines`** — one word per line to the output path.
6. **`main`** — CLI in `main.cc`, orchestrates the above.

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
