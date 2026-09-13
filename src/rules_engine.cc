// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/rules_engine.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "rules_engine.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <format>
#include <unordered_set>

namespace
{

[[nodiscard]] int pos_value(const char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'Z')
        return 10 + (c - 'A');
    return -1;
}

[[nodiscard]] char to_lower(const char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] char to_upper(const char c) noexcept
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] char toggle(const char c) noexcept
{
    if (c >= 'a' && c <= 'z')
        return to_upper(c);
    if (c >= 'A' && c <= 'Z')
        return to_lower(c);
    return c;
}

} // namespace

[[nodiscard]] std::expected<void, std::string> RulesEngine::validate_rule(const std::string_view rule)
{
    std::size_t i = 0;
    while (i < rule.size())
    {
        const char op = rule[i++];
        switch (op)
        {
        case ':':
        case 'l':
        case 'u':
        case 'c':
        case 'C':
        case 't':
        case 'r':
        case 'd':
        case 'f':
        case '{':
        case '}':
        case '[':
        case ']':
            break;
        case 'D':
        case '<':
        case '>':
        case '_':
            if (i >= rule.size() || pos_value(rule[i]) < 0)
                return std::unexpected(std::format("Rule '{}': opcode '{}' needs a position/length digit", rule, op));
            ++i;
            break;
        case '$':
        case '^':
        case '@':
            if (i >= rule.size())
                return std::unexpected(std::format("Rule '{}': opcode '{}' needs a character", rule, op));
            ++i;
            break;
        case 's':
            if (i + 1 >= rule.size())
                return std::unexpected(std::format("Rule '{}': opcode 's' needs two characters", rule));
            i += 2;
            break;
        case 'o':
        case 'i':
            if (i + 1 >= rule.size() || pos_value(rule[i]) < 0)
                return std::unexpected(
                    std::format("Rule '{}': opcode '{}' needs position and character", rule, op));
            i += 2;
            break;
        default:
            return std::unexpected(std::format("Rule '{}': unsupported opcode '{}'", rule, op));
        }
    }
    return {};
}

[[nodiscard]] std::optional<std::string> RulesEngine::apply_rule(const std::string_view word,
                                                                 const std::string_view rule)
{
    std::string out{word};
    std::size_t i = 0;
    while (i < rule.size())
    {
        const char op = rule[i++];
        switch (op)
        {
        case ':':
            break;
        case 'l':
            for (char &c : out)
                c = to_lower(c);
            break;
        case 'u':
            for (char &c : out)
                c = to_upper(c);
            break;
        case 'c':
            if (!out.empty())
            {
                out[0] = to_upper(out[0]);
                for (std::size_t j = 1; j < out.size(); ++j)
                    out[j] = to_lower(out[j]);
            }
            break;
        case 'C':
            if (!out.empty())
            {
                out[0] = to_lower(out[0]);
                for (std::size_t j = 1; j < out.size(); ++j)
                    out[j] = to_upper(out[j]);
            }
            break;
        case 't':
            for (char &c : out)
                c = toggle(c);
            break;
        case 'r':
            std::reverse(out.begin(), out.end());
            break;
        case 'd':
            out += out;
            break;
        case 'f':
        {
            std::string rev = out;
            std::reverse(rev.begin(), rev.end());
            out += rev;
            break;
        }
        case '{':
            if (!out.empty())
            {
                const char first = out.front();
                out.erase(out.begin());
                out.push_back(first);
            }
            break;
        case '}':
            if (!out.empty())
            {
                const char last = out.back();
                out.pop_back();
                out.insert(out.begin(), last);
            }
            break;
        case '[':
            if (!out.empty())
                out.erase(out.begin());
            break;
        case ']':
            if (!out.empty())
                out.pop_back();
            break;
        case 'D':
        {
            const int pos = pos_value(rule[i++]);
            if (pos >= 0 && static_cast<std::size_t>(pos) < out.size())
                out.erase(out.begin() + pos);
            break;
        }
        case '$':
            out.push_back(rule[i++]);
            break;
        case '^':
            out.insert(out.begin(), rule[i++]);
            break;
        case '@':
        {
            const char ch = rule[i++];
            out.erase(std::remove(out.begin(), out.end(), ch), out.end());
            break;
        }
        case 's':
        {
            const char from = rule[i++];
            const char to = rule[i++];
            for (char &c : out)
            {
                if (c == from)
                    c = to;
            }
            break;
        }
        case 'o':
        {
            const int pos = pos_value(rule[i++]);
            const char ch = rule[i++];
            if (pos >= 0 && static_cast<std::size_t>(pos) < out.size())
                out[static_cast<std::size_t>(pos)] = ch;
            break;
        }
        case 'i':
        {
            const int pos = pos_value(rule[i++]);
            const char ch = rule[i++];
            if (pos >= 0 && static_cast<std::size_t>(pos) <= out.size())
                out.insert(out.begin() + pos, ch);
            break;
        }
        case '<':
        {
            const int n = pos_value(rule[i++]);
            if (static_cast<int>(out.size()) >= n)
                return std::nullopt;
            break;
        }
        case '>':
        {
            const int n = pos_value(rule[i++]);
            if (static_cast<int>(out.size()) <= n)
                return std::nullopt;
            break;
        }
        case '_':
        {
            const int n = pos_value(rule[i++]);
            if (static_cast<int>(out.size()) != n)
                return std::nullopt;
            break;
        }
        default:
            return std::nullopt;
        }
    }
    return out;
}

[[nodiscard]] RulesEngine RulesEngine::basic_ruleset()
{
    RulesEngine engine;
    engine.rules_ = {
        ":",   // identity
        "l",   // lowercase
        "c",   // capitalize
        "r",   // reverse
        "$!",  // append !
        "$1",  // append 1
        "sa@", // leet a→@
        "se3", // e→3
        "si1", // i→1
        "so0", // o→0
        "ss$", // s→$
    };
    return engine;
}

[[nodiscard]] std::expected<RulesEngine, std::string>
RulesEngine::load_file(const std::filesystem::path &path)
{
    std::ifstream in(path);
    if (!in)
        return std::unexpected(std::format("Failed to open rules file: {}", path.string()));

    RulesEngine engine;
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line))
    {
        ++line_no;
        // Trim trailing CR
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // Leading whitespace skip for comment detection
        std::size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
            ++start;
        if (start >= line.size() || line[start] == '#')
            continue;
        const std::string_view rule{line.data() + start, line.size() - start};
        if (auto ok = validate_rule(rule); !ok)
            return std::unexpected(std::format("{}:{}: {}", path.string(), line_no, ok.error()));
        engine.rules_.emplace_back(rule);
    }
    return engine;
}

void RulesEngine::append_rules(const RulesEngine &other)
{
    rules_.insert(rules_.end(), other.rules_.begin(), other.rules_.end());
}

[[nodiscard]] std::vector<std::string> RulesEngine::expand(const std::string_view word,
                                                           const int max_variants) const
{
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    const auto push = [&](std::string s) {
        if (max_variants > 0 && static_cast<int>(out.size()) >= max_variants)
            return false;
        if (seen.insert(s).second)
            out.push_back(std::move(s));
        return max_variants <= 0 || static_cast<int>(out.size()) < max_variants;
    };

    if (!push(std::string{word}))
        return out;

    for (const auto &rule : rules_)
    {
        auto applied = apply_rule(word, rule);
        if (!applied)
            continue;
        if (!push(std::move(*applied)))
            break;
    }
    return out;
}
