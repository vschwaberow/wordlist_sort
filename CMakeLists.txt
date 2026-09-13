cmake_minimum_required(VERSION 3.18)
project(wordlist_sort LANGUAGES CXX)

set(PROJECT_NAME "wordlist_sort")
set(PROJECT_VERSION "0.4.0")
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
option(WORDLIST_SORT_NATIVE_ARCH "Add -march=native on GCC/Clang (non-portable)" OFF)

set(WORDLIST_SORT_CORE_SOURCES
    src/word_pipeline.cc
    src/sort_dedup_cpu.cc
    src/export_format.cc
    src/cdb_export.cc
    src/fst_export.cc)

add_executable(wordlist_sort
    src/main.cc
    ${WORDLIST_SORT_CORE_SOURCES})

target_include_directories(wordlist_sort PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WORDLIST_SORT_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(WORDLIST_SORT_CUDA_SOURCES
        src/sort_dedup_cuda.cu
        src/cuda_available.cc)
    target_sources(wordlist_sort PRIVATE ${WORDLIST_SORT_CUDA_SOURCES})
    target_link_libraries(wordlist_sort PRIVATE CUDA::cudart)
    target_compile_definitions(wordlist_sort PRIVATE WORDLIST_SORT_CUDA=1)
    set_target_properties(wordlist_sort PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_ARCHITECTURES native)
else()
    target_sources(wordlist_sort PRIVATE src/sort_dedup_cuda_stub.cc)
endif()

# Warnings always; optimization only in Release; MSVC gets /W4 and /O2.
target_compile_options(wordlist_sort PRIVATE
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Werror>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/W4;/permissive->
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-O3>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>
    $<$<COMPILE_LANGUAGE:CUDA>:-O3;--expt-extended-lambda>)

if(WORDLIST_SORT_NATIVE_ARCH)
    target_compile_options(wordlist_sort PRIVATE
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-march=native>)
endif()

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
