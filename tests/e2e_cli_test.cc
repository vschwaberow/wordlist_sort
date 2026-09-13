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

TEST(E2eCli, DeduplicateImpliesSort)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "b\na\nb\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " --deduplicate " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\n");
    EXPECT_NE(result.stderr_text.find("implies --sort"), std::string::npos);
}

TEST(E2eCli, QuietSuppressesBanner)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "a\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) + " -q " +
                                                shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "a\n");
    EXPECT_EQ(result.stdout_text.find("version"), std::string::npos);
    EXPECT_EQ(result.stdout_text.find("Processed"), std::string::npos);
}

TEST(E2eCli, TmpDirExternalSort)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    const fs::path tmp = work / "spill";
    test_helpers::write_text_file(input, "c\na\nb\na\n");

    const auto result = test_helpers::run_command(
        shell_quote(wordlist_sort_exe()) + " -q --sort --deduplicate --sort-chunk 1 --tmp-dir " +
        shell_quote(tmp.string()) + " -o " + shell_quote(output.string()) + " " +
        shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\nc\n");
    EXPECT_TRUE(fs::is_directory(tmp));
}

TEST(E2eCli, StdinStdoutDash)
{
    const auto result = test_helpers::run_command(
        "printf 'b\\na\\nb\\n' | " + shell_quote(wordlist_sort_exe()) +
        " --sort --deduplicate -o - -");
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "a\nb\n");
    EXPECT_EQ(result.stdout_text.find("version"), std::string::npos);
}

TEST(E2eCli, StdoutRejectsBinaryFormat)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    test_helpers::write_text_file(input, "a\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --format=cdb -o - " + shell_quote(input.string()));
    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("stdout"), std::string::npos);
}

TEST(E2eCli, MultipleStdinRejected)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path output = work / "out.txt";

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) + " -q " +
                                                shell_quote(output.string()) + " - -");
    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("stdin"), std::string::npos);
}

TEST(E2eCli, ProgressReportsToStderr)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "a\nb\nc\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --progress " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\nc\n");
    EXPECT_NE(result.stderr_text.find("progress:"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("ingest done"), std::string::npos);
}

TEST(E2eCli, MissingInputExitsNonZero)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path output = work / "out.txt";
    const fs::path missing = work / "no-such-input.txt";

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) + " -q " +
                                                shell_quote(output.string()) + " " +
                                                shell_quote(missing.string()));
    EXPECT_NE(result.exit_code, 0);
}

TEST(E2eCli, GzipInput)
{
#if !defined(WORDLIST_SORT_ZLIB)
    GTEST_SKIP() << "built without zlib";
#else
    const auto work = test_helpers::make_temp_dir();
    const fs::path plain = work / "in.txt";
    const fs::path gz = work / "in.txt.gz";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(plain, "b\na\nb\n");

    const int z = std::system(("gzip -c " + shell_quote(plain.string()) + " > " + shell_quote(gz.string())).c_str());
    ASSERT_EQ(z, 0);

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --sort --deduplicate " + shell_quote(output.string()) +
                                                " " + shell_quote(gz.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\n");
#endif
}

TEST(E2eCli, GzipTextOutput)
{
#if !defined(WORDLIST_SORT_ZLIB)
    GTEST_SKIP() << "built without zlib";
#else
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path gz_out = work / "out.txt.gz";
    const fs::path plain_out = work / "out.txt";
    test_helpers::write_text_file(input, "b\na\nb\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --sort --deduplicate " + shell_quote(gz_out.string()) +
                                                " " + shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);

    const int z = std::system(("gzip -dc " + shell_quote(gz_out.string()) + " > " + shell_quote(plain_out.string())).c_str());
    ASSERT_EQ(z, 0);
    EXPECT_EQ(test_helpers::read_text_file(plain_out), "a\nb\n");
#endif
}

TEST(E2eCli, ZstdTextOutput)
{
#if !defined(WORDLIST_SORT_ZSTD)
    GTEST_SKIP() << "built without libzstd";
#else
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path zst_out = work / "out.txt.zst";
    const fs::path plain_out = work / "out.txt";
    test_helpers::write_text_file(input, "c\na\nb\na\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --sort --deduplicate " + shell_quote(zst_out.string()) +
                                                " " + shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);

    const int z = std::system(("zstd -q -d -f " + shell_quote(zst_out.string()) + " -o " + shell_quote(plain_out.string())).c_str());
    ASSERT_EQ(z, 0);
    EXPECT_EQ(test_helpers::read_text_file(plain_out), "a\nb\nc\n");
#endif
}

TEST(E2eCli, XzTextOutput)
{
#if !defined(WORDLIST_SORT_LZMA)
    GTEST_SKIP() << "built without liblzma";
#else
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path xz_out = work / "out.txt.xz";
    const fs::path plain_out = work / "out.txt";
    test_helpers::write_text_file(input, "c\na\nb\na\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --sort --deduplicate " + shell_quote(xz_out.string()) +
                                                " " + shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);

    const int z = std::system(("xz -dc " + shell_quote(xz_out.string()) + " > " + shell_quote(plain_out.string())).c_str());
    ASSERT_EQ(z, 0);
    EXPECT_EQ(test_helpers::read_text_file(plain_out), "a\nb\nc\n");
#endif
}

TEST(E2eCli, Lz4TextOutput)
{
#if !defined(WORDLIST_SORT_LZ4)
    GTEST_SKIP() << "built without liblz4";
#else
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path lz4_out = work / "out.txt.lz4";
    const fs::path plain_out = work / "out.txt";
    test_helpers::write_text_file(input, "c\na\nb\na\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --sort --deduplicate " + shell_quote(lz4_out.string()) +
                                                " " + shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);

    const int z = std::system(("lz4 -dc " + shell_quote(lz4_out.string()) + " > " + shell_quote(plain_out.string())).c_str());
    ASSERT_EQ(z, 0);
    EXPECT_EQ(test_helpers::read_text_file(plain_out), "a\nb\nc\n");
#endif
}

TEST(E2eCli, ZstdInput)
{
#if !defined(WORDLIST_SORT_ZSTD)
    GTEST_SKIP() << "built without libzstd";
#else
    const auto work = test_helpers::make_temp_dir();
    const fs::path plain = work / "in.txt";
    const fs::path zst = work / "in.txt.zst";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(plain, "c\na\nb\na\n");

    const int z = std::system(("zstd -q -f " + shell_quote(plain.string()) + " -o " + shell_quote(zst.string())).c_str());
    ASSERT_EQ(z, 0);

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --sort --deduplicate " + shell_quote(output.string()) +
                                                " " + shell_quote(zst.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\nc\n");
#endif
}

TEST(E2eCli, LimitCapsSurvivors)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "a\nb\nc\nd\ne\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --limit 2 " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\n");
}

TEST(E2eCli, ForceRequiredToOverwrite)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "a\n");
    test_helpers::write_text_file(output, "old\n");

    const auto blocked = test_helpers::run_command(shell_quote(wordlist_sort_exe()) + " -q " +
                                                   shell_quote(output.string()) + " " +
                                                   shell_quote(input.string()));
    EXPECT_NE(blocked.exit_code, 0);
    EXPECT_NE(blocked.stderr_text.find("exists"), std::string::npos);
    EXPECT_EQ(test_helpers::read_text_file(output), "old\n");

    const auto forced = test_helpers::run_command(shell_quote(wordlist_sort_exe()) + " -q --force " +
                                                  shell_quote(output.string()) + " " +
                                                  shell_quote(input.string()));
    EXPECT_EQ(forced.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "a\n");
}

TEST(E2eCli, SkipComments)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "# header\na\n  # indented\nb\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --skip-comments " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\n");
}

TEST(E2eCli, AppendTextOutput)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "b\n");
    test_helpers::write_text_file(output, "a\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --append " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\n");
}

TEST(E2eCli, NullSeparatedIo)
{
    const auto work = test_helpers::make_temp_dir();
    fs::create_directories(work);
    const fs::path input = work / "in.null";
    const fs::path output = work / "out.null";
    {
        std::ofstream out(input, std::ios::binary);
        ASSERT_TRUE(out) << input;
        out << "b" << '\0' << "a" << '\0' << "b" << '\0';
        ASSERT_TRUE(out);
    }

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --null --sort --deduplicate -f " +
                                                shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    std::ifstream in(output, std::ios::binary);
    const std::string got{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string expected;
    expected.push_back('a');
    expected.push_back('\0');
    expected.push_back('b');
    expected.push_back('\0');
    EXPECT_EQ(got, expected);
}

TEST(E2eCli, XzInput)
{
#if !defined(WORDLIST_SORT_LZMA)
    GTEST_SKIP() << "built without liblzma";
#else
    const auto work = test_helpers::make_temp_dir();
    const fs::path plain = work / "in.txt";
    const fs::path xz = work / "in.txt.xz";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(plain, "c\na\nb\na\n");

    const int z = std::system(("xz -q -k -f -c " + shell_quote(plain.string()) + " > " + shell_quote(xz.string())).c_str());
    ASSERT_EQ(z, 0);

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --sort --deduplicate " + shell_quote(output.string()) + " " +
                                                shell_quote(xz.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\nc\n");
#endif
}

TEST(E2eCli, Lz4Input)
{
#if !defined(WORDLIST_SORT_LZ4)
    GTEST_SKIP() << "built without liblz4";
#else
    const auto work = test_helpers::make_temp_dir();
    const fs::path plain = work / "in.txt";
    const fs::path lz4 = work / "in.txt.lz4";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(plain, "c\na\nb\na\n");

    const int z = std::system(("lz4 -q -f " + shell_quote(plain.string()) + " " + shell_quote(lz4.string())).c_str());
    ASSERT_EQ(z, 0);

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --sort --deduplicate " + shell_quote(output.string()) + " " +
                                                shell_quote(lz4.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\nc\n");
#endif
}


TEST(E2eCli, StatsStderr)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "a\nb\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --stats " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_NE(result.stderr_text.find("stats: ingest="), std::string::npos) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nb\n");
}

TEST(E2eCli, UpperCase)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "AbC\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --upper " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "ABC\n");
}

TEST(E2eCli, ReverseChars)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "abc\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --reverse " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "cba\n");
}

TEST(E2eCli, PrefixSuffixFilter)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "alpha\nbeta\nalgebra\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --prefix al --suffix a --sort " + shell_quote(output.string()) +
                                                " " + shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "algebra\nalpha\n");
}

TEST(E2eCli, RegexFilter)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "abc\n123\nx1y\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --regex '^[0-9]+$' --sort " + shell_quote(output.string()) +
                                                " " + shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "123\n");
}

TEST(E2eCli, CheckSortedOk)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "a\nb\nc\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --check-sorted " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(E2eCli, CheckSortedDisorder)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "b\na\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --check-sorted " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("disorder"), std::string::npos) << result.stderr_text;
}

TEST(E2eCli, EveryNth)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "a\nb\nc\nd\ne\nf\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --every 2 " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "a\nc\ne\n");
}

TEST(E2eCli, SampleSize)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "a\nb\nc\nd\ne\nf\ng\nh\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --sample 3 --sort " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    const auto out = test_helpers::read_text_file(output);
    std::size_t lines = 0;
    for (char c : out)
        if (c == '\n')
            ++lines;
    EXPECT_EQ(lines, 3u) << out;
}

TEST(E2eCli, EmailSplit)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "user@example.com\nplain\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --email-split --sort " + shell_quote(output.string()) +
                                                " " + shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "example.com\nplain\nuser\n");
}

TEST(E2eCli, FieldCut)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "a\tb\tc\nx,y,z\nonly\n");

    const auto tab = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                               " -q --field 2 --sort " + shell_quote(output.string()) +
                                               " " + shell_quote(input.string()));
    EXPECT_EQ(tab.exit_code, 0) << tab.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "b\n");

    const auto csv = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                               " -q --force --field 3 --delimiter , --sort " +
                                               shell_quote(output.string()) + " " + shell_quote(input.string()));
    EXPECT_EQ(csv.exit_code, 0) << csv.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "z\n");
}

TEST(E2eCli, IgnoreCaseSortDedup)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "Apple\napple\nBanana\nbanana\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --ignore-case --sort --deduplicate " +
                                                shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "Apple\nBanana\n");
}

TEST(E2eCli, IgnoreCaseCheckSorted)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "Apple\napple\nBanana\n");

    const auto ok = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                             " -q --ignore-case --check-sorted " +
                                             shell_quote(output.string()) + " " +
                                             shell_quote(input.string()));
    EXPECT_EQ(ok.exit_code, 0) << ok.stderr_text;

    const auto strict = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                  " -q --ignore-case --check-sorted --deduplicate " +
                                                  shell_quote(output.string()) + " " +
                                                  shell_quote(input.string()));
    EXPECT_NE(strict.exit_code, 0);
}

TEST(E2eCli, GlobInputs)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path a = work / "a.txt";
    const fs::path b = work / "b.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(a, "alpha\n");
    test_helpers::write_text_file(b, "beta\n");

    const auto pattern = (work / "*.txt").string();
    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --sort -o " + shell_quote(output.string()) + " " +
                                                shell_quote(pattern));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "alpha\nbeta\n");
}

TEST(E2eCli, GlobNoMatchFails)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path output = work / "out.txt";
    const auto pattern = (work / "missing-*.txt").string();
    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q -o " + shell_quote(output.string()) + " " +
                                                shell_quote(pattern));
    EXPECT_NE(result.exit_code, 0);
}

TEST(E2eCli, RecursiveDirectoryInputs)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path sub = work / "sub";
    fs::create_directories(sub);
    test_helpers::write_text_file(sub / "a.txt", "one\n");
    test_helpers::write_text_file(sub / "b.txt", "two\n");
    test_helpers::write_text_file(sub / "skip.bin", "nope\n");
    const fs::path output = work / "out.txt";

    const auto denied = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                  " -q --sort -o " + shell_quote(output.string()) + " " +
                                                  shell_quote(sub.string()));
    EXPECT_NE(denied.exit_code, 0);

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q -r --sort -o " + shell_quote(output.string()) + " " +
                                                shell_quote(sub.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "one\ntwo\n");
}

TEST(E2eCli, CompressStdoutGzip)
{
#if !defined(WORDLIST_SORT_ZLIB)
    GTEST_SKIP() << "built without zlib";
#else
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path gz_out = work / "out.gz";
    const fs::path plain = work / "plain.txt";
    test_helpers::write_text_file(input, "b\na\nb\n");

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --sort --deduplicate --compress gzip -o - " +
                                                shell_quote(input.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    {
        std::ofstream out(gz_out, std::ios::binary);
        out.write(result.stdout_text.data(), static_cast<std::streamsize>(result.stdout_text.size()));
    }

    const int z = std::system(("gzip -dc " + shell_quote(gz_out.string()) + " > " + shell_quote(plain.string())).c_str());
    ASSERT_EQ(z, 0);
    EXPECT_EQ(test_helpers::read_text_file(plain), "a\nb\n");
#endif
}

TEST(E2eCli, CompressStdoutRequiresDash)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(input, "a\n");
    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --compress gzip -o " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_NE(result.exit_code, 0);
}

TEST(E2eCli, LookupCdbIndex)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path dict = work / "dict.txt";
    const fs::path index = work / "dict.cdb";
    const fs::path queries = work / "queries.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(dict, "alpha\nbeta\ngamma\n");
    test_helpers::write_text_file(queries, "beta\nzeta\nalpha\n");

    const auto build = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                 " -q --format=cdb --sort --deduplicate " +
                                                 shell_quote(index.string()) + " " +
                                                 shell_quote(dict.string()));
    EXPECT_EQ(build.exit_code, 0) << build.stderr_text;

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --lookup " + shell_quote(index.string()) +
                                                " --sort -o " + shell_quote(output.string()) + " " +
                                                shell_quote(queries.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "alpha\nbeta\n");
}

TEST(E2eCli, LookupMutuallyExclusive)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path input = work / "in.txt";
    const fs::path output = work / "out.txt";
    const fs::path other = work / "other.txt";
    test_helpers::write_text_file(input, "a\n");
    test_helpers::write_text_file(other, "a\n");
    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --lookup " + shell_quote(other.string()) +
                                                " --intersect " + shell_quote(other.string()) +
                                                " -o " + shell_quote(output.string()) + " " +
                                                shell_quote(input.string()));
    EXPECT_NE(result.exit_code, 0);
}

TEST(E2eCli, FuzzyLookupFst)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path dict = work / "dict.txt";
    const fs::path index = work / "dict.fst";
    const fs::path queries = work / "queries.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(dict, "cat\ncot\ndog\n");
    test_helpers::write_text_file(queries, "cet\n");

    const auto build = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                 " -q --format=fst --sort --deduplicate " +
                                                 shell_quote(index.string()) + " " +
                                                 shell_quote(dict.string()));
    EXPECT_EQ(build.exit_code, 0) << build.stderr_text;

    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --lookup " + shell_quote(index.string()) +
                                                " --fuzzy --distance 1 --sort -o " +
                                                shell_quote(output.string()) + " " +
                                                shell_quote(queries.string()));
    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_EQ(test_helpers::read_text_file(output), "cat\ncot\n");
}

TEST(E2eCli, FuzzyRequiresFstLookup)
{
    const auto work = test_helpers::make_temp_dir();
    const fs::path dict = work / "dict.txt";
    const fs::path index = work / "dict.cdb";
    const fs::path queries = work / "queries.txt";
    const fs::path output = work / "out.txt";
    test_helpers::write_text_file(dict, "a\n");
    test_helpers::write_text_file(queries, "a\n");
    const auto build = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                 " -q --format=cdb --sort " + shell_quote(index.string()) +
                                                 " " + shell_quote(dict.string()));
    EXPECT_EQ(build.exit_code, 0) << build.stderr_text;
    const auto result = test_helpers::run_command(shell_quote(wordlist_sort_exe()) +
                                                " -q --lookup " + shell_quote(index.string()) +
                                                " --fuzzy -o " + shell_quote(output.string()) + " " +
                                                shell_quote(queries.string()));
    EXPECT_NE(result.exit_code, 0);
}
