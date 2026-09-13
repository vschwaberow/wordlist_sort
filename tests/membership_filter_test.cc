// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: tests/membership_filter_test.cc

#include "membership_filter.hpp"
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
