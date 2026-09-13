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

#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::duration<double, std::milli>;

struct WordTable
{
    std::vector<char> payload;
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> lengths;
};

template <typename T>
struct PinnedBuffer
{
    T *data = nullptr;
    std::size_t size = 0;

    PinnedBuffer() = default;

    explicit PinnedBuffer(const std::size_t count) : size(count)
    {
        if (count == 0)
            return;
        if (cudaMallocHost(reinterpret_cast<void **>(&data), count * sizeof(T)) != cudaSuccess)
            throw std::runtime_error("cudaMallocHost failed");
    }

    PinnedBuffer(const PinnedBuffer &) = delete;
    PinnedBuffer &operator=(const PinnedBuffer &) = delete;

    PinnedBuffer(PinnedBuffer &&other) noexcept : data(other.data), size(other.size)
    {
        other.data = nullptr;
        other.size = 0;
    }

    PinnedBuffer &operator=(PinnedBuffer &&other) noexcept
    {
        if (this == &other)
            return *this;
        reset();
        data = other.data;
        size = other.size;
        other.data = nullptr;
        other.size = 0;
        return *this;
    }

    ~PinnedBuffer()
    {
        reset();
    }

    void reset()
    {
        if (data != nullptr)
        {
            cudaFreeHost(data);
            data = nullptr;
            size = 0;
        }
    }
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

template <typename T>
void copy_to_pinned(PinnedBuffer<T> &dst, const std::vector<T> &src)
{
    dst = PinnedBuffer<T>(src.size());
    if (!src.empty())
        std::memcpy(dst.data, src.data(), src.size() * sizeof(T));
}

template <typename T>
void h2d_pinned(thrust::device_vector<T> &device, const PinnedBuffer<T> &host)
{
    device.resize(host.size);
    if (host.size == 0)
        return;
    if (cudaMemcpy(thrust::raw_pointer_cast(device.data()), host.data, host.size * sizeof(T),
                   cudaMemcpyHostToDevice) != cudaSuccess)
        throw std::runtime_error("cudaMemcpy H2D failed");
}

template <typename T>
void d2h_pinned(PinnedBuffer<T> &host, const thrust::device_vector<T> &device)
{
    host = PinnedBuffer<T>(device.size());
    if (device.empty())
        return;
    if (cudaMemcpy(host.data, thrust::raw_pointer_cast(device.data()), device.size() * sizeof(T),
                   cudaMemcpyDeviceToHost) != cudaSuccess)
        throw std::runtime_error("cudaMemcpy D2H failed");
}

[[nodiscard]] bool run_cuda_sort_dedup(std::vector<std::string> &words,
                                       const SortDedupPlan &plan,
                                       const bool timing)
{
    if (words.empty())
        return true;

    if (!plan.perform_sort && !plan.perform_deduplicate)
        return true;

    const auto wall_start = Clock::now();
    const std::size_t in_words = words.size();
    const WordTable table = build_word_table(words);

    PinnedBuffer<char> pinned_payload;
    PinnedBuffer<std::uint32_t> pinned_offsets;
    PinnedBuffer<std::uint32_t> pinned_lengths;
    copy_to_pinned(pinned_payload, table.payload);
    copy_to_pinned(pinned_offsets, table.offsets);
    copy_to_pinned(pinned_lengths, table.lengths);

    thrust::device_vector<char> device_payload;
    thrust::device_vector<std::uint32_t> device_offsets;
    thrust::device_vector<std::uint32_t> device_lengths;
    thrust::device_vector<std::uint32_t> device_indices(words.size());

    const auto h2d_start = Clock::now();
    h2d_pinned(device_payload, pinned_payload);
    h2d_pinned(device_offsets, pinned_offsets);
    h2d_pinned(device_lengths, pinned_lengths);
    thrust::sequence(device_indices.begin(), device_indices.end(), 0u);
    if (cudaDeviceSynchronize() != cudaSuccess)
        throw std::runtime_error("cudaDeviceSynchronize after H2D failed");
    const double h2d_ms = Ms(Clock::now() - h2d_start).count();

    const char *payload = thrust::raw_pointer_cast(device_payload.data());
    const std::uint32_t *offsets = thrust::raw_pointer_cast(device_offsets.data());
    const std::uint32_t *lengths = thrust::raw_pointer_cast(device_lengths.data());

    double sort_ms = 0.0;
    if (plan.perform_sort)
    {
        const auto sort_start = Clock::now();
        sort_indices(device_indices, payload, offsets, lengths);
        if (cudaDeviceSynchronize() != cudaSuccess)
            throw std::runtime_error("cudaDeviceSynchronize after sort failed");
        sort_ms = Ms(Clock::now() - sort_start).count();
    }

    double dedup_ms = 0.0;
    if (plan.perform_deduplicate)
    {
        announce_implicit_sort_if_needed(plan);
        const auto dedup_start = Clock::now();
        dedup_indices(device_indices, payload, offsets, lengths);
        if (cudaDeviceSynchronize() != cudaSuccess)
            throw std::runtime_error("cudaDeviceSynchronize after dedup failed");
        dedup_ms = Ms(Clock::now() - dedup_start).count();
    }

    const auto d2h_start = Clock::now();
    PinnedBuffer<std::uint32_t> pinned_order;
    d2h_pinned(pinned_order, device_indices);
    if (cudaDeviceSynchronize() != cudaSuccess)
        throw std::runtime_error("cudaDeviceSynchronize after D2H failed");
    const double d2h_ms = Ms(Clock::now() - d2h_start).count();

    std::vector<std::uint32_t> order(pinned_order.size);
    if (pinned_order.size > 0)
        std::memcpy(order.data(), pinned_order.data, pinned_order.size * sizeof(std::uint32_t));

    std::vector<std::string> reordered;
    if (!rebuild_words(words, order, reordered))
        return false;

    words = std::move(reordered);

    if (timing)
    {
        const double total_ms = Ms(Clock::now() - wall_start).count();
        std::fprintf(stderr,
                     "cuda timing: in_words=%zu out_words=%zu h2d=%.3fms sort=%.3fms dedup=%.3fms d2h=%.3fms total=%.3fms\n",
                     in_words, words.size(), h2d_ms, sort_ms, dedup_ms, d2h_ms, total_ms);
    }

    return true;
}

}

[[nodiscard]] bool try_sort_and_deduplicate_words_cuda(std::vector<std::string> &words,
                                                       const SortDedupPlan &plan,
                                                       const bool timing)
{
    try
    {
        return run_cuda_sort_dedup(words, plan, timing);
    }
    catch (...)
    {
        return false;
    }
}
