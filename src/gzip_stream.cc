// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/gzip_stream.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "gzip_stream.hpp"
#include "io_buffer.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdint>
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

[[nodiscard]] bool path_looks_xz(const std::filesystem::path &path) noexcept
{
    const auto ext = path.extension().string();
    return ext.size() == 3 && ext[0] == '.' && eq_ci(ext[1], 'x') && eq_ci(ext[2], 'z');
}

[[nodiscard]] bool path_looks_lz4(const std::filesystem::path &path) noexcept
{
    const auto ext = path.extension().string();
    return ext.size() == 4 && ext[0] == '.' && eq_ci(ext[1], 'l') && eq_ci(ext[2], 'z') &&
           eq_ci(ext[3], '4');
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


class GzipOutputStream::Buf final : public std::streambuf
{
  public:
    Buf(const std::filesystem::path &path, const bool append)
        : file_(gzopen(path.string().c_str(), append ? "ab" : "wb"))
    {
        if (file_ != nullptr)
            setp(buffer_.data(), buffer_.data() + buffer_.size());
    }

    ~Buf() override { close_all(); }

    [[nodiscard]] bool ok() const noexcept { return file_ != nullptr; }

  protected:
    int_type overflow(int_type ch) override
    {
        if (!flush_buffer())
            return traits_type::eof();
        if (traits_type::eq_int_type(ch, traits_type::eof()))
            return traits_type::not_eof(ch);
        const char c = traits_type::to_char_type(ch);
        if (gzwrite(file_, &c, 1) != 1)
            return traits_type::eof();
        return ch;
    }

    std::streamsize xsputn(const char *s, std::streamsize n) override
    {
        if (file_ == nullptr || n <= 0)
            return 0;
        if (!flush_buffer())
            return 0;
        const int wrote = gzwrite(file_, s, static_cast<unsigned>(n));
        return wrote > 0 ? static_cast<std::streamsize>(wrote) : 0;
    }

    int sync() override
    {
        if (!flush_buffer())
            return -1;
        if (file_ == nullptr)
            return -1;
        return gzflush(file_, Z_SYNC_FLUSH) == Z_OK ? 0 : -1;
    }

  private:
    [[nodiscard]] bool flush_buffer()
    {
        if (file_ == nullptr)
            return false;
        const auto pending = pptr() - pbase();
        if (pending > 0)
        {
            const int wrote = gzwrite(file_, pbase(), static_cast<unsigned>(pending));
            if (wrote != static_cast<int>(pending))
                return false;
            pbump(static_cast<int>(-pending));
        }
        return true;
    }

    void close_all()
    {
        if (file_ == nullptr)
            return;
        static_cast<void>(flush_buffer());
        gzclose(file_);
        file_ = nullptr;
    }

    gzFile file_ = nullptr;
    std::vector<char> buffer_ = std::vector<char>(1 << 16);
};

GzipOutputStream::GzipOutputStream(const std::filesystem::path &path, const bool append)
    : std::ostream(nullptr), buf_(std::make_unique<Buf>(path, append))
{
    rdbuf(buf_.get());
    if (!buf_->ok())
        setstate(std::ios::failbit);
}

GzipOutputStream::~GzipOutputStream() = default;

bool GzipOutputStream::is_open() const noexcept
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


class ZstdOutputStream::Buf final : public std::streambuf
{
  public:
    Buf(const std::filesystem::path &path, const bool append)
        : file_(std::fopen(path.string().c_str(), append ? "ab" : "wb")),
          cstream_(ZSTD_createCStream()),
          out_storage_(ZSTD_CStreamOutSize())
    {
        if (file_ == nullptr || cstream_ == nullptr)
        {
            close_all();
            return;
        }
        if (ZSTD_isError(ZSTD_initCStream(cstream_, 3)))
        {
            close_all();
            return;
        }
        setp(in_storage_.data(), in_storage_.data() + in_storage_.size());
        ok_ = true;
    }

    ~Buf() override { close_all(); }

    [[nodiscard]] bool ok() const noexcept { return ok_; }

  protected:
    int_type overflow(int_type ch) override
    {
        if (!compress_pending(ZSTD_e_continue))
            return traits_type::eof();
        if (traits_type::eq_int_type(ch, traits_type::eof()))
            return traits_type::not_eof(ch);
        *pptr() = traits_type::to_char_type(ch);
        pbump(1);
        return ch;
    }

    std::streamsize xsputn(const char *s, std::streamsize n) override
    {
        if (!ok_ || n <= 0)
            return 0;
        std::streamsize done = 0;
        while (done < n)
        {
            const auto space = epptr() - pptr();
            if (space == 0)
            {
                if (!compress_pending(ZSTD_e_continue))
                    return done;
                continue;
            }
            const auto chunk = std::min<std::streamsize>(n - done, space);
            std::memcpy(pptr(), s + done, static_cast<std::size_t>(chunk));
            pbump(static_cast<int>(chunk));
            done += chunk;
        }
        return done;
    }

    int sync() override
    {
        return compress_pending(ZSTD_e_flush) ? 0 : -1;
    }

  private:
    [[nodiscard]] bool compress_pending(const ZSTD_EndDirective end_op)
    {
        if (!ok_)
            return false;
        const auto pending = static_cast<std::size_t>(pptr() - pbase());
        ZSTD_inBuffer input{pbase(), pending, 0};
        std::size_t remaining = 1;
        while (input.pos < input.size || (end_op != ZSTD_e_continue && remaining != 0))
        {
            ZSTD_outBuffer output{out_storage_.data(), out_storage_.size(), 0};
            remaining = ZSTD_compressStream2(cstream_, &output, &input, end_op);
            if (ZSTD_isError(remaining))
                return false;
            if (output.pos > 0)
            {
                if (std::fwrite(out_storage_.data(), 1, output.pos, file_) != output.pos)
                    return false;
            }
            if (end_op == ZSTD_e_continue && input.pos >= input.size)
                break;
        }
        if (pending > 0)
            pbump(-static_cast<int>(pending));
        return true;
    }

    void close_all()
    {
        if (ok_ && cstream_ != nullptr && file_ != nullptr)
            static_cast<void>(compress_pending(ZSTD_e_end));
        if (cstream_ != nullptr)
        {
            ZSTD_freeCStream(cstream_);
            cstream_ = nullptr;
        }
        if (file_ != nullptr)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
        ok_ = false;
    }

    FILE *file_ = nullptr;
    ZSTD_CStream *cstream_ = nullptr;
    std::vector<char> in_storage_ = std::vector<char>(1 << 16);
    std::vector<char> out_storage_;
    bool ok_ = false;
};

ZstdOutputStream::ZstdOutputStream(const std::filesystem::path &path, const bool append)
    : std::ostream(nullptr), buf_(std::make_unique<Buf>(path, append))
{
    rdbuf(buf_.get());
    if (!buf_->ok())
        setstate(std::ios::failbit);
}

ZstdOutputStream::~ZstdOutputStream() = default;

bool ZstdOutputStream::is_open() const noexcept
{
    return buf_ && buf_->ok();
}


#endif

#if defined(WORDLIST_SORT_LZMA)

#include <lzma.h>

class XzInputStream::Buf final : public std::streambuf
{
  public:
    explicit Buf(const std::filesystem::path &path)
        : file_(std::fopen(path.string().c_str(), "rb")),
          in_storage_(1 << 16),
          out_storage_(1 << 16)
    {
        if (file_ == nullptr)
            return;
        stream_ = LZMA_STREAM_INIT;
        const lzma_ret ret = lzma_stream_decoder(&stream_, UINT64_MAX, LZMA_CONCATENATED);
        if (ret != LZMA_OK)
        {
            close_all();
            return;
        }
        decoder_live_ = true;
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
            if (stream_.avail_in == 0 && !input_eof_)
            {
                const std::size_t n = std::fread(in_storage_.data(), 1, in_storage_.size(), file_);
                stream_.next_in = reinterpret_cast<uint8_t *>(in_storage_.data());
                stream_.avail_in = n;
                if (n == 0)
                    input_eof_ = true;
            }

            stream_.next_out = reinterpret_cast<uint8_t *>(out_storage_.data());
            stream_.avail_out = out_storage_.size();

            const lzma_ret ret = lzma_code(&stream_, input_eof_ ? LZMA_FINISH : LZMA_RUN);
            const std::size_t produced = out_storage_.size() - stream_.avail_out;
            if (produced > 0)
            {
                setg(out_storage_.data(), out_storage_.data(),
                     out_storage_.data() + static_cast<std::ptrdiff_t>(produced));
                return traits_type::to_int_type(*gptr());
            }

            if (ret == LZMA_STREAM_END)
                return traits_type::eof();
            if (ret != LZMA_OK)
                return traits_type::eof();
            if (input_eof_ && stream_.avail_in == 0)
                return traits_type::eof();
        }
    }

  private:
    void close_all()
    {
        if (decoder_live_)
        {
            lzma_end(&stream_);
            decoder_live_ = false;
        }
        stream_ = LZMA_STREAM_INIT;
        if (file_ != nullptr)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
        ok_ = false;
    }

    FILE *file_ = nullptr;
    lzma_stream stream_ = LZMA_STREAM_INIT;
    std::vector<char> in_storage_;
    std::vector<char> out_storage_;
    bool input_eof_ = false;
    bool decoder_live_ = false;
    bool ok_ = false;
};

XzInputStream::XzInputStream(const std::filesystem::path &path)
    : std::istream(nullptr), buf_(std::make_unique<Buf>(path))
{
    rdbuf(buf_.get());
    if (!buf_->ok())
        setstate(std::ios::failbit);
}

XzInputStream::~XzInputStream() = default;

bool XzInputStream::is_open() const noexcept
{
    return buf_ && buf_->ok();
}


class XzOutputStream::Buf final : public std::streambuf
{
  public:
    Buf(const std::filesystem::path &path, const bool append)
        : file_(std::fopen(path.string().c_str(), append ? "ab" : "wb")),
          in_storage_(1 << 16),
          out_storage_(1 << 16)
    {
        if (file_ == nullptr)
            return;
        stream_ = LZMA_STREAM_INIT;
        const lzma_ret ret = lzma_easy_encoder(&stream_, 6, LZMA_CHECK_CRC64);
        if (ret != LZMA_OK)
        {
            close_all();
            return;
        }
        encoder_live_ = true;
        setp(in_storage_.data(), in_storage_.data() + in_storage_.size());
        ok_ = true;
    }

    ~Buf() override { close_all(); }

    [[nodiscard]] bool ok() const noexcept { return ok_; }

  protected:
    int_type overflow(int_type ch) override
    {
        if (!compress_pending(LZMA_RUN))
            return traits_type::eof();
        if (traits_type::eq_int_type(ch, traits_type::eof()))
            return traits_type::not_eof(ch);
        *pptr() = traits_type::to_char_type(ch);
        pbump(1);
        return ch;
    }

    std::streamsize xsputn(const char *s, std::streamsize n) override
    {
        if (!ok_ || n <= 0)
            return 0;
        std::streamsize done = 0;
        while (done < n)
        {
            const auto space = epptr() - pptr();
            if (space == 0)
            {
                if (!compress_pending(LZMA_RUN))
                    return done;
                continue;
            }
            const auto chunk = std::min<std::streamsize>(n - done, space);
            std::memcpy(pptr(), s + done, static_cast<std::size_t>(chunk));
            pbump(static_cast<int>(chunk));
            done += chunk;
        }
        return done;
    }

    int sync() override
    {
        return compress_pending(LZMA_FULL_FLUSH) ? 0 : -1;
    }

  private:
    [[nodiscard]] bool compress_pending(const lzma_action action)
    {
        if (!ok_)
            return false;
        const auto pending = static_cast<std::size_t>(pptr() - pbase());
        stream_.next_in = reinterpret_cast<uint8_t *>(pbase());
        stream_.avail_in = pending;

        lzma_ret ret = LZMA_OK;
        while (stream_.avail_in > 0 || action != LZMA_RUN)
        {
            stream_.next_out = reinterpret_cast<uint8_t *>(out_storage_.data());
            stream_.avail_out = out_storage_.size();
            ret = lzma_code(&stream_, action);
            const std::size_t produced = out_storage_.size() - stream_.avail_out;
            if (produced > 0)
            {
                if (std::fwrite(out_storage_.data(), 1, produced, file_) != produced)
                    return false;
            }
            if (ret == LZMA_STREAM_END)
                break;
            if (ret != LZMA_OK)
                return false;
            if (action == LZMA_RUN && stream_.avail_in == 0)
                break;
            if (action == LZMA_FULL_FLUSH && stream_.avail_in == 0 && produced == 0)
                break;
        }
        if (pending > 0)
            pbump(-static_cast<int>(pending));
        return true;
    }

    void close_all()
    {
        if (ok_ && encoder_live_ && file_ != nullptr)
            static_cast<void>(compress_pending(LZMA_FINISH));
        if (encoder_live_)
        {
            lzma_end(&stream_);
            encoder_live_ = false;
        }
        stream_ = LZMA_STREAM_INIT;
        if (file_ != nullptr)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
        ok_ = false;
    }

    FILE *file_ = nullptr;
    lzma_stream stream_ = LZMA_STREAM_INIT;
    std::vector<char> in_storage_;
    std::vector<char> out_storage_;
    bool encoder_live_ = false;
    bool ok_ = false;
};

XzOutputStream::XzOutputStream(const std::filesystem::path &path, const bool append)
    : std::ostream(nullptr), buf_(std::make_unique<Buf>(path, append))
{
    rdbuf(buf_.get());
    if (!buf_->ok())
        setstate(std::ios::failbit);
}

XzOutputStream::~XzOutputStream() = default;

bool XzOutputStream::is_open() const noexcept
{
    return buf_ && buf_->ok();
}


#endif

#if defined(WORDLIST_SORT_LZ4)

#include <lz4frame.h>

class Lz4InputStream::Buf final : public std::streambuf
{
  public:
    explicit Buf(const std::filesystem::path &path)
        : file_(std::fopen(path.string().c_str(), "rb")),
          in_storage_(1 << 16),
          out_storage_(1 << 16)
    {
        if (file_ == nullptr)
            return;
        if (LZ4F_isError(LZ4F_createDecompressionContext(&dctx_, LZ4F_VERSION)))
        {
            dctx_ = nullptr;
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
        if (!ok_ || dctx_ == nullptr)
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

            std::size_t dst_size = out_storage_.size();
            std::size_t src_size = in_size_ - in_pos_;
            const std::size_t src_before = src_size;
            const std::size_t ret = LZ4F_decompress(
                dctx_, out_storage_.data(), &dst_size, in_storage_.data() + in_pos_, &src_size, nullptr);
            in_pos_ += src_size;

            if (LZ4F_isError(ret))
                return traits_type::eof();

            if (dst_size > 0)
            {
                setg(out_storage_.data(), out_storage_.data(),
                     out_storage_.data() + static_cast<std::ptrdiff_t>(dst_size));
                return traits_type::to_int_type(*gptr());
            }

            // Frame complete.
            if (ret == 0)
                return traits_type::eof();

            // Consumed input but no output yet (e.g. header) — keep going.
            if (src_size > 0)
                continue;

            // Need more input.
            if (!input_eof_)
                continue;

            // EOF with no progress — stop (avoid spin).
            if (src_before == 0)
                return traits_type::eof();
        }
    }

  private:
    void close_all()
    {
        if (dctx_ != nullptr)
        {
            LZ4F_freeDecompressionContext(dctx_);
            dctx_ = nullptr;
        }
        if (file_ != nullptr)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
        ok_ = false;
    }

    FILE *file_ = nullptr;
    LZ4F_dctx *dctx_ = nullptr;
    std::vector<char> in_storage_;
    std::vector<char> out_storage_;
    std::size_t in_size_ = 0;
    std::size_t in_pos_ = 0;
    bool input_eof_ = false;
    bool ok_ = false;
};

Lz4InputStream::Lz4InputStream(const std::filesystem::path &path)
    : std::istream(nullptr), buf_(std::make_unique<Buf>(path))
{
    rdbuf(buf_.get());
    if (!buf_->ok())
        setstate(std::ios::failbit);
}

Lz4InputStream::~Lz4InputStream() = default;

bool Lz4InputStream::is_open() const noexcept
{
    return buf_ && buf_->ok();
}


class Lz4OutputStream::Buf final : public std::streambuf
{
  public:
    Buf(const std::filesystem::path &path, const bool append)
        : file_(std::fopen(path.string().c_str(), append ? "ab" : "wb")),
          in_storage_(1 << 16),
          out_storage_(LZ4F_compressBound(1 << 16, nullptr) + LZ4F_HEADER_SIZE_MAX)
    {
        if (file_ == nullptr)
            return;
        if (LZ4F_isError(LZ4F_createCompressionContext(&cctx_, LZ4F_VERSION)))
        {
            cctx_ = nullptr;
            close_all();
            return;
        }
        const std::size_t header = LZ4F_compressBegin(cctx_, out_storage_.data(), out_storage_.size(), nullptr);
        if (LZ4F_isError(header) || std::fwrite(out_storage_.data(), 1, header, file_) != header)
        {
            close_all();
            return;
        }
        setp(in_storage_.data(), in_storage_.data() + in_storage_.size());
        ok_ = true;
    }

    ~Buf() override { close_all(); }

    [[nodiscard]] bool ok() const noexcept { return ok_; }

  protected:
    int_type overflow(int_type ch) override
    {
        if (!flush_put_area())
            return traits_type::eof();
        if (traits_type::eq_int_type(ch, traits_type::eof()))
            return traits_type::not_eof(ch);
        *pptr() = traits_type::to_char_type(ch);
        pbump(1);
        return ch;
    }

    std::streamsize xsputn(const char *s, std::streamsize n) override
    {
        if (!ok_ || n <= 0)
            return 0;
        std::streamsize done = 0;
        while (done < n)
        {
            const auto space = epptr() - pptr();
            if (space == 0)
            {
                if (!flush_put_area())
                    return done;
                continue;
            }
            const auto chunk = std::min<std::streamsize>(n - done, space);
            std::memcpy(pptr(), s + done, static_cast<std::size_t>(chunk));
            pbump(static_cast<int>(chunk));
            done += chunk;
        }
        return done;
    }

    int sync() override
    {
        if (!flush_put_area())
            return -1;
        const std::size_t n = LZ4F_flush(cctx_, out_storage_.data(), out_storage_.size(), nullptr);
        if (LZ4F_isError(n))
            return -1;
        if (n > 0 && std::fwrite(out_storage_.data(), 1, n, file_) != n)
            return -1;
        return 0;
    }

  private:
    [[nodiscard]] bool flush_put_area()
    {
        if (!ok_ || cctx_ == nullptr)
            return false;
        const auto pending = static_cast<std::size_t>(pptr() - pbase());
        if (pending == 0)
            return true;
        const std::size_t n =
            LZ4F_compressUpdate(cctx_, out_storage_.data(), out_storage_.size(), pbase(), pending, nullptr);
        if (LZ4F_isError(n))
            return false;
        if (n > 0 && std::fwrite(out_storage_.data(), 1, n, file_) != n)
            return false;
        pbump(-static_cast<int>(pending));
        return true;
    }

    void close_all()
    {
        if (ok_ && cctx_ != nullptr && file_ != nullptr)
        {
            static_cast<void>(flush_put_area());
            const std::size_t n = LZ4F_compressEnd(cctx_, out_storage_.data(), out_storage_.size(), nullptr);
            if (!LZ4F_isError(n) && n > 0)
                static_cast<void>(std::fwrite(out_storage_.data(), 1, n, file_));
        }
        if (cctx_ != nullptr)
        {
            LZ4F_freeCompressionContext(cctx_);
            cctx_ = nullptr;
        }
        if (file_ != nullptr)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
        ok_ = false;
    }

    FILE *file_ = nullptr;
    LZ4F_cctx *cctx_ = nullptr;
    std::vector<char> in_storage_;
    std::vector<char> out_storage_;
    bool ok_ = false;
};

Lz4OutputStream::Lz4OutputStream(const std::filesystem::path &path, const bool append)
    : std::ostream(nullptr), buf_(std::make_unique<Buf>(path, append))
{
    rdbuf(buf_.get());
    if (!buf_->ok())
        setstate(std::ios::failbit);
}

Lz4OutputStream::~Lz4OutputStream() = default;

bool Lz4OutputStream::is_open() const noexcept
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
    Xz,
    Lz4,
};

} // namespace

[[nodiscard]] std::unique_ptr<std::istream> open_input_stream(const std::filesystem::path &path,
                                                              std::string *error_out)
{
    // Single open: peek magic on the same handle; reuse for plain files.
    auto plain = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*plain)
    {
        if (error_out)
            *error_out = "Unable to open file: " + path.string();
        return nullptr;
    }

    unsigned char magic[6]{};
    plain->read(reinterpret_cast<char *>(magic), 6);
    const auto n = plain->gcount();

    const bool ext_gz = path_looks_gzip(path);
    const bool ext_zst = path_looks_zstd(path);
    const bool ext_xz = path_looks_xz(path);
    const bool ext_lz4 = path_looks_lz4(path);
    const bool magic_gzip = n >= 2 && magic[0] == 0x1f && magic[1] == 0x8b;
    const bool magic_zstd =
        n >= 4 && magic[0] == 0x28 && magic[1] == 0xb5 && magic[2] == 0x2f && magic[3] == 0xfd;
    const bool magic_xz = n >= 6 && magic[0] == 0xfd && magic[1] == 0x37 && magic[2] == 0x7a &&
                          magic[3] == 0x58 && magic[4] == 0x5a && magic[5] == 0x00;
    const bool magic_lz4 =
        n >= 4 && magic[0] == 0x04 && magic[1] == 0x22 && magic[2] == 0x4d && magic[3] == 0x18;

    CompressionKind kind = CompressionKind::None;
    if (ext_gz || magic_gzip)
        kind = CompressionKind::Gzip;
    else if (ext_zst || magic_zstd)
        kind = CompressionKind::Zstd;
    else if (ext_xz || magic_xz)
        kind = CompressionKind::Xz;
    else if (ext_lz4 || magic_lz4)
        kind = CompressionKind::Lz4;

    if (kind == CompressionKind::None)
    {
        plain->clear();
        plain->seekg(0, std::ios::beg);
        if (*plain)
            return plain;

        // Non-seekable (pipe / process substitution): replay peeked magic.
        plain->clear();
        std::string prefix(reinterpret_cast<const char *>(magic), static_cast<std::size_t>(n > 0 ? n : 0));
        return std::make_unique<PrefixedInputStream>(std::move(plain), std::move(prefix));
    }

    plain.reset();

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

    if (kind == CompressionKind::Xz)
    {
#if defined(WORDLIST_SORT_LZMA)
        auto xz = std::make_unique<XzInputStream>(path);
        if (!xz->is_open())
        {
            if (error_out)
                *error_out = "Unable to open xz file: " + path.string();
            return nullptr;
        }
        return xz;
#else
        if (error_out)
            *error_out = "xz input requires a build with liblzma (WORDLIST_SORT_LZMA)";
        return nullptr;
#endif
    }

    if (kind == CompressionKind::Lz4)
    {
#if defined(WORDLIST_SORT_LZ4)
        auto lz = std::make_unique<Lz4InputStream>(path);
        if (!lz->is_open())
        {
            if (error_out)
                *error_out = "Unable to open lz4 file: " + path.string();
            return nullptr;
        }
        return lz;
#else
        if (error_out)
            *error_out = "lz4 input requires a build with liblz4 (WORDLIST_SORT_LZ4)";
        return nullptr;
#endif
    }

    if (error_out)
        *error_out = "Unable to open file: " + path.string();
    return nullptr;
}

[[nodiscard]] std::unique_ptr<std::ostream> open_text_output_stream(const std::filesystem::path &path,
                                                                    const bool append,
                                                                    std::string *error_out)
{
    if (path_looks_gzip(path))
    {
#if defined(WORDLIST_SORT_ZLIB)
        auto gz = std::make_unique<GzipOutputStream>(path, append);
        if (!gz->is_open())
        {
            if (error_out)
                *error_out = "Unable to open gzip output file: " + path.string();
            return nullptr;
        }
        return gz;
#else
        if (error_out)
            *error_out = "gzip output requires a build with zlib (WORDLIST_SORT_ZLIB)";
        return nullptr;
#endif
    }

    if (path_looks_zstd(path))
    {
#if defined(WORDLIST_SORT_ZSTD)
        auto zs = std::make_unique<ZstdOutputStream>(path, append);
        if (!zs->is_open())
        {
            if (error_out)
                *error_out = "Unable to open zstd output file: " + path.string();
            return nullptr;
        }
        return zs;
#else
        if (error_out)
            *error_out = "zstd output requires a build with libzstd (WORDLIST_SORT_ZSTD)";
        return nullptr;
#endif
    }

    if (path_looks_xz(path))
    {
#if defined(WORDLIST_SORT_LZMA)
        auto xz = std::make_unique<XzOutputStream>(path, append);
        if (!xz->is_open())
        {
            if (error_out)
                *error_out = "Unable to open xz output file: " + path.string();
            return nullptr;
        }
        return xz;
#else
        if (error_out)
            *error_out = "xz output requires a build with liblzma (WORDLIST_SORT_LZMA)";
        return nullptr;
#endif
    }

    if (path_looks_lz4(path))
    {
#if defined(WORDLIST_SORT_LZ4)
        auto lz = std::make_unique<Lz4OutputStream>(path, append);
        if (!lz->is_open())
        {
            if (error_out)
                *error_out = "Unable to open lz4 output file: " + path.string();
            return nullptr;
        }
        return lz;
#else
        if (error_out)
            *error_out = "lz4 output requires a build with liblz4 (WORDLIST_SORT_LZ4)";
        return nullptr;
#endif
    }

    const auto mode = std::ios::binary | (append ? std::ios::app : std::ios::trunc);
    auto file = std::make_unique<std::ofstream>(path, mode);
    if (!*file)
    {
        if (error_out)
            *error_out = "Unable to open output file: " + path.string();
        return nullptr;
    }
    return file;
}
