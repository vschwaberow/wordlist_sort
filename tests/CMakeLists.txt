function(wordlist_sort_add_cxx_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    target_link_libraries(${target} PRIVATE GTest::gtest_main)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

set(WORDLIST_SORT_SORT_DEDUP_CPU ${CMAKE_SOURCE_DIR}/src/sort_dedup_cpu.cc)
set(WORDLIST_SORT_PIPELINE ${CMAKE_SOURCE_DIR}/src/word_pipeline.cc)
set(WORDLIST_SORT_GZIP ${CMAKE_SOURCE_DIR}/src/gzip_stream.cc)

wordlist_sort_add_cxx_test(sort_dedup_test
    sort_dedup_test.cc
    ${WORDLIST_SORT_SORT_DEDUP_CPU}
    ${CMAKE_SOURCE_DIR}/src/sort_dedup_cuda_stub.cc)

wordlist_sort_add_cxx_test(word_pipeline_test
    word_pipeline_test.cc
    ${WORDLIST_SORT_PIPELINE}
    ${WORDLIST_SORT_GZIP}
    ${WORDLIST_SORT_SORT_DEDUP_CPU}
    ${CMAKE_SOURCE_DIR}/src/sort_dedup_cuda_stub.cc)

wordlist_sort_add_cxx_test(e2e_cli_test
    e2e_cli_test.cc)

target_compile_definitions(e2e_cli_test PRIVATE
    WORDLIST_SORT_EXE="$<TARGET_FILE:wordlist_sort>")
add_dependencies(e2e_cli_test wordlist_sort)

if(WORDLIST_SORT_CUDA)
    target_compile_definitions(sort_dedup_test PRIVATE WORDLIST_SORT_CUDA=1)
    target_compile_definitions(e2e_cli_test PRIVATE WORDLIST_SORT_CUDA=1)

    add_executable(sort_dedup_cuda_test
        sort_dedup_cuda_test.cc
        ${WORDLIST_SORT_SORT_DEDUP_CPU}
        ${CMAKE_SOURCE_DIR}/src/sort_dedup_cuda.cu
        ${CMAKE_SOURCE_DIR}/src/cuda_available.cc)
    target_include_directories(sort_dedup_cuda_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(sort_dedup_cuda_test PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror>)
    target_compile_options(sort_dedup_cuda_test PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:-O3 --expt-extended-lambda>)
    target_link_libraries(sort_dedup_cuda_test PRIVATE GTest::gtest_main CUDA::cudart)
    set_target_properties(sort_dedup_cuda_test PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_ARCHITECTURES native)
    add_test(NAME sort_dedup_cuda_test COMMAND sort_dedup_cuda_test)
    set_tests_properties(sort_dedup_cuda_test PROPERTIES LABELS "cuda")
endif()

add_test(NAME build_and_test_cpu COMMAND ${CMAKE_SOURCE_DIR}/build_and_test.sh)
set_tests_properties(build_and_test_cpu PROPERTIES LABELS "integration")

if(WORDLIST_SORT_CUDA)
    add_test(NAME build_and_test_cuda COMMAND ${CMAKE_SOURCE_DIR}/build_and_test_cuda.sh)
    set_tests_properties(build_and_test_cuda PROPERTIES LABELS "integration;cuda")
endif()

wordlist_sort_add_cxx_test(export_format_test
    export_format_test.cc
    ${CMAKE_SOURCE_DIR}/src/export_format.cc
    ${CMAKE_SOURCE_DIR}/src/cdb_export.cc
    ${CMAKE_SOURCE_DIR}/src/fst_export.cc
    ${CMAKE_SOURCE_DIR}/src/membership_filter.cc
    ${WORDLIST_SORT_PIPELINE}
    ${WORDLIST_SORT_GZIP}
    ${WORDLIST_SORT_SORT_DEDUP_CPU}
    ${CMAKE_SOURCE_DIR}/src/sort_dedup_cuda_stub.cc)

wordlist_sort_add_cxx_test(membership_filter_test
    membership_filter_test.cc
    ${CMAKE_SOURCE_DIR}/src/membership_filter.cc
    ${CMAKE_SOURCE_DIR}/src/export_format.cc
    ${CMAKE_SOURCE_DIR}/src/cdb_export.cc
    ${CMAKE_SOURCE_DIR}/src/fst_export.cc
    ${WORDLIST_SORT_PIPELINE}
    ${WORDLIST_SORT_GZIP}
    ${WORDLIST_SORT_SORT_DEDUP_CPU}
    ${CMAKE_SOURCE_DIR}/src/sort_dedup_cuda_stub.cc)

if(WORDLIST_SORT_PTHASH)
    foreach(_t IN ITEMS membership_filter_test export_format_test)
        target_link_libraries(${_t} PRIVATE PTHASH)
        target_include_directories(${_t} PRIVATE ${pthash_SOURCE_DIR})
        target_compile_definitions(${_t} PRIVATE WORDLIST_SORT_PTHASH=1)
    endforeach()
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/membership_filter.cc PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-conversion;-Wno-float-conversion;-Wno-unused-but-set-variable;-Wno-error>")
endif()

if(WORDLIST_SORT_ZLIB)
    foreach(_t IN ITEMS word_pipeline_test export_format_test membership_filter_test)
        target_link_libraries(${_t} PRIVATE ZLIB::ZLIB)
        target_compile_definitions(${_t} PRIVATE WORDLIST_SORT_ZLIB=1)
    endforeach()
    target_compile_definitions(e2e_cli_test PRIVATE WORDLIST_SORT_ZLIB=1)
endif()

if(WORDLIST_SORT_ZSTD)
    set(_zstd_lib "")
    if(TARGET zstd::libzstd_shared)
        set(_zstd_lib zstd::libzstd_shared)
    elseif(TARGET zstd::libzstd_static)
        set(_zstd_lib zstd::libzstd_static)
    elseif(TARGET zstd::libzstd)
        set(_zstd_lib zstd::libzstd)
    else()
        find_library(WORDLIST_SORT_ZSTD_LIB zstd REQUIRED)
        set(_zstd_lib ${WORDLIST_SORT_ZSTD_LIB})
    endif()
    foreach(_t IN ITEMS word_pipeline_test export_format_test membership_filter_test)
        target_link_libraries(${_t} PRIVATE ${_zstd_lib})
        target_compile_definitions(${_t} PRIVATE WORDLIST_SORT_ZSTD=1)
    endforeach()
    target_compile_definitions(e2e_cli_test PRIVATE WORDLIST_SORT_ZSTD=1)
endif()

if(WORDLIST_SORT_LZMA)
    foreach(_t IN ITEMS word_pipeline_test export_format_test membership_filter_test)
        target_link_libraries(${_t} PRIVATE LibLZMA::LibLZMA)
        target_compile_definitions(${_t} PRIVATE WORDLIST_SORT_LZMA=1)
    endforeach()
    target_compile_definitions(e2e_cli_test PRIVATE WORDLIST_SORT_LZMA=1)
endif()
