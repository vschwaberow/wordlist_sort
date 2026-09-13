// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/membership_filter.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "membership_filter.hpp"

#include "export_format.hpp"

#include <fstream>
#include <system_error>
#include <format>
#include <unordered_set>

#if defined(WORDLIST_SORT_PTHASH)
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif
#include "include/pthash.hpp"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

namespace
{

class HashMembershipFilter final : public MembershipFilter
{
public:
    explicit HashMembershipFilter(std::unordered_set<std::string> keys) : keys_(std::move(keys)) {}

    [[nodiscard]] bool contains(const std::string_view key) const override
    {
        return keys_.contains(std::string{key});
    }

    [[nodiscard]] std::size_t size() const noexcept override { return keys_.size(); }

private:
    std::unordered_set<std::string> keys_;
};

class FstMembershipFilter final : public MembershipFilter
{
public:
    explicit FstMembershipFilter(std::filesystem::path path, const std::size_t size)
        : path_(std::move(path)), size_(size)
    {
    }

    ~FstMembershipFilter() override
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    FstMembershipFilter(const FstMembershipFilter &) = delete;
    FstMembershipFilter &operator=(const FstMembershipFilter &) = delete;

    [[nodiscard]] bool contains(const std::string_view key) const override
    {
        const auto result = fst_contains(path_, key);
        return result && *result;
    }

    [[nodiscard]] std::size_t size() const noexcept override { return size_; }

private:
    std::filesystem::path path_;
    std::size_t size_ = 0;
};

#if defined(WORDLIST_SORT_PTHASH)
class PthashMembershipFilter final : public MembershipFilter
{
public:
    using pthash_type =
        pthash::single_phf<pthash::murmurhash2_64, pthash::dictionary_dictionary, true>;

    PthashMembershipFilter(pthash_type fn, std::vector<std::string> table)
        : fn_(std::move(fn)), table_(std::move(table))
    {
    }

    [[nodiscard]] bool contains(const std::string_view key) const override
    {
        const std::string owned{key};
        const auto index = fn_(owned);
        return index < table_.size() && table_[index] == owned;
    }

    [[nodiscard]] std::size_t size() const noexcept override { return table_.size(); }

private:
    pthash_type fn_;
    std::vector<std::string> table_;
};
#endif

[[nodiscard]] std::vector<std::string> unique_keys(const std::vector<std::string> &keys)
{
    std::unordered_set<std::string_view> seen;
    seen.reserve(keys.size());
    std::vector<std::string> out;
    out.reserve(keys.size());
    for (const auto &key : keys)
    {
        if (seen.insert(key).second)
            out.push_back(key);
    }
    return out;
}

}

[[nodiscard]] std::expected<FilterEngine, std::string> parse_filter_engine(const std::string_view text)
{
    if (text == "hash")
        return FilterEngine::Hash;
    if (text == "fst" || text == "trie")
        return FilterEngine::Fst;
    if (text == "pthash" || text == "mphf")
        return FilterEngine::Pthash;
    return std::unexpected(
        std::format("Unknown --filter-engine '{}' (expected hash|fst|pthash)", text));
}

[[nodiscard]] const char *filter_engine_name(const FilterEngine engine) noexcept
{
    switch (engine)
    {
    case FilterEngine::Hash:
        return "hash";
    case FilterEngine::Fst:
        return "fst";
    case FilterEngine::Pthash:
        return "pthash";
    }
    return "hash";
}

[[nodiscard]] std::expected<std::vector<std::string>, std::string>
load_filter_keys(const std::filesystem::path &path)
{
    std::ifstream in(path);
    if (!in)
        return std::unexpected(std::format("Failed to open filter file: {}", path.string()));

    std::vector<std::string> keys;
    std::string line;
    while (std::getline(in, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (!line.empty())
            keys.push_back(std::move(line));
    }
    return keys;
}

[[nodiscard]] std::expected<std::unique_ptr<MembershipFilter>, std::string>
build_membership_filter(const std::vector<std::string> &keys, const FilterEngine engine)
{
    auto unique = unique_keys(keys);
    if (unique.empty())
        return std::unexpected("Filter file contains no usable keys");

    switch (engine)
    {
    case FilterEngine::Hash:
    {
        std::unordered_set<std::string> set(unique.begin(), unique.end());
        return std::make_unique<HashMembershipFilter>(std::move(set));
    }
    case FilterEngine::Fst:
    {
        const auto tmp = std::filesystem::temp_directory_path() /
                         std::format("wordlist_sort_filter_{}.fst",
                                     std::hash<std::string>{}(unique.front() + std::to_string(unique.size())));
        if (const auto written = write_fst(unique, tmp); !written)
            return std::unexpected(written.error());
        return std::make_unique<FstMembershipFilter>(tmp, unique.size());
    }
    case FilterEngine::Pthash:
    {
#if defined(WORDLIST_SORT_PTHASH)
        pthash::build_configuration config;
        config.c = 6.0;
        config.alpha = 0.94;
        config.minimal_output = true;
        config.verbose_output = false;
        config.num_threads = 1;

        PthashMembershipFilter::pthash_type fn;
        fn.build_in_internal_memory(unique.begin(), unique.size(), config);

        std::vector<std::string> table(unique.size());
        for (const auto &key : unique)
            table[fn(key)] = key;

        return std::make_unique<PthashMembershipFilter>(std::move(fn), std::move(table));
#else
        return std::unexpected(
            "pthash filter engine requires -DWORDLIST_SORT_PTHASH=ON at configure time");
#endif
    }
    }
    return std::unexpected("internal: invalid filter engine");
}
