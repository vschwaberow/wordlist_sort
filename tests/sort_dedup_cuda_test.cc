// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: tests/sort_dedup_cuda_test.cc

#include "sort_dedup.hpp"
#include "test_helpers.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace
{

std::vector<std::string> sample_words()
{
    return {"banana", "apple", "cherry", "apple"};
}

std::vector<std::string> sorted_unique_words()
{
    return {"apple", "banana", "cherry"};
}

void sort_cpu_reference(std::vector<std::string> &words, const bool sort, const bool deduplicate)
{
    sort_and_deduplicate_words_cpu(words, make_sort_dedup_plan(sort, deduplicate));
}

}

TEST(SortDedupCuda, CompiledWhenEnabled)
{
    EXPECT_TRUE(cuda_sort_dedup_is_compiled());
}

TEST(SortDedupCuda, MatchesCpuGoldenVectors)
{
    if (!cuda_sort_dedup_runtime_available())
        GTEST_SKIP() << "No CUDA device available";

    auto cpu_words = sample_words();
    auto gpu_words = sample_words();
    sort_cpu_reference(cpu_words, true, true);

    ASSERT_TRUE(try_sort_and_deduplicate_words_cuda(gpu_words, make_sort_dedup_plan(true, true)));
    EXPECT_EQ(gpu_words, cpu_words);
    EXPECT_EQ(gpu_words, sorted_unique_words());
}

TEST(SortDedupCuda, DedupOnlyMatchesCpu)
{
    if (!cuda_sort_dedup_runtime_available())
        GTEST_SKIP() << "No CUDA device available";

    auto cpu_words = sample_words();
    auto gpu_words = sample_words();
    sort_cpu_reference(cpu_words, false, true);

    ASSERT_TRUE(try_sort_and_deduplicate_words_cuda(gpu_words, make_sort_dedup_plan(false, true)));
    EXPECT_EQ(gpu_words, cpu_words);
}

TEST(SortDedupCuda, EmptyList)
{
    if (!cuda_sort_dedup_runtime_available())
        GTEST_SKIP() << "No CUDA device available";

    std::vector<std::string> words;
    EXPECT_TRUE(try_sort_and_deduplicate_words_cuda(words, make_sort_dedup_plan(true, true)));
    EXPECT_TRUE(words.empty());
}

TEST(SortDedupCuda, DispatchUsesGpuAtLowThreshold)
{
    if (!cuda_sort_dedup_runtime_available())
        GTEST_SKIP() << "No CUDA device available";

    auto words = sample_words();
    sort_and_deduplicate_words(words, SortDedupOptions{
                                            .sort = true,
                                            .deduplicate = true,
                                            .use_cuda = true,
                                            .cuda_threshold = 1,
                                        });
    EXPECT_EQ(words, sorted_unique_words());
}
