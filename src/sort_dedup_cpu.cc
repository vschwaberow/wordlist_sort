// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/sort_dedup_cpu.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "sort_dedup.hpp"

#include <algorithm>
#include <print>
#include <ranges>

namespace
{

[[nodiscard]] bool cuda_requested(const SortDedupOptions &options) noexcept
{
    return options.use_cuda && !options.no_cuda;
}

[[nodiscard]] bool cuda_eligible(const SortDedupOptions &options, const std::size_t word_count) noexcept
{
    return cuda_requested(options) &&
           cuda_sort_dedup_is_compiled() &&
           word_count >= options.cuda_threshold &&
           cuda_sort_dedup_runtime_available();
}

void print_cuda_not_compiled_note()
{
    std::println(stderr,
                 "Note: --cuda ignored (built without CUDA support; reconfigure with -DWORDLIST_SORT_CUDA=ON).");
}

void print_cuda_fallback_warning()
{
    std::println(stderr, "Warning: CUDA sort/dedup failed; falling back to CPU.");
}

}

[[nodiscard]] SortDedupPlan make_sort_dedup_plan(const bool sort, const bool deduplicate) noexcept
{
    return SortDedupPlan{
        .perform_sort = sort || deduplicate,
        .perform_deduplicate = deduplicate,
        .announce_implicit_sort = deduplicate && !sort,
    };
}

void announce_implicit_sort_if_needed(const SortDedupPlan &plan)
{
    if (plan.announce_implicit_sort)
        std::println("Note: Deduplication requires sorting. Words were sorted.");
}

void sort_and_deduplicate_words_cpu(std::vector<std::string> &words, const SortDedupPlan &plan)
{
    if (plan.perform_sort)
        std::ranges::sort(words);

    if (!plan.perform_deduplicate)
        return;

    announce_implicit_sort_if_needed(plan);
    words.erase(std::ranges::unique(words).begin(), words.end());
}

void sort_and_deduplicate_words(std::vector<std::string> &words, const SortDedupOptions &options)
{
    const SortDedupPlan plan = make_sort_dedup_plan(options.sort, options.deduplicate);
    if (!plan.perform_sort && !plan.perform_deduplicate)
        return;

    if (cuda_requested(options) && !cuda_sort_dedup_is_compiled())
        print_cuda_not_compiled_note();
    else if (cuda_eligible(options, words.size()))
    {
        if (try_sort_and_deduplicate_words_cuda(words, plan))
            return;

        print_cuda_fallback_warning();
    }

    sort_and_deduplicate_words_cpu(words, plan);
}
