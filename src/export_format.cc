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
    if (text == "pthash" || text == "mphf")
        return ExportFormat::Pthash;
    return std::unexpected(std::format("Unknown --format '{}' (expected text|cdb|fst|pthash)", text));
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
    case ExportFormat::Pthash:
        return "pthash";
    }
    return "text";
}

[[nodiscard]] std::expected<void, std::string> write_export(const std::vector<std::string> &words,
                                                            const std::filesystem::path &path,
                                                            const ExportFormat format, const bool append,
                                                            const bool null_separated)
{
    if (path == "-" && format != ExportFormat::Text)
        return std::unexpected("stdout (-) is only supported with --format=text");
    if (append && format != ExportFormat::Text)
        return std::unexpected("--append is only supported with --format=text");
    if (null_separated && format != ExportFormat::Text)
        return std::unexpected("--null is only supported with --format=text");

    switch (format)
    {
    case ExportFormat::Text:
        return write_lines(words, path, append, null_separated);
    case ExportFormat::Cdb:
        return write_cdb(words, path);
    case ExportFormat::Fst:
        return write_fst(words, path);
    case ExportFormat::Pthash:
        return write_pthash(words, path);
    }
    return std::unexpected("internal: invalid export format");
}
