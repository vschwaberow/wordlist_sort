// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/sort_dedup_cuda.cu
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "sort_dedup.hpp"

#include <cuda_runtime.h>
#include <thrust/copy.h>
#include <thrust/device_vector.h>
#include <thrust/sequence.h>
#include <thrust/sort.h>
#include <thrust/unique.h>

#include <cstdint>
#include <vector>

namespace
{

struct WordTable
{
    std::vector<char> payload;
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> lengths;
};

[[nodiscard]] __device__ int compare_lex(const char *payload,
                                         const std::uint32_t off_a,
                                         const std::uint32_t len_a,
                                         const std::uint32_t off_b,
                                         const std::uint32_t len_b)
{
    const std::uint32_t min_len = len_a < len_b ? len_a : len_b;
    for (std::uint32_t i = 0; i < min_len; ++i)
    {
        const unsigned char left = static_cast<unsigned char>(payload[off_a + i]);
        const unsigned char right = static_cast<unsigned char>(payload[off_b + i]);
        if (left != right)
            return left < right ? -1 : 1;
    }

    if (len_a < len_b)
        return -1;
    if (len_a > len_b)
        return 1;
    return 0;
}

struct IndexLess
{
    const char *payload;
    const std::uint32_t *offsets;
    const std::uint32_t *lengths;

    __device__ bool operator()(const std::uint32_t left, const std::uint32_t right) const
    {
        return compare_lex(payload, offsets[left], lengths[left], offsets[right], lengths[right]) < 0;
    }
};

struct IndexEqual
{
    const char *payload;
    const std::uint32_t *offsets;
    const std::uint32_t *lengths;

    __device__ bool operator()(const std::uint32_t left, const std::uint32_t right) const
    {
        return compare_lex(payload, offsets[left], lengths[left], offsets[right], lengths[right]) == 0;
    }
};

[[nodiscard]] WordTable build_word_table(const std::vector<std::string> &words)
{
    WordTable table;
    table.offsets.reserve(words.size());
    table.lengths.reserve(words.size());

    for (const auto &word : words)
    {
        table.offsets.push_back(static_cast<std::uint32_t>(table.payload.size()));
        table.lengths.push_back(static_cast<std::uint32_t>(word.size()));
        table.payload.insert(table.payload.end(), word.begin(), word.end());
    }

    return table;
}

[[nodiscard]] bool rebuild_words(const std::vector<std::string> &source,
                                 const std::vector<std::uint32_t> &order,
                                 std::vector<std::string> &destination)
{
    destination.clear();
    destination.reserve(order.size());

    for (const std::uint32_t index : order)
    {
        if (index >= source.size())
            return false;
        destination.push_back(source[index]);
    }

    return true;
}

void sort_indices(thrust::device_vector<std::uint32_t> &indices,
                  const char *payload,
                  const std::uint32_t *offsets,
                  const std::uint32_t *lengths)
{
    thrust::sort(indices.begin(), indices.end(), IndexLess{payload, offsets, lengths});
}

void dedup_indices(thrust::device_vector<std::uint32_t> &indices,
                   const char *payload,
                   const std::uint32_t *offsets,
                   const std::uint32_t *lengths)
{
    const auto end = thrust::unique(indices.begin(), indices.end(), IndexEqual{payload, offsets, lengths});
    indices.erase(end, indices.end());
}

}

[[nodiscard]] bool try_sort_and_deduplicate_words_cuda(std::vector<std::string> &words,
                                                       const SortDedupPlan &plan)
{
    if (words.empty())
        return true;

    if (!plan.perform_sort && !plan.perform_deduplicate)
        return true;

    const WordTable table = build_word_table(words);

    thrust::device_vector<char> device_payload(table.payload.begin(), table.payload.end());
    thrust::device_vector<std::uint32_t> device_offsets(table.offsets.begin(), table.offsets.end());
    thrust::device_vector<std::uint32_t> device_lengths(table.lengths.begin(), table.lengths.end());
    thrust::device_vector<std::uint32_t> device_indices(words.size());
    thrust::sequence(device_indices.begin(), device_indices.end(), 0u);

    const char *payload = thrust::raw_pointer_cast(device_payload.data());
    const std::uint32_t *offsets = thrust::raw_pointer_cast(device_offsets.data());
    const std::uint32_t *lengths = thrust::raw_pointer_cast(device_lengths.data());

    if (plan.perform_sort)
        sort_indices(device_indices, payload, offsets, lengths);

    if (plan.perform_deduplicate)
    {
        announce_implicit_sort_if_needed(plan);
        dedup_indices(device_indices, payload, offsets, lengths);
    }

    std::vector<std::uint32_t> order(device_indices.size());
    thrust::copy(device_indices.begin(), device_indices.end(), order.begin());

    std::vector<std::string> reordered;
    if (!rebuild_words(words, order, reordered))
        return false;

    words = std::move(reordered);
    return true;
}
