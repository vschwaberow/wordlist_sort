// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/rules_engine.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Hashcat-style rule engine (opcode subset; not full Hashcat parity).
class RulesEngine
{
public:
    /// Parse a single rule line (no comments). Empty / `:` are valid.
    [[nodiscard]] static std::expected<void, std::string> validate_rule(std::string_view rule);

    [[nodiscard]] static std::expected<RulesEngine, std::string>
    load_file(const std::filesystem::path &path);

    [[nodiscard]] static RulesEngine basic_ruleset();

    void append_rules(const RulesEngine &other);

    /// Apply one rule to word. Returns nullopt if a reject opcode drops the candidate.
    [[nodiscard]] static std::optional<std::string> apply_rule(std::string_view word,
                                                               std::string_view rule);

    /// Expand word under all rules. Always includes the original as first result.
    /// Caps at max_variants (0 = unlimited). Skips duplicate strings in order.
    [[nodiscard]] std::vector<std::string> expand(std::string_view word, int max_variants) const;

    [[nodiscard]] std::size_t rule_count() const noexcept { return rules_.size(); }

private:
    std::vector<std::string> rules_;
};
