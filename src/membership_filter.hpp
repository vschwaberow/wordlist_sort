// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/membership_filter.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

enum class FilterEngine
{
    Hash,
    Fst,
    Pthash,
};

[[nodiscard]] std::expected<FilterEngine, std::string> parse_filter_engine(std::string_view text);
[[nodiscard]] const char *filter_engine_name(FilterEngine engine) noexcept;

class MembershipFilter
{
public:
    virtual ~MembershipFilter() = default;
    [[nodiscard]] virtual bool contains(std::string_view key) const = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
};

[[nodiscard]] std::expected<std::unique_ptr<MembershipFilter>, std::string>
build_membership_filter(const std::vector<std::string> &keys, FilterEngine engine);

[[nodiscard]] std::expected<std::vector<std::string>, std::string>
load_filter_keys(const std::filesystem::path &path);
