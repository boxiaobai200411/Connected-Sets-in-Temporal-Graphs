cmake_minimum_required(VERSION 3.16)

project(TCS-Enum LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(temporal_scc_core OBJECT
        src/TemporalSCCs.cpp
        src/MutualClique.cpp
        src/TemporalSCCs.h
        src/type.h
)
target_include_directories(temporal_scc_core PUBLIC src)

add_executable(TCS-Enum src/main.cpp)
add_executable(ProcessDataset src/ProcessDataset.cpp)

find_package(Threads REQUIRED)
set(CCR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/coreCliqueRemoval-main/src")
add_library(core_clique_kcc STATIC
        "${CCR_SOURCE_DIR}/graph/graph.cpp"
        "${CCR_SOURCE_DIR}/clique/kCListClique_v6.cpp"
)
target_include_directories(core_clique_kcc PUBLIC
        "${CCR_SOURCE_DIR}/clique"
        "${CCR_SOURCE_DIR}/tools"
)
target_link_libraries(core_clique_kcc PUBLIC Threads::Threads)
target_link_libraries(temporal_scc_core PUBLIC core_clique_kcc)
target_link_libraries(TCS-Enum PRIVATE temporal_scc_core core_clique_kcc)
target_link_libraries(ProcessDataset PRIVATE temporal_scc_core core_clique_kcc)
target_compile_definitions(TCS-Enum PRIVATE
        TCS_ENUM_DATASET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/dataset")
target_compile_definitions(ProcessDataset PRIVATE
        TCS_ENUM_DATASET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/dataset")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(temporal_scc_core PRIVATE -O3)
    target_compile_options(TCS-Enum PRIVATE -O3)
    target_compile_options(ProcessDataset PRIVATE -O3)
    target_compile_options(core_clique_kcc PRIVATE -O3)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9 AND NOT MINGW)
    target_link_libraries(temporal_scc_core INTERFACE stdc++fs)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME process_dataset_test
        COMMAND ${CMAKE_COMMAND}
            "-DPROCESS_EXE=$<TARGET_FILE:ProcessDataset>"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/process_dataset_test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/process_dataset_test.cmake")
    add_test(NAME core_clique_main_test
        COMMAND ${CMAKE_COMMAND}
            "-DMAIN_EXE=$<TARGET_FILE:TCS-Enum>"
            "-DSAMPLE_GRAPH=${CMAKE_CURRENT_SOURCE_DIR}/coreCliqueRemoval-main/examples/output-demo.txt"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/core_clique_main_test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_clique_main_test.cmake")
    foreach(test_name IN ITEMS
            dataset_scc_test
            multiway_split_test
            early_stop_test
            neighbor_enumeration_test
            incremental_enumeration_test
            v1_query_state_test
            temporal_pivot_test
            enumeration_count_test
            prefix_labels_test
            strict_temporal_test
            mutual_clique_test
            v1_enumeration_test
            reverse_pivot_test)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE temporal_scc_core)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
