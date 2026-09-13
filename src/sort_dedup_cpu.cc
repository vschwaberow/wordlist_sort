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

[[nodiscard]] std::size_t total_payload_bytes(const std::vector<std::string> &words) noexcept
{
    std::size_t bytes = 0;
    for (const auto &word : words)
        bytes += word.size();
    return bytes;
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

void print_cuda_below_threshold_note(const std::size_t word_count, const std::size_t threshold)
{
    std::println(stderr,
                 "Note: --cuda ignored ({} words < threshold {}); use --cuda-threshold to lower or 0 for auto.",
                 word_count, threshold);
}

void print_cuda_oom_note()
{
    std::println(stderr,
                 "Note: --cuda ignored (working set does not fit in free VRAM); falling back to CPU.");
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

    if (cuda_requested(options))
    {
        if (!cuda_sort_dedup_is_compiled())
        {
            print_cuda_not_compiled_note();
        }
        else if (!cuda_sort_dedup_runtime_available())
        {
            std::println(stderr, "Note: --cuda ignored (no usable CUDA device); using CPU.");
        }
        else
        {
            const std::size_t threshold = resolve_cuda_word_threshold(options.cuda_threshold);
            const std::size_t payload = total_payload_bytes(words);

            if (words.size() < threshold)
            {
                print_cuda_below_threshold_note(words.size(), threshold);
            }
            else if (!cuda_sort_dedup_memory_available(words.size(), payload))
            {
                print_cuda_oom_note();
            }
            else if (try_sort_and_deduplicate_words_cuda(words, plan, options.cuda_timing))
            {
                return;
            }
            else
            {
                print_cuda_fallback_warning();
            }
        }
    }

    sort_and_deduplicate_words_cpu(words, plan);
}
