// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/export_format.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <span>
#include <string_view>
#include <vector>

enum class ExportFormat
{
    Text,
    Cdb,
    Fst,
    Pthash,
};

[[nodiscard]] std::expected<ExportFormat, std::string> parse_export_format(std::string_view text);

[[nodiscard]] const char *export_format_name(ExportFormat format) noexcept;

/// Write words in the selected format. CDB/FST use each word as key with empty value.
/// Duplicate keys: first occurrence wins (later duplicates skipped).
[[nodiscard]] std::expected<void, std::string> write_export(const std::vector<std::string> &words,
                                                            const std::filesystem::path &path,
                                                            ExportFormat format,
                                                            bool append = false);

[[nodiscard]] std::expected<void, std::string> write_cdb(const std::vector<std::string> &words,
                                                         const std::filesystem::path &path);

[[nodiscard]] std::expected<void, std::string> write_fst(const std::vector<std::string> &words,
                                                         const std::filesystem::path &path);

/// Persist PTHash + key table as WLPTH1 (requires WORDLIST_SORT_PTHASH).
[[nodiscard]] std::expected<void, std::string> write_pthash(const std::vector<std::string> &words,
                                                            const std::filesystem::path &path,
                                                            const std::filesystem::path &tmp_dir = {});

/// Exact-key probe against an already-loaded WLTRIE1 buffer.
[[nodiscard]] std::expected<bool, std::string> fst_contains_bytes(std::span<const unsigned char> data,
                                                                  std::string_view key);

/// Exact-key probe for tests / tooling (mmap-free read of our WLTRIE1 files).
[[nodiscard]] std::expected<bool, std::string> fst_contains(const std::filesystem::path &path,
                                                            std::string_view key);

/// Exact-key probe against an already-loaded DJB CDB buffer.
[[nodiscard]] std::expected<bool, std::string> cdb_contains_bytes(std::span<const unsigned char> data,
                                                                  std::string_view key);

/// Exact-key probe for DJB CDB files.
[[nodiscard]] std::expected<bool, std::string> cdb_contains(const std::filesystem::path &path,
                                                            std::string_view key);

/// Count records in an already-loaded DJB CDB buffer (data section walk).
[[nodiscard]] std::expected<std::size_t, std::string> cdb_key_count(std::span<const unsigned char> data);

/// Key count from WLTRIE1 header (unique keys), if buffer is a valid header prefix.
[[nodiscard]] std::expected<std::uint32_t, std::string> fst_key_count(std::span<const unsigned char> data);
