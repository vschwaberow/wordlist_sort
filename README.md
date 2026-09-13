# wordlist_sort

A C++26 CLI tool to process, filter, and sort wordlists. Processes multiple input files in parallel, applies per-word transformations, and writes a single output file. Used for subdomain enumeration, security wordlist preparation, and text data processing.

## Requirements

- **C++26** compiler (GCC 16+, Clang, or MSVC)
- CMake 3.15+

## Building

```bash
git clone https://github.com/username/wordlist_sort.git
cd wordlist_sort
cmake -B build
cmake --build build -j
```

The binary is `build/word_sorter`. No external dependencies.

## Usage

```bash
./build/word_sorter [OPTIONS] <output> <input> [input ...]
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

## Example

```bash
./build/word_sorter --maxlen 10 --sort --detab sorted.txt list1.txt list2.txt list3.txt
```

Filters words over 10 chars, removes leading tabs/spaces, sorts unique words, writes to `sorted.txt`.

## Performance

- **Parallel:** each input file processed in its own `std::async` task
- **Bulk I/O:** files read into memory in a single operation
- **Ranges:** lazy transforms via `std::ranges`
- **Move semantics:** per-task results moved into output without copying

The tool prints word counts and elapsed time on completion.

## License

MIT — see [LICENSE](LICENSE).

## Author

Volker Schwaberow <volker@schwaberow.de>

## Contributing

Contributions via fork, branch, and Pull Request.

## Testing

GoogleTest + CTest (requires `-DWORDLIST_SORT_BUILD_TESTS=ON`, default ON):

```bash
cmake -B build -DWORDLIST_SORT_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure -LE integration
```

CUDA build adds `sort_dedup_cuda_test` (label `cuda`). Convenience wrappers:

```bash
./build_and_test.sh
./build_and_test_cuda.sh
```

