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
option(WORDLIST_SORT_PTHASH "Enable PTHash membership engine for --exclude/--intersect" ON)
option(WORDLIST_SORT_ZLIB "Enable transparent gzip (.gz) input via zlib" ON)
option(WORDLIST_SORT_ZSTD "Enable transparent zstd (.zst) input via libzstd" ON)

set(WORDLIST_SORT_CORE_SOURCES
    src/word_pipeline.cc
    src/sort_dedup_cpu.cc
    src/export_format.cc
    src/cdb_export.cc
    src/fst_export.cc
    src/membership_filter.cc
    src/gzip_stream.cc)

add_executable(wordlist_sort
    src/main.cc
    ${WORDLIST_SORT_CORE_SOURCES})

target_include_directories(wordlist_sort PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WORDLIST_SORT_ZLIB)
    find_package(ZLIB REQUIRED)
    target_link_libraries(wordlist_sort PRIVATE ZLIB::ZLIB)
    target_compile_definitions(wordlist_sort PRIVATE WORDLIST_SORT_ZLIB=1)
endif()

if(WORDLIST_SORT_ZSTD)
    find_package(zstd REQUIRED)
    # CMake configs vary: prefer namespaced target when present.
    if(TARGET zstd::libzstd_shared)
        target_link_libraries(wordlist_sort PRIVATE zstd::libzstd_shared)
    elseif(TARGET zstd::libzstd_static)
        target_link_libraries(wordlist_sort PRIVATE zstd::libzstd_static)
    elseif(TARGET zstd::libzstd)
        target_link_libraries(wordlist_sort PRIVATE zstd::libzstd)
    else()
        find_library(WORDLIST_SORT_ZSTD_LIB zstd REQUIRED)
        target_link_libraries(wordlist_sort PRIVATE ${WORDLIST_SORT_ZSTD_LIB})
    endif()
    target_compile_definitions(wordlist_sort PRIVATE WORDLIST_SORT_ZSTD=1)
endif()

if(WORDLIST_SORT_PTHASH)
    include(FetchContent)
    FetchContent_Declare(
        pthash
        GIT_REPOSITORY https://github.com/jermp/pthash.git
        GIT_TAG v2.0.0
        GIT_SHALLOW TRUE
        GIT_SUBMODULES_RECURSE TRUE)
    set(PTHASH_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(pthash)
    target_link_libraries(wordlist_sort PRIVATE PTHASH)
    # pthash.hpp lives under include/ and is included as "include/pthash.hpp"
    target_include_directories(wordlist_sort PRIVATE ${pthash_SOURCE_DIR})
    target_compile_definitions(wordlist_sort PRIVATE WORDLIST_SORT_PTHASH=1)
    # PTHash headers are not -Wconversion/-Werror clean; isolate that TU.
    set_source_files_properties(src/membership_filter.cc PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-conversion;-Wno-float-conversion;-Wno-unused-but-set-variable;-Wno-error>")
endif()

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
