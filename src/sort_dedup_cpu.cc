// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/sort_dedup_cpu.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "sort_dedup.hpp"

#include <algorithm>
#include <system_error>
#include <thread>
#include <fstream>
#include <ostream>
#include <format>
#include <filesystem>
#include <queue>
#include <print>
#include <ranges>

namespace
{

[[nodiscard]] bool cuda_requested(const SortDedupOptions &options) noexcept
{
    return options.use_cuda && !options.no_cuda;
}

void print_cuda_not_compiled_note()
{
    std::println(stderr,
                 "Note: --cuda ignored (built without CUDA support; reconfigure with -DWORDLIST_SORT_CUDA=ON).");
}

void print_cuda_fallback_warning()
{
    std::println(stderr, "Warning: CUDA sort/dedup failed; falling back to CPU.");
}

void print_cuda_below_threshold_note(const std::size_t word_count, const std::size_t threshold)
{
    std::println(stderr,
                 "Note: --cuda ignored ({} words < threshold {}); use --cuda-threshold to lower or 0 for auto.",
                 word_count, threshold);
}

}

[[nodiscard]] SortDedupPlan make_sort_dedup_plan(const bool sort, const bool deduplicate) noexcept
{
    return SortDedupPlan{
        .perform_sort = sort || deduplicate,
        .perform_deduplicate = deduplicate,
        .announce_implicit_sort = deduplicate && !sort,
    };
}

void announce_implicit_sort_if_needed(const SortDedupPlan &plan)
{
    if (plan.announce_implicit_sort)
        std::println(stderr, "Note: Deduplication requires sorting. Words were sorted.");
}


void merge_sorted_word_runs(std::vector<std::vector<std::string>> runs,
                            const bool deduplicate,
                            std::vector<std::string> &out)
{
    out.clear();
    std::size_t total = 0;
    for (const auto &run : runs)
        total += run.size();
    out.reserve(total);

    struct Item
    {
        const std::string *word = nullptr;
        std::size_t run = 0;
        std::size_t index = 0;

        [[nodiscard]] bool operator>(const Item &other) const
        {
            const int cmp = word->compare(*other.word);
            if (cmp != 0)
                return cmp > 0; // greater<> => min-heap by word
            if (run != other.run)
                return run > other.run;
            return index > other.index;
        }
    };

    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
    for (std::size_t r = 0; r < runs.size(); ++r)
    {
        if (!runs[r].empty())
            heap.push(Item{.word = &runs[r][0], .run = r, .index = 0});
    }

    std::string last;
    bool have_last = false;
    while (!heap.empty())
    {
        const Item top = heap.top();
        heap.pop();

        if (!(deduplicate && have_last && *top.word == last))
        {
            out.push_back(*top.word);
            last = *top.word;
            have_last = true;
        }

        const std::size_t next = top.index + 1;
        if (next < runs[top.run].size())
            heap.push(Item{.word = &runs[top.run][next], .run = top.run, .index = next});
    }
}

void sort_and_deduplicate_words_cpu(std::vector<std::string> &words, const SortDedupPlan &plan)
{
    if (plan.perform_sort)
        std::ranges::sort(words);

    if (!plan.perform_deduplicate)
        return;

    announce_implicit_sort_if_needed(plan);
    words.erase(std::ranges::unique(words).begin(), words.end());
}

namespace
{

[[nodiscard]] std::filesystem::path resolve_tmp_dir(const std::filesystem::path &tmp_dir)
{
    return tmp_dir.empty() ? std::filesystem::temp_directory_path() : tmp_dir;
}

[[nodiscard]] std::filesystem::path make_run_temp_path(const std::size_t index,
                                                       const std::filesystem::path &tmp_dir)
{
    return resolve_tmp_dir(tmp_dir) /
           std::format("wordlist_sort_run_{}_{}.txt",
                       std::hash<std::thread::id>{}(std::this_thread::get_id()), index);
}

[[nodiscard]] bool write_run_file(const std::vector<std::string> &run_words,
                                  const std::filesystem::path &path)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    for (const auto &word : run_words)
        out << word << '\n';
    return static_cast<bool>(out);
}

struct RunReader
{
    std::ifstream in;
    std::string current;
    bool alive = false;

    explicit RunReader(const std::filesystem::path &path) : in(path, std::ios::binary)
    {
        alive = static_cast<bool>(in) && static_cast<bool>(std::getline(in, current));
        if (alive && !current.empty() && current.back() == '\r')
            current.pop_back();
    }

    [[nodiscard]] bool advance()
    {
        alive = static_cast<bool>(std::getline(in, current));
        if (alive && !current.empty() && current.back() == '\r')
            current.pop_back();
        return alive;
    }
};

}

void merge_run_files(const std::vector<std::filesystem::path> &run_paths,
                     const SortDedupPlan &plan,
                     std::vector<std::string> &out)
{
    out.clear();
    if (run_paths.empty())
        return;

    std::vector<RunReader> readers;
    readers.reserve(run_paths.size());
    for (const auto &path : run_paths)
        readers.emplace_back(path);

    struct Item
    {
        std::size_t run = 0;
    };

    auto cmp = [&readers](const Item &a, const Item &b) {
        const int c = readers[a.run].current.compare(readers[b.run].current);
        if (c != 0)
            return c > 0;
        return a.run > b.run;
    };
    std::priority_queue<Item, std::vector<Item>, decltype(cmp)> heap(cmp);

    for (std::size_t r = 0; r < readers.size(); ++r)
    {
        if (readers[r].alive)
            heap.push(Item{.run = r});
    }

    std::string last;
    bool have_last = false;
    while (!heap.empty())
    {
        const Item top = heap.top();
        heap.pop();
        auto &reader = readers[top.run];
        if (!(plan.perform_deduplicate && have_last && reader.current == last))
        {
            out.push_back(reader.current);
            last = reader.current;
            have_last = true;
        }
        if (reader.advance())
            heap.push(Item{.run = top.run});
    }
}

[[nodiscard]] std::size_t merge_run_files_to_stream(const std::vector<std::filesystem::path> &run_paths,
                                                    const SortDedupPlan &plan,
                                                    std::ostream &out)
{
    if (run_paths.empty())
        return 0;

    std::vector<RunReader> readers;
    readers.reserve(run_paths.size());
    for (const auto &path : run_paths)
        readers.emplace_back(path);

    struct Item
    {
        std::size_t run = 0;
    };

    auto cmp = [&readers](const Item &a, const Item &b) {
        const int c = readers[a.run].current.compare(readers[b.run].current);
        if (c != 0)
            return c > 0;
        return a.run > b.run;
    };
    std::priority_queue<Item, std::vector<Item>, decltype(cmp)> heap(cmp);

    for (std::size_t r = 0; r < readers.size(); ++r)
    {
        if (readers[r].alive)
            heap.push(Item{.run = r});
    }

    std::size_t written = 0;
    std::string last;
    bool have_last = false;
    while (!heap.empty())
    {
        const Item top = heap.top();
        heap.pop();
        auto &reader = readers[top.run];
        if (!(plan.perform_deduplicate && have_last && reader.current == last))
        {
            out << reader.current << '\n';
            last = reader.current;
            have_last = true;
            ++written;
        }
        if (reader.advance())
            heap.push(Item{.run = top.run});
    }
    return written;
}

void remove_run_files(const std::vector<std::filesystem::path> &run_paths)
{
    for (const auto &path : run_paths)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

void sort_and_deduplicate_words_external(std::vector<std::string> &words,
                                         const SortDedupPlan &plan,
                                         const std::size_t chunk_words,
                                         const bool quiet,
                                         const std::filesystem::path &tmp_dir)
{
    if (chunk_words == 0 || words.size() <= chunk_words)
    {
        sort_and_deduplicate_words_cpu(words, plan);
        return;
    }

    announce_implicit_sort_if_needed(plan);

    const SortDedupPlan chunk_plan{.perform_sort = true,
                                   .perform_deduplicate = plan.perform_deduplicate,
                                   .announce_implicit_sort = false};

    std::vector<std::filesystem::path> run_paths;
    run_paths.reserve((words.size() + chunk_words - 1) / chunk_words);

    for (std::size_t offset = 0; offset < words.size(); offset += chunk_words)
    {
        const std::size_t end = std::min(offset + chunk_words, words.size());
        std::vector<std::string> chunk(words.begin() + static_cast<std::ptrdiff_t>(offset),
                                       words.begin() + static_cast<std::ptrdiff_t>(end));
        sort_and_deduplicate_words_cpu(chunk, chunk_plan);

        const auto path = make_run_temp_path(run_paths.size(), tmp_dir);
        if (!write_run_file(chunk, path))
        {
            remove_run_files(run_paths);
            std::println(stderr, "Warning: external sort failed to write a run file; using in-memory sort.");
            sort_and_deduplicate_words_cpu(words, plan);
            return;
        }
        run_paths.push_back(path);
    }

    words.clear();
    words.shrink_to_fit();
    merge_run_files(run_paths, plan, words);
    remove_run_files(run_paths);

    if (!quiet)
        std::println("External sort: {} run file(s), chunk={}, resulting words={}.",
                     run_paths.size(), chunk_words, words.size());
}

ExternalSortBuilder::ExternalSortBuilder(const SortDedupPlan plan, const std::size_t chunk_words,
                                           const bool quiet, std::filesystem::path tmp_dir)
    : plan_(plan), chunk_words_(chunk_words == 0 ? 1 : chunk_words), quiet_(quiet),
      tmp_dir_(std::move(tmp_dir))
{
    buffer_.reserve(chunk_words_);
}

void ExternalSortBuilder::flush_unlocked()
{
    if (buffer_.empty())
        return;

    const SortDedupPlan chunk_plan{.perform_sort = true,
                                   .perform_deduplicate = plan_.perform_deduplicate,
                                   .announce_implicit_sort = false};
    sort_and_deduplicate_words_cpu(buffer_, chunk_plan);

    const auto path = make_run_temp_path(run_paths_.size(), tmp_dir_);
    if (!write_run_file(buffer_, path))
    {
        std::println(stderr, "Warning: external sort builder failed to write a run; keeping words in memory.");
        // Leave buffer in place; finish() will fall back.
        return;
    }
    run_paths_.push_back(path.string());
    buffer_.clear();
}

void ExternalSortBuilder::push(std::string word)
{
    std::lock_guard lock(mutex_);
    ++pushed_;
    buffer_.push_back(std::move(word));
    if (buffer_.size() >= chunk_words_)
        flush_unlocked();
}

void ExternalSortBuilder::finish(std::vector<std::string> &out)
{
    std::lock_guard lock(mutex_);
    announce_implicit_sort_if_needed(plan_);

    if (run_paths_.empty())
    {
        // Never spilled — plain in-memory path.
        out = std::move(buffer_);
        buffer_.clear();
        sort_and_deduplicate_words_cpu(out, plan_);
        return;
    }

    if (!buffer_.empty())
        flush_unlocked();

    // If a flush failed and left a non-empty buffer, fold it into out after merge.
    std::vector<std::filesystem::path> paths;
    paths.reserve(run_paths_.size());
    for (const auto &p : run_paths_)
        paths.emplace_back(p);

    merge_run_files(paths, plan_, out);
    remove_run_files(paths);

    if (!buffer_.empty())
    {
        sort_and_deduplicate_words_cpu(buffer_, plan_);
        out.insert(out.end(), std::make_move_iterator(buffer_.begin()),
                   std::make_move_iterator(buffer_.end()));
        buffer_.clear();
        sort_and_deduplicate_words_cpu(out, plan_);
    }

    if (!quiet_)
        std::println("External sort (ingest flush): {} run file(s), chunk={}, pushed={}, resulting words={}.",
                     paths.size(), chunk_words_, pushed_, out.size());
    run_paths_.clear();
}

std::size_t ExternalSortBuilder::finish_to_stream(std::ostream &out)
{
    std::lock_guard lock(mutex_);
    announce_implicit_sort_if_needed(plan_);

    if (run_paths_.empty())
    {
        sort_and_deduplicate_words_cpu(buffer_, plan_);
        for (const auto &word : buffer_)
            out << word << '\n';
        const auto n = buffer_.size();
        buffer_.clear();
        if (!quiet_)
            std::println("External sort (ingest flush → stream): 0 run file(s), chunk={}, pushed={}, resulting words={}.",
                         chunk_words_, pushed_, n);
        return n;
    }

    if (!buffer_.empty())
        flush_unlocked();

    std::vector<std::filesystem::path> paths;
    paths.reserve(run_paths_.size());
    for (const auto &p : run_paths_)
        paths.emplace_back(p);

    std::size_t written = merge_run_files_to_stream(paths, plan_, out);
    remove_run_files(paths);

    if (!buffer_.empty())
    {
        sort_and_deduplicate_words_cpu(buffer_, plan_);
        for (const auto &word : buffer_)
        {
            out << word << '\n';
            ++written;
        }
        buffer_.clear();
    }

    if (!quiet_)
        std::println("External sort (ingest flush → stream): {} run file(s), chunk={}, pushed={}, resulting words={}.",
                     paths.size(), chunk_words_, pushed_, written);
    run_paths_.clear();
    return written;
}

void sort_and_deduplicate_words(std::vector<std::string> &words, const SortDedupOptions &options)
{
    const SortDedupPlan plan = make_sort_dedup_plan(options.sort, options.deduplicate);
    if (!plan.perform_sort && !plan.perform_deduplicate)
        return;

    if (cuda_requested(options))
    {
        if (!cuda_sort_dedup_is_compiled())
        {
            print_cuda_not_compiled_note();
        }
        else if (!cuda_sort_dedup_runtime_available())
        {
            std::println(stderr, "Note: --cuda ignored (no usable CUDA device); using CPU.");
        }
        else
        {
            const std::size_t threshold = resolve_cuda_word_threshold(options.cuda_threshold);

            if (words.size() < threshold)
            {
                print_cuda_below_threshold_note(words.size(), threshold);
            }
            else if (try_sort_and_deduplicate_words_cuda(words, plan, options.cuda_timing))
            {
                return;
            }
            else
            {
                print_cuda_fallback_warning();
            }
        }
    }

    if (options.sort_chunk > 0 && words.size() > options.sort_chunk)
        sort_and_deduplicate_words_external(words, plan, options.sort_chunk, options.quiet,
                                           options.tmp_dir);
    else
        sort_and_deduplicate_words_cpu(words, plan);
}
