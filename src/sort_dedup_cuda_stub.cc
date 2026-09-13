// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/sort_dedup_cuda_stub.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "sort_dedup.hpp"

[[nodiscard]] bool cuda_sort_dedup_is_compiled() noexcept
{
    return false;
}

[[nodiscard]] bool cuda_sort_dedup_runtime_available() noexcept
{
    return false;
}

[[nodiscard]] bool try_sort_and_deduplicate_words_cuda(std::vector<std::string> &,
                                                       const SortDedupPlan &)
{
    return false;
}
