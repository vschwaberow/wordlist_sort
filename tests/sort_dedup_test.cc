// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: tests/sort_dedup_test.cc

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

}

TEST(SortDedupPlan, NoOpWhenDisabled)
{
    const SortDedupPlan plan = make_sort_dedup_plan(false, false);
    EXPECT_FALSE(plan.perform_sort);
    EXPECT_FALSE(plan.perform_deduplicate);
    EXPECT_FALSE(plan.announce_implicit_sort);
}

TEST(SortDedupPlan, SortOnly)
{
    const SortDedupPlan plan = make_sort_dedup_plan(true, false);
    EXPECT_TRUE(plan.perform_sort);
    EXPECT_FALSE(plan.perform_deduplicate);
    EXPECT_FALSE(plan.announce_implicit_sort);
}

TEST(SortDedupPlan, DedupImpliesSort)
{
    const SortDedupPlan plan = make_sort_dedup_plan(false, true);
    EXPECT_TRUE(plan.perform_sort);
    EXPECT_TRUE(plan.perform_deduplicate);
    EXPECT_TRUE(plan.announce_implicit_sort);
}

TEST(SortDedupPlan, SortAndDedup)
{
    const SortDedupPlan plan = make_sort_dedup_plan(true, true);
    EXPECT_TRUE(plan.perform_sort);
    EXPECT_TRUE(plan.perform_deduplicate);
    EXPECT_FALSE(plan.announce_implicit_sort);
}

TEST(SortDedupCpu, SortOnly)
{
    auto words = sample_words();
    sort_and_deduplicate_words_cpu(words, make_sort_dedup_plan(true, false));
    EXPECT_EQ(words, (std::vector<std::string>{"apple", "apple", "banana", "cherry"}));
}

TEST(SortDedupCpu, DedupOnlyAnnouncesImplicitSort)
{
    auto words = sample_words();
    std::string captured_stdout;
    {
        testing::internal::CaptureStdout();
        sort_and_deduplicate_words_cpu(words, make_sort_dedup_plan(false, true));
        captured_stdout = testing::internal::GetCapturedStdout();
    }
    EXPECT_EQ(words, sorted_unique_words());
    EXPECT_NE(captured_stdout.find("Deduplication requires sorting"), std::string::npos);
}

TEST(SortDedupCpu, EmptyList)
{
    std::vector<std::string> words;
    sort_and_deduplicate_words_cpu(words, make_sort_dedup_plan(true, true));
    EXPECT_TRUE(words.empty());
}

TEST(SortDedupCpu, SingleElement)
{
    std::vector<std::string> words{"solo"};
    sort_and_deduplicate_words_cpu(words, make_sort_dedup_plan(true, true));
    ASSERT_EQ(words.size(), 1U);
    EXPECT_EQ(words.front(), "solo");
}

TEST(SortDedupCpu, AllDuplicates)
{
    std::vector<std::string> words{"x", "x", "x"};
    sort_and_deduplicate_words_cpu(words, make_sort_dedup_plan(true, true));
    ASSERT_EQ(words.size(), 1U);
    EXPECT_EQ(words.front(), "x");
}

TEST(SortDedupCpu, LexicographicPrefixOrder)
{
    std::vector<std::string> words{"b", "aa", "a"};
    sort_and_deduplicate_words_cpu(words, make_sort_dedup_plan(true, false));
    EXPECT_EQ(words, (std::vector<std::string>{"a", "aa", "b"}));
}

TEST(SortDedupCpu, NonAsciiByteOrder)
{
    std::vector<std::string> words{"\xFF", "\x01", "\x80"};
    sort_and_deduplicate_words_cpu(words, make_sort_dedup_plan(true, false));
    EXPECT_EQ(words, (std::vector<std::string>{"\x01", "\x80", "\xFF"}));
}

TEST(SortDedupDispatch, CudaIgnoredWithoutCudaBuild)
{
    auto words = sample_words();
    sort_and_deduplicate_words(words, SortDedupOptions{
                                            .sort = true,
                                            .deduplicate = true,
                                            .use_cuda = true,
                                        });
    EXPECT_EQ(words, sorted_unique_words());
}

TEST(SortDedupDispatch, ThresholdUsesCpuPath)
{
    auto words = sample_words();
    sort_and_deduplicate_words(words, SortDedupOptions{
                                            .sort = true,
                                            .deduplicate = true,
                                            .use_cuda = true,
                                            .cuda_threshold = 1'000'000,
                                        });
    EXPECT_EQ(words, sorted_unique_words());
}
