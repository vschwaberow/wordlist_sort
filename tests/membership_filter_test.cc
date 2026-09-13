// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: tests/membership_filter_test.cc

#include "membership_filter.hpp"
#include "export_format.hpp"
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

TEST(MembershipFilterTest, OpenExistingFstAndCdb)
{
    const auto dir = std::filesystem::temp_directory_path() / "wordlist_sort_open_filter";
    std::filesystem::create_directories(dir);
    const std::vector<std::string> keys{"alpha", "beta", "gamma"};

    const auto fst_path = dir / "keys.fst";
    const auto cdb_path = dir / "keys.cdb";
    ASSERT_TRUE(write_fst(keys, fst_path));
    ASSERT_TRUE(write_cdb(keys, cdb_path));

    auto fst = open_membership_filter(fst_path, FilterEngine::Hash);
    ASSERT_TRUE(fst);
    EXPECT_EQ((*fst)->size(), 3u);
    EXPECT_TRUE((*fst)->contains("beta"));
    EXPECT_FALSE((*fst)->contains("delta"));

    auto cdb = open_membership_filter(cdb_path, FilterEngine::Hash);
    ASSERT_TRUE(cdb);
    EXPECT_TRUE((*cdb)->contains("alpha"));
    EXPECT_FALSE((*cdb)->contains("delta"));

    std::filesystem::remove_all(dir);
}
