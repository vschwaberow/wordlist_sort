// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/word_pipeline.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "word_pipeline.hpp"
#include "gzip_stream.hpp"
#include "membership_filter.hpp"
#include "sort_dedup.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iostream>
#include <future>
#include <memory>
#include <mutex>
#include <semaphore>
#include <thread>
#include <print>
#include <ranges>
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

[[nodiscard]] std::optional<std::string> process_word(const std::string_view word, const Options &options)
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
                                                           const fs::path &path)
{
    if (is_stdio_path(path))
    {
        for (const auto &word : words)
        {
            std::cout << word << '\n';
            if (!std::cout)
                return std::unexpected("Failed to write to stdout");
        }
        return {};
    }

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

    const auto try_add_word = [&](const std::string_view candidate)
    {
        auto processed = process_word(candidate, options);
        if (!processed)
            return;
        if (options.minlen != 0 && processed->size() < static_cast<std::size_t>(options.minlen))
            return;
        if (options.maxlen != 0 && processed->size() > static_cast<std::size_t>(options.maxlen))
            return;

        total_words_processed_counter++;
        if (options.membership != nullptr)
        {
            const bool hit = options.membership->contains(*processed);
            const bool drop = options.membership_exclude ? hit : !hit;
            if (drop)
                return;
        }
        if (options.stream_out != nullptr)
        {
            if (options.stream_mutex != nullptr)
            {
                std::lock_guard<std::mutex> lock(*options.stream_mutex);
                *options.stream_out << *processed << '\n';
            }
            else
            {
                *options.stream_out << *processed << '\n';
            }
            if (options.stream_emitted != nullptr)
                options.stream_emitted->fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (options.external_sort != nullptr)
        {
            options.external_sort->push(std::move(*processed));
            return;
        }
        output_words.push_back(std::move(*processed));
    };

    std::string line_str;
    while (std::getline(*in, line_str))
    {
        if (!line_str.empty() && line_str.back() == '\r')
            line_str.pop_back();

        if (options.dewebify)
            line_str = strip_html_tags(line_str);

        if (options.noutf8)
            std::erase_if(line_str, [](const unsigned char c) { return c > 127; });

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

    if (in->bad())
    {
        std::println(stderr, "Error: Unable to read file: {}", path.string());
        return false;
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
