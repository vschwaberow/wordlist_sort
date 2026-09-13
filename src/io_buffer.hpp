// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/io_buffer.hpp
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <istream>
#include <memory>
#include <streambuf>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

/// Large user-space I/O helpers (no mmap). Default slab ~1 MiB.
inline constexpr std::size_t kIoBufferBytes = 1u << 20;


/// istream that yields a fixed prefix, then continues from an owned underlying istream.
/// Used when magic-byte peek cannot rewind (pipes / process substitution).
class PrefixedInputStream final : public std::istream
{
  public:
    PrefixedInputStream(std::unique_ptr<std::istream> inner, std::string prefix)
        : std::istream(nullptr), buf_(std::make_unique<Buf>(std::move(inner), std::move(prefix)))
    {
        rdbuf(buf_.get());
    }

    PrefixedInputStream(const PrefixedInputStream &) = delete;
    PrefixedInputStream &operator=(const PrefixedInputStream &) = delete;

  private:
    class Buf final : public std::streambuf
    {
      public:
        Buf(std::unique_ptr<std::istream> inner, std::string prefix)
            : inner_(std::move(inner)), prefix_(std::move(prefix))
        {
            if (!prefix_.empty())
                setg(prefix_.data(), prefix_.data(), prefix_.data() + prefix_.size());
        }

      protected:
        int_type underflow() override
        {
            if (gptr() < egptr())
                return traits_type::to_int_type(*gptr());
            if (!inner_)
                return traits_type::eof();
            inner_->read(scratch_, static_cast<std::streamsize>(sizeof(scratch_)));
            const auto n = inner_->gcount();
            if (n <= 0)
                return traits_type::eof();
            setg(scratch_, scratch_, scratch_ + static_cast<std::size_t>(n));
            return traits_type::to_int_type(*gptr());
        }

      private:
        std::unique_ptr<std::istream> inner_;
        std::string prefix_;
        char scratch_[4096]{};
    };

    std::unique_ptr<Buf> buf_;
};

/// Scan records from an istream using a large read buffer + memchr.
class BufferedRecordReader
{
  public:
    BufferedRecordReader(std::istream &in, const char record_sep, const std::size_t capacity = kIoBufferBytes)
        : in_(in), sep_(record_sep), buf_(capacity)
    {
    }

    [[nodiscard]] bool next(std::string &out)
    {
        out.clear();
        while (true)
        {
            if (pos_ >= end_)
            {
                if (!refill())
                    return !out.empty();
            }

            char *const start = buf_.data() + static_cast<std::ptrdiff_t>(pos_);
            const std::size_t avail = end_ - pos_;
            if (char *const found = static_cast<char *>(std::memchr(start, sep_, avail)))
            {
                out.append(start, static_cast<std::size_t>(found - start));
                pos_ = static_cast<std::size_t>(found - buf_.data()) + 1;
                if (sep_ == '\n' && !out.empty() && out.back() == '\r')
                    out.pop_back();
                return true;
            }

            out.append(start, avail);
            pos_ = end_;
        }
    }

  private:
    [[nodiscard]] bool refill()
    {
        in_.read(buf_.data(), static_cast<std::streamsize>(buf_.size()));
        const auto n = in_.gcount();
        end_ = n > 0 ? static_cast<std::size_t>(n) : 0;
        pos_ = 0;
        return end_ > 0;
    }

    std::istream &in_;
    char sep_;
    std::vector<char> buf_;
    std::size_t pos_ = 0;
    std::size_t end_ = 0;
};

/// Accumulate records and flush with a single write (optional mutex for shared sinks).
class BufferedRecordWriter
{
  public:
    BufferedRecordWriter(std::ostream &out, const char record_sep, std::mutex *mutex = nullptr,
                         const std::size_t flush_bytes = kIoBufferBytes)
        : out_(&out), file_(nullptr), mutex_(mutex), sep_(record_sep), flush_bytes_(flush_bytes)
    {
        acc_.reserve(flush_bytes_);
    }

    BufferedRecordWriter(FILE *file, const char record_sep, const std::size_t flush_bytes = kIoBufferBytes)
        : out_(nullptr), file_(file), mutex_(nullptr), sep_(record_sep), flush_bytes_(flush_bytes)
    {
        acc_.reserve(flush_bytes_);
    }

    ~BufferedRecordWriter() { static_cast<void>(flush()); }

    BufferedRecordWriter(const BufferedRecordWriter &) = delete;
    BufferedRecordWriter &operator=(const BufferedRecordWriter &) = delete;

    void write(const std::string_view word)
    {
        acc_.append(word);
        acc_.push_back(sep_);
        if (acc_.size() >= flush_bytes_)
            static_cast<void>(flush());
    }

    [[nodiscard]] bool flush()
    {
        if (acc_.empty())
            return true;

        bool ok = true;
        if (mutex_ != nullptr)
        {
            std::lock_guard<std::mutex> lock(*mutex_);
            ok = emit_unlocked();
        }
        else
        {
            ok = emit_unlocked();
        }
        acc_.clear();
        return ok;
    }

    [[nodiscard]] bool good() const noexcept { return good_; }

  private:
    [[nodiscard]] bool emit_unlocked()
    {
        if (file_ != nullptr)
        {
            const auto n = std::fwrite(acc_.data(), 1, acc_.size(), file_);
            if (n != acc_.size())
            {
                good_ = false;
                return false;
            }
            return true;
        }
        if (out_ != nullptr)
        {
            out_->write(acc_.data(), static_cast<std::streamsize>(acc_.size()));
            if (!*out_)
            {
                good_ = false;
                return false;
            }
            return true;
        }
        good_ = false;
        return false;
    }

    std::ostream *out_;
    FILE *file_;
    std::mutex *mutex_;
    char sep_;
    std::size_t flush_bytes_;
    std::string acc_;
    bool good_ = true;
};
