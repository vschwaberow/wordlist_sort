// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/main.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2024 Volker Schwaberow

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <unordered_set>
#include <vector>
#include <regex>
#include <sstream>
#include <unordered_map>

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

  ~FileDescriptor()
  {
    if (fd_ != -1)
    {
      close(fd_);
    }
  }

  int get() const { return fd_; }

private:
  explicit FileDescriptor(int fd) : fd_(fd) {}
  int fd_;
};

class MemoryMapping
{
public:
  static std::unique_ptr<MemoryMapping> Create(int fd, size_t size, int prot, int flags)
  {
    void *data = mmap(nullptr, size, prot, flags, fd, 0);
    if (data == MAP_FAILED)
    {
      return nullptr;
    }
    return std::unique_ptr<MemoryMapping>(new MemoryMapping(data, size));
  }

  ~MemoryMapping()
  {
    if (data_ != MAP_FAILED)
    {
      munmap(data_, size_);
    }
  }

  void *get() const { return data_; }
  size_t get_size() const { return size_; }

private:
  MemoryMapping(void *data, size_t size) : data_(data), size_(size) {}
  void *data_;
  size_t size_;
};

class CompressedMemoryMappedFile
{
public:
  static std::unique_ptr<CompressedMemoryMappedFile> Create(const fs::path &path)
  {
    auto file = std::unique_ptr<CompressedMemoryMappedFile>(new CompressedMemoryMappedFile());
    if (!file->Initialize(path))
    {
      return nullptr;
    }
    return file;
  }

  const char *data() const { return data_; }
  size_t size() const { return size_; }

private:
  CompressedMemoryMappedFile() = default;

  bool Initialize(const fs::path &path)
  {
    fd_ = FileDescriptor::Create(path.c_str(), O_RDONLY);
    if (!fd_)
    {
      return false;
    }

    size_t file_size = fs::file_size(path);
    mapping_ = MemoryMapping::Create(fd_->get(), file_size, PROT_READ, MAP_PRIVATE);
    if (!mapping_)
    {
      return false;
    }

    data_ = static_cast<const char *>(mapping_->get());
    size_ = mapping_->get_size();
    return true;
  }

  std::unique_ptr<FileDescriptor> fd_;
  std::unique_ptr<MemoryMapping> mapping_;
  const char *data_ = nullptr;
  size_t size_ = 0;
};

std::string process_word(const std::string &word, const Options &options)
{
  std::string processed = word;

  if (options.lower)
  {
    std::transform(processed.begin(), processed.end(), processed.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });
  }

  if (options.digit_trim)
  {
    processed = std::regex_replace(processed, std::regex("^\\d+|\\d+$"), "");
  }

  if (options.special_trim)
  {
    processed = std::regex_replace(processed, std::regex("^[^a-zA-Z0-9]+|[^a-zA-Z0-9]+$"), "");
  }

  if (options.detab)
  {
    processed.erase(0, processed.find_first_not_of(" \t"));
  }

  if (options.maxtrim > 0 && processed.length() > static_cast<size_t>(options.maxtrim))
  {
    processed = processed.substr(0, options.maxtrim);
  }

  if (options.dup_remove)
  {
    std::string deduped;
    for (char c : processed)
    {
      if (deduped.empty() || c != deduped.back())
      {
        deduped += c;
      }
    }
    processed = deduped;
  }

  if (options.no_numbers && std::all_of(processed.begin(), processed.end(), ::isdigit))
  {
    return "";
  }

  if (options.hash_remove && std::regex_match(processed, std::regex("^[a-fA-F0-9]{32,}$")))
  {
    return "";
  }

  if (options.dup_sense > 0)
  {
    std::unordered_map<char, int> char_count;
    for (char c : processed)
    {
      char_count[c]++;
    }
    for (const auto &pair : char_count)
    {
      if (static_cast<double>(pair.second) / processed.length() > options.dup_sense / 100.0)
      {
        return "";
      }
    }
  }

  if (options.email_sort)
  {
    std::regex email_regex(R"(([^@\s]+)@([^@\s]+\.[^@\s]+))");
    std::smatch match;
    if (std::regex_match(processed, match, email_regex))
    {
      return match[1].str() + " " + match[2].str();
    }
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

  while (!file_content.empty())
  {
    auto line_end = file_content.find('\n');
    std::string_view line_view = file_content.substr(0, line_end);

    if (options.wordify)
    {
      std::string line_str(line_view);
      std::istringstream iss{line_str};
      std::string subword;
      while (iss >> subword)
      {
        std::string processed = process_word(subword, options);
        if (!processed.empty() &&
            (options.minlen == 0 || processed.length() >= static_cast<size_t>(options.minlen)) &&
            (options.maxlen == 0 || processed.length() <= static_cast<size_t>(options.maxlen)))
        {
          words.push_back(processed);
          total_words++;
        }
      }
    }
    else
    {
      std::string processed = process_word(std::string(line_view), options);
      if (!processed.empty() &&
          (options.minlen == 0 || processed.length() >= static_cast<size_t>(options.minlen)) &&
          (options.maxlen == 0 || processed.length() <= static_cast<size_t>(options.maxlen)))
      {
        words.push_back(processed);
        total_words++;
      }
    }

    if (line_end == std::string_view::npos)
    {
      break;
    }
    file_content.remove_prefix(line_end + 1);
  }

  return true;
}

bool process_multiple_files(const std::vector<fs::path> &paths, std::vector<std::string> &words, std::atomic<size_t> &total_words, const Options &options)
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
    if (!output->Initialize(path))
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

  bool Initialize(const fs::path &path)
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
  std::cout << PROGRAM_COPYRIGHT << " (" << BUILD_DATE << "-" << BUILD_TIME << "-" << BUILD_PLATFORM << "-" << COMPILER_INFO << ")" << std::endl;
}

int main(int argc, char *argv[])
{

  CLI::App app{PROGRAM_NAME};
  app.set_version_flag("--version", std::string(PROGRAM_VERSION) + " (" + BUILD_DATE + " " + BUILD_TIME + " " + BUILD_PLATFORM + ")");

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
  app.add_flag("--detab", options.detab, "Remove tabs or space from beginning of words");
  app.add_option("--dup-sense", options.dup_sense, "Remove word if more than <specified>% of characters are duplicates");
  app.add_flag("--hash-remove", options.hash_remove, "Filter out word candidates that are actually hashes");
  app.add_flag("--email-sort", options.email_sort, "Convert email addresses to username and domain as separate words");
  app.add_option("--email-split", options.email_split, "Extract email addresses to username and domain wordlists (format: user:domain)")
      ->expected(1);
  app.add_flag("--dewebify", options.dewebify, "Extract words from HTML input");
  app.add_flag("--noutf8", options.noutf8, "Only output non UTF-8 characters (works with --dewebify only)");
  app.add_flag("--sort", options.sort, "Sort the output words");
  app.add_flag("--deduplicate", options.deduplicate, "Remove duplicate words from the output");

  CLI11_PARSE(app, argc, argv);

  if (!options.email_split.empty())
  {
    size_t colon_pos = options.email_split.find(':');
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

  std::cout << PROGRAM_NAME << " version " << PROGRAM_VERSION << " (" << BUILD_DATE << " " << BUILD_TIME << " " << BUILD_PLATFORM << ")" << std::endl;
  std::cout << PROGRAM_COPYRIGHT << std::endl;
  std::cout << std::endl;

  auto start = std::chrono::high_resolution_clock::now();

  std::atomic<size_t> total_words(0);
  std::vector<std::string> words;

  if (!process_multiple_files(input_paths, words, total_words, options))
  {
    return 1;
  }

  if (options.sort)
  {
    std::sort(words.begin(), words.end());
  }

  if (options.deduplicate)
  {
    auto last = std::unique(words.begin(), words.end());
    words.erase(last, words.end());
  }

  if (!write_result_to_file(words, output_path))
  {
    std::cerr << "Error: Failed to write output file" << std::endl;
    return 1;
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Processed " << total_words << " total words (" << words.size() << " unique) in " << duration.count() << " ms" << std::endl;

  return 0;
}