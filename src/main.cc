#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <iterator>
#include <optional>
#include <print>
#include <ranges>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
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

struct Options
{
    int minlen = 0;
    int maxlen = 0;
    int maxtrim = 0;
    int dup_sense = 0;

    bool detab = false;
    bool digit_trim = false;
    bool special_trim = false;
    bool dup_remove = false;
    bool lower = false;
    bool no_numbers = false;
    bool hash_remove = false;
    bool email_sort = false;
    bool dewebify = false;
    bool noutf8 = false;
    bool wordify = false;
    bool sort = false;
    bool deduplicate = false;

};

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
};

constexpr std::array int_opt_specs{
    IntOptSpec{"--maxlen",    &Options::maxlen,    "Filter out words over a certain max length (chars)"},
    IntOptSpec{"--maxtrim",   &Options::maxtrim,   "Trim words over a certain max length (chars)"},
    IntOptSpec{"--minlen",    &Options::minlen,    "Filter out words below a certain min length (chars)"},
    IntOptSpec{"--dup-sense", &Options::dup_sense, "Remove word if any single char is more than <N>% of the word (0-100)"},
};

constexpr std::size_t compute_help_col_width()
{
    std::size_t w = 0;
    for (const auto &s : int_opt_specs)
        w = std::max(w, s.name.size() + std::size_t{6});
    for (const auto &s : flag_specs)
        w = std::max(w, s.name.size());
    w = std::max(w, std::size_t{10});
    w = std::max(w, std::size_t{9});
    return w;
}

constexpr std::size_t help_col_width = compute_help_col_width();

[[nodiscard]] Expected<int> parse_int_value(std::string_view str, std::string_view opt_name)
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
    std::println("{} version {} by {} ({} {} {})",
                 PROGRAM_NAME, PROGRAM_VERSION, PROGRAM_AUTHOR, BUILD_DATE, BUILD_TIME, BUILD_PLATFORM);
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

[[nodiscard]] ParseResult parse_args(int argc, char *argv[])
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

        if (auto it = std::ranges::find(flag_specs, name, &FlagSpec::name);
            it != flag_specs.end())
        {
            if (has_inline_value)
                return std::unexpected(std::format("Flag {} does not take a value", name));
            result.options.*(it->target) = true;
            continue;
        }

        if (auto it = std::ranges::find(int_opt_specs, name, &IntOptSpec::name);
            it != int_opt_specs.end())
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

constexpr bool is_digit_char(char c) noexcept
{
    return c >= '0' && c <= '9';
}

constexpr bool is_alpha_char(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr bool is_alnum_char(char c) noexcept
{
    return is_alpha_char(c) || is_digit_char(c);
}

constexpr bool is_space_char(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r';
}

constexpr bool is_hex_char(char c) noexcept
{
    return is_digit_char(c) ||
           (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

constexpr char to_lower_char(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

[[nodiscard]] std::string strip_html_tags(std::string_view html)
{
    std::string result;
    result.reserve(html.size());
    bool in_tag = false;
    for (char c : html)
    {
        if (c == '<')
            in_tag = true;
        else if (c == '>')
            in_tag = false;
        else if (!in_tag)
            result.push_back(c);
    }
    return result;
}

void trim_edges(std::string &str, auto &&is_edge_char) noexcept
{
    if (str.empty())
        return;

    const auto is_keep = [&](char ch) noexcept { return !is_edge_char(ch); };

    str.erase(str.begin(), std::ranges::find_if(str, is_keep));
    if (str.empty())
        return;
    str.erase(std::find_if(str.rbegin(), str.rend(), is_keep).base(), str.end());
}

void trim_digits_inplace(std::string &str) noexcept
{
    trim_edges(str, is_digit_char);
}

void trim_special_inplace(std::string &str) noexcept
{
    trim_edges(str, [](char c) noexcept { return !is_alnum_char(c); });
}

[[nodiscard]] constexpr bool is_valid_email(std::string_view str) noexcept
{
    const auto at_pos = str.find('@');
    if (at_pos == std::string_view::npos || at_pos == 0 || at_pos == str.length() - 1)
        return false;

    const auto dot_pos = str.find('.', at_pos + 1);
    return dot_pos != std::string_view::npos &&
           dot_pos > at_pos + 1 &&
           dot_pos < str.length() - 1;
}

[[nodiscard]] std::pair<std::string, std::string> split_email(std::string_view email)
{
    const auto at_pos = email.find('@');
    if (at_pos == std::string_view::npos)
        return {std::string{email}, ""};
    return {std::string{email.substr(0, at_pos)},
            std::string{email.substr(at_pos + 1)}};
}

[[nodiscard]] Expected<std::vector<char>> read_file(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return std::unexpected(std::format("Unable to open file: {}", path.string()));

    const std::streamoff size = file.tellg();
    if (size < 0)
        return std::unexpected(std::format("Unable to determine file size: {}", path.string()));

    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(static_cast<std::size_t>(size));
    if (size > 0 && !file.read(buffer.data(), size))
        return std::unexpected(std::format("Unable to read file: {}", path.string()));

    return buffer;
}

[[nodiscard]] Expected<void> write_lines(const std::vector<std::string> &words, const fs::path &path)
{
    std::ofstream out(path);
    if (!out)
        return std::unexpected(std::format("Failed to open output file for writing: {}", path.string()));

    for (const auto &word : words)
    {
        out << word << '\n';
        if (!out)
            return std::unexpected(std::format("Failed to write to output file: {}", path.string()));
    }
    return {};
}

[[nodiscard]] std::optional<std::string> process_word(std::string_view word, const Options &options)
{
    std::string processed{word};

    if (options.lower)
        std::ranges::transform(processed, processed.begin(), to_lower_char);

    if (options.digit_trim)
        trim_digits_inplace(processed);

    if (options.special_trim)
        trim_special_inplace(processed);

    if (options.detab)
    {
        const auto first_non_space = processed.find_first_not_of(" \t");
        processed = (first_non_space != std::string::npos)
                        ? processed.substr(first_non_space)
                        : std::string{};
    }

    if (options.maxtrim > 0 && processed.size() > static_cast<std::size_t>(options.maxtrim))
        processed.resize(static_cast<std::size_t>(options.maxtrim));

    if (options.dup_remove)
        processed.erase(std::ranges::unique(processed).begin(), processed.end());

    if (options.no_numbers && !processed.empty() &&
        std::ranges::all_of(processed, is_digit_char))
        return std::nullopt;

    if (options.hash_remove && processed.size() >= 32uz &&
        std::ranges::all_of(processed, is_hex_char))
        return std::nullopt;

    if (options.dup_sense > 0 && !processed.empty())
    {
        const double ratio_threshold = static_cast<double>(options.dup_sense) / 100.0;
        std::array<unsigned int, 256> char_counts{};
        for (char raw : processed)
            ++char_counts[static_cast<unsigned char>(raw)];

        if (std::ranges::any_of(char_counts, [&](unsigned int count)
                                { return count > 0 &&
                                         static_cast<double>(count) / static_cast<double>(processed.length()) > ratio_threshold; }))
            return std::nullopt;
    }

    if (options.email_sort && is_valid_email(processed))
    {
        const auto [username, domain] = split_email(processed);
        return std::format("{} {}", username, domain);
    }

    if (processed.empty())
        return std::nullopt;
    return processed;
}

[[nodiscard]] bool process_file(const fs::path &path,
                                std::vector<std::string> &output_words,
                                std::atomic<std::size_t> &total_words_processed_counter,
                                const Options &options)
{
    auto content_result = read_file(path);
    if (!content_result)
    {
        std::println(stderr, "Error: {}", content_result.error());
        return false;
    }

    const std::span file_content{*content_result};

    auto try_add_word = [&](std::string_view candidate)
    {
        auto processed = process_word(candidate, options);
        if (processed &&
            (options.minlen == 0 || processed->size() >= static_cast<std::size_t>(options.minlen)) &&
            (options.maxlen == 0 || processed->size() <= static_cast<std::size_t>(options.maxlen)))
        {
            output_words.push_back(std::move(*processed));
            total_words_processed_counter++;
        }
    };

    for (auto line_range : file_content | std::views::split('\n'))
    {
        std::string line_str(line_range.begin(), line_range.end());

        if (options.dewebify)
        {
            line_str = strip_html_tags(line_str);
            if (options.noutf8)
                std::erase_if(line_str, [](unsigned char c) { return c > 127; });
        }

        if (options.wordify)
        {
            for (auto word_chunk : line_str |
                                      std::views::chunk_by([](char a, char b)
                                                            { return is_space_char(a) == is_space_char(b); }) |
                                      std::views::filter([](auto &&chunk)
                                                         { return !chunk.empty() && !is_space_char(chunk.front()); }))
            {
                try_add_word(std::string(word_chunk.begin(), word_chunk.end()));
            }
        }
        else
        {
            try_add_word(line_str);
        }
    }
    return true;
}

[[nodiscard]] bool process_multiple_files_parallel(const std::vector<fs::path> &paths,
                                                   std::vector<std::string> &words,
                                                   std::atomic<std::size_t> &total_words,
                                                   const Options &options)
{
    std::vector<std::future<std::pair<std::vector<std::string>, bool>>> futures;
    futures.reserve(paths.size());

    for (const auto &path : paths)
    {
        futures.emplace_back(
            std::async(std::launch::async,
                       [&options, path, &total_words]() -> std::pair<std::vector<std::string>, bool>
                       {
                           std::vector<std::string> local_task_words;
                           const bool success = process_file(path, local_task_words, total_words, options);
                           return {std::move(local_task_words), success};
                       }));
    }

    bool all_tasks_successful = true;
    std::vector<std::vector<std::string>> results_from_tasks;
    results_from_tasks.reserve(futures.size());

    for (auto &fut : futures)
    {
        auto [task_words, success] = fut.get();
        if (!success)
        {
            all_tasks_successful = false;
            std::println(stderr, "Error: Failed to process file.");
        }
        results_from_tasks.push_back(std::move(task_words));
    }

    const std::size_t total_elements = std::ranges::fold_left(
        results_from_tasks, std::size_t{0},
        [](std::size_t acc, const auto &vec) { return acc + vec.size(); });

    words.reserve(words.size() + total_elements);

    for (auto &task_result_vec : results_from_tasks)
    {
        words.insert(words.end(),
                     std::make_move_iterator(task_result_vec.begin()),
                     std::make_move_iterator(task_result_vec.end()));
    }

    return all_tasks_successful;
}

int main(int argc, char *argv[])
{
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

    std::println("{} version {} by {} ({} {} {})",
                 PROGRAM_NAME, PROGRAM_VERSION, PROGRAM_AUTHOR, BUILD_DATE, BUILD_TIME, BUILD_PLATFORM);
    std::println("{}\n", PROGRAM_COPYRIGHT);

    const auto start_time = std::chrono::high_resolution_clock::now();
    std::atomic<std::size_t> total_words_processed{0};
    std::vector<std::string> words;

    if (!process_multiple_files_parallel(args.input_paths, words, total_words_processed, args.options))
        std::println(stderr, "Warning: One or more files may have failed to process completely.");

    if (args.options.sort)
        std::ranges::sort(words);

    if (args.options.deduplicate)
    {
        if (!args.options.sort)
        {
            std::ranges::sort(words);
            std::println("Note: Deduplication requires sorting. Words were sorted.");
        }
        words.erase(std::ranges::unique(words).begin(), words.end());
    }

    if (auto write_result = write_lines(words, args.output_path); !write_result)
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
