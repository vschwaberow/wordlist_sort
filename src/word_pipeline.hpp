// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/word_pipeline.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <atomic>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <mutex>
#include <ostream>
#include <vector>

class MembershipFilter;

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
    bool cuda = false;
    bool no_cuda = false;
    bool cuda_timing = false;
    int cuda_threshold = 10'000'000;
    /// Parallel input workers: -1 = auto (default; omit --jobs), 0 = unlimited, >0 = cap.
    int jobs = -1;
    int sort_chunk = 0;
    std::string format = "text";
    std::string exclude_path;
    std::string intersect_path;
    std::string filter_engine = "hash";
    const MembershipFilter *membership = nullptr;
    bool membership_exclude = true;
    /// When set, survivors are written here instead of buffered in memory (text path).
    std::ostream *stream_out = nullptr;
    std::mutex *stream_mutex = nullptr;
    std::atomic<std::size_t> *stream_emitted = nullptr;
};

[[nodiscard]] constexpr bool is_digit_char(char c) noexcept;
[[nodiscard]] constexpr bool is_alpha_char(char c) noexcept;
[[nodiscard]] constexpr bool is_alnum_char(char c) noexcept;
[[nodiscard]] constexpr bool is_space_char(char c) noexcept;
[[nodiscard]] constexpr bool is_hex_char(char c) noexcept;
[[nodiscard]] constexpr char to_lower_char(char c) noexcept;

[[nodiscard]] std::string strip_html_tags(std::string_view html);

void trim_digits_inplace(std::string &str) noexcept;
void trim_special_inplace(std::string &str) noexcept;

[[nodiscard]] constexpr bool is_valid_email(std::string_view str) noexcept;
[[nodiscard]] std::pair<std::string, std::string> split_email(std::string_view email);

[[nodiscard]] std::optional<std::string> process_word(std::string_view word, const Options &options);

[[nodiscard]] std::expected<std::vector<char>, std::string> read_file(const std::filesystem::path &path);
[[nodiscard]] std::expected<void, std::string> write_lines(const std::vector<std::string> &words,
                                                           const std::filesystem::path &path);

[[nodiscard]] bool process_file(const std::filesystem::path &path,
                                std::vector<std::string> &output_words,
                                std::atomic<std::size_t> &total_words_processed_counter,
                                const Options &options);

[[nodiscard]] bool process_multiple_files_parallel(const std::vector<std::filesystem::path> &paths,
                                                   std::vector<std::string> &words,
                                                   std::atomic<std::size_t> &total_words,
                                                   const Options &options);
