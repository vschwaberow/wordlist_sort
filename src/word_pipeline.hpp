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
#include <regex>
#include <string>
#include <string_view>
#include <mutex>
#include <ostream>
#include <vector>

class MembershipFilter;

struct SampleState
{
    std::mutex mutex;
    std::vector<std::string> reservoir;
    std::atomic<std::size_t> seen{0};
    std::size_t capacity = 0;
};

class ExternalSortBuilder;

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
    bool upper = false;
    bool reverse = false;
    bool no_numbers = false;
    bool hash_remove = false;
    bool email_sort = false;
    bool email_split = false;
    bool dewebify = false;
    bool noutf8 = false;
    bool wordify = false;
    bool sort = false;
    bool deduplicate = false;
    bool cuda = false;
    bool no_cuda = false;
    bool cuda_timing = false;
    bool quiet = false;
    bool progress = false;
    bool stats = false;
    bool check_sorted = false;
    bool ignore_case = false;
    bool recursive = false;
    bool fuzzy = false;
    bool force = false;
    bool skip_comments = false;
    bool append = false;
    bool null_separated = false;
    int cuda_threshold = 10'000'000;
    /// Parallel input workers: -1 = auto (default; omit --jobs), 0 = unlimited, >0 = cap.
    int jobs = -1;
    int sort_chunk = 0;
    /// Cap accepted survivors during ingest (0 = unlimited).
    int limit = 0;
    int every_n = 0;
    int sample_n = 0;
    /// 1-based field cut; 0 = off.
    int field = 0;
    /// Max Levenshtein distance for --fuzzy (0..3; default 1 when fuzzy).
    int distance = 1;
    std::string delimiter = "	";
    std::string tmp_dir;
    std::string prefix;
    std::string suffix;
    std::string regex_pattern;
    std::string compress;
    const std::regex *regex = nullptr;
    std::string format = "text";
    std::string exclude_path;
    std::string intersect_path;
    std::string lookup_path;
    std::string filter_engine = "hash";
    std::string output_override;
    const MembershipFilter *membership = nullptr;
    bool membership_exclude = true;
    /// When set, survivors are written here instead of buffered in memory (text path).
    std::ostream *stream_out = nullptr;
    std::mutex *stream_mutex = nullptr;
    std::atomic<std::size_t> *stream_emitted = nullptr;
    /// Counts accepted survivors; used with --limit and optional stream accounting.
    std::atomic<std::size_t> *survivor_count = nullptr;
    ExternalSortBuilder *external_sort = nullptr;
    std::atomic<std::size_t> *every_counter = nullptr;
    SampleState *sample = nullptr;
};

[[nodiscard]] inline bool is_stdio_path(const std::filesystem::path &path) noexcept
{
    return path == "-";
}

[[nodiscard]] constexpr bool is_digit_char(char c) noexcept;
[[nodiscard]] constexpr bool is_alpha_char(char c) noexcept;
[[nodiscard]] constexpr bool is_alnum_char(char c) noexcept;
[[nodiscard]] constexpr bool is_space_char(char c) noexcept;
[[nodiscard]] constexpr bool is_hex_char(char c) noexcept;
[[nodiscard]] constexpr char to_lower_char(char c) noexcept;
[[nodiscard]] constexpr char to_upper_char(char c) noexcept;

[[nodiscard]] std::string strip_html_tags(std::string_view html);

void trim_digits_inplace(std::string &str) noexcept;
void trim_special_inplace(std::string &str) noexcept;

[[nodiscard]] constexpr bool is_valid_email(std::string_view str) noexcept;
[[nodiscard]] std::pair<std::string, std::string> split_email(std::string_view email);

[[nodiscard]] std::optional<std::string> process_word(std::string_view word, const Options &options);

[[nodiscard]] std::expected<std::vector<char>, std::string> read_file(const std::filesystem::path &path);
[[nodiscard]] std::expected<void, std::string> write_lines(const std::vector<std::string> &words,
                                                           const std::filesystem::path &path,
                                                           bool append = false,
                                                           bool null_separated = false);

[[nodiscard]] bool process_file(const std::filesystem::path &path,
                                std::vector<std::string> &output_words,
                                std::atomic<std::size_t> &total_words_processed_counter,
                                const Options &options);

[[nodiscard]] bool check_inputs_sorted(const std::vector<std::filesystem::path> &paths,
                                       bool null_separated,
                                       bool skip_comments,
                                       bool require_unique,
                                       bool ignore_case,
                                       std::string *error_out);

[[nodiscard]] bool process_multiple_files_parallel(const std::vector<std::filesystem::path> &paths,
                                                   std::vector<std::string> &words,
                                                   std::atomic<std::size_t> &total_words,
                                                   const Options &options);
