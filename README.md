# wordlist_sort

## Description

wordlist_sort is a high-performance tool designed to process and sort large wordlists efficiently. It can handle multiple input files, remove duplicates, and produce a single sorted output file. This tool is particularly useful for tasks involving large datasets, such as subdomain enumeration in cybersecurity or text processing in data analysis.

## Features

- Process multiple input files simultaneously
- Remove duplicate words across all input files
- Sort the resulting unique words
- Efficient memory usage through memory-mapped file I/O
- Fast processing of large datasets
- Cross-platform compatibility (Linux, macOS, Windows)

## Requirements

- C++17 compatible compiler
- CMake (version 3.12 or higher)
- Conan package manager (optional, for managing dependencies)

## Building

1. Clone the repository:
```bash
git clone https://github.com/username/wordlist_sort.git
cd wordlist_sort
```
2. If using Conan (recommended):
```bash
mkdir build && cd build
conan install ..
cmake ..
cmake --build .
```

3. If not using Conan:
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage

```bash
./wordlist_sort [OPTIONS] <output_file> <input_file1> [input_file2 ...]
```

### Positionals:

- `<output_file>`: Path to the output file where the sorted, unique words will be written.
- `<input_file1>`: Path to the first input file.
- `[input_file2 ...]`: Paths to additional input files (optional).

### Options:

- `-h,--help`: Print this help message and exit
- `--version`: Display program version information and exit
- `--maxlen INT`: Filter out words over a certain max length
- `--maxtrim INT`: Trim words over a certain max length
- `--digit-trim`: Trim all digits from the beginning and end of words
- `--special-trim`: Trim all special characters from the beginning and end of words
- `--dup-remove`: Remove duplicate characters within words
- `--no-sentence`: Remove all spaces between words
- `--lower`: Change word to all lower case
- `--wordify`: Convert all input sentences into separate words
- `--no-numbers`: Ignore/delete words that are all numeric
- `--minlen INT`: Filter out words below a certain min length
- `--detab`: Remove tabs or space from the beginning of words
- `--dup-sense INT`: Remove word if more than <specified>% of characters are duplicates
- `--hash-remove`: Filter out word candidates that are actually hashes
- `--email-sort`: Convert email addresses to username and domain as separate words
- `--email-split TEXT`: Extract email addresses to username and domain wordlists (format: user:domain)
- `--dewebify`: Extract words from HTML input
- `--noutf8`: Only output non UTF-8 characters (works with --dewebify only)
- `--sort`: Sort the output words
- `--deduplicate`: Remove duplicate words from the output

## Example

```bash
./wordlist_sort --maxlen 2 --sort --detab sorted_wordlist.txt wordlist1.txt wordlist2.txt wordlist3.txt
```

This command will process `wordlist1.txt`, `wordlist2.txt`, and `wordlist3.txt`, apply a maximum word length filter of 2 characters, sort the unique words, remove tabs or spaces from the beginning of words, and write the result to `sorted_wordlist.txt`.

## Performance

wordlist_sort is designed for high performance:

- It uses memory-mapped file I/O for efficient reading of large files.
- Duplicate removal is done using an unordered_set for O(1) average case complexity.
- The final sorting step uses the standard library's efficient sorting algorithm.

The tool will output the total number of words processed, the number of unique words, and the processing time upon completion.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Author

Volker Schwaberow <volker@schwaberow.de>

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## Acknowledgments

- This project uses [CMake](https://cmake.org/) for build configuration.
- [Conan](https://conan.io/) is used for package management (optional).
