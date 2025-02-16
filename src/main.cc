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
#include <fcntl.h>
#include <unistd.h>

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

class FileDescriptor
{
public:
  static std::unique_ptr<FileDescriptor> Create(const char *path, int flags)
  {
    int file_descriptor = open(path, flags);
    if (file_descriptor == -1)
    {
      return nullptr;
    }
    return std::unique_ptr<FileDescriptor>(new FileDescriptor(file_descriptor));
  }

  ~FileDescriptor() noexcept
  {
    if (fd_ != -1)
    {
      close(fd_);
    }
  }

  int get() const { return fd_; }

private:
  explicit FileDescriptor(int fd) : fd_{fd} {}
  int fd_;
};

class MemoryMapping
{
public:
  static std::unique_ptr<MemoryMapping> Create(const fs::path &path)
  {
    try
    {
      return std::unique_ptr<MemoryMapping>(new MemoryMapping(path));
    }
    catch (const std::exception &)
    {
      return nullptr;
    }
  }

  const char *data() const { return file_contents_.data(); }
  std::size_t size() const { return file_contents_.size(); }

private:
  explicit MemoryMapping(const fs::path &path)
  {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
      throw std::runtime_error("Unable to open file");
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    file_contents_.resize(static_cast<size_t>(size));
    if (!file.read(file_contents_.data(), size))
    {
      throw std::runtime_error("Unable to read file");
    }
  }

  std::vector<char> file_contents_;
};

class CompressedMemoryMappedFile
{
public:
  static std::unique_ptr<CompressedMemoryMappedFile> Create(const fs::path &path)
  {
    auto file = std::make_unique<CompressedMemoryMappedFile>();
    if (!file->initialize(path))
    {
      return nullptr;
    }
    return file;
  }

  const char *data() const { return mapping_ ? mapping_->data() : nullptr; }
  std::size_t size() const { return mapping_ ? mapping_->size() : 0; }

  CompressedMemoryMappedFile() = default;

private:
  bool initialize(const fs::path &path)
  {
    mapping_ = MemoryMapping::Create(path);
    return mapping_ != nullptr;
  }

  std::unique_ptr<MemoryMapping> mapping_;
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

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

constexpr bool is_alpha(char c) noexcept
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr bool is_alnum(char c) noexcept { return is_alpha(c) || is_digit(c); }

std::string trim_digits(const std::string &str)
{
  const auto not_digit = [](char ch) noexcept
  { return !is_digit(ch); };
  auto start_it = std::find_if(str.begin(), str.end(), not_digit);
  auto end_it = std::find_if(str.rbegin(), str.rend(), not_digit).base();
  return (start_it < end_it ? std::string(start_it, end_it) : std::string{});
}

std::string trim_special(const std::string &str)
{
  const auto is_alnum_fn = [](char ch) noexcept
  { return is_alnum(ch); };
  auto start_it = std::find_if(str.begin(), str.end(), is_alnum_fn);
  auto end_it = std::find_if(str.rbegin(), str.rend(), is_alnum_fn).base();
  return (start_it < end_it ? std::string(start_it, end_it) : std::string{});
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
    processed = trim_digits(processed);
  }

  if (options.special_trim)
  {
    processed = trim_special(processed);
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

  if (options.no_numbers && std::all_of(processed.begin(), processed.end(), is_digit))
  {
    return "";
  }

  if (options.hash_remove && processed.size() >= 32)
  {
    const auto is_hex = [](char c) noexcept
    {
      return is_digit(c) ||
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
    for (char c : processed)
    {
      const auto count_c = std::ranges::count(processed, c);
      if (static_cast<double>(count_c) / processed.size() > ratio_threshold)
      {
        return "";
      }
    }
  }

  if (options.email_sort && is_valid_email(processed))
  {
    const auto [username, domain] = split_email(processed);
    return username + " " + domain;
  }

  return processed;
}

[[nodiscard]] bool process_file(const fs::path &path, std::vector<std::string> &words,
                                std::atomic<size_t> &total_words, const Options &options)
{
  auto file = CompressedMemoryMappedFile::Create(path);
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
      words.push_back(processed);
      total_words++;
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
                           { return c <= 127; }),
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

bool process_multiple_files(const std::vector<fs::path> &paths, std::vector<std::string> &words,
                            std::atomic<size_t> &total_words, const Options &options)
{
  for (const auto &path : paths)
  {
    if (!process_file(path, words, total_words, options))
    {
      return false;
    }
  }
  return true;
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
    return file_.is_open();
  }

  std::ofstream file_;
};

bool write_result_to_file(const std::vector<std::string> &words, const fs::path &output_path)
{
  auto output = OutputFile::Create(output_path);
  if (!output)
  {
    std::cerr << "Error: Failed to open output file: " << output_path << std::endl;
    return false;
  }
  for (const auto &word : words)
  {
    if (!output->Write(word))
    {
      std::cerr << "Error: Failed to write to output file" << std::endl;
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
  CLI::App app{PROGRAM_NAME};
  app.set_version_flag("--version",
                       std::string(PROGRAM_VERSION) + " (" + BUILD_DATE + " " + BUILD_TIME + " " +
                           BUILD_PLATFORM + ")");

  Options options{};
  fs::path output_path;
  std::vector<fs::path> input_paths;

  app.add_option("output", output_path, "Output file path")->required();
  app.add_option("input", input_paths, "Input file paths")->required()->expected(-1);

  app.add_option("--maxlen", options.maxlen, "Filter out words over a certain max length");
  app.add_option("--maxtrim", options.maxtrim, "Trim words over a certain max length");
  app.add_flag("--digit-trim", options.digit_trim, "Trim all digits from beginning and end of words");
  app.add_flag("--special-trim", options.special_trim, "Trim all special characters from beginning and end of words");
  app.add_flag("--dup-remove", options.dup_remove, "Remove duplicate characters within words");
  app.add_flag("--no-sentence", options.no_sentence, "Remove all spaces between words");
  app.add_flag("--lower", options.lower, "Change word to all lower case");
  app.add_flag("--wordify", options.wordify, "Convert all input sentences into separate words");
  app.add_flag("--no-numbers", options.no_numbers, "Ignore/delete words that are all numeric");
  app.add_option("--minlen", options.minlen, "Filter out words below a certain min length");
  app.add_flag("--detab", options.detab, "Remove tabs or spaces from beginning of words");
  app.add_option("--dup-sense", options.dup_sense, "Remove word if more than <specified>% of characters are duplicates");
  app.add_flag("--hash-remove", options.hash_remove, "Filter out word candidates that are actually hashes");
  app.add_flag("--email-sort", options.email_sort, "Convert email addresses to username and domain as separate words");
  app.add_option("--email-split", options.email_split,
                 "Extract email addresses to username and domain wordlists (format: user:domain)")
      ->expected(1);
  app.add_flag("--dewebify", options.dewebify, "Extract words from HTML input");
  app.add_flag("--noutf8", options.noutf8, "Only output non UTF-8 characters (works with --dewebify only)");
  app.add_flag("--sort", options.sort, "Sort the output words");
  app.add_flag("--deduplicate", options.deduplicate, "Remove duplicate words from the output");

  CLI11_PARSE(app, argc, argv);

  if (!options.email_split.empty())
  {
    const auto colon_pos = options.email_split.find(':');
    if (colon_pos != std::string::npos)
    {
      options.email_split_user = options.email_split.substr(0, colon_pos);
      options.email_split_domain = options.email_split.substr(colon_pos + 1);
    }
    else
    {
      std::cerr << "Error: Invalid format for --email-split. Expected format: user:domain" << std::endl;
      return 1;
    }
  }

  std::cout << PROGRAM_NAME << " version " << PROGRAM_VERSION << " (" << BUILD_DATE << " " << BUILD_TIME
            << " " << BUILD_PLATFORM << ")" << std::endl;
  std::cout << PROGRAM_COPYRIGHT << std::endl
            << std::endl;

  print_header();

  const auto start = std::chrono::high_resolution_clock::now();
  std::atomic<size_t> total_words{0};
  std::vector<std::string> words;

  if (!process_multiple_files(input_paths, words, total_words, options))
  {
    return 1;
  }

  if (options.sort)
  {
    std::ranges::sort(words);
  }

  if (options.deduplicate)
  {
    words.erase(std::unique(words.begin(), words.end()), words.end());
  }

  if (!write_result_to_file(words, output_path))
  {
    std::cerr << "Error: Failed to write output file" << std::endl;
    return 1;
  }

  const auto end = std::chrono::high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "Processed " << total_words << " total words (" << words.size() << " unique) in "
            << duration.count() << " ms" << std::endl;

  return 0;
}