// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/cdb_export.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "export_format.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <span>
#include <unordered_set>
#include <vector>

namespace
{

constexpr std::uint32_t kCdbHeaderSize = 2048;
constexpr std::uint64_t kCdbMaxSize = 0xffff'ffffULL;

[[nodiscard]] std::uint32_t cdb_hash(const std::string_view key) noexcept
{
    std::uint32_t h = 5381;
    for (const unsigned char c : key)
        h = ((h << 5) + h) ^ c;
    return h;
}

void write_u32_le(std::ostream &out, const std::uint32_t value)
{
    const unsigned char bytes[4]{
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu),
        static_cast<unsigned char>((value >> 16) & 0xffu),
        static_cast<unsigned char>((value >> 24) & 0xffu),
    };
    out.write(reinterpret_cast<const char *>(bytes), 4);
}

[[nodiscard]] std::uint32_t read_u32_le(const unsigned char *p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

struct RecordRef
{
    std::uint32_t hash = 0;
    std::uint32_t offset = 0;
};

}

[[nodiscard]] std::expected<void, std::string> write_cdb(const std::vector<std::string> &words,
                                                         const std::filesystem::path &path)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return std::unexpected(std::format("Failed to open CDB output: {}", path.string()));

    std::vector<char> header(kCdbHeaderSize, '\0');
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    if (!out)
        return std::unexpected(std::format("Failed to write CDB header: {}", path.string()));

    std::unordered_set<std::string_view> seen;
    seen.reserve(words.size());

    std::vector<RecordRef> refs;
    refs.reserve(words.size());

    std::uint64_t pos = kCdbHeaderSize;
    for (const auto &word : words)
    {
        if (!seen.insert(word).second)
            continue;

        if (word.size() > 0xffff'ffffULL)
            return std::unexpected("CDB key longer than 4 GiB is unsupported");

        const std::uint32_t klen = static_cast<std::uint32_t>(word.size());
        const std::uint32_t vlen = 0;
        const std::uint64_t record_end = pos + 8ULL + klen;
        if (record_end > kCdbMaxSize)
            return std::unexpected("CDB output would exceed the classic 4 GiB limit");

        const std::uint32_t hash = cdb_hash(word);
        refs.push_back(RecordRef{.hash = hash, .offset = static_cast<std::uint32_t>(pos)});

        write_u32_le(out, klen);
        write_u32_le(out, vlen);
        if (klen > 0)
            out.write(word.data(), static_cast<std::streamsize>(klen));
        if (!out)
            return std::unexpected(std::format("Failed to write CDB record: {}", path.string()));

        pos = record_end;
    }

    std::vector<std::vector<RecordRef>> buckets(256);
    for (const RecordRef &ref : refs)
        buckets[ref.hash % 256].push_back(ref);

    struct TableInfo
    {
        std::uint32_t offset = 0;
        std::uint32_t nslots = 0;
    };
    std::array<TableInfo, 256> tables{};

    for (std::size_t b = 0; b < 256; ++b)
    {
        const auto &bucket = buckets[b];
        if (bucket.empty())
            continue;

        const std::uint32_t nslots = static_cast<std::uint32_t>(bucket.size() * 2);
        const std::uint64_t table_bytes = static_cast<std::uint64_t>(nslots) * 8ULL;
        if (pos + table_bytes > kCdbMaxSize)
            return std::unexpected("CDB hash tables would exceed the classic 4 GiB limit");

        tables[b].offset = static_cast<std::uint32_t>(pos);
        tables[b].nslots = nslots;

        std::vector<std::uint32_t> slot_hash(nslots, 0);
        std::vector<std::uint32_t> slot_off(nslots, 0);

        for (const RecordRef &ref : bucket)
        {
            std::uint32_t slot = (ref.hash / 256) % nslots;
            while (slot_off[slot] != 0)
                slot = (slot + 1) % nslots;
            slot_hash[slot] = ref.hash;
            slot_off[slot] = ref.offset;
        }

        for (std::uint32_t i = 0; i < nslots; ++i)
        {
            write_u32_le(out, slot_hash[i]);
            write_u32_le(out, slot_off[i]);
        }
        if (!out)
            return std::unexpected(std::format("Failed to write CDB hash table: {}", path.string()));

        pos += table_bytes;
    }

    out.seekp(0);
    for (const TableInfo &table : tables)
    {
        write_u32_le(out, table.offset);
        write_u32_le(out, table.nslots);
    }
    if (!out)
        return std::unexpected(std::format("Failed to finalize CDB header: {}", path.string()));

    return {};
}


[[nodiscard]] std::expected<std::size_t, std::string> cdb_key_count(const std::span<const unsigned char> data)
{
    const auto file_size = static_cast<std::uint64_t>(data.size());
    if (file_size < kCdbHeaderSize)
        return std::unexpected("CDB file too small");

    std::uint64_t data_end = file_size;
    for (std::uint32_t i = 0; i < 256; ++i)
    {
        const std::uint32_t table_offset = read_u32_le(data.data() + i * 8);
        const std::uint32_t nslots = read_u32_le(data.data() + i * 8 + 4);
        if (nslots == 0)
            continue;
        if (static_cast<std::uint64_t>(table_offset) < data_end)
            data_end = table_offset;
    }
    if (data_end < kCdbHeaderSize)
        return std::unexpected("CDB tables overlap header");

    std::size_t count = 0;
    std::uint64_t pos = kCdbHeaderSize;
    while (pos + 8 <= data_end)
    {
        const std::uint32_t klen = read_u32_le(data.data() + pos);
        const std::uint32_t vlen = read_u32_le(data.data() + pos + 4);
        const std::uint64_t next = pos + 8ULL + klen + vlen;
        if (next > data_end)
            return std::unexpected("CDB data section truncated");
        pos = next;
        ++count;
    }
    if (pos != data_end)
        return std::unexpected("CDB data section misaligned");
    return count;
}

[[nodiscard]] std::expected<bool, std::string> cdb_contains_bytes(const std::span<const unsigned char> data,
                                                                  const std::string_view key)
{
    const auto file_size = static_cast<std::uint64_t>(data.size());
    if (file_size < kCdbHeaderSize)
        return std::unexpected("CDB file too small");

    const std::uint32_t hash = cdb_hash(key);
    const std::uint32_t table_index = hash % 256;
    const std::uint32_t table_offset = read_u32_le(data.data() + table_index * 8);
    const std::uint32_t nslots = read_u32_le(data.data() + table_index * 8 + 4);
    if (nslots == 0)
        return false;
    if (static_cast<std::uint64_t>(table_offset) + static_cast<std::uint64_t>(nslots) * 8ULL > file_size)
        return std::unexpected("CDB table out of range");

    std::uint32_t slot = (hash / 256) % nslots;
    for (std::uint32_t i = 0; i < nslots; ++i)
    {
        const unsigned char *slot_ptr = data.data() + table_offset + slot * 8;
        const std::uint32_t slot_h = read_u32_le(slot_ptr);
        const std::uint32_t slot_off = read_u32_le(slot_ptr + 4);
        if (slot_off == 0)
            return false;
        if (slot_h == hash)
        {
            if (static_cast<std::uint64_t>(slot_off) + 8ULL > file_size)
                return std::unexpected("CDB record out of range");
            const std::uint32_t klen = read_u32_le(data.data() + slot_off);
            const std::uint32_t vlen = read_u32_le(data.data() + slot_off + 4);
            if (static_cast<std::uint64_t>(slot_off) + 8ULL + klen + vlen > file_size)
                return std::unexpected("CDB record truncated");
            const std::string_view stored(reinterpret_cast<const char *>(data.data() + slot_off + 8), klen);
            if (stored == key)
                return true;
        }
        slot = (slot + 1) % nslots;
    }
    return false;
}

[[nodiscard]] std::expected<bool, std::string> cdb_contains(const std::filesystem::path &path,
                                                            const std::string_view key)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::unexpected(std::format("Failed to open CDB: {}", path.string()));

    in.seekg(0, std::ios::end);
    const auto file_size = static_cast<std::size_t>(in.tellg());
    in.seekg(0);

    std::vector<unsigned char> data(file_size);
    if (file_size > 0 &&
        !in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(file_size)))
        return std::unexpected("Failed to read CDB file");
    return cdb_contains_bytes(data, key);
}
