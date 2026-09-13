// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: tests/e2e_cli_test.cc

#include "test_helpers.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#ifndef WORDLIST_SORT_EXE
#error "WORDLIST_SORT_EXE must be defined by CMake"
#endif

namespace fs = std::filesystem;

namespace
{

std::string wordlist_sort_exe()
{
    return WORDLIST_SORT_EXE;
}

std::string shell_quote(const std::string &value)
{
    return "'" + value + "'";
}

}

TEST(E2eCli, HelpExitsZero)
{
    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) + " --help");
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Usage:"), std::string::npos);
}

TEST(E2eCli, VersionExitsZero)
{
    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) + " --version");
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("wordlist_sort"), std::string::npos);
}

TEST(E2eCli, MissingInputArgumentFails)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path output = work / "out.txt";
    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) + " " + shell_quote(output.string()));
    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("Missing required argument"), std::string::npos);
}

TEST(E2eCli, SortAndDeduplicateOutput)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "banana\napple\ncherry\napple\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " --sort --deduplicate " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "apple\nbanana\ncherry\n");
}

TEST(E2eCli, MultiFileMerge)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input_a = work / "a.txt";
    const fs::path input_b = work / "b.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input_a, "beta\n");
    test_helpers::write_text_file(input_b, "alpha\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) + " --sort " +
                                                shell_quote(output.string()) + " " + shell_quote(input_a.string()) +
                                                " " + shell_quote(input_b.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "alpha\nbeta\n");
}

TEST(E2eCli, CudaFlagBehavior)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "b\na\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " --sort --deduplicate --cuda " + shell_quote(output.string()) +
                                                " " + shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);
#if defined(WORDLIST_SORT_CUDA)
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\n");
#else
    EXPECT_NE(result.stderr_text.find("built without CUDA support"), std::string::npos);
#endif
}

TEST(E2eCli, OutputFlagTreatsPositionalsAsInputs)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "z\na\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) + " --sort -o " +
                                                shell_quote(output.string()) + " " + shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nz\n");
}
