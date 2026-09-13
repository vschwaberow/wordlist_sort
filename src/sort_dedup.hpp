// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/sort_dedup.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <cstddef>
#include <string>
#include <vector>

inline constexpr std::size_t kCudaThresholdAuto = 0;
inline constexpr std::size_t kCudaHeuristicMinWords = 100'000;
inline constexpr std::size_t kCudaDefaultThreshold = 10'000'000;

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
};

struct SortDedupPlan
{
    bool perform_sort = false;
    bool perform_deduplicate = false;
    bool announce_implicit_sort = false;
};

[[nodiscard]] SortDedupPlan make_sort_dedup_plan(const bool sort, const bool deduplicate) noexcept;

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
                                         std::size_t chunk_words);

void sort_and_deduplicate_words(std::vector<std::string> &words, const SortDedupOptions &options);
