// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: tests/export_format_test.cc

#include "export_format.hpp"
#include "membership_filter.hpp"
#include "test_helpers.hpp"

#include <gtest/gtest.h>
#include <vector>

TEST(ExportFormatParse, AcceptsAliases)
{
    EXPECT_EQ(*parse_export_format("text"), ExportFormat::Text);
    EXPECT_EQ(*parse_export_format("txt"), ExportFormat::Text);
    EXPECT_EQ(*parse_export_format("cdb"), ExportFormat::Cdb);
    EXPECT_EQ(*parse_export_format("fst"), ExportFormat::Fst);
    EXPECT_EQ(*parse_export_format("trie"), ExportFormat::Fst);
    EXPECT_EQ(*parse_export_format("pthash"), ExportFormat::Pthash);
    EXPECT_EQ(*parse_export_format("mphf"), ExportFormat::Pthash);
    EXPECT_FALSE(parse_export_format("json"));
}

TEST(CdbExport, RoundTripContains)
{
    const auto dir = test_helpers::make_temp_dir();
    std::filesystem::create_directories(dir);
    const auto path = dir / "words.cdb";

    const std::vector<std::string> words{"banana", "apple", "cherry", "apple"};
    ASSERT_TRUE(write_cdb(words, path));

    EXPECT_TRUE(*cdb_contains(path, "apple"));
    EXPECT_TRUE(*cdb_contains(path, "banana"));
    EXPECT_TRUE(*cdb_contains(path, "cherry"));
    EXPECT_FALSE(*cdb_contains(path, "date"));
}

TEST(FstExport, RoundTripContains)
{
    const auto dir = test_helpers::make_temp_dir();
    std::filesystem::create_directories(dir);
    const auto path = dir / "words.fst";

    const std::vector<std::string> words{"bank", "banner", "tank", "ban"};
    ASSERT_TRUE(write_fst(words, path));

    EXPECT_TRUE(*fst_contains(path, "bank"));
    EXPECT_TRUE(*fst_contains(path, "banner"));
    EXPECT_TRUE(*fst_contains(path, "tank"));
    EXPECT_TRUE(*fst_contains(path, "ban"));
    EXPECT_FALSE(*fst_contains(path, "bananas"));
    EXPECT_FALSE(*fst_contains(path, "tan"));
}

#if defined(WORDLIST_SORT_PTHASH)
TEST(PthashExport, RoundTripOpenFilter)
{
    const auto dir = test_helpers::make_temp_dir();
    std::filesystem::create_directories(dir);
    const auto path = dir / "words.pthash";
    const std::vector<std::string> words{"zeta", "alpha", "beta", "alpha"};
    ASSERT_TRUE(write_pthash(words, path));

    auto filter = open_membership_filter(path, FilterEngine::Hash);
    ASSERT_TRUE(filter);
    EXPECT_EQ((*filter)->size(), 3u);
    EXPECT_TRUE((*filter)->contains("alpha"));
    EXPECT_TRUE((*filter)->contains("zeta"));
    EXPECT_FALSE((*filter)->contains("gamma"));
}
#endif
