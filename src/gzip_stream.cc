// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/gzip_stream.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "gzip_stream.hpp"

#include <fstream>
#include <cctype>
#include <vector>

[[nodiscard]] bool path_looks_gzip(const std::filesystem::path &path) noexcept
{
    const auto ext = path.extension().string();
    if (ext.size() == 3 &&
        (ext[0] == '.') &&
        (ext[1] == 'g' || ext[1] == 'G') &&
        (ext[2] == 'z' || ext[2] == 'Z'))
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

[[nodiscard]] std::unique_ptr<std::istream> open_input_stream(const std::filesystem::path &path,
                                                              std::string *error_out)
{
#if defined(WORDLIST_SORT_ZLIB)
    const bool want_gzip = path_looks_gzip(path);
    if (!want_gzip)
    {
        // Peek magic for extension-less gzip dumps.
        std::ifstream probe(path, std::ios::binary);
        if (!probe)
        {
            if (error_out)
                *error_out = "Unable to open file: " + path.string();
            return nullptr;
        }
        unsigned char magic[2]{};
        probe.read(reinterpret_cast<char *>(magic), 2);
        const bool magic_gzip = probe.gcount() == 2 && magic[0] == 0x1f && magic[1] == 0x8b;
        probe.close();
        if (magic_gzip)
        {
            auto gz = std::make_unique<GzipInputStream>(path);
            if (!gz->is_open())
            {
                if (error_out)
                    *error_out = "Unable to open gzip file: " + path.string();
                return nullptr;
            }
            return gz;
        }
    }
    else
    {
        auto gz = std::make_unique<GzipInputStream>(path);
        if (!gz->is_open())
        {
            if (error_out)
                *error_out = "Unable to open gzip file: " + path.string();
            return nullptr;
        }
        return gz;
    }
#else
    if (path_looks_gzip(path))
    {
        if (error_out)
            *error_out = "gzip input requires a build with zlib (WORDLIST_SORT_ZLIB)";
        return nullptr;
    }
    {
        std::ifstream probe(path, std::ios::binary);
        if (probe)
        {
            unsigned char magic[2]{};
            probe.read(reinterpret_cast<char *>(magic), 2);
            if (probe.gcount() == 2 && magic[0] == 0x1f && magic[1] == 0x8b)
            {
                if (error_out)
                    *error_out = "gzip input requires a build with zlib (WORDLIST_SORT_ZLIB)";
                return nullptr;
            }
        }
    }
#endif

    auto file = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*file)
    {
        if (error_out)
            *error_out = "Unable to open file: " + path.string();
        return nullptr;
    }
    return file;
}
