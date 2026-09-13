// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/sort_dedup.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct SortDedupOptions
{
    bool sort = false;
    bool deduplicate = false;
    bool use_cuda = false;
    bool no_cuda = false;
    std::size_t cuda_threshold = 10'000'000;
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

[[nodiscard]] bool try_sort_and_deduplicate_words_cuda(std::vector<std::string> &words,
                                                       const SortDedupPlan &plan);

void sort_and_deduplicate_words_cpu(std::vector<std::string> &words, const SortDedupPlan &plan);

void sort_and_deduplicate_words(std::vector<std::string> &words, const SortDedupOptions &options);
