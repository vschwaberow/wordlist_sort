# wordlist_sort

A C++26 CLI tool to process, filter, and sort wordlists. Processes multiple input files in parallel, applies per-word transformations, and writes a single output file. Used for subdomain enumeration, security wordlist preparation, and text data processing.

## Requirements

- **C++26** toolchain (`CMAKE_CXX_STANDARD 26`; GCC 16+, recent Clang, or MSVC with C++26 support)
- CMake 3.18+
- Optional: NVIDIA CUDA Toolkit (only for `-DWORDLIST_SORT_CUDA=ON`)

## Building

```bash
git clone https://github.com/vschwaberow/wordlist_sort.git
cd wordlist_sort
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The binary is `build/wordlist_sort` (same name as the project / version banner). The default build needs no external dependencies and no CUDA toolkit.

Release builds use `-O3` (GCC/Clang) or `/O2` (MSVC). Host-CPU tuning is **opt-in**:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DWORDLIST_SORT_NATIVE_ARCH=ON
```

`-DWORDLIST_SORT_NATIVE_ARCH=ON` adds `-march=native` on GCC/Clang (non-portable; rebuild per machine). It has no effect on MSVC.

### Optional CUDA build

GPU acceleration applies only to `--sort` / `--deduplicate` (not to the filter pipeline):

```bash
cmake -B build-cuda -DWORDLIST_SORT_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=native
cmake --build build-cuda -j
```

## Usage

```bash
./build/wordlist_sort [OPTIONS] <output> <input> [input ...]
```

The first positional is the **output** file, followed by one or more **input** files.

Integer options accept `--opt value` or `--opt=value` (values must be non-negative). Options, flags, and positionals can appear in any order. `--` ends option parsing.

### Options

| Option | Description |
|--------|-------------|
| `-h`, `--help` | Show help and exit |
| `--version` | Show version and exit |
| `--maxlen <int>` | Filter out words longer than N chars |
| `--maxtrim <int>` | Truncate words to N chars |
| `--minlen <int>` | Filter out words shorter than N chars |
| `--dup-sense <int>` | Remove word if any single char exceeds N% (0–100) |
| `--cuda-threshold <int>` | Minimum word count before GPU sort/dedup (default: 10000000; `0` = auto ≈100k; requires `--cuda`) |
| `--format <str>` | Output format: `text` (default), `cdb` (DJB Constant Database), `fst` (compact trie / WLTRIE1) |
| `--exclude <file>` | Drop words that occur in FILE (set difference A\\B) |
| `--intersect <file>` | Keep only words that also occur in FILE (A∩B) |
| `--filter-engine <str>` | Membership backend for exclude/intersect: `hash` (default), `fst`, `pthash` |

### Flags

| Flag | Description |
|------|-------------|
| `--digit-trim` | Trim digits from beginning and end of words |
| `--special-trim` | Trim non-alphanumeric chars from beginning and end of words |
| `--dup-remove` | Collapse consecutive duplicate characters within words |
| `--lower` | Convert to lowercase |
| `--wordify` | Split lines into whitespace-separated words |
| `--no-numbers` | Discard all-numeric words |
| `--detab` | Remove leading tabs/spaces |
| `--hash-remove` | Discard hex hashes (≥32 hex chars) |
| `--email-sort` | Convert `user@domain.com` → `user domain` |
| `--dewebify` | Strip HTML tags |
| `--noutf8` | Keep only ASCII (0–127); effective only with `--dewebify` |
| `--sort` | Sort output lexicographically |
| `--deduplicate` | Remove duplicate words (forces sort if not already set) |
| `--cuda` | Prefer GPU sort/dedup when built with CUDA and word count ≥ `--cuda-threshold` |
| `--no-cuda` | Force CPU sort/dedup even when CUDA is available |
| `--cuda-timing` | Print CUDA phase timings (H2D / sort / dedup / D2H) to stderr |

Without a CUDA build, `--cuda` prints a note and uses the CPU path. If a CUDA run fails at runtime, the tool falls back to CPU with a warning.

GPU transfers use **pinned host memory**. Before launching, the tool checks free VRAM against a conservative working-set estimate. If the full list does not fit, it switches to a **chunked out-of-core** GPU path (sort each VRAM-sized chunk, then k-way merge on the host). `--cuda-threshold 0` selects an auto minimum (~100k words) instead of the 10M default.



## Set filters (`--exclude` / `--intersect`)

Build a membership index from FILE B, then keep/drop words from the processed inputs (A):

```bash
# A \ B  (remove words listed in blocklist.txt)
./build/wordlist_sort --exclude blocklist.txt --filter-engine=pthash --sort --deduplicate out.txt a1.txt a2.txt

# A ∩ B
./build/wordlist_sort --intersect allow.txt --filter-engine=fst --sort out.txt input.txt
```

| Engine | Notes |
|--------|-------|
| `hash` | `unordered_set` (always available; highest RAM) |
| `fst` | Builds a temporary WLTRIE1 trie from B |
| `pthash` | Minimal perfect hash + key table (needs `-DWORDLIST_SORT_PTHASH=ON`, default ON). PTHash may enable host-specific ISA flags via its INTERFACE. |

`--exclude` and `--intersect` are mutually exclusive.

## Output formats

| `--format` | Description |
|------------|-------------|
| `text` (default) | One word per line (existing behavior) |
| `cdb` | DJB **Constant Database** (tinycdb-compatible). Each word is a key with an empty value. Classic CDB 4 GiB limit. Duplicate keys: first wins. |
| `fst` | Compact **prefix trie** on disk (`WLTRIE1`). Exact membership lookups; shared prefixes. Duplicate keys: first wins. |

```bash
./build/wordlist_sort --sort --deduplicate --format=cdb words.cdb list.txt
./build/wordlist_sort --sort --deduplicate --format=fst words.fst list.txt
```

## Example

```bash
./build/wordlist_sort --maxlen 10 --sort --detab sorted.txt list1.txt list2.txt list3.txt
```

Filters words over 10 chars, removes leading tabs/spaces, sorts, writes to `sorted.txt`.

CUDA example (CUDA build required):

```bash
./build-cuda/wordlist_sort --sort --deduplicate --cuda --cuda-threshold 1000000 out.txt big1.txt big2.txt
```

## Performance

- **Parallel:** each input file processed in its own `std::async` task
- **Bulk I/O:** each input file is read fully into a `std::vector<char>` (no `mmap` / memory-mapped I/O)
- **Dedup:** `--deduplicate` uses `std::ranges::sort` + `std::unique` + erase (not `unordered_set`)
- **Ranges:** lazy transforms via `std::ranges`
- **Move semantics:** per-task results moved into output without copying
- **CUDA (optional):** sort/dedup on GPU above `--cuda-threshold`; calibrate with the benchmark harness

The tool prints word counts and elapsed time on completion.

## Benchmarks

CPU baseline harness for sort, isolated unique/erase, and sort+dedup totals (`WORDLIST_SORT_BUILD_BENCHMARKS=ON`, default ON):

```bash
cmake -B build -DWORDLIST_SORT_BUILD_BENCHMARKS=ON
cmake --build build -j --target sort_dedup_bench
./build/benchmarks/sort_dedup_bench --sizes 100000,1000000,10000000 --iters 3
# or
./run_benchmark.sh --sizes 1000000 --csv
```

Optional CUDA comparison: build with `-DWORDLIST_SORT_CUDA=ON`, then pass `--cuda` to the harness (or `WORDLIST_SORT_CUDA=ON ./run_benchmark.sh --cuda`).

## Testing

GoogleTest + CTest (`WORDLIST_SORT_BUILD_TESTS=ON`, default ON):

```bash
cmake -B build -DWORDLIST_SORT_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure -LE integration
```

CUDA builds add `sort_dedup_cuda_test` (label `cuda`). Convenience wrappers:

```bash
./build_and_test.sh
./build_and_test_cuda.sh
```

## License

MIT — see [LICENSE](LICENSE).

## Author

Volker Schwaberow <volker@schwaberow.de>

## Contributing

Contributions via fork, branch, and Pull Request. Keep the README in sync when user-facing or functional behavior changes (CLI flags, build options, defaults).
