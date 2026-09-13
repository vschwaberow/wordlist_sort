// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/main.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "export_format.hpp"
#include "membership_filter.hpp"
#include "sort_dedup.hpp"
#include "word_pipeline.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <print>
#include <ranges>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

inline constexpr const char *PROGRAM_NAME = PROJECT_NAME;
inline constexpr const char *PROGRAM_VERSION = PROJECT_VERSION;
inline constexpr const char *PROGRAM_AUTHOR = PROJECT_AUTHOR;
inline constexpr const char *PROGRAM_COPYRIGHT = PROJECT_COPYRIGHT;
inline constexpr const char *BUILD_DATE = __DATE__;
inline constexpr const char *BUILD_TIME = __TIME__;
inline constexpr const char *BUILD_PLATFORM = BUILD_PLATFORM_INFO;

template <typename T>
using Expected = std::expected<T, std::string>;

struct FlagSpec
{
    std::string_view name;
    bool Options::*target;
    std::string_view help;
};

struct IntOptSpec
{
    std::string_view name;
    int Options::*target;
    std::string_view help;
};

struct StrOptSpec
{
    std::string_view name;
    std::string Options::*target;
    std::string_view help;
};

constexpr std::array flag_specs{
    FlagSpec{"--digit-trim",   &Options::digit_trim,   "Trim all digits from beginning and end of words"},
    FlagSpec{"--special-trim", &Options::special_trim, "Trim non-alphanumeric chars from beginning and end of words"},
    FlagSpec{"--dup-remove",   &Options::dup_remove,   "Remove consecutive duplicate characters within words"},
    FlagSpec{"--lower",        &Options::lower,        "Change word to all lower case"},
    FlagSpec{"--wordify",      &Options::wordify,      "Convert all input lines/sentences into separate words based on whitespace"},
    FlagSpec{"--no-numbers",   &Options::no_numbers,   "Ignore/delete words that are composed entirely of digits"},
    FlagSpec{"--detab",        &Options::detab,        "Remove leading tabs or spaces from words/lines"},
    FlagSpec{"--hash-remove",  &Options::hash_remove,  "Filter out word candidates that are hex hashes (>=32 hex chars)"},
    FlagSpec{"--email-sort",   &Options::email_sort,   "Convert 'user@domain.com' to 'user domain' output"},
    FlagSpec{"--dewebify",     &Options::dewebify,     "Extract text from HTML input (strips tags)"},
    FlagSpec{"--noutf8",       &Options::noutf8,       "Process to keep only ASCII characters (0-127)"},
    FlagSpec{"--sort",         &Options::sort,         "Sort the output words lexicographically"},
    FlagSpec{"--deduplicate",  &Options::deduplicate,  "Remove duplicate words from the final output list"},
    FlagSpec{"--cuda",         &Options::cuda,         "Use GPU for sort/dedup when built with CUDA and word count exceeds threshold"},
    FlagSpec{"--no-cuda",      &Options::no_cuda,      "Force CPU sort/dedup even when CUDA is available"},
    FlagSpec{"--cuda-timing",  &Options::cuda_timing,  "Print CUDA phase timings (H2D/sort/dedup/D2H) to stderr"},
};

constexpr std::array int_opt_specs{
    IntOptSpec{"--maxlen",         &Options::maxlen,         "Filter out words over a certain max length (chars)"},
    IntOptSpec{"--maxtrim",        &Options::maxtrim,        "Trim words over a certain max length (chars)"},
    IntOptSpec{"--minlen",         &Options::minlen,         "Filter out words below a certain min length (chars)"},
    IntOptSpec{"--dup-sense",      &Options::dup_sense,      "Remove word if any single char is more than <N>% of the word (0-100)"},
    IntOptSpec{"--cuda-threshold", &Options::cuda_threshold, "Min words for GPU sort/dedup (0=auto heuristic ~100k; requires --cuda)"},
};

constexpr std::array str_opt_specs{
    StrOptSpec{"--format", &Options::format, "Output format: text (default), cdb (DJB Constant Database), fst (compact trie)"},
    StrOptSpec{"--exclude", &Options::exclude_path, "Drop words present in FILE (set difference A\\B)"},
    StrOptSpec{"--intersect", &Options::intersect_path, "Keep only words also present in FILE (A∩B)"},
    StrOptSpec{"--filter-engine", &Options::filter_engine, "Membership engine for --exclude/--intersect: hash (default), fst, pthash"},
};

constexpr std::size_t compute_help_col_width()
{
    std::size_t w = 0;
    for (const auto &s : int_opt_specs)
        w = std::max(w, s.name.size() + std::size_t{6});
    for (const auto &s : str_opt_specs)
        w = std::max(w, s.name.size() + std::size_t{8});
    for (const auto &s : flag_specs)
        w = std::max(w, s.name.size());
    w = std::max(w, std::size_t{10});
    w = std::max(w, std::size_t{9});
    return w;
}

constexpr std::size_t help_col_width = compute_help_col_width();

[[nodiscard]] Expected<int> parse_int_value(const std::string_view str, const std::string_view opt_name)
{
    int value{};
    const auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec != std::errc{} || ptr != str.data() + str.size())
        return std::unexpected(std::format("Invalid integer for {}: '{}'", opt_name, str));
    if (value < 0)
        return std::unexpected(std::format("Option {} must not be negative: {}", opt_name, value));
    return value;
}

void print_version()
{
    std::println("{} version {} by {} ({} {} {})", PROGRAM_NAME, PROGRAM_VERSION, PROGRAM_AUTHOR, BUILD_DATE,
                 BUILD_TIME, BUILD_PLATFORM);
}

void print_usage()
{
    std::println("Usage: {} [OPTIONS] <output> <input> [input ...]", PROGRAM_NAME);
    std::println();
    std::println("A high-performance tool for processing, filtering, and sorting wordlists.");
    std::println();
    std::println("Arguments:");
    std::println("  {:{}}  Output file path", "<output>", help_col_width);
    std::println("  {:{}}  Input file path(s) (at least one required)", "<input> ...", help_col_width);
    std::println();
    std::println("Options:");
    for (const auto &s : int_opt_specs)
        std::println("  {:{}}  {}", std::format("{} <int>", s.name), help_col_width, s.help);
    for (const auto &s : str_opt_specs)
        std::println("  {:{}}  {}", std::format("{} <str>", s.name), help_col_width, s.help);
    std::println();
    std::println("Flags:");
    for (const auto &s : flag_specs)
        std::println("  {:{}}  {}", s.name, help_col_width, s.help);
    std::println();
    std::println("  {:{}}  Show this help message and exit", "-h, --help", help_col_width);
    std::println("  {:{}}  Display version information and exit", "--version", help_col_width);
}

struct ParsedArgs
{
    Options options;
    fs::path output_path;
    std::vector<fs::path> input_paths;
};

using ParseResult = std::expected<std::optional<ParsedArgs>, std::string>;

[[nodiscard]] ParseResult parse_args(const int argc, char *argv[])
{
    const std::span args{argv, static_cast<std::size_t>(argc)};

    ParsedArgs result;
    std::vector<std::string> positionals;
    bool options_ended = false;

    for (std::size_t i = 1; i < args.size(); ++i)
    {
        const std::string_view arg{args[i]};

        if (options_ended)
        {
            positionals.emplace_back(arg);
            continue;
        }

        if (arg == "--")
        {
            options_ended = true;
            continue;
        }

        if (arg == "-h" || arg == "--help")
        {
            print_usage();
            return std::optional<ParsedArgs>{};
        }

        if (arg == "--version")
        {
            print_version();
            return std::optional<ParsedArgs>{};
        }

        std::string_view name = arg;
        std::string_view inline_value;
        bool has_inline_value = false;

        if (const auto eq = arg.find('='); eq != std::string_view::npos)
        {
            name = arg.substr(0, eq);
            inline_value = arg.substr(eq + 1);
            has_inline_value = true;
        }

        if (auto it = std::ranges::find(flag_specs, name, &FlagSpec::name); it != flag_specs.end())
        {
            if (has_inline_value)
                return std::unexpected(std::format("Flag {} does not take a value", name));
            result.options.*(it->target) = true;
            continue;
        }

        if (auto it = std::ranges::find(int_opt_specs, name, &IntOptSpec::name); it != int_opt_specs.end())
        {
            if (!has_inline_value)
            {
                if (i + 1 >= args.size())
                    return std::unexpected(std::format("Option {} requires an integer value", name));
                inline_value = std::string_view{args[++i]};
            }
            auto parsed = parse_int_value(inline_value, name);
            if (!parsed)
                return std::unexpected(std::move(parsed).error());
            result.options.*(it->target) = *parsed;
            continue;
        }

        if (auto it = std::ranges::find(str_opt_specs, name, &StrOptSpec::name); it != str_opt_specs.end())
        {
            if (!has_inline_value)
            {
                if (i + 1 >= args.size())
                    return std::unexpected(std::format("Option {} requires a value", name));
                inline_value = std::string_view{args[++i]};
            }
            result.options.*(it->target) = std::string{inline_value};
            continue;
        }

        if (arg.starts_with("--"))
            return std::unexpected(std::format("Unknown option: {}", name));

        positionals.emplace_back(arg);
    }

    if (positionals.empty())
        return std::unexpected("Missing required argument: <output>");
    if (positionals.size() < 2)
        return std::unexpected("Missing required argument: <input>");

    result.output_path = positionals[0];
    for (std::size_t i = 1; i < positionals.size(); ++i)
        result.input_paths.emplace_back(positionals[i]);

    return std::optional{std::move(result)};
}

int main(const int argc, char *argv[])
{
    const auto parse_result = parse_args(argc, argv);
    if (!parse_result)
    {
        std::println(stderr, "Error: {}", parse_result.error());
        std::println(stderr, "Try '{} --help' for more information.", PROGRAM_NAME);
        return 1;
    }

    if (!parse_result->has_value())
        return 0;

    auto &args = **parse_result;

    std::println("{} version {} by {} ({} {} {})", PROGRAM_NAME, PROGRAM_VERSION, PROGRAM_AUTHOR, BUILD_DATE,
                 BUILD_TIME, BUILD_PLATFORM);
    std::println("{}\n", PROGRAM_COPYRIGHT);

    const auto start_time = std::chrono::high_resolution_clock::now();
    std::atomic<std::size_t> total_words_processed{0};
    std::vector<std::string> words;

    if (!process_multiple_files_parallel(args.input_paths, words, total_words_processed, args.options))
        std::println(stderr, "Warning: One or more files may have failed to process completely.");

    if (!args.options.exclude_path.empty() && !args.options.intersect_path.empty())
    {
        std::println(stderr, "Error: --exclude and --intersect are mutually exclusive");
        return 1;
    }

    if (!args.options.exclude_path.empty() || !args.options.intersect_path.empty())
    {
        const auto engine = parse_filter_engine(args.options.filter_engine);
        if (!engine)
        {
            std::println(stderr, "Error: {}", engine.error());
            return 1;
        }

        const std::filesystem::path filter_path = !args.options.exclude_path.empty()
                                                      ? args.options.exclude_path
                                                      : args.options.intersect_path;
        const auto filter_keys = load_filter_keys(filter_path);
        if (!filter_keys)
        {
            std::println(stderr, "Error: {}", filter_keys.error());
            return 1;
        }

        const auto filter = build_membership_filter(*filter_keys, *engine);
        if (!filter)
        {
            std::println(stderr, "Error: {}", filter.error());
            return 1;
        }

        const bool exclude = !args.options.exclude_path.empty();
        std::erase_if(words, [&](const std::string &word) {
            const bool hit = (*filter)->contains(word);
            return exclude ? hit : !hit;
        });

        std::println("Applied {} filter via {} ({} keys) → {} words remain.",
                     exclude ? "exclude" : "intersect", filter_engine_name(*engine), (*filter)->size(),
                     words.size());
    }

    sort_and_deduplicate_words(words, SortDedupOptions{
                                           .sort = args.options.sort,
                                           .deduplicate = args.options.deduplicate,
                                           .use_cuda = args.options.cuda,
                                           .no_cuda = args.options.no_cuda,
                                           .cuda_timing = args.options.cuda_timing,
                                           .cuda_threshold = static_cast<std::size_t>(args.options.cuda_threshold),
                                       });

    const auto format = parse_export_format(args.options.format);
    if (!format)
    {
        std::println(stderr, "Error: {}", format.error());
        return 1;
    }

    if (const auto write_result = write_export(words, args.output_path, *format); !write_result)
    {
        std::println(stderr, "Error: {}", write_result.error());
        return 1;
    }

    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::println("Processed {} words from input files, resulting in {} words in the output list, in {} ms.",
                 total_words_processed.load(), words.size(), duration.count());

    return 0;
}
