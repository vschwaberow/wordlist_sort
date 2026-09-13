// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/gzip_stream.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <filesystem>
#include <istream>
#include <memory>
#include <string>

[[nodiscard]] bool path_looks_gzip(const std::filesystem::path &path) noexcept;
[[nodiscard]] bool path_looks_zstd(const std::filesystem::path &path) noexcept;

#if defined(WORDLIST_SORT_ZLIB)

/// Decompressing input stream over a gzip file (zlib `gzFile`).
class GzipInputStream final : public std::istream
{
  public:
    explicit GzipInputStream(const std::filesystem::path &path);
    ~GzipInputStream() override;

    GzipInputStream(const GzipInputStream &) = delete;
    GzipInputStream &operator=(const GzipInputStream &) = delete;

    [[nodiscard]] bool is_open() const noexcept;

  private:
    class Buf;
    std::unique_ptr<Buf> buf_;
};

#endif

#if defined(WORDLIST_SORT_ZSTD)

/// Decompressing input stream over a zstd file (`ZSTD_DStream`).
class ZstdInputStream final : public std::istream
{
  public:
    explicit ZstdInputStream(const std::filesystem::path &path);
    ~ZstdInputStream() override;

    ZstdInputStream(const ZstdInputStream &) = delete;
    ZstdInputStream &operator=(const ZstdInputStream &) = delete;

    [[nodiscard]] bool is_open() const noexcept;

  private:
    class Buf;
    std::unique_ptr<Buf> buf_;
};

#endif

/// Open a path as a line-oriented input stream. Owns the stream.
/// Optional transparent inflate for gzip / zstd (extension or magic).
[[nodiscard]] std::unique_ptr<std::istream> open_input_stream(const std::filesystem::path &path,
                                                              std::string *error_out);
