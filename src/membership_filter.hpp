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

struct BloomOptions
{
    /// Force Bloom gate on (including prebuilt .cdb / WLTRIE1 / WLPTH1).
    bool force_on = false;
    /// Force Bloom gate off (overrides auto-on for text→engine builds).
    bool force_off = false;
    /// Bits per key (clamped 4–24); default ~10 ≈ 1% false-positive rate.
    int bits_per_key = 10;
};

[[nodiscard]] std::expected<std::unique_ptr<MembershipFilter>, std::string>
build_membership_filter(const std::vector<std::string> &keys, FilterEngine engine,
                        const std::filesystem::path &tmp_dir = {});

/// Wrap an exact membership filter with a Bloom early-drop gate filled from @p keys.
[[nodiscard]] std::unique_ptr<MembershipFilter>
wrap_membership_with_bloom(std::unique_ptr<MembershipFilter> exact,
                           const std::vector<std::string> &keys, int bits_per_key);

/// Open B as membership index: WLTRIE1 → FST, WLPTH1 → PTHash, `.cdb` → CDB, else text + engine.
/// Bloom auto-on for text→engine builds; off for prebuilt indexes unless force_on.
[[nodiscard]] std::expected<std::unique_ptr<MembershipFilter>, std::string>
open_membership_filter(const std::filesystem::path &path, FilterEngine engine,
                       const std::filesystem::path &tmp_dir = {},
                       BloomOptions bloom = {});

[[nodiscard]] std::expected<std::vector<std::string>, std::string>
load_filter_keys(const std::filesystem::path &path);
