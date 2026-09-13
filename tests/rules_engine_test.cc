// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: tests/rules_engine_test.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "rules_engine.hpp"

#include <gtest/gtest.h>

TEST(RulesEngine, CapitalizeAppend)
{
    const auto out = RulesEngine::apply_rule("password", "c$1");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, "Password1");
}

TEST(RulesEngine, LeetAndReverse)
{
    EXPECT_EQ(*RulesEngine::apply_rule("pass", "sa@"), "p@ss");
    EXPECT_EQ(*RulesEngine::apply_rule("ab", "r"), "ba");
}

TEST(RulesEngine, RejectLength)
{
    EXPECT_FALSE(RulesEngine::apply_rule("ab", "<2").has_value());
    EXPECT_TRUE(RulesEngine::apply_rule("ab", "<3").has_value());
    EXPECT_FALSE(RulesEngine::apply_rule("ab", "_3").has_value());
    EXPECT_TRUE(RulesEngine::apply_rule("ab", "_2").has_value());
}

TEST(RulesEngine, ExpandIncludesOriginalAndCaps)
{
    RulesEngine engine;
    // Manually via basic
    auto basic = RulesEngine::basic_ruleset();
    const auto variants = basic.expand("Password", 3);
    ASSERT_GE(variants.size(), 1u);
    EXPECT_EQ(variants[0], "Password");
    EXPECT_LE(variants.size(), 3u);
}

TEST(RulesEngine, BasicRulesetSmoke)
{
    auto basic = RulesEngine::basic_ruleset();
    const auto variants = basic.expand("password", 64);
    bool saw_cap = false;
    bool saw_bang = false;
    for (const auto &v : variants)
    {
        if (v == "Password")
            saw_cap = true;
        if (v == "password!")
            saw_bang = true;
    }
    EXPECT_TRUE(saw_cap);
    EXPECT_TRUE(saw_bang);
}
