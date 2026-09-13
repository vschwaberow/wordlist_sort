// SPDX-License-Identifier: MIT
// Project: wordlist_sort
// File: src/cuda_available.cc
// Author: Volker Schwaberow <volker@schwaberow.de>
// Copyright (c) 2026 Volker Schwaberow

#include "sort_dedup.hpp"

#include <cuda_runtime.h>

[[nodiscard]] bool cuda_sort_dedup_is_compiled() noexcept
{
    return true;
}

[[nodiscard]] bool cuda_sort_dedup_runtime_available() noexcept
{
    int device_count = 0;
    return cudaGetDeviceCount(&device_count) == cudaSuccess && device_count > 0;
}
