// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/membership_filter.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "membership_filter.hpp"
#include "gzip_stream.hpp"

#include "export_format.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
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
#include "essentials.hpp"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

namespace
{

[[nodiscard]] std::filesystem::path resolve_filter_tmp_dir(const std::filesystem::path &tmp_dir)
{
    return tmp_dir.empty() ? std::filesystem::temp_directory_path() : tmp_dir;
}

class HashMembershipFilter final : public MembershipFilter
{
public:
    explicit HashMembershipFilter(std::unordered_set<std::string> keys) : keys_(std::move(keys)) {}

    [[nodiscard]] bool contains(const std::string_view key) const override
    {
        return keys_.contains(std::string{key});
    }

    [[nodiscard]] std::size_t size() const noexcept override { return keys_.size(); }
    [[nodiscard]] const char *backend_name() const noexcept override { return "hash"; }

private:
    std::unordered_set<std::string> keys_;
};

class FstMembershipFilter final : public MembershipFilter
{
public:
    explicit FstMembershipFilter(std::vector<unsigned char> data, const std::size_t size)
        : data_(std::move(data)), size_(size)
    {
    }

    [[nodiscard]] bool contains(const std::string_view key) const override
    {
        const auto result = fst_contains_bytes(data_, key);
        return result && *result;
    }

    [[nodiscard]] std::size_t size() const noexcept override { return size_; }
    [[nodiscard]] const char *backend_name() const noexcept override { return "fst"; }

    [[nodiscard]] std::expected<std::vector<std::string>, std::string>
    fuzzy_search(const std::string_view query, const int max_distance) const override
    {
        return fst_fuzzy_search_bytes(data_, query, max_distance);
    }

private:
    std::vector<unsigned char> data_;
    std::size_t size_ = 0;
};

class CdbMembershipFilter final : public MembershipFilter
{
public:
    explicit CdbMembershipFilter(std::vector<unsigned char> data, const std::size_t size)
        : data_(std::move(data)), size_(size)
    {
    }

    [[nodiscard]] bool contains(const std::string_view key) const override
    {
        const auto result = cdb_contains_bytes(data_, key);
        return result && *result;
    }

    [[nodiscard]] std::size_t size() const noexcept override { return size_; }
    [[nodiscard]] const char *backend_name() const noexcept override { return "cdb"; }

private:
    std::vector<unsigned char> data_;
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
    [[nodiscard]] const char *backend_name() const noexcept override { return "pthash"; }

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
    std::string open_error;
    auto in = open_input_stream(path, &open_error);
    if (!in)
        return std::unexpected(std::format("Failed to open filter file: {}", open_error));

    std::vector<std::string> keys;
    std::string line;
    while (std::getline(*in, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (!line.empty())
            keys.push_back(std::move(line));
    }
    return keys;
}

[[nodiscard]] std::expected<std::unique_ptr<MembershipFilter>, std::string>
build_membership_filter(const std::vector<std::string> &keys, const FilterEngine engine,
                        const std::filesystem::path &tmp_dir)
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
        const auto tmp = resolve_filter_tmp_dir(tmp_dir) /
                         std::format("wordlist_sort_filter_{}.fst",
                                     std::hash<std::string>{}(unique.front() + std::to_string(unique.size())));
        if (const auto written = write_fst(unique, tmp); !written)
            return std::unexpected(written.error());

        std::ifstream in(tmp, std::ios::binary);
        if (!in)
        {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return std::unexpected("Failed to reopen temporary FST filter");
        }
        in.seekg(0, std::ios::end);
        const auto file_size = static_cast<std::size_t>(in.tellg());
        in.seekg(0);
        std::vector<unsigned char> data(file_size);
        if (file_size > 0 &&
            !in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(file_size)))
        {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return std::unexpected("Failed to read temporary FST filter");
        }
        {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
        }
        return std::make_unique<FstMembershipFilter>(std::move(data), unique.size());
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

[[nodiscard]] std::expected<std::vector<unsigned char>, std::string>
read_binary_file(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::unexpected(std::format("Failed to open filter file: {}", path.string()));
    in.seekg(0, std::ios::end);
    const auto file_size = static_cast<std::size_t>(in.tellg());
    in.seekg(0);
    std::vector<unsigned char> data(file_size);
    if (file_size > 0 &&
        !in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(file_size)))
        return std::unexpected(std::format("Failed to read filter file: {}", path.string()));
    return data;
}

[[nodiscard]] bool looks_like_wltrie1(const std::span<const unsigned char> data) noexcept
{
    static constexpr unsigned char kMagic[8] = {'W', 'L', 'T', 'R', 'I', 'E', '1', 0};
    return data.size() >= 8 && std::memcmp(data.data(), kMagic, 8) == 0;
}

#if defined(WORDLIST_SORT_PTHASH)
constexpr unsigned char kWlpthMagic[8] = {'W', 'L', 'P', 'T', 'H', '1', '\0', '\0'};

[[nodiscard]] bool looks_like_wlpth1(const std::span<const unsigned char> data) noexcept
{
    return data.size() >= 8 && std::memcmp(data.data(), kWlpthMagic, 8) == 0;
}

void write_u32_le_mf(std::ostream &out, const std::uint32_t value)
{
    const unsigned char bytes[4]{
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu),
        static_cast<unsigned char>((value >> 16) & 0xffu),
        static_cast<unsigned char>((value >> 24) & 0xffu),
    };
    out.write(reinterpret_cast<const char *>(bytes), 4);
}

void write_u64_le_mf(std::ostream &out, const std::uint64_t value)
{
    write_u32_le_mf(out, static_cast<std::uint32_t>(value & 0xffffffffu));
    write_u32_le_mf(out, static_cast<std::uint32_t>((value >> 32) & 0xffffffffu));
}

[[nodiscard]] std::uint32_t read_u32_le_mf(const unsigned char *p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

[[nodiscard]] std::uint64_t read_u64_le_mf(const unsigned char *p) noexcept
{
    return static_cast<std::uint64_t>(read_u32_le_mf(p)) |
           (static_cast<std::uint64_t>(read_u32_le_mf(p + 4)) << 32);
}

[[nodiscard]] std::expected<std::unique_ptr<MembershipFilter>, std::string>
open_wlpth1_file(const std::filesystem::path &path)
{
    auto bytes = read_binary_file(path);
    if (!bytes)
        return std::unexpected(bytes.error());
    if (!looks_like_wlpth1(*bytes) || bytes->size() < 20)
        return std::unexpected("Not a WLPTH1 PTHash file");

    const auto version = read_u32_le_mf(bytes->data() + 8);
    if (version != 1)
        return std::unexpected(std::format("Unsupported WLPTH1 version {}", version));

    const auto num_keys = read_u64_le_mf(bytes->data() + 12);
    std::size_t off = 20;
    std::vector<std::string> table;
    table.reserve(static_cast<std::size_t>(num_keys));
    for (std::uint64_t i = 0; i < num_keys; ++i)
    {
        if (off + 4 > bytes->size())
            return std::unexpected("WLPTH1 key table truncated");
        const auto len = read_u32_le_mf(bytes->data() + off);
        off += 4;
        if (off + static_cast<std::size_t>(len) > bytes->size())
            return std::unexpected("WLPTH1 key table truncated");
        table.emplace_back(reinterpret_cast<const char *>(bytes->data() + off),
                           static_cast<std::size_t>(len));
        off += static_cast<std::size_t>(len);
    }
    if (off + 8 > bytes->size())
        return std::unexpected("WLPTH1 missing PHF blob size");
    const auto blob_size = read_u64_le_mf(bytes->data() + off);
    off += 8;
    if (off + static_cast<std::size_t>(blob_size) > bytes->size())
        return std::unexpected("WLPTH1 PHF blob truncated");

    const auto phf_tmp = std::filesystem::temp_directory_path() /
                         std::format("wordlist_sort_phf_load_{}.bin",
                                     std::hash<std::string>{}(path.string()));
    {
        std::ofstream tmp(phf_tmp, std::ios::binary | std::ios::trunc);
        if (!tmp)
            return std::unexpected("Failed to create temporary PHF file");
        if (blob_size > 0)
            tmp.write(reinterpret_cast<const char *>(bytes->data() + off),
                      static_cast<std::streamsize>(blob_size));
        if (!tmp)
            return std::unexpected("Failed to write temporary PHF file");
    }

    PthashMembershipFilter::pthash_type fn;
    essentials::load(fn, phf_tmp.string().c_str());
    {
        std::error_code ec;
        std::filesystem::remove(phf_tmp, ec);
    }
    return std::make_unique<PthashMembershipFilter>(std::move(fn), std::move(table));
}
#endif

[[nodiscard]] std::expected<std::unique_ptr<MembershipFilter>, std::string>
open_membership_filter(const std::filesystem::path &path, const FilterEngine engine,
                       const std::filesystem::path &tmp_dir)
{
    std::ifstream peek(path, std::ios::binary);
    if (!peek)
        return std::unexpected(std::format("Failed to open filter file: {}", path.string()));

    unsigned char magic[8]{};
    peek.read(reinterpret_cast<char *>(magic), 8);
    const auto got = static_cast<std::size_t>(peek.gcount());
    peek.close();

    const bool is_fst = got == 8 && looks_like_wltrie1(std::span<const unsigned char>(magic, 8));
#if defined(WORDLIST_SORT_PTHASH)
    const bool is_pthash = got == 8 && looks_like_wlpth1(std::span<const unsigned char>(magic, 8));
#else
    const bool is_pthash = false;
#endif
    const auto ext = path.extension().string();
    const bool is_cdb = ext == ".cdb" || ext == ".CDB";

    if (is_pthash)
    {
#if defined(WORDLIST_SORT_PTHASH)
        return open_wlpth1_file(path);
#else
        return std::unexpected("WLPTH1 file requires -DWORDLIST_SORT_PTHASH=ON");
#endif
    }

    if (is_fst || is_cdb)
    {
        auto bytes = read_binary_file(path);
        if (!bytes)
            return std::unexpected(bytes.error());

        if (is_fst)
        {
            const auto count = fst_key_count(*bytes);
            if (!count)
                return std::unexpected(count.error());
            if (const auto probe = fst_contains_bytes(*bytes, ""); !probe)
                return std::unexpected(probe.error());
            return std::make_unique<FstMembershipFilter>(std::move(*bytes),
                                                         static_cast<std::size_t>(*count));
        }

        if (const auto probe = cdb_contains_bytes(*bytes, ""); !probe)
            return std::unexpected(probe.error());
        const auto count = cdb_key_count(*bytes);
        if (!count)
            return std::unexpected(count.error());
        return std::make_unique<CdbMembershipFilter>(std::move(*bytes), *count);
    }

    const auto keys = load_filter_keys(path);
    if (!keys)
        return std::unexpected(keys.error());
    return build_membership_filter(*keys, engine, tmp_dir);
}

[[nodiscard]] std::expected<void, std::string> write_pthash(const std::vector<std::string> &words,
                                                            const std::filesystem::path &path,
                                                            const std::filesystem::path &tmp_dir)
{
#if !defined(WORDLIST_SORT_PTHASH)
    (void)words;
    (void)path;
    (void)tmp_dir;
    return std::unexpected("pthash format requires -DWORDLIST_SORT_PTHASH=ON at configure time");
#else
    std::unordered_set<std::string_view> seen;
    seen.reserve(words.size());
    std::vector<std::string> unique;
    unique.reserve(words.size());
    for (const auto &word : words)
    {
        if (seen.insert(word).second)
            unique.push_back(word);
    }
    if (unique.empty())
        return std::unexpected("No usable keys for PTHash output");

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

    const auto phf_tmp = resolve_filter_tmp_dir(tmp_dir) /
                         std::format("wordlist_sort_phf_{}.bin",
                                     std::hash<std::string>{}(unique.front() + std::to_string(unique.size())));
    essentials::save(fn, phf_tmp.string().c_str());
    auto blob = read_binary_file(phf_tmp);
    {
        std::error_code ec;
        std::filesystem::remove(phf_tmp, ec);
    }
    if (!blob)
        return std::unexpected(blob.error());

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return std::unexpected(std::format("Failed to open PTHash output: {}", path.string()));

    out.write(reinterpret_cast<const char *>(kWlpthMagic), 8);
    write_u32_le_mf(out, 1);
    write_u64_le_mf(out, static_cast<std::uint64_t>(table.size()));
    for (const auto &key : table)
    {
        if (key.size() > 0xffffffffu)
            return std::unexpected("PTHash key exceeds 4 GiB length limit");
        write_u32_le_mf(out, static_cast<std::uint32_t>(key.size()));
        out.write(key.data(), static_cast<std::streamsize>(key.size()));
    }
    write_u64_le_mf(out, static_cast<std::uint64_t>(blob->size()));
    if (!blob->empty())
        out.write(reinterpret_cast<const char *>(blob->data()), static_cast<std::streamsize>(blob->size()));
    if (!out)
        return std::unexpected(std::format("Failed to write PTHash output: {}", path.string()));
    return {};
#endif
}
