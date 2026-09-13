#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <string_view>

namespace test_helpers
{

inline std::string read_text_file(const std::filesystem::path &path)
{
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline std::filesystem::path make_temp_dir()
{
    const auto base = std::filesystem::temp_directory_path() / "wordlist_sort_test";
    std::filesystem::create_directories(base);
    return base / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

inline void write_text_file(const std::filesystem::path &path, const std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

struct ScopedStdoutCapture
{
    ScopedStdoutCapture()
    {
        testing::internal::CaptureStdout();
    }

    ~ScopedStdoutCapture()
    {
        captured = testing::internal::GetCapturedStdout();
    }

    std::string captured;
};

struct CommandResult
{
    int exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;
};

inline CommandResult run_command(const std::string &command)
{
    const auto stdout_path = std::filesystem::temp_directory_path() / "wordlist_sort_stdout.txt";
    const auto stderr_path = std::filesystem::temp_directory_path() / "wordlist_sort_stderr.txt";
    const std::string wrapped =
        command + " >" + stdout_path.string() + " 2>" + stderr_path.string();
    CommandResult result;
    result.exit_code = std::system(wrapped.c_str());
    result.stdout_text = read_text_file(stdout_path);
    result.stderr_text = read_text_file(stderr_path);
    return result;
}

}
