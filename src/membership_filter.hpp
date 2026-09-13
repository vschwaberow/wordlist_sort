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
    [[nodiscard]] virtual const char *backend_name() const noexcept = 0;
    /// FST-only; other backends return an error.
    [[nodiscard]] virtual std::expected<std::vector<std::string>, std::string>
    fuzzy_search(std::string_view query, int max_distance) const
    {
        (void)query;
        (void)max_distance;
        return std::unexpected("fuzzy search requires an FST index (WLTRIE1 via --lookup)");
    }
};

[[nodiscard]] std::expected<std::unique_ptr<MembershipFilter>, std::string>
build_membership_filter(const std::vector<std::string> &keys, FilterEngine engine,
                        const std::filesystem::path &tmp_dir = {});

/// Open B as membership index: WLTRIE1 → FST, WLPTH1 → PTHash, `.cdb` → CDB, else text + engine.
[[nodiscard]] std::expected<std::unique_ptr<MembershipFilter>, std::string>
open_membership_filter(const std::filesystem::path &path, FilterEngine engine,
                       const std::filesystem::path &tmp_dir = {});

[[nodiscard]] std::expected<std::vector<std::string>, std::string>
load_filter_keys(const std::filesystem::path &path);
