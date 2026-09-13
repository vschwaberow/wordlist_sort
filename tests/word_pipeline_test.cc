// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: tests/word_pipeline_test.cc

#include "word_pipeline.hpp"
#include "test_helpers.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

TEST(WordPipeline, Lower)
{
    Options options;
    options.lower = true;
    ASSERT_TRUE(process_word("HeLLo", options).has_value());
    EXPECT_EQ(*process_word("HeLLo", options), "hello");
}

TEST(WordPipeline, DigitTrim)
{
    Options options;
    options.digit_trim = true;
    ASSERT_TRUE(process_word("123abc456", options).has_value());
    EXPECT_EQ(*process_word("123abc456", options), "abc");
}

TEST(WordPipeline, SpecialTrim)
{
    Options options;
    options.special_trim = true;
    ASSERT_TRUE(process_word("!!foo!!", options).has_value());
    EXPECT_EQ(*process_word("!!foo!!", options), "foo");
}

TEST(WordPipeline, DupRemove)
{
    Options options;
    options.dup_remove = true;
    ASSERT_TRUE(process_word("aabbb", options).has_value());
    EXPECT_EQ(*process_word("aabbb", options), "ab");
}

TEST(WordPipeline, NoNumbersRejectsDigitsOnly)
{
    Options options;
    options.no_numbers = true;
    EXPECT_FALSE(process_word("12345", options).has_value());
}

TEST(WordPipeline, HashRemoveRejectsLongHex)
{
    Options options;
    options.hash_remove = true;
    const std::string hash(32, 'a');
    EXPECT_FALSE(process_word(hash, options).has_value());
}

TEST(WordPipeline, MinLenAndMaxLen)
{
    const auto work = fs::temp_directory_path() / "word_pipeline_minmax";
    fs::create_directories(work);
    const fs::path input = work / "in.txt";
    test_helpers::write_text_file(input, "ab\nabcd\nabcdef\n");

    Options options;
    options.minlen = 3;
    options.maxlen = 5;

    std::vector<std::string> words;
    std::atomic<std::size_t> counter{0};
    ASSERT_TRUE(process_file(input, words, counter, options));
    ASSERT_EQ(words.size(), 1U);
    EXPECT_EQ(words.front(), "abcd");
}

TEST(WordPipeline, DupSenseRejectsSkewedWord)
{
    Options options;
    options.dup_sense = 50;
    EXPECT_FALSE(process_word("aaaa", options).has_value());
    ASSERT_TRUE(process_word("aabb", options).has_value());
    EXPECT_EQ(*process_word("aabb", options), "aabb");
}

TEST(WordPipeline, EmailSort)
{
    Options options;
    options.email_sort = true;
    ASSERT_TRUE(process_word("user@domain.com", options).has_value());
    EXPECT_EQ(*process_word("user@domain.com", options), "user domain.com");
}

TEST(WordPipeline, StripHtmlTags)
{
    EXPECT_EQ(strip_html_tags("<b>x</b>"), "x");
}

TEST(WordPipeline, DewebifyAndNoUtf8InFile)
{
    const auto work = fs::temp_directory_path() / "word_pipeline_dewebify";
    fs::create_directories(work);
    const fs::path input = work / "in.html";

    {
        std::ofstream out(input);
        out << "<b>caf\xe9</b>\n";
    }

    Options options;
    options.dewebify = true;
    options.noutf8 = true;

    std::vector<std::string> words;
    std::atomic<std::size_t> counter{0};
    ASSERT_TRUE(process_file(input, words, counter, options));
    ASSERT_EQ(words.size(), 1U);
    EXPECT_EQ(words.front(), "caf");
}
