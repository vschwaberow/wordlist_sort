// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/sort_dedup_cuda_stub.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "sort_dedup.hpp"

#include <cstdint>

[[nodiscard]] bool cuda_sort_dedup_is_compiled() noexcept
{
    return false;
}

[[nodiscard]] bool cuda_sort_dedup_runtime_available() noexcept
{
    return false;
}

[[nodiscard]] std::size_t resolve_cuda_word_threshold(const std::size_t configured_threshold) noexcept
{
    if (configured_threshold == kCudaThresholdAuto)
        return kCudaHeuristicMinWords;
    return configured_threshold;
}

[[nodiscard]] std::size_t estimate_cuda_device_bytes(const std::size_t word_count,
                                                     const std::size_t payload_bytes) noexcept
{
    const std::size_t index_bytes = word_count * sizeof(std::uint32_t) * 3;
    const std::size_t base = payload_bytes + index_bytes;
    return base + base;
}

[[nodiscard]] bool cuda_sort_dedup_memory_available(std::size_t, std::size_t) noexcept
{
    return false;
}

[[nodiscard]] std::size_t cuda_sort_dedup_max_words_for_payload(std::size_t, std::size_t) noexcept
{
    return 0;
}

[[nodiscard]] bool try_sort_and_deduplicate_words_cuda(std::vector<std::string> &,
                                                       const SortDedupPlan &,
                                                       bool)
{
    return false;
}
