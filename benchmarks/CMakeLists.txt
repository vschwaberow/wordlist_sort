add_executable(sort_dedup_bench sort_dedup_bench.cc)

target_include_directories(sort_dedup_bench PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_sources(sort_dedup_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sort_dedup_cpu.cc)

if(WORDLIST_SORT_CUDA)
    target_sources(sort_dedup_bench PRIVATE
        ${CMAKE_SOURCE_DIR}/src/sort_dedup_cuda.cu
        ${CMAKE_SOURCE_DIR}/src/cuda_available.cc)
    target_link_libraries(sort_dedup_bench PRIVATE CUDA::cudart)
    target_compile_definitions(sort_dedup_bench PRIVATE WORDLIST_SORT_CUDA=1)
    set_target_properties(sort_dedup_bench PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_ARCHITECTURES native)
    target_compile_options(sort_dedup_bench PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:-O3 --expt-extended-lambda>)
else()
    target_sources(sort_dedup_bench PRIVATE
        ${CMAKE_SOURCE_DIR}/src/sort_dedup_cuda_stub.cc)
endif()

target_compile_options(sort_dedup_bench PRIVATE
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Werror>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/W4;/permissive->
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-O3>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>)

if(WORDLIST_SORT_NATIVE_ARCH)
    target_compile_options(sort_dedup_bench PRIVATE
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-march=native>)
endif()
