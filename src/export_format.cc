// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/export_format.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "export_format.hpp"

#include <format>

#include "word_pipeline.hpp"

[[nodiscard]] std::expected<ExportFormat, std::string> parse_export_format(const std::string_view text)
{
    if (text == "text" || text == "txt")
        return ExportFormat::Text;
    if (text == "cdb")
        return ExportFormat::Cdb;
    if (text == "fst" || text == "trie")
        return ExportFormat::Fst;
    return std::unexpected(std::format("Unknown --format '{}' (expected text|cdb|fst)", text));
}

[[nodiscard]] const char *export_format_name(const ExportFormat format) noexcept
{
    switch (format)
    {
    case ExportFormat::Text:
        return "text";
    case ExportFormat::Cdb:
        return "cdb";
    case ExportFormat::Fst:
        return "fst";
    }
    return "text";
}

[[nodiscard]] std::expected<void, std::string> write_export(const std::vector<std::string> &words,
                                                            const std::filesystem::path &path,
                                                            const ExportFormat format)
{
    switch (format)
    {
    case ExportFormat::Text:
        return write_lines(words, path);
    case ExportFormat::Cdb:
        return write_cdb(words, path);
    case ExportFormat::Fst:
        return write_fst(words, path);
    }
    return std::unexpected("internal: invalid export format");
}
