function(word_sorter_add_cxx_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    target_link_libraries(${target} PRIVATE GTest::gtest_main)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

set(WORD_SORTER_SORT_DEDUP_CPU ${CMAKE_SOURCE_DIR}/src/sort_dedup_cpu.cc)
set(WORD_SORTER_PIPELINE ${CMAKE_SOURCE_DIR}/src/word_pipeline.cc)

word_sorter_add_cxx_test(sort_dedup_test
    sort_dedup_test.cc
    ${WORD_SORTER_SORT_DEDUP_CPU}
    ${CMAKE_SOURCE_DIR}/src/sort_dedup_cuda_stub.cc)

word_sorter_add_cxx_test(word_pipeline_test
    word_pipeline_test.cc
    ${WORD_SORTER_PIPELINE})

word_sorter_add_cxx_test(e2e_cli_test
    e2e_cli_test.cc)

target_compile_definitions(e2e_cli_test PRIVATE
    WORD_SORTER_EXE="$<TARGET_FILE:word_sorter>")
add_dependencies(e2e_cli_test word_sorter)

if(WORDLIST_SORT_CUDA)
    target_compile_definitions(sort_dedup_test PRIVATE WORDLIST_SORT_CUDA=1)
    target_compile_definitions(e2e_cli_test PRIVATE WORDLIST_SORT_CUDA=1)

    add_executable(sort_dedup_cuda_test
        sort_dedup_cuda_test.cc
        ${WORD_SORTER_SORT_DEDUP_CPU}
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
