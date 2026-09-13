// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/fst_export.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "export_format.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace
{

constexpr char kMagic[8] = {'W', 'L', 'T', 'R', 'I', 'E', '1', '\0'};

struct TrieNode
{
    bool terminal = false;
    // edges sorted by character for deterministic encoding / binary search
    std::vector<std::pair<unsigned char, std::unique_ptr<TrieNode>>> edges;
};

[[nodiscard]] TrieNode *ensure_edge(TrieNode &node, const unsigned char ch)
{
    const auto it = std::lower_bound(
        node.edges.begin(), node.edges.end(), ch,
        [](const auto &edge, const unsigned char value) { return edge.first < value; });
    if (it != node.edges.end() && it->first == ch)
        return it->second.get();
    auto inserted = node.edges.emplace(it, ch, std::make_unique<TrieNode>());
    return inserted->second.get();
}

void insert_word(TrieNode &root, const std::string_view word)
{
    TrieNode *node = &root;
    for (const unsigned char ch : word)
        node = ensure_edge(*node, ch);
    node->terminal = true;
}

struct FlatNode
{
    bool terminal = false;
    std::uint32_t edge_start = 0;
    std::uint32_t edge_count = 0;
};

struct FlatEdge
{
    unsigned char ch = 0;
    std::uint32_t target = 0;
};

void flatten(const TrieNode &node,
             std::vector<FlatNode> &nodes,
             std::vector<FlatEdge> &edges,
             std::uint32_t &next_index)
{
    const std::uint32_t index = next_index++;
    if (nodes.size() <= index)
        nodes.resize(index + 1);

    nodes[index].terminal = node.terminal;
    nodes[index].edge_start = static_cast<std::uint32_t>(edges.size());
    nodes[index].edge_count = static_cast<std::uint32_t>(node.edges.size());

    // Reserve edge slots so child indices are stable as we recurse depth-first
    const std::size_t edge_base = edges.size();
    edges.resize(edge_base + node.edges.size());

    for (std::size_t i = 0; i < node.edges.size(); ++i)
    {
        const std::uint32_t child = next_index;
        flatten(*node.edges[i].second, nodes, edges, next_index);
        edges[edge_base + i] = FlatEdge{.ch = node.edges[i].first, .target = child};
    }
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

}

[[nodiscard]] std::expected<void, std::string> write_fst(const std::vector<std::string> &words,
                                                         const std::filesystem::path &path)
{
    TrieNode root;
    std::unordered_set<std::string_view> seen;
    seen.reserve(words.size());
    std::uint32_t unique = 0;

    for (const auto &word : words)
    {
        if (!seen.insert(word).second)
            continue;
        insert_word(root, word);
        ++unique;
    }

    std::vector<FlatNode> nodes;
    std::vector<FlatEdge> edges;
    std::uint32_t next_index = 0;
    flatten(root, nodes, edges, next_index);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return std::unexpected(std::format("Failed to open FST/trie output: {}", path.string()));

    out.write(kMagic, 8);
    write_u32_le(out, unique);
    write_u32_le(out, static_cast<std::uint32_t>(nodes.size()));
    write_u32_le(out, static_cast<std::uint32_t>(edges.size()));

    for (const FlatNode &node : nodes)
    {
        const std::uint32_t packed = (node.terminal ? 1u : 0u) | (node.edge_count << 1);
        write_u32_le(out, packed);
        write_u32_le(out, node.edge_start);
    }

    for (const FlatEdge &edge : edges)
    {
        out.put(static_cast<char>(edge.ch));
        write_u32_le(out, edge.target);
    }

    if (!out)
        return std::unexpected(std::format("Failed to write FST/trie output: {}", path.string()));
    return {};
}

[[nodiscard]] std::expected<std::uint32_t, std::string> fst_key_count(const std::span<const unsigned char> data)
{
    if (data.size() < 12)
        return std::unexpected("FST/trie file too small");
    if (std::memcmp(data.data(), kMagic, 8) != 0)
        return std::unexpected("Not a WLTRIE1 FST/trie file");
    return read_u32_le(data.data() + 8);
}

[[nodiscard]] std::expected<bool, std::string> fst_contains_bytes(const std::span<const unsigned char> data,
                                                                  const std::string_view key)
{
    if (data.size() < 20)
        return std::unexpected("FST/trie file too small");
    if (std::memcmp(data.data(), kMagic, 8) != 0)
        return std::unexpected("Not a WLTRIE1 FST/trie file");

    const std::uint32_t node_count = read_u32_le(data.data() + 12);
    const std::uint32_t edge_count = read_u32_le(data.data() + 16);
    const std::size_t nodes_off = 20;
    const std::size_t edges_off = nodes_off + static_cast<std::size_t>(node_count) * 8;
    if (edges_off + static_cast<std::size_t>(edge_count) * 5 > data.size())
        return std::unexpected("FST/trie truncated");

    std::uint32_t node = 0;
    for (const unsigned char ch : key)
    {
        if (node >= node_count)
            return false;
        const unsigned char *np = data.data() + nodes_off + static_cast<std::size_t>(node) * 8;
        const std::uint32_t packed = read_u32_le(np);
        const std::uint32_t edge_count_n = packed >> 1;
        const std::uint32_t edge_start = read_u32_le(np + 4);
        bool found = false;
        for (std::uint32_t i = 0; i < edge_count_n; ++i)
        {
            const unsigned char *ep = data.data() + edges_off + static_cast<std::size_t>(edge_start + i) * 5;
            if (ep[0] == ch)
            {
                node = read_u32_le(ep + 1);
                found = true;
                break;
            }
            if (ep[0] > ch)
                break;
        }
        if (!found)
            return false;
    }

    if (node >= node_count)
        return false;
    const unsigned char *np = data.data() + nodes_off + static_cast<std::size_t>(node) * 8;
    const std::uint32_t packed = read_u32_le(np);
    return (packed & 1u) != 0;
}

[[nodiscard]] std::expected<bool, std::string> fst_contains(const std::filesystem::path &path,
                                                            const std::string_view key)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::unexpected(std::format("Failed to open FST/trie: {}", path.string()));

    in.seekg(0, std::ios::end);
    const auto file_size = static_cast<std::size_t>(in.tellg());
    in.seekg(0);
    std::vector<unsigned char> data(file_size);
    if (file_size > 0 &&
        !in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(file_size)))
        return std::unexpected("Failed to read FST/trie file");
    return fst_contains_bytes(data, key);
}
