// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/word_pipeline.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "word_pipeline.hpp"
#include "gzip_stream.hpp"
#include "io_buffer.hpp"
#include "membership_filter.hpp"
#include "sort_dedup.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iostream>
#include <future>
#include <memory>
#include <optional>
#include <mutex>
#include <semaphore>
#include <thread>
#include <print>
#include <ranges>
#include <random>
#include <span>

namespace fs = std::filesystem;

[[nodiscard]] constexpr bool is_digit_char(const char c) noexcept
{
    return c >= '0' && c <= '9';
}

[[nodiscard]] constexpr bool is_alpha_char(const char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

[[nodiscard]] constexpr bool is_alnum_char(const char c) noexcept
{
    return is_alpha_char(c) || is_digit_char(c);
}

[[nodiscard]] constexpr bool is_space_char(const char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r';
}

[[nodiscard]] constexpr bool is_hex_char(const char c) noexcept
{
    return is_digit_char(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

[[nodiscard]] constexpr char to_lower_char(const char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

[[nodiscard]] constexpr char to_upper_char(const char c) noexcept
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] std::string strip_html_tags(const std::string_view html)
{
    std::string result;
    result.reserve(html.size());
    bool in_tag = false;
    for (const char c : html)
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

namespace
{

void trim_edges(std::string &str, auto &&is_edge_char) noexcept
{
    if (str.empty())
        return;

    const auto is_keep = [&](const char ch) noexcept { return !is_edge_char(ch); };

    str.erase(str.begin(), std::ranges::find_if(str, is_keep));
    if (str.empty())
        return;
    str.erase(std::find_if(str.rbegin(), str.rend(), is_keep).base(), str.end());
}

}

void trim_digits_inplace(std::string &str) noexcept
{
    trim_edges(str, is_digit_char);
}

void trim_special_inplace(std::string &str) noexcept
{
    trim_edges(str, [](const char c) noexcept { return !is_alnum_char(c); });
}

[[nodiscard]] constexpr bool is_valid_email(const std::string_view str) noexcept
{
    const auto at_pos = str.find('@');
    if (at_pos == std::string_view::npos || at_pos == 0 || at_pos == str.length() - 1)
        return false;

    const auto dot_pos = str.find('.', at_pos + 1);
    return dot_pos != std::string_view::npos && dot_pos > at_pos + 1 && dot_pos < str.length() - 1;
}

[[nodiscard]] std::pair<std::string, std::string> split_email(const std::string_view email)
{
    const auto at_pos = email.find('@');
    if (at_pos == std::string_view::npos)
        return {std::string{email}, ""};
    return {std::string{email.substr(0, at_pos)}, std::string{email.substr(at_pos + 1)}};
}

[[nodiscard]] char field_delimiter_char(const std::string_view delim) noexcept
{
    if (delim.empty())
        return '\t';
    if (delim == "\\t" || delim == "\t")
        return '\t';
    if (delim == ",")
        return ',';
    return delim.front();
}

/// Extract 1-based field; nullopt if the field is missing.
[[nodiscard]] std::optional<std::string_view> extract_field(const std::string_view line,
                                                            const int field,
                                                            const char delim) noexcept
{
    if (field <= 0)
        return line;

    int index = 1;
    std::size_t start = 0;
    while (true)
    {
        const auto pos = line.find(delim, start);
        if (index == field)
        {
            if (pos == std::string_view::npos)
                return line.substr(start);
            return line.substr(start, pos - start);
        }
        if (pos == std::string_view::npos)
            return std::nullopt;
        start = pos + 1;
        ++index;
    }
}

[[nodiscard]] std::optional<std::string> process_word(const std::string_view word, const Options &options)
{
    std::string processed{word};

    if (options.lower)
        std::ranges::transform(processed, processed.begin(), to_lower_char);
    if (options.upper)
        std::ranges::transform(processed, processed.begin(), to_upper_char);
    if (options.reverse)
        std::ranges::reverse(processed);

    if (options.digit_trim)
        trim_digits_inplace(processed);

    if (options.special_trim)
        trim_special_inplace(processed);

    if (options.detab)
    {
        const auto first_non_space = processed.find_first_not_of(" \t");
        processed = (first_non_space != std::string::npos) ? processed.substr(first_non_space) : std::string{};
    }

    if (options.maxtrim > 0 && processed.size() > static_cast<std::size_t>(options.maxtrim))
        processed.resize(static_cast<std::size_t>(options.maxtrim));

    if (options.dup_remove)
        processed.erase(std::ranges::unique(processed).begin(), processed.end());

    if (options.no_numbers && !processed.empty() && std::ranges::all_of(processed, is_digit_char))
        return std::nullopt;

    if (options.hash_remove && processed.size() >= 32uz && std::ranges::all_of(processed, is_hex_char))
        return std::nullopt;

    if (options.dup_sense > 0 && !processed.empty())
    {
        const double ratio_threshold = static_cast<double>(options.dup_sense) / 100.0;
        std::array<unsigned int, 256> char_counts{};
        for (const char raw : processed)
            ++char_counts[static_cast<unsigned char>(raw)];

        if (std::ranges::any_of(char_counts, [&](const unsigned int count)
                                {
                                    return count > 0 &&
                                           static_cast<double>(count) /
                                                   static_cast<double>(processed.length()) > ratio_threshold;
                                }))
            return std::nullopt;
    }

    if (options.email_sort && is_valid_email(processed))
    {
        const auto [username, domain] = split_email(processed);
        return std::format("{} {}", username, domain);
    }

    if (!options.prefix.empty() && !processed.starts_with(options.prefix))
        return std::nullopt;
    if (!options.suffix.empty() && !processed.ends_with(options.suffix))
        return std::nullopt;
    if (options.regex != nullptr && !std::regex_search(processed, *options.regex))
        return std::nullopt;

    if (processed.empty())
        return std::nullopt;
    return processed;
}

[[nodiscard]] std::expected<std::vector<char>, std::string> read_file(const fs::path &path)
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

[[nodiscard]] std::expected<void, std::string> write_lines(const std::vector<std::string> &words,
                                                           const fs::path &path, const bool append,
                                                           const bool null_separated)
{
    const char sep = null_separated ? '\0' : '\n';
    if (is_stdio_path(path))
    {
        BufferedRecordWriter writer(std::cout, sep);
        for (const auto &word : words)
            writer.write(word);
        if (!writer.flush() || !writer.good())
            return std::unexpected("Failed to write to stdout");
        return {};
    }

    std::string open_error;
    auto out = open_text_output_stream(path, append, &open_error);
    if (!out)
        return std::unexpected(open_error.empty()
                                   ? std::format("Failed to open output file for writing: {}", path.string())
                                   : open_error);

    BufferedRecordWriter writer(*out, sep);
    for (const auto &word : words)
        writer.write(word);
    if (!writer.flush() || !writer.good())
        return std::unexpected(std::format("Failed to write to output file: {}", path.string()));
    return {};
}

[[nodiscard]] bool process_file(const fs::path &path,
                                std::vector<std::string> &output_words,
                                std::atomic<std::size_t> &total_words_processed_counter,
                                const Options &options)
{
    std::unique_ptr<std::istream> owned_in;
    std::istream *in = nullptr;
    if (is_stdio_path(path))
    {
        in = &std::cin;
    }
    else
    {
        std::string open_error;
        owned_in = open_input_stream(path, &open_error);
        if (!owned_in)
        {
            std::println(stderr, "Error: {}", open_error);
            return false;
        }
        in = owned_in.get();
    }

    const char record_sep = options.null_separated ? '\0' : '\n';
    std::unique_ptr<BufferedRecordWriter> stream_writer;
    if (options.stream_out != nullptr)
        stream_writer = std::make_unique<BufferedRecordWriter>(*options.stream_out, record_sep, options.stream_mutex);

    const auto accept_survivor = [&]() -> bool
    {
        if (options.survivor_count == nullptr)
            return options.limit <= 0;

        if (options.limit <= 0)
        {
            options.survivor_count->fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        const std::size_t lim = static_cast<std::size_t>(options.limit);
        std::size_t cur = options.survivor_count->load(std::memory_order_relaxed);
        while (cur < lim)
        {
            if (options.survivor_count->compare_exchange_weak(cur, cur + 1, std::memory_order_relaxed,
                                                              std::memory_order_relaxed))
                return true;
        }
        return false;
    };

    const auto limit_reached = [&]() -> bool
    {
        return options.limit > 0 && options.survivor_count != nullptr &&
               options.survivor_count->load(std::memory_order_relaxed) >=
                   static_cast<std::size_t>(options.limit);
    };

    const auto try_add_word = [&](const std::string_view candidate)
    {
        if (limit_reached())
            return;

        auto processed = process_word(candidate, options);
        if (!processed)
            return;
        if (options.minlen != 0 && processed->size() < static_cast<std::size_t>(options.minlen))
            return;
        if (options.maxlen != 0 && processed->size() > static_cast<std::size_t>(options.maxlen))
            return;

        total_words_processed_counter++;

        const auto emit_one = [&](std::string word) {
            if (!accept_survivor())
                return false;

            if (options.every_n > 0 && options.every_counter != nullptr)
            {
                const std::size_t i = options.every_counter->fetch_add(1, std::memory_order_relaxed);
                if ((i % static_cast<std::size_t>(options.every_n)) != 0)
                    return true;
            }

            if (options.sample != nullptr && options.sample->capacity > 0)
            {
                const std::size_t i = options.sample->seen.fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(options.sample->mutex);
                if (i < options.sample->capacity)
                {
                    options.sample->reservoir.push_back(std::move(word));
                }
                else
                {
                    thread_local std::mt19937_64 rng{std::random_device{}()};
                    std::uniform_int_distribution<std::size_t> dist(0, i);
                    const std::size_t j = dist(rng);
                    if (j < options.sample->capacity)
                        options.sample->reservoir[j] = std::move(word);
                }
                return true;
            }

            if (stream_writer != nullptr)
            {
                stream_writer->write(word);
                if (options.stream_emitted != nullptr && options.stream_emitted != options.survivor_count)
                    options.stream_emitted->fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            if (options.external_sort != nullptr)
            {
                options.external_sort->push(std::move(word));
                return true;
            }
            output_words.push_back(std::move(word));
            return true;
        };

        if (options.fuzzy && options.membership != nullptr)
        {
            const auto matches = options.membership->fuzzy_search(*processed, options.distance);
            if (!matches)
            {
                std::println(stderr, "Error: {}", matches.error());
                return;
            }
            for (const auto &match : *matches)
            {
                if (limit_reached())
                    break;
                if (!emit_one(match))
                    break;
            }
            return;
        }

        if (options.membership != nullptr)
        {
            const bool hit = options.membership->contains(*processed);
            const bool drop = options.membership_exclude ? hit : !hit;
            if (drop)
                return;
        }
        static_cast<void>(emit_one(std::move(*processed)));
    };

    BufferedRecordReader reader(*in, record_sep);
    std::string line_str;
    while (reader.next(line_str))
    {
        if (limit_reached())
            break;

        if (options.skip_comments)
        {
            const auto first = line_str.find_first_not_of(" \t");
            if (first != std::string::npos && line_str[first] == '#')
                continue;
        }

        if (options.dewebify)
            line_str = strip_html_tags(line_str);

        if (options.noutf8)
            std::erase_if(line_str, [](const unsigned char c) { return c > 127; });

        if (options.field > 0)
        {
            const char delim = field_delimiter_char(options.delimiter);
            const auto field_view = extract_field(line_str, options.field, delim);
            if (!field_view)
                continue;
            line_str.assign(*field_view);
        }

        if (options.wordify)
        {
            for (auto word_chunk : line_str |
                                      std::views::chunk_by([](char a, char b)
                                                            { return is_space_char(a) == is_space_char(b); }) |
                                      std::views::filter([](auto &&chunk)
                                                         { return !chunk.empty() && !is_space_char(chunk.front()); }))
            {
                const std::string chunk_word(word_chunk.begin(), word_chunk.end());
                if (options.email_split && is_valid_email(chunk_word))
                {
                    const auto [username, domain] = split_email(chunk_word);
                    try_add_word(username);
                    if (!domain.empty())
                        try_add_word(domain);
                }
                else
                {
                    try_add_word(chunk_word);
                }
            }
        }
        else
        {
            if (options.email_split && is_valid_email(line_str))
            {
                const auto [username, domain] = split_email(line_str);
                try_add_word(username);
                if (!domain.empty())
                    try_add_word(domain);
            }
            else
            {
                try_add_word(line_str);
            }
        }
    }

    if (in->bad())
    {
        std::println(stderr, "Error: Unable to read file: {}", path.string());
        return false;
    }
    if (stream_writer != nullptr && (!stream_writer->flush() || !stream_writer->good()))
    {
        std::println(stderr, "Error: Failed while writing streamed output for: {}", path.string());
        return false;
    }
    return true;
}


[[nodiscard]] bool check_inputs_sorted(const std::vector<fs::path> &paths,
                                       const bool null_separated,
                                       const bool skip_comments,
                                       const bool require_unique,
                                       const bool ignore_case,
                                       std::string *error_out)
{
    const char sep = null_separated ? '\0' : '\n';
    std::string prev;
    bool have_prev = false;
    std::size_t line_no = 0;

    for (const auto &path : paths)
    {
        std::unique_ptr<std::istream> owned_in;
        std::istream *in = nullptr;
        if (is_stdio_path(path))
        {
            in = &std::cin;
        }
        else
        {
            std::string open_error;
            owned_in = open_input_stream(path, &open_error);
            if (!owned_in)
            {
                if (error_out)
                    *error_out = open_error;
                return false;
            }
            in = owned_in.get();
        }

        BufferedRecordReader reader(*in, sep);
        std::string rec;
        while (reader.next(rec))
        {
            ++line_no;
            if (skip_comments)
            {
                const auto first = rec.find_first_not_of(" \t");
                if (first != std::string::npos && rec[first] == '#')
                    continue;
            }
            if (have_prev)
            {
                const bool ok = ignore_case
                                    ? (require_unique ? compare_ignore_case(prev, rec) < 0
                                                      : compare_ignore_case(prev, rec) <= 0)
                                    : (require_unique ? (prev < rec) : !(prev > rec));
                if (!ok)
                {
                    if (error_out)
                        *error_out = std::format(
                            "{} at record {} (file {}): \"{}\" then \"{}\"",
                            require_unique ? "not strictly ascending" : "disorder",
                            line_no, path.string(), prev, rec);
                    return false;
                }
            }
            prev = std::move(rec);
            have_prev = true;
        }
        if (in->bad())
        {
            if (error_out)
                *error_out = std::format("Unable to read file: {}", path.string());
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::size_t resolve_job_limit(const int jobs, const std::size_t path_count) noexcept
{
    if (path_count == 0)
        return 1;
    if (jobs == 0)
        return path_count; // unlimited: one task per file
    if (jobs > 0)
        return std::min(path_count, static_cast<std::size_t>(jobs));
    const unsigned hw = std::thread::hardware_concurrency();
    const std::size_t auto_jobs = hw == 0 ? 1u : static_cast<std::size_t>(hw);
    return std::min(path_count, auto_jobs);
}

[[nodiscard]] bool process_multiple_files_parallel(const std::vector<fs::path> &paths,
                                                   std::vector<std::string> &words,
                                                   std::atomic<std::size_t> &total_words,
                                                   const Options &options)
{
    const std::size_t stdin_count = static_cast<std::size_t>(
        std::count_if(paths.begin(), paths.end(), [](const fs::path &p) { return is_stdio_path(p); }));
    if (stdin_count > 1)
    {
        std::println(stderr, "Error: stdin (-) may be specified as an input at most once");
        return false;
    }

    std::vector<std::future<std::pair<std::vector<std::string>, bool>>> futures;
    futures.reserve(paths.size());

    const std::size_t job_limit = resolve_job_limit(options.jobs, paths.size());
    // counting_semaphore requires a compile-time-ish max; use a large bound and acquire down to job_limit.
    std::counting_semaphore<> slots{static_cast<std::ptrdiff_t>(job_limit)};

    for (const auto &path : paths)
    {
        futures.emplace_back(std::async(std::launch::async,
                                        [&options, path, &total_words, &slots]() -> std::pair<std::vector<std::string>, bool>
                                        {
                                            slots.acquire();
                                            std::vector<std::string> local_task_words;
                                            const bool success =
                                                process_file(path, local_task_words, total_words, options);
                                            slots.release();
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
        [](const std::size_t acc, const auto &vec) { return acc + vec.size(); });

    words.reserve(words.size() + total_elements);

    for (auto &task_result_vec : results_from_tasks)
    {
        words.insert(words.end(), std::make_move_iterator(task_result_vec.begin()),
                     std::make_move_iterator(task_result_vec.end()));
    }

    return all_tasks_successful;
}
