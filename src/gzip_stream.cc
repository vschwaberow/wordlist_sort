// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/gzip_stream.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "gzip_stream.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {

[[nodiscard]] bool eq_ci(char a, char b) noexcept
{
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
}

} // namespace

[[nodiscard]] bool path_looks_gzip(const std::filesystem::path &path) noexcept
{
    const auto ext = path.extension().string();
    return ext.size() == 3 && ext[0] == '.' && eq_ci(ext[1], 'g') && eq_ci(ext[2], 'z');
}

[[nodiscard]] bool path_looks_zstd(const std::filesystem::path &path) noexcept
{
    const auto ext = path.extension().string();
    if (ext.size() == 4 && ext[0] == '.' && eq_ci(ext[1], 'z') && eq_ci(ext[2], 's') && eq_ci(ext[3], 't'))
        return true;
    // .zstd
    if (ext.size() == 5 && ext[0] == '.' && eq_ci(ext[1], 'z') && eq_ci(ext[2], 's') && eq_ci(ext[3], 't') &&
        eq_ci(ext[4], 'd'))
        return true;
    return false;
}

#if defined(WORDLIST_SORT_ZLIB)

#include <zlib.h>

class GzipInputStream::Buf final : public std::streambuf
{
  public:
    explicit Buf(const std::filesystem::path &path)
        : file_(gzopen(path.string().c_str(), "rb"))
    {
        if (file_ != nullptr)
            setg(buffer_.data(), buffer_.data(), buffer_.data());
    }

    ~Buf() override
    {
        if (file_ != nullptr)
            gzclose(file_);
    }

    [[nodiscard]] bool ok() const noexcept { return file_ != nullptr; }

  protected:
    int_type underflow() override
    {
        if (file_ == nullptr)
            return traits_type::eof();
        if (gptr() < egptr())
            return traits_type::to_int_type(*gptr());

        const int n = gzread(file_, buffer_.data(), static_cast<unsigned>(buffer_.size()));
        if (n <= 0)
            return traits_type::eof();

        setg(buffer_.data(), buffer_.data(), buffer_.data() + n);
        return traits_type::to_int_type(*gptr());
    }

  private:
    gzFile file_ = nullptr;
    std::vector<char> buffer_ = std::vector<char>(1 << 16);
};

GzipInputStream::GzipInputStream(const std::filesystem::path &path)
    : std::istream(nullptr), buf_(std::make_unique<Buf>(path))
{
    rdbuf(buf_.get());
    if (!buf_->ok())
        setstate(std::ios::failbit);
}

GzipInputStream::~GzipInputStream() = default;

bool GzipInputStream::is_open() const noexcept
{
    return buf_ && buf_->ok();
}

#endif

#if defined(WORDLIST_SORT_ZSTD)

#include <zstd.h>

class ZstdInputStream::Buf final : public std::streambuf
{
  public:
    explicit Buf(const std::filesystem::path &path)
        : file_(std::fopen(path.string().c_str(), "rb")),
          dstream_(ZSTD_createDStream()),
          in_storage_(ZSTD_DStreamInSize()),
          out_storage_(ZSTD_DStreamOutSize())
    {
        if (file_ == nullptr || dstream_ == nullptr)
        {
            close_all();
            return;
        }
        if (ZSTD_isError(ZSTD_initDStream(dstream_)))
        {
            close_all();
            return;
        }
        setg(out_storage_.data(), out_storage_.data(), out_storage_.data());
        ok_ = true;
    }

    ~Buf() override { close_all(); }

    [[nodiscard]] bool ok() const noexcept { return ok_; }

  protected:
    int_type underflow() override
    {
        if (!ok_)
            return traits_type::eof();
        if (gptr() < egptr())
            return traits_type::to_int_type(*gptr());

        while (true)
        {
            if (in_pos_ >= in_size_ && !input_eof_)
            {
                in_size_ = std::fread(in_storage_.data(), 1, in_storage_.size(), file_);
                in_pos_ = 0;
                if (in_size_ == 0)
                    input_eof_ = true;
            }

            ZSTD_inBuffer input{in_storage_.data(), in_size_, in_pos_};
            ZSTD_outBuffer output{out_storage_.data(), out_storage_.size(), 0};
            const std::size_t ret = ZSTD_decompressStream(dstream_, &output, &input);
            in_pos_ = input.pos;

            if (ZSTD_isError(ret))
                return traits_type::eof();

            if (output.pos > 0)
            {
                setg(out_storage_.data(), out_storage_.data(), out_storage_.data() + static_cast<std::ptrdiff_t>(output.pos));
                return traits_type::to_int_type(*gptr());
            }

            if (input_eof_ && in_pos_ >= in_size_ && output.pos == 0)
                return traits_type::eof();
        }
    }

  private:
    void close_all()
    {
        if (dstream_ != nullptr)
        {
            ZSTD_freeDStream(dstream_);
            dstream_ = nullptr;
        }
        if (file_ != nullptr)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
        ok_ = false;
    }

    FILE *file_ = nullptr;
    ZSTD_DStream *dstream_ = nullptr;
    std::vector<char> in_storage_;
    std::vector<char> out_storage_;
    std::size_t in_size_ = 0;
    std::size_t in_pos_ = 0;
    bool input_eof_ = false;
    bool ok_ = false;
};

ZstdInputStream::ZstdInputStream(const std::filesystem::path &path)
    : std::istream(nullptr), buf_(std::make_unique<Buf>(path))
{
    rdbuf(buf_.get());
    if (!buf_->ok())
        setstate(std::ios::failbit);
}

ZstdInputStream::~ZstdInputStream() = default;

bool ZstdInputStream::is_open() const noexcept
{
    return buf_ && buf_->ok();
}

#endif

namespace {

enum class CompressionKind
{
    None,
    Gzip,
    Zstd,
};

[[nodiscard]] CompressionKind detect_compression(const std::filesystem::path &path)
{
    const bool ext_gz = path_looks_gzip(path);
    const bool ext_zst = path_looks_zstd(path);

    std::ifstream probe(path, std::ios::binary);
    if (!probe)
        return CompressionKind::None;

    unsigned char magic[4]{};
    probe.read(reinterpret_cast<char *>(magic), 4);
    const auto n = probe.gcount();
    probe.close();

    const bool magic_gzip = n >= 2 && magic[0] == 0x1f && magic[1] == 0x8b;
    const bool magic_zstd =
        n >= 4 && magic[0] == 0x28 && magic[1] == 0xb5 && magic[2] == 0x2f && magic[3] == 0xfd;

    if (ext_gz || magic_gzip)
        return CompressionKind::Gzip;
    if (ext_zst || magic_zstd)
        return CompressionKind::Zstd;
    return CompressionKind::None;
}

} // namespace

[[nodiscard]] std::unique_ptr<std::istream> open_input_stream(const std::filesystem::path &path,
                                                              std::string *error_out)
{
    // Probe existence first.
    {
        std::ifstream probe(path, std::ios::binary);
        if (!probe)
        {
            if (error_out)
                *error_out = "Unable to open file: " + path.string();
            return nullptr;
        }
    }

    const CompressionKind kind = detect_compression(path);

    if (kind == CompressionKind::Gzip)
    {
#if defined(WORDLIST_SORT_ZLIB)
        auto gz = std::make_unique<GzipInputStream>(path);
        if (!gz->is_open())
        {
            if (error_out)
                *error_out = "Unable to open gzip file: " + path.string();
            return nullptr;
        }
        return gz;
#else
        if (error_out)
            *error_out = "gzip input requires a build with zlib (WORDLIST_SORT_ZLIB)";
        return nullptr;
#endif
    }

    if (kind == CompressionKind::Zstd)
    {
#if defined(WORDLIST_SORT_ZSTD)
        auto zs = std::make_unique<ZstdInputStream>(path);
        if (!zs->is_open())
        {
            if (error_out)
                *error_out = "Unable to open zstd file: " + path.string();
            return nullptr;
        }
        return zs;
#else
        if (error_out)
            *error_out = "zstd input requires a build with libzstd (WORDLIST_SORT_ZSTD)";
        return nullptr;
#endif
    }

    auto file = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*file)
    {
        if (error_out)
            *error_out = "Unable to open file: " + path.string();
        return nullptr;
    }
    return file;
}
