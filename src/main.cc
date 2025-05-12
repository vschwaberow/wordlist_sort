// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/main.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2024 Volker Schwaberow

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype> 
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>
#include <array>   
#include <future>  
#include <thread>  

#include <CLI/CLI.hpp>

inline constexpr const char *PROGRAM_NAME = PROJECT_NAME;
inline constexpr const char *PROGRAM_VERSION = PROJECT_VERSION;
inline constexpr const char *PROGRAM_AUTHOR = PROJECT_AUTHOR;
inline constexpr const char *PROGRAM_COPYRIGHT = PROJECT_COPYRIGHT;
inline constexpr const char *BUILD_DATE = __DATE__;
inline constexpr const char *BUILD_TIME = __TIME__;
inline constexpr const char *BUILD_PLATFORM = BUILD_PLATFORM_INFO;
inline constexpr const char *COMPILER_INFO = COMPILER_INFO_STRING;

namespace fs = std::filesystem;

struct Options
{
    int maxlen = 0;
    int maxtrim = 0;
    bool digit_trim = false;
    bool special_trim = false;
    bool dup_remove = false;
    bool no_sentence = false; 
    bool lower = false;
    bool wordify = false;
    bool no_numbers = false;
    int minlen = 0;
    bool detab = false;
    int dup_sense = 0;
    bool hash_remove = false;
    bool email_sort = false;
    std::string email_split;
    std::string email_split_user;     
    std::string email_split_domain; 
    bool dewebify = false;
    bool noutf8 = false;
    bool sort = false;
    bool deduplicate = false;
};

class FileBuffer
{
public:
    static std::unique_ptr<FileBuffer> Create(const fs::path &path)
    {
        try
        {
            return std::unique_ptr<FileBuffer>(new FileBuffer(path));
        }
        catch (const std::exception &e)
        {
            std::cerr << "FileBuffer Error: " << e.what() << " for path: " << path << std::endl;
            return nullptr;
        }
    }

    const char *data() const { return file_contents_.data(); }
    std::size_t size() const { return file_contents_.size(); }

private:
    explicit FileBuffer(const fs::path &path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            throw std::runtime_error("Unable to open file: " + path.string());
        }

        std::streamsize size = file.tellg();
        if (size < 0) {
             throw std::runtime_error("Unable to determine file size or file is empty: " + path.string());
        }
        file.seekg(0, std::ios::beg);
        file_contents_.resize(static_cast<size_t>(size));
        if (size > 0 && !file.read(file_contents_.data(), size))
        {
            throw std::runtime_error("Unable to read file: " + path.string());
        }
    }
    std::vector<char> file_contents_;
};


class BufferedFile
{
public:
    static std::unique_ptr<BufferedFile> Create(const fs::path &path)
    {
        auto file = std::make_unique<BufferedFile>();
        if (!file->initialize(path))
        {
            return nullptr;
        }
        return file;
    }
    const char *data() const { return buffer_ ? buffer_->data() : nullptr; }
    std::size_t size() const { return buffer_ ? buffer_->size() : 0; }

    BufferedFile() = default;

private:
    bool initialize(const fs::path &path)
    {
        buffer_ = FileBuffer::Create(path);
        return buffer_ != nullptr;
    }

    std::unique_ptr<FileBuffer> buffer_;
};

std::string strip_html_tags(const std::string &html)
{
    std::string result;
    result.reserve(html.size());
    bool in_tag = false;
    for (char c : html)
    {
        if (c == '<')
        {
            in_tag = true;
        }
        else if (c == '>')
        {
            in_tag = false;
        }
        else if (!in_tag)
        {
            result.push_back(c);
        }
    }
    return result;
}

constexpr bool is_digit_char(char c) noexcept { return c >= '0' && c <= '9'; }

constexpr bool is_alpha_char(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr bool is_alnum_char(char c) noexcept { return is_alpha_char(c) || is_digit_char(c); }


void trim_digits_inplace(std::string &str)
{
    if (str.empty()) return;
    const auto is_not_digit = [](char ch) noexcept { return !is_digit_char(ch); };
    
    auto start_it = std::find_if(str.begin(), str.end(), is_not_digit);
    str.erase(str.begin(), start_it);

    if (str.empty()) return;

    auto end_it = std::find_if(str.rbegin(), str.rend(), is_not_digit).base();
    str.erase(end_it, str.end());
}


void trim_special_inplace(std::string &str)
{
    if (str.empty()) return;
    const auto is_alnum_fn = [](char ch) noexcept { return is_alnum_char(ch); };
    
    auto start_it = std::find_if(str.begin(), str.end(), is_alnum_fn);
    str.erase(str.begin(), start_it); 

    if (str.empty()) return;
    
    auto end_it = std::find_if(str.rbegin(), str.rend(), is_alnum_fn).base();
    str.erase(end_it, str.end()); 
}


bool is_valid_email(const std::string &str)
{
    const auto at_pos = str.find('@');
    if (at_pos == std::string::npos || at_pos == 0 || at_pos == str.length() - 1)
    {
        return false;
    }
    const auto dot_pos = str.find('.', at_pos + 1);
    return (dot_pos != std::string::npos && dot_pos > at_pos + 1 && dot_pos < str.length() - 1);
}

std::pair<std::string, std::string> split_email(const std::string &email)
{
    const auto at_pos = email.find('@');
    if (at_pos == std::string::npos) { 
        return {email, ""};
    }
    return {email.substr(0, at_pos), email.substr(at_pos + 1)};
}

std::string process_word(const std::string &word, const Options &options)
{
    std::string processed = word;

    if (options.dewebify)
    {
        processed = strip_html_tags(processed);
    }

    if (options.lower)
    {
        std::transform(processed.begin(), processed.end(), processed.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
    }

    if (options.digit_trim)
    {
        trim_digits_inplace(processed);
    }

    if (options.special_trim)
    {
        trim_special_inplace(processed);
    }

    if (options.detab)
    {
        const auto first_non_space = processed.find_first_not_of(" \t");
        processed = (first_non_space != std::string::npos) ? processed.substr(first_non_space)
                                                           : std::string{};
    }

    if (options.maxtrim > 0 && processed.size() > static_cast<std::size_t>(options.maxtrim))
    {
        processed.resize(options.maxtrim);
    }

    if (options.dup_remove) 
    {
        processed.erase(std::unique(processed.begin(), processed.end()), processed.end());
    }

    if (options.no_numbers && !processed.empty() && std::all_of(processed.begin(), processed.end(), is_digit_char))
    {
        return "";
    }

    if (options.hash_remove && processed.size() >= 32) 
    {
        const auto is_hex = [](char c) noexcept
        {
            return is_digit_char(c) ||
                   ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
        };
        if (std::all_of(processed.begin(), processed.end(), is_hex))
        {
            return "";
        }
    }

    if (options.dup_sense > 0 && !processed.empty())
    {
        const double ratio_threshold = static_cast<double>(options.dup_sense) / 100.0;
        std::array<unsigned int, 256> char_counts{}; 
        for (unsigned char c : processed)
        {
            char_counts[c]++;
        }

        bool reject = false;
        for (unsigned int i = 0; i < 256; ++i)
        {
            if (char_counts[i] == 0) continue; 

            if (static_cast<double>(char_counts[i]) / processed.length() > ratio_threshold)
            {
                reject = true;
                break;
            }
        }
        if (reject)
        {
            return "";
        }
    }

    if (options.email_sort && is_valid_email(processed))
    {
        const auto [username, domain] = split_email(processed);
        return username + " " + domain; 
    }

    return processed;
}

[[nodiscard]] bool process_file(const fs::path &path, std::vector<std::string> &output_words,
                                std::atomic<size_t> &total_words_processed_counter, const Options &options)
{
    auto file = BufferedFile::Create(path);
    if (!file)
    {
        std::cerr << "Error: Failed to open file: " << path << std::endl;
        return false;
    }

    std::string_view file_content(file->data(), file->size());

    auto try_add_word = [&](const std::string &candidate)
    {
        std::string processed = process_word(candidate, options);
        if (!processed.empty() &&
            (options.minlen == 0 || processed.size() >= static_cast<size_t>(options.minlen)) &&
            (options.maxlen == 0 || processed.size() <= static_cast<size_t>(options.maxlen)))
        {
            output_words.push_back(processed);
            total_words_processed_counter++; 
        }
    };

    for (auto line_range : file_content | std::views::split('\n'))
    {
        
        std::string line_str(line_range.begin(), line_range.end());


        if (options.dewebify)
        {
            line_str = strip_html_tags(line_str);
            if (options.noutf8)
            {
                line_str.erase(
                    std::remove_if(line_str.begin(), line_str.end(),
                                   [](unsigned char c)
                                   { return c > 127; }), 
                    line_str.end());
            }
        }

        if (options.wordify)
        {
            std::istringstream iss{line_str};
            std::string subword;
            while (iss >> subword)
            {
                try_add_word(subword);
            }
        }
        else
        {
            try_add_word(line_str);
        }
    }
    return true;
}


bool process_multiple_files_parallel(const std::vector<fs::path> &paths, std::vector<std::string> &words,
                                     std::atomic<size_t> &total_words, const Options &options)
{
    std::vector<std::future<std::pair<std::vector<std::string>, bool>>> futures;
    futures.reserve(paths.size());

    for (const auto &path : paths)
    {
        futures.emplace_back(
            std::async(std::launch::async,
                       
                       [&options, path, &total_words]() -> std::pair<std::vector<std::string>, bool> {
                           std::vector<std::string> local_task_words;
                           
                           bool success = process_file(path, local_task_words, total_words, options);
                           return {std::move(local_task_words), success};
                       }));
    }

    bool all_tasks_successful = true;

    std::vector<std::vector<std::string>> results_from_tasks;
    results_from_tasks.reserve(futures.size());

    for (auto &fut : futures)
    {
        std::pair<std::vector<std::string>, bool> result = fut.get();
        if (!result.second)
        {
            all_tasks_successful = false;
            std::cerr << "Error: Failed to process file." << std::endl;
        }
        results_from_tasks.push_back(std::move(result.first));
    }
    
    size_t total_elements_to_insert = 0;
    for(const auto& task_result_vec : results_from_tasks) {
        total_elements_to_insert += task_result_vec.size();
    }
    words.reserve(words.size() + total_elements_to_insert); 

    
    for (auto &task_result_vec : results_from_tasks)
    {
        words.insert(words.end(),
                     std::make_move_iterator(task_result_vec.begin()),
                     std::make_move_iterator(task_result_vec.end()));
    }

    return all_tasks_successful;
}

class OutputFile
{
public:
    static std::unique_ptr<OutputFile> Create(const fs::path &path)
    {
        auto output = std::unique_ptr<OutputFile>(new OutputFile());
        if (!output->initialize(path))
        {
            return nullptr;
        }
        return output;
    }

    bool Write(const std::string &str)
    {
        file_ << str << '\n';
        return !file_.fail();
    }

private:
    OutputFile() = default;

    bool initialize(const fs::path &path)
    {
        file_.open(path);
        if (!file_.is_open()) {
            std::cerr << "Error: Failed to open output file for writing: " << path << std::endl;
            return false;
        }
        return true;
    }

    std::ofstream file_;
};

bool write_result_to_file(const std::vector<std::string> &words, const fs::path &output_path)
{
    auto output = OutputFile::Create(output_path);
    if (!output)
    {
        
        return false;
    }
    for (const auto &word : words)
    {
        if (!output->Write(word))
        {
            std::cerr << "Error: Failed to write to output file: " << output_path << std::endl;
            return false;
        }
    }
    return true;
}

void print_header()
{
    
    std::cout << PROGRAM_NAME << " " << PROGRAM_VERSION << " by " << PROGRAM_AUTHOR << std::endl;
    std::cout << PROGRAM_COPYRIGHT << " (" << BUILD_DATE << "-" << BUILD_TIME << "-"
              << BUILD_PLATFORM << "-" << COMPILER_INFO << ")" << std::endl;
}

int main(int argc, char *argv[])
{
    std::ios_base::sync_with_stdio(false); 
    std::cin.tie(nullptr);

    CLI::App app{PROGRAM_NAME};
    app.set_version_flag("--version",
                         std::string(PROGRAM_VERSION) + " (" + BUILD_DATE + " " + BUILD_TIME + " " +
                             BUILD_PLATFORM + ")");

    Options options{};
    fs::path output_path;
    std::vector<fs::path> input_paths;

    app.add_option("output", output_path, "Output file path")->required();
    app.add_option("input", input_paths, "Input file paths")->required()->expected(-1);

    app.add_option("--maxlen", options.maxlen, "Filter out words over a certain max length (chars)");
    app.add_option("--maxtrim", options.maxtrim, "Trim words over a certain max length (chars)");
    app.add_flag("--digit-trim", options.digit_trim, "Trim all digits from beginning and end of words");
    app.add_flag("--special-trim", options.special_trim, "Trim non-alphanumeric chars from beginning and end of words");
    app.add_flag("--dup-remove", options.dup_remove, "Remove consecutive duplicate characters within words");
    app.add_flag("--no-sentence", options.no_sentence, "Remove all spaces between words (Note: check implementation details)");
    app.add_flag("--lower", options.lower, "Change word to all lower case");
    app.add_flag("--wordify", options.wordify, "Convert all input lines/sentences into separate words based on whitespace");
    app.add_flag("--no-numbers", options.no_numbers, "Ignore/delete words that are composed entirely of digits");
    app.add_option("--minlen", options.minlen, "Filter out words below a certain min length (chars)");
    app.add_flag("--detab", options.detab, "Remove leading tabs or spaces from words/lines");
    app.add_option("--dup-sense", options.dup_sense, "Remove word if any single char is more than <N>% of the word (0-100)");
    app.add_flag("--hash-remove", options.hash_remove, "Filter out word candidates that appear to be hex hashes (>=32 hex chars)");
    app.add_flag("--email-sort", options.email_sort, "Convert 'user@domain.com' to 'user domain' output");
    app.add_option("--email-split", options.email_split,
                   "Extract email addresses to username and domain wordlists (format: user:domain) (Note: Feature incomplete, parsed but not used for file output)")
        ->expected(1);
    app.add_flag("--dewebify", options.dewebify, "Extract text from HTML input (strips tags)");
    app.add_flag("--noutf8", options.noutf8, "Process to keep only ASCII characters (0-127) (works with --dewebify only contextually, but applied to line after dewebify if both active)");
    app.add_flag("--sort", options.sort, "Sort the output words lexicographically");
    app.add_flag("--deduplicate", options.deduplicate, "Remove duplicate words from the final output list");

    CLI11_PARSE(app, argc, argv);

    if (!options.email_split.empty())
    {
        const auto colon_pos = options.email_split.find(':');
        if (colon_pos != std::string::npos && colon_pos > 0 && colon_pos < options.email_split.length() -1)
        {
            options.email_split_user = options.email_split.substr(0, colon_pos);
            options.email_split_domain = options.email_split.substr(colon_pos + 1);            
        }
        else
        {
            std::cerr << "Error: Invalid format for --email-split. Expected format: user_output_file:domain_output_file" << std::endl;
            return 1;
        }
    }
    
    std::cout << PROGRAM_NAME << " version " << PROGRAM_VERSION << " (" << BUILD_DATE << " " << BUILD_TIME
              << " " << BUILD_PLATFORM << ")" << std::endl;
    std::cout << PROGRAM_COPYRIGHT << std::endl << std::endl;
    
    const auto start_time = std::chrono::high_resolution_clock::now();
    std::atomic<size_t> total_words_processed{0}; 
    std::vector<std::string> words;

    if (!process_multiple_files_parallel(input_paths, words, total_words_processed, options))
    {
        
        std::cerr << "Warning: One or more files may have failed to process completely." << std::endl;  
    }

    if (options.sort)
    {
        std::ranges::sort(words);
    }

    if (options.deduplicate)
    {
        if (!options.sort) { 
            std::ranges::sort(words); 
            std::cout << "Note: Deduplication requires sorting. Words were sorted." << std::endl;
        }
        words.erase(std::unique(words.begin(), words.end()), words.end());
    }

    if (!write_result_to_file(words, output_path))
    {
        
        return 1;
    }

    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Processed " << total_words_processed.load() << " words from input files, resulting in "
              << words.size() << " words in the output list, in "
              << duration.count() << " ms." << std::endl;

    return 0;
}