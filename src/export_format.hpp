// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/export_format.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

enum class ExportFormat
{
    Text,
    Cdb,
    Fst,
};

[[nodiscard]] std::expected<ExportFormat, std::string> parse_export_format(std::string_view text);

[[nodiscard]] const char *export_format_name(ExportFormat format) noexcept;

/// Write words in the selected format. CDB/FST use each word as key with empty value.
/// Duplicate keys: first occurrence wins (later duplicates skipped).
[[nodiscard]] std::expected<void, std::string> write_export(const std::vector<std::string> &words,
                                                            const std::filesystem::path &path,
                                                            ExportFormat format);

[[nodiscard]] std::expected<void, std::string> write_cdb(const std::vector<std::string> &words,
                                                         const std::filesystem::path &path);

[[nodiscard]] std::expected<void, std::string> write_fst(const std::vector<std::string> &words,
                                                         const std::filesystem::path &path);

/// Exact-key probe for tests / tooling (mmap-free read of our WLTRIE1 files).
[[nodiscard]] std::expected<bool, std::string> fst_contains(const std::filesystem::path &path,
                                                            std::string_view key);

/// Exact-key probe for DJB CDB files.
[[nodiscard]] std::expected<bool, std::string> cdb_contains(const std::filesystem::path &path,
                                                            std::string_view key);
