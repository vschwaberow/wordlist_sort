// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: tests/bloom_filter_test.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "bloom_filter.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(BloomFilter, NeverFalseNegativeOnInsertedKeys)
{
    const std::vector<std::string> keys = {"alpha", "beta", "gamma", "delta", "epsilon"};
    BloomFilter bloom(keys.size(), 10);
    for (const auto &k : keys)
        bloom.insert(k);
    for (const auto &k : keys)
        EXPECT_TRUE(bloom.maybe_contains(k)) << k;
}

TEST(BloomFilter, AbsentKeysOftenRejected)
{
    BloomFilter bloom(1000, 10);
    for (int i = 0; i < 1000; ++i)
        bloom.insert("key-" + std::to_string(i));

    int false_positives = 0;
    constexpr int probes = 2000;
    for (int i = 0; i < probes; ++i)
    {
        const std::string miss = "miss-" + std::to_string(i);
        if (bloom.maybe_contains(miss))
            ++false_positives;
    }
    // ~1% expected at 10 bits/key; allow generous slack for small sample.
    EXPECT_LT(false_positives, probes / 5);
}

TEST(BloomFilter, BitsPerKeyClamped)
{
    BloomFilter low(10, 1);
    EXPECT_EQ(low.bits_per_key(), 4);
    BloomFilter high(10, 100);
    EXPECT_EQ(high.bits_per_key(), 24);
}
