cmake_minimum_required(VERSION 3.15)
project(word_sorter)

set(PROJECT_NAME "wordlist_sort")
set(PROJECT_VERSION "0.1.0")
set(PROJECT_AUTHOR "Volker Schwaberow <volker@schwaberow.de>")
set(PROJECT_COPYRIGHT "Copyright (c) 2024 Volker Schwaberow")

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

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CPM_DOWNLOAD_VERSION 0.40.5)

if(CPM_SOURCE_CACHE)
    set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
    set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

if(NOT(EXISTS ${CPM_DOWNLOAD_LOCATION}))
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
        ${CPM_DOWNLOAD_LOCATION}
    )
endif()

include(${CPM_DOWNLOAD_LOCATION})

CPMAddPackage(
    NAME CLI11
    GITHUB_REPOSITORY CLIUtils/CLI11
    VERSION 2.4.2
    OPTIONS
    "CLI11_BUILD_EXAMPLES OFF"
    "CLI11_BUILD_TESTS OFF"
)

add_executable(word_sorter src/main.cc)

target_link_libraries(word_sorter PRIVATE CLI11::CLI11)

target_compile_options(word_sorter PRIVATE -O3 -march=native)