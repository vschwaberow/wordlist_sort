// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/cuda_available.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "sort_dedup.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cstdint>

[[nodiscard]] bool cuda_sort_dedup_is_compiled() noexcept
{
    return true;
}

[[nodiscard]] bool cuda_sort_dedup_runtime_available() noexcept
{
    int device_count = 0;
    return cudaGetDeviceCount(&device_count) == cudaSuccess && device_count > 0;
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
    // payload + offsets + lengths + indices, with 2x headroom for temporaries / fragmentation
    const std::size_t index_bytes = word_count * sizeof(std::uint32_t) * 3;
    const std::size_t base = payload_bytes + index_bytes;
    return base + base; // 2x
}

[[nodiscard]] bool cuda_sort_dedup_memory_available(const std::size_t word_count,
                                                    const std::size_t payload_bytes) noexcept
{
    if (!cuda_sort_dedup_runtime_available())
        return false;

    std::size_t free_bytes = 0;
    std::size_t total_bytes = 0;
    if (cudaMemGetInfo(&free_bytes, &total_bytes) != cudaSuccess)
        return false;

    const std::size_t need = estimate_cuda_device_bytes(word_count, payload_bytes);
    // Keep 12.5% VRAM free for the driver / other allocations.
    return need > 0 && need < (free_bytes - free_bytes / 8);
}

[[nodiscard]] std::size_t cuda_sort_dedup_max_words_for_payload(const std::size_t avg_payload_bytes,
                                                                const std::size_t word_count_hint) noexcept
{
    if (!cuda_sort_dedup_runtime_available())
        return 0;

    std::size_t free_bytes = 0;
    std::size_t total_bytes = 0;
    if (cudaMemGetInfo(&free_bytes, &total_bytes) != cudaSuccess)
        return 0;

    const std::size_t budget = free_bytes - free_bytes / 8;
    const std::size_t per_word = std::max<std::size_t>(avg_payload_bytes, 1) + sizeof(std::uint32_t) * 3;
    // account for 2x headroom used in estimate_cuda_device_bytes
    const std::size_t max_words = budget / (per_word * 2);
    if (word_count_hint > 0)
        return std::min(max_words, word_count_hint);
    return max_words;
}
