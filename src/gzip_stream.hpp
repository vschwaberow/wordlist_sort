// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/gzip_stream.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <filesystem>
#include <istream>
#include <ostream>
#include <memory>
#include <string>
#include <string_view>

[[nodiscard]] bool path_looks_gzip(const std::filesystem::path &path) noexcept;
[[nodiscard]] bool path_looks_zstd(const std::filesystem::path &path) noexcept;
[[nodiscard]] bool path_looks_xz(const std::filesystem::path &path) noexcept;
[[nodiscard]] bool path_looks_lz4(const std::filesystem::path &path) noexcept;

/// Tag for constructing compressing ostreams that write to stdout (via dup'd fd).
struct stdout_tag_t
{
    explicit stdout_tag_t() = default;
};
inline constexpr stdout_tag_t stdout_tag{};


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

#if defined(WORDLIST_SORT_LZMA)

/// Decompressing input stream over an xz file (liblzma stream decoder).
class XzInputStream final : public std::istream
{
  public:
    explicit XzInputStream(const std::filesystem::path &path);
    ~XzInputStream() override;

    XzInputStream(const XzInputStream &) = delete;
    XzInputStream &operator=(const XzInputStream &) = delete;

    [[nodiscard]] bool is_open() const noexcept;

  private:
    class Buf;
    std::unique_ptr<Buf> buf_;
};

#endif

#if defined(WORDLIST_SORT_LZ4)

/// Decompressing input stream over an LZ4 frame file.
class Lz4InputStream final : public std::istream
{
  public:
    explicit Lz4InputStream(const std::filesystem::path &path);
    ~Lz4InputStream() override;

    Lz4InputStream(const Lz4InputStream &) = delete;
    Lz4InputStream &operator=(const Lz4InputStream &) = delete;

    [[nodiscard]] bool is_open() const noexcept;

  private:
    class Buf;
    std::unique_ptr<Buf> buf_;
};

#endif


#if defined(WORDLIST_SORT_ZLIB)

/// Compressing output stream over a gzip file (zlib `gzFile`).
class GzipOutputStream final : public std::ostream
{
  public:
    GzipOutputStream(const std::filesystem::path &path, bool append);
    explicit GzipOutputStream(stdout_tag_t);
    ~GzipOutputStream() override;

    GzipOutputStream(const GzipOutputStream &) = delete;
    GzipOutputStream &operator=(const GzipOutputStream &) = delete;

    [[nodiscard]] bool is_open() const noexcept;

  private:
    class Buf;
    std::unique_ptr<Buf> buf_;
};

#endif


#if defined(WORDLIST_SORT_ZSTD)

/// Compressing output stream over a zstd file (`ZSTD_CStream`).
class ZstdOutputStream final : public std::ostream
{
  public:
    ZstdOutputStream(const std::filesystem::path &path, bool append);
    explicit ZstdOutputStream(stdout_tag_t);
    ~ZstdOutputStream() override;

    ZstdOutputStream(const ZstdOutputStream &) = delete;
    ZstdOutputStream &operator=(const ZstdOutputStream &) = delete;

    [[nodiscard]] bool is_open() const noexcept;

  private:
    class Buf;
    std::unique_ptr<Buf> buf_;
};

#endif


#if defined(WORDLIST_SORT_LZMA)

/// Compressing output stream over an xz file (liblzma easy encoder).
class XzOutputStream final : public std::ostream
{
  public:
    XzOutputStream(const std::filesystem::path &path, bool append);
    explicit XzOutputStream(stdout_tag_t);
    ~XzOutputStream() override;

    XzOutputStream(const XzOutputStream &) = delete;
    XzOutputStream &operator=(const XzOutputStream &) = delete;

    [[nodiscard]] bool is_open() const noexcept;

  private:
    class Buf;
    std::unique_ptr<Buf> buf_;
};

#endif


#if defined(WORDLIST_SORT_LZ4)

/// Compressing output stream over an LZ4 frame file.
class Lz4OutputStream final : public std::ostream
{
  public:
    Lz4OutputStream(const std::filesystem::path &path, bool append);
    explicit Lz4OutputStream(stdout_tag_t);
    ~Lz4OutputStream() override;

    Lz4OutputStream(const Lz4OutputStream &) = delete;
    Lz4OutputStream &operator=(const Lz4OutputStream &) = delete;

    [[nodiscard]] bool is_open() const noexcept;

  private:
    class Buf;
    std::unique_ptr<Buf> buf_;
};

#endif

/// Open a path as a line-oriented input stream. Owns the stream.
/// Optional transparent inflate for gzip / zstd / xz / lz4 (extension or magic).
[[nodiscard]] std::unique_ptr<std::istream> open_input_stream(const std::filesystem::path &path,
                                                              std::string *error_out);

/// Open a text output stream. Transparent gzip / zstd / xz / lz4 when extension matches.
/// `append` adds another compressed member/frame/stream where supported.
[[nodiscard]] std::unique_ptr<std::ostream> open_text_output_stream(const std::filesystem::path &path,
                                                                   bool append,
                                                                   std::string *error_out);

/// Compress text to stdout. `codec`: gzip|gz|zstd|zst|xz|lz4.
[[nodiscard]] std::unique_ptr<std::ostream> open_compressed_stdout(std::string_view codec,
                                                                  std::string *error_out);
