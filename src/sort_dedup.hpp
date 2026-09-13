// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/sort_dedup.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <cctype>
#include <cstddef>
#include <mutex>
#include <filesystem>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::size_t kCudaThresholdAuto = 0;
inline constexpr std::size_t kCudaHeuristicMinWords = 100'000;
inline constexpr std::size_t kCudaDefaultThreshold = 10'000'000;

[[nodiscard]] inline int compare_ignore_case(const std::string_view a, const std::string_view b) noexcept
{
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto ca = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(a[i])));
        const auto cb = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    if (a.size() < b.size())
        return -1;
    if (a.size() > b.size())
        return 1;
    return 0;
}

[[nodiscard]] inline bool less_ignore_case(const std::string_view a, const std::string_view b) noexcept
{
    return compare_ignore_case(a, b) < 0;
}

[[nodiscard]] inline bool equal_ignore_case(const std::string_view a, const std::string_view b) noexcept
{
    return compare_ignore_case(a, b) == 0;
}

struct SortDedupOptions
{
    bool sort = false;
    bool deduplicate = false;
    bool use_cuda = false;
    bool no_cuda = false;
    bool cuda_timing = false;
    std::size_t cuda_threshold = kCudaDefaultThreshold; // 0 = auto heuristic
    /// When >0 and word count exceeds this, spill sorted runs to temp files and k-way merge.
    std::size_t sort_chunk = 0;
    bool quiet = false;
    bool ignore_case = false;
    std::filesystem::path tmp_dir; // empty = system temp
};

struct SortDedupPlan
{
    bool perform_sort = false;
    bool perform_deduplicate = false;
    bool announce_implicit_sort = false;
    bool ignore_case = false;
};

[[nodiscard]] SortDedupPlan make_sort_dedup_plan(bool sort, bool deduplicate,
                                                bool ignore_case = false) noexcept;

void announce_implicit_sort_if_needed(const SortDedupPlan &plan);

[[nodiscard]] bool cuda_sort_dedup_is_compiled() noexcept;

[[nodiscard]] bool cuda_sort_dedup_runtime_available() noexcept;

/// Resolve configured threshold: 0 means auto (heuristic minimum word count).
[[nodiscard]] std::size_t resolve_cuda_word_threshold(std::size_t configured_threshold) noexcept;

/// Conservative device-memory estimate for one full SoA sort/dedup pass.
[[nodiscard]] std::size_t estimate_cuda_device_bytes(std::size_t word_count,
                                                     std::size_t payload_bytes) noexcept;

[[nodiscard]] bool cuda_sort_dedup_memory_available(std::size_t word_count,
                                                    std::size_t payload_bytes) noexcept;

[[nodiscard]] std::size_t cuda_sort_dedup_max_words_for_payload(std::size_t payload_bytes_per_chunk,
                                                               std::size_t word_count_hint) noexcept;

[[nodiscard]] bool try_sort_and_deduplicate_words_cuda(std::vector<std::string> &words,
                                                       const SortDedupPlan &plan,
                                                       bool timing = false);

void sort_and_deduplicate_words_cpu(std::vector<std::string> &words, const SortDedupPlan &plan);

/// K-way merge of already-sorted runs; optional cross-run deduplication.
void merge_sorted_word_runs(std::vector<std::vector<std::string>> runs,
                            bool deduplicate,
                            std::vector<std::string> &out);

/// File-backed external sort/dedup: chunk → temp runs → k-way merge into `words`.
void sort_and_deduplicate_words_external(std::vector<std::string> &words,
                                         const SortDedupPlan &plan,
                                         std::size_t chunk_words,
                                         bool quiet = false,
                                         const std::filesystem::path &tmp_dir = {});

/// Incremental external sort: push words during ingest; flush runs at `chunk_words`; finish merges.
class ExternalSortBuilder
{
public:
    ExternalSortBuilder(SortDedupPlan plan, std::size_t chunk_words, bool quiet = false,
                        std::filesystem::path tmp_dir = {});

    void push(std::string word);
    /// Flush remaining buffer and k-way merge into `out`.
    void finish(std::vector<std::string> &out);
    /// Flush/merge directly to a text stream (one word per line); returns lines written.
    [[nodiscard]] std::size_t finish_to_stream(std::ostream &out, char record_sep = '\n');
    [[nodiscard]] std::size_t pushed() const noexcept { return pushed_; }
    [[nodiscard]] std::size_t run_count() const noexcept { return run_paths_.size(); }

    ExternalSortBuilder(const ExternalSortBuilder &) = delete;
    ExternalSortBuilder &operator=(const ExternalSortBuilder &) = delete;

private:
    void flush_unlocked();

    SortDedupPlan plan_{};
    std::size_t chunk_words_ = 0;
    bool quiet_ = false;
    std::filesystem::path tmp_dir_;
    std::size_t pushed_ = 0;
    std::mutex mutex_;
    std::vector<std::string> buffer_;
    std::vector<std::string> run_paths_;
};

void sort_and_deduplicate_words(std::vector<std::string> &words, const SortDedupOptions &options);
