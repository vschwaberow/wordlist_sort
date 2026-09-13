// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: benchmarks/sort_dedup_bench.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "sort_dedup.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <print>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::duration<double, std::milli>;

struct BenchConfig
{
    std::vector<std::size_t> sizes{100'000, 1'000'000, 5'000'000, 10'000'000};
    int iters = 3;
    int warmup = 1;
    double dup_ratio = 0.20;
    bool csv = false;
    bool use_cuda = false;
    std::uint32_t seed = 0xC0FFEEu;
};

[[nodiscard]] bool parse_u32(const std::string_view text, std::uint32_t &out) noexcept
{
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && ptr == text.data() + text.size();
}

[[nodiscard]] bool parse_i32(const std::string_view text, int &out) noexcept
{
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && ptr == text.data() + text.size() && out >= 0;
}

[[nodiscard]] bool parse_f64(const std::string_view text, double &out) noexcept
{
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && ptr == text.data() + text.size() && std::isfinite(out);
}

[[nodiscard]] bool parse_sizes(const std::string_view text, std::vector<std::size_t> &out)
{
    out.clear();
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t comma = text.find(',', start);
        const auto token = text.substr(start, comma == std::string_view::npos ? text.npos : comma - start);
        if (token.empty())
            return false;

        std::uint64_t value = 0;
        const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
        if (ec != std::errc{} || ptr != token.data() + token.size() || value == 0)
            return false;

        out.push_back(static_cast<std::size_t>(value));
        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    return !out.empty();
}

void print_usage(const char *argv0)
{
    std::println("Usage: {} [options]", argv0);
    std::println("  --sizes N,N,...   Word counts to measure (default: 100000,1000000,5000000,10000000)");
    std::println("  --iters N         Timed iterations per size/mode (default: 3)");
    std::println("  --warmup N        Untimed warmup iterations (default: 1)");
    std::println("  --dup-ratio R     Duplicate fraction in synthetic input, 0..1 (default: 0.20)");
    std::println("  --seed N          RNG seed (default: 12648430)");
    std::println("  --csv             Machine-readable CSV output");
    std::println("  --cuda            Prefer CUDA path when compiled with CUDA support");
    std::println("  -h, --help        Show this help");
}

[[nodiscard]] bool parse_args(const int argc, char **argv, BenchConfig &cfg)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        auto need_value = [&](std::string_view &value) -> bool {
            if (i + 1 >= argc)
            {
                std::println(stderr, "Missing value for {}", arg);
                return false;
            }
            value = argv[++i];
            return true;
        };

        if (arg == "-h" || arg == "--help")
        {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (arg == "--csv")
        {
            cfg.csv = true;
            continue;
        }
        if (arg == "--cuda")
        {
            cfg.use_cuda = true;
            continue;
        }

        std::string_view value;
        if (arg == "--sizes")
        {
            if (!need_value(value) || !parse_sizes(value, cfg.sizes))
            {
                std::println(stderr, "Invalid --sizes value");
                return false;
            }
            continue;
        }
        if (arg == "--iters")
        {
            if (!need_value(value) || !parse_i32(value, cfg.iters) || cfg.iters < 1)
            {
                std::println(stderr, "Invalid --iters value");
                return false;
            }
            continue;
        }
        if (arg == "--warmup")
        {
            if (!need_value(value) || !parse_i32(value, cfg.warmup))
            {
                std::println(stderr, "Invalid --warmup value");
                return false;
            }
            continue;
        }
        if (arg == "--dup-ratio")
        {
            if (!need_value(value) || !parse_f64(value, cfg.dup_ratio) || cfg.dup_ratio < 0.0 ||
                cfg.dup_ratio > 1.0)
            {
                std::println(stderr, "Invalid --dup-ratio value (expected 0..1)");
                return false;
            }
            continue;
        }
        if (arg == "--seed")
        {
            if (!need_value(value) || !parse_u32(value, cfg.seed))
            {
                std::println(stderr, "Invalid --seed value");
                return false;
            }
            continue;
        }

        std::println(stderr, "Unknown option: {}", arg);
        return false;
    }
    return true;
}

[[nodiscard]] std::vector<std::string> make_words(const std::size_t count, const double dup_ratio,
                                                  const std::uint32_t seed)
{
    std::vector<std::string> words;
    words.reserve(count);

    const std::size_t unique_count =
        std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(static_cast<double>(count) *
                                                                        (1.0 - dup_ratio))));

    std::mt19937 rng(seed ^ static_cast<std::uint32_t>(count));
    std::uniform_int_distribution<std::size_t> pick(0, unique_count - 1);

    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t id = (i < unique_count) ? i : pick(rng);
        words.push_back(std::format("w{:08x}-{:06}", static_cast<unsigned>(id * 2654435761u), id % 1'000'000));
    }

    std::shuffle(words.begin(), words.end(), rng);
    return words;
}

struct TimingResult
{
    double ms = 0.0;
    std::size_t out_words = 0;
};

enum class BenchMode
{
    SortOnly,
    DedupOnly,
    Total,
};

[[nodiscard]] const char *mode_name(const BenchMode mode) noexcept
{
    switch (mode)
    {
    case BenchMode::SortOnly:
        return "sort";
    case BenchMode::DedupOnly:
        return "dedup";
    case BenchMode::Total:
        return "total";
    }
    return "unknown";
}

void apply_backend(std::vector<std::string> &words, const SortDedupPlan &plan, const bool use_cuda)
{
    if (use_cuda)
    {
        if (try_sort_and_deduplicate_words_cuda(words, plan))
            return;
    }
    sort_and_deduplicate_words_cpu(words, plan);
}

[[nodiscard]] TimingResult time_once(const std::vector<std::string> &base, const BenchMode mode,
                                     const bool use_cuda)
{
    const SortDedupPlan sort_only{.perform_sort = true, .perform_deduplicate = false,
                                  .announce_implicit_sort = false};
    const SortDedupPlan sort_and_dedup{.perform_sort = true, .perform_deduplicate = true,
                                       .announce_implicit_sort = false};

    if (mode == BenchMode::DedupOnly)
    {
        // Isolate unique/erase: start from an already-sorted copy (CPU-only phase).
        auto words = base;
        sort_and_deduplicate_words_cpu(words, sort_only);
        const auto start = Clock::now();
        words.erase(std::ranges::unique(words).begin(), words.end());
        return TimingResult{.ms = Ms(Clock::now() - start).count(), .out_words = words.size()};
    }

    auto words = base;
    const SortDedupPlan &plan = (mode == BenchMode::SortOnly) ? sort_only : sort_and_dedup;
    const auto start = Clock::now();
    apply_backend(words, plan, use_cuda);
    return TimingResult{.ms = Ms(Clock::now() - start).count(), .out_words = words.size()};
}

[[nodiscard]] double median(std::vector<double> samples)
{
    if (samples.empty())
        return 0.0;
    std::ranges::sort(samples);
    const std::size_t mid = samples.size() / 2;
    if (samples.size() % 2 == 0)
        return (samples[mid - 1] + samples[mid]) / 2.0;
    return samples[mid];
}

void report_mode(const BenchConfig &cfg, const std::size_t size, const bool cuda_active,
                 const BenchMode mode, std::vector<double> samples, const std::size_t out_words)
{
    const double med_ms = median(std::move(samples));
    const double words_per_s = med_ms > 0.0 ? (static_cast<double>(size) * 1000.0 / med_ms) : 0.0;
    const std::string_view backend = cuda_active ? "cuda" : "cpu";
    const char *name = mode_name(mode);

    if (cfg.csv)
    {
        std::println("{},{},{},{},{:.3f},{:.0f},{}", backend, name, size, out_words, med_ms, words_per_s,
                     cfg.iters);
        return;
    }

    std::println("{:<5} {:<5} {:>12} {:>12} {:>10.3f} ms {:>12.0f} words/s  (n={})", backend, name, size,
                 out_words, med_ms, words_per_s, cfg.iters);
}

void run_size(const BenchConfig &cfg, const std::size_t size, const bool cuda_active)
{
    const auto base = make_words(size, cfg.dup_ratio, cfg.seed);
    constexpr std::array modes{BenchMode::SortOnly, BenchMode::DedupOnly, BenchMode::Total};

    for (const BenchMode mode : modes)
    {
        const bool mode_cuda = cuda_active && mode != BenchMode::DedupOnly;

        for (int w = 0; w < cfg.warmup; ++w)
            (void)time_once(base, mode, mode_cuda);

        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(cfg.iters));
        std::size_t out_words = 0;
        for (int i = 0; i < cfg.iters; ++i)
        {
            const TimingResult result = time_once(base, mode, mode_cuda);
            samples.push_back(result.ms);
            out_words = result.out_words;
        }

        report_mode(cfg, size, mode_cuda, mode, std::move(samples), out_words);
    }
}

}

int main(int argc, char **argv)
{
    BenchConfig cfg;
    if (!parse_args(argc, argv, cfg))
    {
        print_usage(argv[0]);
        return 2;
    }

    bool cuda_active = false;
    if (cfg.use_cuda)
    {
        if (!cuda_sort_dedup_is_compiled())
        {
            std::println(stderr,
                         "Note: --cuda ignored (built without CUDA; reconfigure with -DWORDLIST_SORT_CUDA=ON).");
        }
        else if (!cuda_sort_dedup_runtime_available())
        {
            std::println(stderr, "Note: --cuda ignored (no usable CUDA device); measuring CPU.");
        }
        else
        {
            cuda_active = true;
        }
    }

    if (!cfg.csv)
    {
        std::println("wordlist_sort sort/dedup benchmark");
        std::println("backend={}  cuda_compiled={}  cuda_runtime={}  dup_ratio={:.2f}  seed={}",
                     cuda_active ? "cuda" : "cpu", cuda_sort_dedup_is_compiled(),
                     cuda_sort_dedup_runtime_available(), cfg.dup_ratio, cfg.seed);
        std::println("{:<5} {:<5} {:>12} {:>12} {:>13} {:>12}", "be", "mode", "in_words", "out_words", "median",
                     "throughput");
    }
    else
    {
        std::println("backend,mode,in_words,out_words,median_ms,words_per_s,iters");
    }

    for (const std::size_t size : cfg.sizes)
        run_size(cfg, size, cuda_active);

    return 0;
}
