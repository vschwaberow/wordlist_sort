cmake_minimum_required(VERSION 3.18)
project(word_sorter LANGUAGES CXX)

set(PROJECT_NAME "wordlist_sort")
set(PROJECT_VERSION "0.3.0")
set(PROJECT_AUTHOR "Volker Schwaberow <volker@schwaberow.de>")
set(PROJECT_COPYRIGHT "Copyright (c) 2024-2026 Volker Schwaberow")

if(WIN32)
  set(BUILD_PLATFORM_INFO "Windows")
elseif(APPLE)
  set(BUILD_PLATFORM_INFO "macOS")
elseif(UNIX)
  set(BUILD_PLATFORM_INFO "Linux")
else()
  set(BUILD_PLATFORM_INFO "Unknown")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(COMPILER_INFO_STRING "Clang ${CMAKE_CXX_COMPILER_VERSION}")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  set(COMPILER_INFO_STRING "GCC ${CMAKE_CXX_COMPILER_VERSION}")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  set(COMPILER_INFO_STRING "MSVC ${CMAKE_CXX_COMPILER_VERSION}")
else()
  set(COMPILER_INFO_STRING "Unknown compiler")
endif()

add_definitions(
    -DPROJECT_NAME="${PROJECT_NAME}"
    -DPROJECT_VERSION="${PROJECT_VERSION}"
    -DPROJECT_AUTHOR="${PROJECT_AUTHOR}"
    -DPROJECT_COPYRIGHT="${PROJECT_COPYRIGHT}"
    -DBUILD_PLATFORM_INFO="${BUILD_PLATFORM_INFO}"
    -DCOMPILER_INFO_STRING="${COMPILER_INFO_STRING}"
)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

option(WORDLIST_SORT_CUDA "Enable CUDA-accelerated sort/dedup" OFF)
option(WORDLIST_SORT_BUILD_TESTS "Build GoogleTest suite and register CTest targets" ON)
option(WORDLIST_SORT_BUILD_BENCHMARKS "Build sort/dedup CPU baseline benchmark harness" ON)

set(WORD_SORTER_CORE_SOURCES
    src/word_pipeline.cc
    src/sort_dedup_cpu.cc)

add_executable(word_sorter
    src/main.cc
    ${WORD_SORTER_CORE_SOURCES})

target_include_directories(word_sorter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WORDLIST_SORT_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(WORD_SORTER_CUDA_SOURCES
        src/sort_dedup_cuda.cu
        src/cuda_available.cc)
    target_sources(word_sorter PRIVATE ${WORD_SORTER_CUDA_SOURCES})
    target_link_libraries(word_sorter PRIVATE CUDA::cudart)
    target_compile_definitions(word_sorter PRIVATE WORDLIST_SORT_CUDA=1)
    set_target_properties(word_sorter PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_ARCHITECTURES native)
else()
    target_sources(word_sorter PRIVATE src/sort_dedup_cuda_stub.cc)
endif()

target_compile_options(word_sorter PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -O3 -march=native>)
target_compile_options(word_sorter PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-O3 --expt-extended-lambda>)

if(WORDLIST_SORT_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_subdirectory(tests)
endif()

if(WORDLIST_SORT_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
