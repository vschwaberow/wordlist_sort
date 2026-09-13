// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/main.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "export_format.hpp"
#include "gzip_stream.hpp"
#include "io_buffer.hpp"
#include "membership_filter.hpp"
#include "sort_dedup.hpp"
#include "word_pipeline.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <memory>
#include <mutex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <format>
#include <glob.h>
#include <optional>
#include <system_error>
#include <print>
#include <ranges>
#include <regex>
#include <expected>
#include <span>
#include <thread>
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
    FlagSpec{"--upper",        &Options::upper,        "Change word to all upper case (wins over --lower if both set)"},
    FlagSpec{"--reverse",      &Options::reverse,      "Reverse characters within each word"},
    FlagSpec{"--wordify",      &Options::wordify,      "Convert all input lines/sentences into separate words based on whitespace"},
    FlagSpec{"--no-numbers",   &Options::no_numbers,   "Ignore/delete words that are composed entirely of digits"},
    FlagSpec{"--detab",        &Options::detab,        "Remove leading tabs or spaces from words/lines"},
    FlagSpec{"--hash-remove",  &Options::hash_remove,  "Filter out word candidates that are hex hashes (>=32 hex chars)"},
    FlagSpec{"--email-sort",   &Options::email_sort,   "Convert 'user@domain.com' to 'user domain' output"},
    FlagSpec{"--email-split",  &Options::email_split,  "Emit username and domain as two separate words"},
    FlagSpec{"--dewebify",     &Options::dewebify,     "Extract text from HTML input (strips tags)"},
    FlagSpec{"--noutf8",       &Options::noutf8,       "Keep only ASCII characters (0-127) on each input line"},
    FlagSpec{"--sort",         &Options::sort,         "Sort the output words lexicographically"},
    FlagSpec{"--ignore-case",  &Options::ignore_case,  "Case-insensitive sort/dedup/check-sorted (keep original form)"},
    FlagSpec{"--recursive",    &Options::recursive,    "Recurse into directory inputs for .txt and compressed wordlists"},
    FlagSpec{"--fuzzy",        &Options::fuzzy,        "Fuzzy lookup via Levenshtein on FST (--lookup or query on WLTRIE1)"},
    FlagSpec{"--miss",         &Options::miss,         "With query: emit non-hits instead of hits"},
    FlagSpec{"-r",             &Options::recursive,    "Short form of --recursive"},
    FlagSpec{"--deduplicate",  &Options::deduplicate,  "Remove duplicate words (implies --sort)"},
    FlagSpec{"--cuda",         &Options::cuda,         "Use GPU for sort/dedup when built with CUDA and word count exceeds threshold"},
    FlagSpec{"--no-cuda",      &Options::no_cuda,      "Force CPU sort/dedup even when CUDA is available"},
    FlagSpec{"--cuda-timing",  &Options::cuda_timing,  "Print CUDA phase timings (H2D/sort/dedup/D2H) to stderr"},
    FlagSpec{"--quiet",        &Options::quiet,        "Suppress informational stdout (errors/warnings still print)"},
    FlagSpec{"-q",             &Options::quiet,        "Short form of --quiet"},
    FlagSpec{"--progress",     &Options::progress,     "Print ingest progress to stderr (words/sec)"},
    FlagSpec{"--stats",        &Options::stats,        "Print final ingest/output counts and duration to stderr"},
    FlagSpec{"--check-sorted", &Options::check_sorted, "Verify inputs are sorted (with --deduplicate: strictly ascending); no output write"},
    FlagSpec{"--force",        &Options::force,        "Overwrite existing output file"},
    FlagSpec{"-f",             &Options::force,        "Short form of --force"},
    FlagSpec{"--skip-comments", &Options::skip_comments, "Ignore lines whose first non-space char is #"},
    FlagSpec{"--append",       &Options::append,        "Append text output to an existing file (implies no truncate)"},
    FlagSpec{"--null",         &Options::null_separated, "Use NUL (\\0) as record separator for text I/O"},
    FlagSpec{"-0",             &Options::null_separated, "Short form of --null"},
};

constexpr std::array int_opt_specs{
    IntOptSpec{"--maxlen",         &Options::maxlen,         "Filter out words over a certain max length (chars)"},
    IntOptSpec{"--maxtrim",        &Options::maxtrim,        "Trim words over a certain max length (chars)"},
    IntOptSpec{"--minlen",         &Options::minlen,         "Filter out words below a certain min length (chars)"},
    IntOptSpec{"--dup-sense",      &Options::dup_sense,      "Remove word if any single char is more than <N>% of the word (0-100)"},
    IntOptSpec{"--cuda-threshold", &Options::cuda_threshold, "Min words for GPU sort/dedup (0=auto heuristic ~100k; requires --cuda)"},
    IntOptSpec{"--jobs", &Options::jobs, "Parallel input workers (omit=auto CPU count, 0=unlimited, >0=cap)"},
    IntOptSpec{"--sort-chunk", &Options::sort_chunk, "External CPU sort/dedup: words per temp run (0=off; spills when larger)"},
    IntOptSpec{"--limit", &Options::limit, "Stop ingest after N accepted survivors (0=unlimited)"},
    IntOptSpec{"--every", &Options::every_n, "Keep every N-th accepted survivor (0=off; 0-based)"},
    IntOptSpec{"--sample", &Options::sample_n, "Reservoir-sample N accepted survivors (0=off)"},
    IntOptSpec{"--field", &Options::field, "Select 1-based field from each line before transforms (0=off)"},
    IntOptSpec{"--distance", &Options::distance, "Max edit distance for --fuzzy (0-3; default 1)"},
};

constexpr std::array str_opt_specs{
    StrOptSpec{"--format", &Options::format, "Output format: text (default), cdb, fst (WLTRIE1), pthash (WLPTH1)"},
    StrOptSpec{"--exclude", &Options::exclude_path, "Drop words present in FILE (set difference A\\B)"},
    StrOptSpec{"--intersect", &Options::intersect_path, "Keep only words also present in FILE (A∩B)"},
    StrOptSpec{"--lookup", &Options::lookup_path, "Keep words present in INDEX (.cdb/.fst/.pthash or text+--filter-engine)"},
    StrOptSpec{"--filter-engine", &Options::filter_engine, "Membership engine for --exclude/--intersect/--lookup text indexes: hash (default), fst, pthash"},
    StrOptSpec{"--output", &Options::output_override, "Output path or - for stdout (alternative to positional <output>)"},
    StrOptSpec{"-o", &Options::output_override, "Short form of --output"},
    StrOptSpec{"--tmp-dir", &Options::tmp_dir, "Directory for external-sort / filter temp files (default: system temp)"},
    StrOptSpec{"--prefix", &Options::prefix, "Keep only words that start with PREFIX"},
    StrOptSpec{"--suffix", &Options::suffix, "Keep only words that end with SUFFIX"},
    StrOptSpec{"--regex", &Options::regex_pattern, "Keep only words matching ECMAScript regex"},
    StrOptSpec{"--delimiter", &Options::delimiter, "Field delimiter for --field (default TAB; first char, or \\t / ,)"},
    StrOptSpec{"--compress", &Options::compress, "Compress stdout text: gzip|gz|zstd|zst|xz|lz4 (only with -o -)"},
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
    std::println("   or: {} [OPTIONS] -o <output> <input> [input ...]", PROGRAM_NAME);
    std::println("   or: {} index [OPTIONS] <index-out> <input> [input ...]", PROGRAM_NAME);
    std::println("   or: {} query [OPTIONS] [-o <out>] <index> <query-input> [query-input ...]", PROGRAM_NAME);
    std::println();
    std::println("A high-performance tool for processing, filtering, and sorting wordlists.");
    std::println();
    std::println("Subcommands:");
    std::println("  {:{}}  Build a membership index (implies --sort --deduplicate; format from --format or extension)",
                 "index", help_col_width);
    std::println("  {:{}}  Probe queries against an index (default output stdout; --miss emits non-hits)",
                 "query", help_col_width);
    std::println();
    std::println("Arguments:");
    std::println("  {:{}}  Output file path, or - for stdout (or use -o/--output)", "<output>", help_col_width);
    std::println("  {:{}}  Input file path(s); use - for stdin (at most once)", "<input> ...", help_col_width);
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

enum class CliMode
{
    Sort,
    Index,
    Query,
};

struct ParsedArgs
{
    Options options;
    fs::path output_path;
    std::vector<fs::path> input_paths;
    CliMode mode = CliMode::Sort;
};


[[nodiscard]] bool looks_like_glob(const std::string_view s) noexcept
{
    return s.find_first_of("*?[") != std::string_view::npos;
}

[[nodiscard]] bool is_txt_extension(const fs::path &path) noexcept
{
    const auto ext = path.extension().string();
    return ext.size() == 4 && ext[0] == '.' &&
           (ext[1] == 't' || ext[1] == 'T') &&
           (ext[2] == 'x' || ext[2] == 'X') &&
           (ext[3] == 't' || ext[3] == 'T');
}

[[nodiscard]] bool is_recursive_ingest_file(const fs::path &path) noexcept
{
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
        return false;
    return is_txt_extension(path) || path_looks_gzip(path) || path_looks_zstd(path) ||
           path_looks_xz(path) || path_looks_lz4(path);
}

[[nodiscard]] Expected<void> collect_recursive_dir(const fs::path &dir, std::vector<fs::path> &out)
{
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec))
    {
        if (ec)
            return std::unexpected(std::format("Unable to read directory {}: {}", dir.string(), ec.message()));
        if (is_recursive_ingest_file(it->path()))
            out.push_back(it->path());
    }
    return {};
}

[[nodiscard]] Expected<void> append_expanded_path(const fs::path &path, const bool recursive,
                                                  std::vector<fs::path> &out)
{
    std::error_code ec;
    if (fs::is_directory(path, ec))
    {
        if (!recursive)
            return std::unexpected(
                std::format("Input path is a directory (use --recursive / -r): {}", path.string()));
        return collect_recursive_dir(path, out);
    }
    out.push_back(path);
    return {};
}

[[nodiscard]] Expected<std::vector<fs::path>> expand_input_paths(const std::vector<fs::path> &inputs,
                                                                 const bool recursive)
{
    std::vector<fs::path> expanded;
    expanded.reserve(inputs.size());

    for (const auto &raw : inputs)
    {
        if (is_stdio_path(raw))
        {
            expanded.push_back(raw);
            continue;
        }

        std::error_code ec;
        if (fs::exists(raw, ec))
        {
            if (auto r = append_expanded_path(raw, recursive, expanded); !r)
                return std::unexpected(std::move(r).error());
            continue;
        }

        const std::string pattern = raw.string();
        if (!looks_like_glob(pattern))
        {
            // Keep literal missing paths for the existing open-time error path.
            expanded.push_back(raw);
            continue;
        }

        glob_t g{};
        const int rc = ::glob(pattern.c_str(), GLOB_NOSORT, nullptr, &g);
        if (rc == GLOB_NOMATCH)
        {
            ::globfree(&g);
            return std::unexpected(std::format("Glob matched no files: {}", pattern));
        }
        if (rc != 0)
        {
            ::globfree(&g);
            return std::unexpected(std::format("Glob failed for pattern: {}", pattern));
        }

        for (std::size_t i = 0; i < g.gl_pathc; ++i)
        {
            if (auto r = append_expanded_path(fs::path{g.gl_pathv[i]}, recursive, expanded); !r)
            {
                ::globfree(&g);
                return std::unexpected(std::move(r).error());
            }
        }
        ::globfree(&g);
    }

    std::ranges::sort(expanded);
    const auto [first, last] = std::ranges::unique(expanded);
    expanded.erase(first, last);
    return expanded;
}


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

    if (!positionals.empty())
    {
        if (positionals[0] == "index")
        {
            result.mode = CliMode::Index;
            positionals.erase(positionals.begin());
        }
        else if (positionals[0] == "query")
        {
            result.mode = CliMode::Query;
            positionals.erase(positionals.begin());
        }
    }

    if (result.mode == CliMode::Query)
    {
        if (positionals.size() < 2)
            return std::unexpected("Missing required arguments: query <index> <query-input>...");
        result.options.query_index_path = positionals[0];
        for (std::size_t i = 1; i < positionals.size(); ++i)
            result.input_paths.emplace_back(positionals[i]);
        if (!result.options.output_override.empty())
            result.output_path = result.options.output_override;
        else
            result.output_path = "-";
        return std::optional{std::move(result)};
    }

    if (!result.options.output_override.empty())
    {
        if (positionals.empty())
            return std::unexpected("Missing required argument: <input>");
        result.output_path = result.options.output_override;
        for (const auto &p : positionals)
            result.input_paths.emplace_back(p);
    }
    else
    {
        if (positionals.empty())
            return std::unexpected(result.mode == CliMode::Index
                                      ? "Missing required argument: <index-out>"
                                      : "Missing required argument: <output>");
        if (positionals.size() < 2)
            return std::unexpected("Missing required argument: <input>");
        result.output_path = positionals[0];
        for (std::size_t i = 1; i < positionals.size(); ++i)
            result.input_paths.emplace_back(positionals[i]);
    }

    return std::optional{std::move(result)};
}

int main(const int argc, char *argv[])
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    auto parse_result = parse_args(argc, argv);
    if (!parse_result)
    {
        std::println(stderr, "Error: {}", parse_result.error());
        std::println(stderr, "Try '{} --help' for more information.", PROGRAM_NAME);
        return 1;
    }

    if (!parse_result->has_value())
        return 0;

    auto &args = **parse_result;

    if (auto expanded = expand_input_paths(args.input_paths, args.options.recursive); !expanded)
    {
        std::println(stderr, "Error: {}", expanded.error());
        return 1;
    }
    else
    {
        args.input_paths = std::move(*expanded);
    }

    // Writing word data to stdout must not mix with banners/status on stdout.
    if (is_stdio_path(args.output_path))
        args.options.quiet = true;

    if (args.options.email_sort && args.options.email_split)
    {
        std::println(stderr, "Error: --email-sort and --email-split are mutually exclusive");
        return 1;
    }

    if (args.options.deduplicate && !args.options.sort)
    {
        if (!args.options.quiet)
            std::println(stderr, "Note: --deduplicate implies --sort (global dedup requires a sorted pass).");
        args.options.sort = true;
    }

    if (args.mode == CliMode::Index)
    {
        args.options.sort = true;
        args.options.deduplicate = true;
        if (args.options.format == "text")
        {
            if (const auto inferred = infer_export_format_from_path(args.output_path))
                args.options.format = export_format_name(*inferred);
            else
            {
                std::println(stderr,
                             "Error: index requires --format=cdb|fst|pthash or an extension .cdb/.fst/.pthash/.wlp");
                return 1;
            }
        }
    }

    if (args.options.miss && args.mode != CliMode::Query)
    {
        std::println(stderr, "Error: --miss is only valid with the query subcommand");
        return 1;
    }

    std::filesystem::path tmp_dir_path;
    if (!args.options.tmp_dir.empty())
    {
        tmp_dir_path = args.options.tmp_dir;
        std::error_code ec;
        std::filesystem::create_directories(tmp_dir_path, ec);
        if (ec || !std::filesystem::is_directory(tmp_dir_path))
        {
            std::println(stderr, "Error: --tmp-dir is not a usable directory: {}", args.options.tmp_dir);
            return 1;
        }
    }

    if (!args.options.quiet)
    {
        std::println("{} version {} by {} ({} {} {})", PROGRAM_NAME, PROGRAM_VERSION, PROGRAM_AUTHOR, BUILD_DATE,
                     BUILD_TIME, BUILD_PLATFORM);
        std::println("{}", PROGRAM_COPYRIGHT);
        std::println();
    }

    const int membership_modes =
        (!args.options.exclude_path.empty() ? 1 : 0) + (!args.options.intersect_path.empty() ? 1 : 0) +
        (!args.options.lookup_path.empty() ? 1 : 0);
    if (membership_modes > 1)
    {
        std::println(stderr, "Error: --exclude, --intersect, and --lookup are mutually exclusive");
        return 1;
    }
    if (args.mode == CliMode::Query && membership_modes > 0)
    {
        std::println(stderr, "Error: query is mutually exclusive with --exclude, --intersect, and --lookup");
        return 1;
    }

    std::unique_ptr<MembershipFilter> membership_filter;
    const bool use_membership = membership_modes == 1 || args.mode == CliMode::Query;
    if (use_membership)
    {
        const auto engine = parse_filter_engine(args.options.filter_engine);
        if (!engine)
        {
            std::println(stderr, "Error: {}", engine.error());
            return 1;
        }

        std::filesystem::path filter_path;
        if (args.mode == CliMode::Query)
            filter_path = args.options.query_index_path;
        else if (!args.options.exclude_path.empty())
            filter_path = args.options.exclude_path;
        else if (!args.options.intersect_path.empty())
            filter_path = args.options.intersect_path;
        else
            filter_path = args.options.lookup_path;
        auto filter = open_membership_filter(filter_path, *engine, tmp_dir_path);
        if (!filter)
        {
            std::println(stderr, "Error: {}", filter.error());
            return 1;
        }

        membership_filter = std::move(*filter);
        args.options.membership = membership_filter.get();
        if (args.mode == CliMode::Query)
            args.options.membership_exclude = args.options.miss;
        else
            args.options.membership_exclude = !args.options.exclude_path.empty();
        if (!args.options.quiet)
        {
            const char *mode = args.mode == CliMode::Query
                                   ? (args.options.miss ? "query-miss" : "query")
                               : args.options.membership_exclude                          ? "exclude"
                               : !args.options.lookup_path.empty()                      ? "lookup"
                                                                                        : "intersect";
            std::println("Built {} filter via {} ({} keys); streaming inputs with early drop.", mode,
                         membership_filter->backend_name(), membership_filter->size());
        }
    }

    if (args.options.fuzzy)
    {
        const bool fuzzy_ok_context =
            !args.options.lookup_path.empty() || args.mode == CliMode::Query;
        if (!fuzzy_ok_context)
        {
            std::println(stderr, "Error: --fuzzy requires --lookup or query on a WLTRIE1 FST index");
            return 1;
        }
        if (args.options.distance < 0 || args.options.distance > 3)
        {
            std::println(stderr, "Error: --distance must be between 0 and 3");
            return 1;
        }
        if (!use_membership || membership_filter == nullptr ||
            std::string_view{membership_filter->backend_name()} != "fst")
        {
            std::println(stderr, "Error: --fuzzy requires a WLTRIE1 FST index");
            return 1;
        }
    }

    const auto format = parse_export_format(args.options.format);
    if (!format)
    {
        std::println(stderr, "Error: {}", format.error());
        return 1;
    }

    if (args.mode == CliMode::Index && *format == ExportFormat::Text)
    {
        std::println(stderr, "Error: index mode cannot write text; use --format=cdb|fst|pthash");
        return 1;
    }

    if (is_stdio_path(args.output_path) && *format != ExportFormat::Text)
    {
        std::println(stderr, "Error: stdout (-) is only supported with --format=text");
        return 1;
    }

    if (!args.options.compress.empty())
    {
        if (*format != ExportFormat::Text || !is_stdio_path(args.output_path))
        {
            std::println(stderr, "Error: --compress requires --format=text and output -");
            return 1;
        }
    }
    if (args.options.append && *format != ExportFormat::Text)
    {
        std::println(stderr, "Error: --append is only supported with --format=text");
        return 1;
    }
    if (args.options.null_separated && *format != ExportFormat::Text)
    {
        std::println(stderr, "Error: --null is only supported with --format=text");
        return 1;
    }

    std::optional<std::regex> compiled_regex;
    if (!args.options.regex_pattern.empty())
    {
        try
        {
            compiled_regex.emplace(args.options.regex_pattern, std::regex::ECMAScript);
        }
        catch (const std::regex_error &ex)
        {
            std::println(stderr, "Error: invalid --regex: {}", ex.what());
            return 1;
        }
        args.options.regex = &(*compiled_regex);
    }
    if (args.options.every_n > 0 && args.options.sample_n > 0)
    {
        std::println(stderr, "Error: --every and --sample are mutually exclusive");
        return 1;
    }

    if (*format != ExportFormat::Text && path_looks_gzip(args.output_path))
    {
        std::println(stderr, "Error: gzip output (.gz) is only supported with --format=text");
        return 1;
    }
    if (*format != ExportFormat::Text && path_looks_zstd(args.output_path))
    {
        std::println(stderr, "Error: zstd output (.zst) is only supported with --format=text");
        return 1;
    }
    if (*format != ExportFormat::Text && path_looks_xz(args.output_path))
    {
        std::println(stderr, "Error: xz output (.xz) is only supported with --format=text");
        return 1;
    }
    if (*format != ExportFormat::Text && path_looks_lz4(args.output_path))
    {
        std::println(stderr, "Error: lz4 output (.lz4) is only supported with --format=text");
        return 1;
    }

    const std::size_t stdin_inputs = static_cast<std::size_t>(std::count_if(
        args.input_paths.begin(), args.input_paths.end(),
        [](const fs::path &p) { return is_stdio_path(p); }));
    if (stdin_inputs > 1)
    {
        std::println(stderr, "Error: stdin (-) may be specified as an input at most once");
        return 1;
    }

    if (!is_stdio_path(args.output_path))
    {
        std::error_code ec;
        if (std::filesystem::exists(args.output_path, ec))
        {
            if (std::filesystem::is_directory(args.output_path, ec))
            {
                std::println(stderr, "Error: output path is a directory: {}", args.output_path.string());
                return 1;
            }
            if (!args.options.check_sorted && !args.options.force && !args.options.append)
            {
                std::println(stderr, "Error: output file exists (use --force to overwrite, or --append): {}",
                             args.output_path.string());
                return 1;
            }
        }
    }

    if (args.options.check_sorted)
    {
        std::string check_error;
        const bool ok = check_inputs_sorted(args.input_paths, args.options.null_separated,
                                            args.options.skip_comments, args.options.deduplicate,
                                            args.options.ignore_case, &check_error);
        if (!ok)
        {
            std::println(stderr, "Error: {}", check_error);
            return 1;
        }
        if (!args.options.quiet)
            std::println(stderr, "check-sorted: ok");
        return 0;
    }

    const bool stream_text = (*format == ExportFormat::Text) && !args.options.sort && !args.options.deduplicate &&
                             args.options.sample_n <= 0;
    const bool want_cuda = args.options.cuda && !args.options.no_cuda;
    const bool external_ingest = !stream_text && !want_cuda && args.options.sort_chunk > 0 &&
                                 (args.options.sort || args.options.deduplicate);

    const auto start_time = std::chrono::high_resolution_clock::now();
    std::atomic<std::size_t> total_words_processed{0};
    std::atomic<std::size_t> streamed_words{0};
    std::atomic<std::size_t> survivors{0};
    std::atomic<std::size_t> every_counter{0};
    SampleState sample_state;
    std::vector<std::string> words;
    args.options.survivor_count = &survivors;
    if (args.options.every_n > 0)
        args.options.every_counter = &every_counter;
    if (args.options.sample_n > 0)
    {
        sample_state.capacity = static_cast<std::size_t>(args.options.sample_n);
        sample_state.reservoir.reserve(sample_state.capacity);
        args.options.sample = &sample_state;
    }
    std::mutex stream_mutex;
    std::unique_ptr<std::ostream> stream_out_owned;
    std::unique_ptr<std::ostream> compressed_stdout;
    std::ostream *text_stdout = &std::cout;
    if (!args.options.compress.empty())
    {
        std::string open_error;
        compressed_stdout = open_compressed_stdout(args.options.compress, &open_error);
        if (!compressed_stdout)
        {
            std::println(stderr, "Error: {}", open_error.empty() ? "Failed to open stdout compressor"
                                                                  : open_error);
            return 1;
        }
        text_stdout = compressed_stdout.get();
    }
    std::optional<ExternalSortBuilder> external_builder;

    if (stream_text)
    {
        if (is_stdio_path(args.output_path))
        {
            args.options.stream_out = text_stdout;
        }
        else
        {
            std::string open_error;
            stream_out_owned = open_text_output_stream(args.output_path, args.options.append, &open_error);
            if (!stream_out_owned)
            {
                std::println(stderr, "Error: {}", open_error.empty()
                                                     ? std::format("Failed to open output file for writing: {}",
                                                                   args.output_path.string())
                                                     : open_error);
                return 1;
            }
            args.options.stream_out = stream_out_owned.get();
        }
        args.options.stream_mutex = &stream_mutex;
        args.options.stream_emitted = &survivors;
        if (!args.options.quiet)
            std::println("Streaming text output (no in-memory word buffer).");
    }
    else if (external_ingest)
    {
        const auto plan = make_sort_dedup_plan(args.options.sort, args.options.deduplicate,
                                               args.options.ignore_case);
        external_builder.emplace(plan, static_cast<std::size_t>(args.options.sort_chunk),
                                  args.options.quiet, tmp_dir_path);
        args.options.external_sort = &(*external_builder);
        if (!args.options.quiet)
            std::println("External sort ingest flush enabled (chunk={}).", args.options.sort_chunk);
    }

    std::atomic<bool> ingest_done{false};
    std::thread progress_thread;
    if (args.options.progress)
    {
        progress_thread = std::thread(
            [&total_words_processed, &ingest_done]
            {
                using clock = std::chrono::steady_clock;
                const auto start = clock::now();
                std::size_t last = 0;
                while (!ingest_done.load(std::memory_order_acquire))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    if (ingest_done.load(std::memory_order_acquire))
                        break;
                    const auto now = clock::now();
                    const double secs = std::chrono::duration<double>(now - start).count();
                    const std::size_t n = total_words_processed.load(std::memory_order_relaxed);
                    const double rate = secs > 0.0 ? static_cast<double>(n) / secs : 0.0;
                    const double delta_rate =
                        secs > 0.0 ? static_cast<double>(n - last) / 0.5 : 0.0;
                    last = n;
                    std::println(stderr, "progress: {} words ({:.0f}/s avg, {:.0f}/s recent)", n, rate,
                                 delta_rate);
                }
            });
    }

    const bool ingest_ok =
        process_multiple_files_parallel(args.input_paths, words, total_words_processed, args.options);
    ingest_done.store(true, std::memory_order_release);
    if (progress_thread.joinable())
        progress_thread.join();

    if (args.options.progress)
    {
        const auto ingest_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::high_resolution_clock::now() - start_time)
                                   .count();
        std::println(stderr, "progress: ingest done, {} words in {} ms",
                     total_words_processed.load(), ingest_ms);
    }

    if (!ingest_ok)
    {
        std::println(stderr, "Error: One or more input files failed to process completely.");
        return 1;
    }

    if (args.options.sample != nullptr)
        words = std::move(args.options.sample->reservoir);

    if (use_membership)
    {
        const std::size_t remain = stream_text ? survivors.load()
                                 : external_ingest ? external_builder->pushed()
                                                   : words.size();
        if (!args.options.quiet)
            std::println("Applied membership filter during ingest → {} words remain.", remain);
    }

    const auto write_text_or_export = [&](const std::vector<std::string> &out_words) -> int {
        if (!args.options.compress.empty() && is_stdio_path(args.output_path) && *format == ExportFormat::Text)
        {
            const char sep = args.options.null_separated ? '\0' : '\n';
            BufferedRecordWriter writer(*text_stdout, sep);
            for (const auto &word : out_words)
                writer.write(word);
            if (!writer.flush() || !writer.good() || !text_stdout->flush() || !*text_stdout)
            {
                std::println(stderr, "Error: Failed to write compressed stdout");
                return 1;
            }
            return 0;
        }
        if (const auto write_result = write_export(out_words, args.output_path, *format, args.options.append,
                                                   args.options.null_separated);
            !write_result)
        {
            std::println(stderr, "Error: {}", write_result.error());
            return 1;
        }
        return 0;
    };

    std::size_t external_streamed = 0;
    if (!stream_text)
    {
        if (external_ingest && *format == ExportFormat::Text)
        {
            if (is_stdio_path(args.output_path))
            {
                external_streamed = external_builder->finish_to_stream(*text_stdout, args.options.null_separated ? static_cast<char>(0) : char{10});
                args.options.external_sort = nullptr;
                text_stdout->flush();
                if (!*text_stdout)
                {
                    std::println(stderr, "Error: Failed while writing external-sort output to stdout");
                    return 1;
                }
            }
            else
            {
                std::string open_error;
                auto out_file = open_text_output_stream(args.output_path, args.options.append, &open_error);
                if (!out_file)
                {
                    std::println(stderr, "Error: {}", open_error.empty()
                                                         ? std::format("Failed to open output file for writing: {}",
                                                                       args.output_path.string())
                                                         : open_error);
                    return 1;
                }
                external_streamed =
                    external_builder->finish_to_stream(*out_file, args.options.null_separated ? '\0' : '\n');
                args.options.external_sort = nullptr;
                out_file->flush();
                if (!*out_file)
                {
                    std::println(stderr, "Error: Failed while writing external-sort output: {}", args.output_path.string());
                    return 1;
                }
            }
        }
        else if (external_ingest)
        {
            external_builder->finish(words);
            args.options.external_sort = nullptr;
            if (const int wr = write_text_or_export(words); wr != 0)
                return wr;
        }
        else
        {
            sort_and_deduplicate_words(words, SortDedupOptions{
                                                   .sort = args.options.sort,
                                                   .deduplicate = args.options.deduplicate,
                                                   .use_cuda = args.options.cuda,
                                                   .no_cuda = args.options.no_cuda,
                                                   .cuda_timing = args.options.cuda_timing,
                                                   .cuda_threshold = static_cast<std::size_t>(args.options.cuda_threshold),
                                                   .sort_chunk = static_cast<std::size_t>(args.options.sort_chunk),
                                                   .quiet = args.options.quiet,
                                                   .ignore_case = args.options.ignore_case,
                                                   .tmp_dir = tmp_dir_path,
                                               });

            if (const int wr = write_text_or_export(words); wr != 0)
                return wr;
        }
    }
    else
    {
        if (is_stdio_path(args.output_path))
        {
            text_stdout->flush();
            if (!*text_stdout)
            {
                std::println(stderr, "Error: Failed while writing streamed output to stdout");
                return 1;
            }
        }
        else
        {
            stream_out_owned->flush();
            if (!*stream_out_owned)
            {
                std::println(stderr, "Error: Failed while writing streamed output: {}", args.output_path.string());
                return 1;
            }
        }
    }

    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    const std::size_t out_count = stream_text ? survivors.load()
                                : (external_ingest && *format == ExportFormat::Text) ? external_streamed
                                                                                      : words.size();
    if (!args.options.quiet)
        std::println("Processed {} words from input files, resulting in {} words in the output list, in {} ms.",
                     total_words_processed.load(), out_count, duration.count());

    if (args.options.stats)
        std::println(stderr, "stats: ingest={} output={} duration_ms={}",
                     total_words_processed.load(), out_count, duration.count());

    return 0;
}
