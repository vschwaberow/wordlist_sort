// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: tests/membership_filter_test.cc

#include "membership_filter.hpp"
#include "word_pipeline.hpp"
#include <atomic>
#include <fstream>
#include <filesystem>
#include "test_helpers.hpp"

#include <gtest/gtest.h>
#include <vector>

TEST(MembershipFilter, HashExcludeSemantics)
{
    const std::vector<std::string> keys{"a", "b", "c"};
    auto filter = build_membership_filter(keys, FilterEngine::Hash);
    ASSERT_TRUE(filter);
    EXPECT_TRUE((*filter)->contains("a"));
    EXPECT_FALSE((*filter)->contains("z"));
}

TEST(MembershipFilter, FstRoundTrip)
{
    const std::vector<std::string> keys{"bank", "banner", "tank"};
    auto filter = build_membership_filter(keys, FilterEngine::Fst);
    ASSERT_TRUE(filter);
    EXPECT_TRUE((*filter)->contains("bank"));
    EXPECT_FALSE((*filter)->contains("ban"));
}

#if defined(WORDLIST_SORT_PTHASH)
TEST(MembershipFilter, PthashRoundTrip)
{
    const std::vector<std::string> keys{"apple", "banana", "cherry", "date"};
    auto filter = build_membership_filter(keys, FilterEngine::Pthash);
    ASSERT_TRUE(filter);
    EXPECT_TRUE((*filter)->contains("banana"));
    EXPECT_FALSE((*filter)->contains("blueberry"));
}
#endif

TEST(MembershipFilterTest, ProcessFileDropsDuringIngest)
{
    const auto dir = std::filesystem::temp_directory_path() / "wordlist_sort_stream_filter";
    std::filesystem::create_directories(dir);
    const auto input = dir / "a.txt";
    {
        std::ofstream out(input);
        out << "a\nb\nc\nd\n";
    }

    const auto filter = build_membership_filter({"b", "d"}, FilterEngine::Hash);
    ASSERT_TRUE(filter.has_value());

    Options options;
    options.membership = filter->get();
    options.membership_exclude = true;

    std::vector<std::string> words;
    std::atomic<std::size_t> counter{0};
    ASSERT_TRUE(process_file(input, words, counter, options));
    EXPECT_EQ(counter.load(), 4u);
    ASSERT_EQ(words.size(), 2u);
    EXPECT_EQ(words[0], "a");
    EXPECT_EQ(words[1], "c");

    std::filesystem::remove_all(dir);
}
