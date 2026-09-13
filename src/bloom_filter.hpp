// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/bloom_filter.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

/// Double-hashing Bloom filter (no false negatives on inserted keys).
class BloomFilter
{
public:
    BloomFilter() = default;

    /// @param n_keys expected insertions; @param bits_per_key clamped to [4, 24] (default ~10 → ~1% FP).
    BloomFilter(std::size_t n_keys, int bits_per_key)
    {
        if (bits_per_key < 4)
            bits_per_key = 4;
        if (bits_per_key > 24)
            bits_per_key = 24;
        bits_per_key_ = bits_per_key;

        const std::size_t n = n_keys == 0 ? 1 : n_keys;
        bit_count_ = n * static_cast<std::size_t>(bits_per_key_);
        if (bit_count_ < 64)
            bit_count_ = 64;
        bit_count_ = (bit_count_ + 63) / 64 * 64;

        // k ≈ ln(2) * bits/key
        hash_count_ = static_cast<std::size_t>((693 * bits_per_key_ + 500) / 1000);
        if (hash_count_ < 1)
            hash_count_ = 1;
        if (hash_count_ > 16)
            hash_count_ = 16;

        bits_.assign(bit_count_ / 64, 0);
    }

    void insert(const std::string_view key) noexcept
    {
        if (bit_count_ == 0)
            return;
        const auto [h1, h2] = hash_pair(key);
        for (std::size_t i = 0; i < hash_count_; ++i)
        {
            const std::size_t bit = (h1 + i * h2) % bit_count_;
            bits_[bit / 64] |= std::uint64_t{1} << (bit % 64);
        }
    }

    /// false => definitely absent; true => maybe present.
    [[nodiscard]] bool maybe_contains(const std::string_view key) const noexcept
    {
        if (bit_count_ == 0)
            return true;
        const auto [h1, h2] = hash_pair(key);
        for (std::size_t i = 0; i < hash_count_; ++i)
        {
            const std::size_t bit = (h1 + i * h2) % bit_count_;
            if ((bits_[bit / 64] & (std::uint64_t{1} << (bit % 64))) == 0)
                return false;
        }
        return true;
    }

    [[nodiscard]] std::size_t bit_count() const noexcept { return bit_count_; }
    [[nodiscard]] int bits_per_key() const noexcept { return bits_per_key_; }

private:
    [[nodiscard]] static std::pair<std::size_t, std::size_t> hash_pair(const std::string_view key) noexcept
    {
        // FNV-1a 64 + mix for independent second hash (Kirsch-Mitzenmacher).
        std::uint64_t h = 14695981039346656037ull;
        for (const unsigned char c : key)
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        std::uint64_t h2 = h ^ (h >> 33);
        h2 *= 0xff51afd7ed558ccdull;
        h2 ^= h2 >> 33;
        h2 *= 0xc4ceb9fe1a85ec53ull;
        h2 ^= h2 >> 33;
        if (h2 == 0)
            h2 = 0x9e3779b97f4a7c15ull;
        return {static_cast<std::size_t>(h), static_cast<std::size_t>(h2)};
    }

    std::vector<std::uint64_t> bits_;
    std::size_t bit_count_ = 0;
    std::size_t hash_count_ = 0;
    int bits_per_key_ = 10;
};
