# Verify the temporal CLI runs all three paper methods per query.
file(MAKE_DIRECTORY "${TEST_DIR}")
get_filename_component(sample_dir "${SAMPLE_GRAPH}" DIRECTORY)
get_filename_component(source_dir "${sample_dir}/../.." ABSOLUTE)
file(RELATIVE_PATH dataid "${source_dir}/dataset" "${TEST_DIR}/temporal")

function(check_temporal_results expected_answer expected_runs)
    set(expected_percent 20)
    if(ARGC GREATER 2)
        set(expected_percent "${ARGV2}")
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Temporal comparison failed: ${output}\n${error}")
    endif()
    if(NOT output MATCHES "k=3" OR NOT output MATCHES "percent=${expected_percent}([^0-9]|$)"
            OR output MATCHES "alpha=all|beta=|Mean V1[(]|kCListClique.h|kClistClique_v6.cpp|initHash|read_GlobalSCCs|scc_size:|depart_size|depart_combinations|scc_combinations|requiredHits=|\\[V1\\]|\\[baseline\\]")
        message(FATAL_ERROR "Unexpected comparison output: ${output}\n${error}")
    endif()
    foreach(label IN ITEMS Enumerate-Dec Enumerate-Com Enumerate-RG)
        string(REGEX MATCHALL "(^|[\r\n])${label}:[^\r\n]*" method_results "${output}")
        list(LENGTH method_results method_count)
        if(NOT method_count EQUAL expected_runs)
            message(FATAL_ERROR "Expected ${expected_runs} ${label} results: ${output}\n${error}")
        endif()
        string(REGEX MATCHALL "(^|[\r\n])${label}:[^\r\n]* ms answers: ${expected_answer}([\r\n]|$)"
            matching_results "${output}")
        list(LENGTH matching_results matching_count)
        if(NOT matching_count EQUAL expected_runs)
            message(FATAL_ERROR "Incorrect ${label} answer count: ${output}\n${error}")
        endif()
    endforeach()
endfunction()

# Every ordered pair has a direct event, so all three vertices form one TCS.
file(WRITE "${TEST_DIR}/temporal.txt" "3 6 2\n0 1 1\n1 0 1\n0 2 1\n2 0 1\n1 2 1\n2 1 1\n")
file(REMOVE "${TEST_DIR}/temporal_SCCs.txt")
file(MAKE_DIRECTORY "${TEST_DIR}/temporal_query")
file(WRITE "${TEST_DIR}/temporal_query/20.txt" "0 1\n")
execute_process(
    COMMAND "${MAIN_EXE}" -d "${dataid}" -k 3 -p 20
    WORKING_DIRECTORY "${TEST_DIR}"
    ENCODING UTF-8
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
check_temporal_results(1 1)
if(NOT EXISTS "${TEST_DIR}/temporal_SCCs.txt")
    message(FATAL_ERROR "CLI did not generate the missing SCC file")
endif()

execute_process(
    COMMAND "${MAIN_EXE}" -d "${dataid}" -k 4 -p 20
    WORKING_DIRECTORY "${TEST_DIR}"
    ENCODING UTF-8
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output MATCHES "k=4([^0-9]|$)" OR output MATCHES "k=3([^0-9]|$)")
    message(FATAL_ERROR "CLI ignored the requested k: ${output}\n${error}")
endif()
foreach(label IN ITEMS Enumerate-Dec Enumerate-Com Enumerate-RG)
    if(NOT output MATCHES "${label}:[^\r\n]* ms answers: 0([\r\n]|$)")
        message(FATAL_ERROR "Incorrect result for requested k=4: ${output}\n${error}")
    endif()
endforeach()

# Five-percent queries use the same dataset query-file dispatch.
file(WRITE "${TEST_DIR}/temporal_query/5.txt" "0 1\n")
execute_process(
    COMMAND "${MAIN_EXE}" -d "${dataid}" -k 3 -p 5
    WORKING_DIRECTORY "${TEST_DIR}"
    ENCODING UTF-8
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
check_temporal_results(1 1 5)

# Repeated windows must each invoke all enabled methods, without parameter sweeps.
file(WRITE "${TEST_DIR}/temporal_query/20.txt" "0 1\n0 1\n")
execute_process(
    COMMAND "${MAIN_EXE}" -d "${dataid}" -k 3 -p 20
    WORKING_DIRECTORY "${TEST_DIR}"
    ENCODING UTF-8
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
check_temporal_results(1 2)
file(WRITE "${TEST_DIR}/temporal_query/20.txt" "0 1\n")

# The comparison CLI exposes only dataset, k and window percentage.
foreach(removed_option IN ITEMS -a -b -clique -o)
    execute_process(
        COMMAND "${MAIN_EXE}" -d "${dataid}" -k 3 -p 20 "${removed_option}" all
        WORKING_DIRECTORY "${TEST_DIR}"
        ENCODING UTF-8
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT error MATCHES "Unknown option: ${removed_option}")
        message(FATAL_ERROR "Main accepted removed option ${removed_option}: ${output}\n${error}")
    endif()
endforeach()
foreach(bad_k IN ITEMS 0 1 junk 4294967296)
    execute_process(
        COMMAND "${MAIN_EXE}" -d "${dataid}" -k "${bad_k}" -p 20
        WORKING_DIRECTORY "${TEST_DIR}"
        ENCODING UTF-8
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0)
        message(FATAL_ERROR "Main accepted invalid k=${bad_k}: ${output}\n${error}")
    endif()
endforeach()
execute_process(
    COMMAND "${MAIN_EXE}" -d "${dataid}" -k 3 -p
    WORKING_DIRECTORY "${TEST_DIR}"
    ENCODING UTF-8
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "Main accepted an option without its value: ${output}\n${error}")
endif()

# No method may use vertices outside the selected induced subgraph or chain
# equal-time events under strict temporal reachability.
file(WRITE "${TEST_DIR}/temporal.txt" "4 4 2\n0 1 1\n1 2 1\n2 3 1\n3 0 1\n")
file(WRITE "${TEST_DIR}/temporal_SCCs.txt" "1\n4 0 1 2 3\n")
execute_process(
    COMMAND "${MAIN_EXE}" -d "${dataid}" -k 3 -p 20
    WORKING_DIRECTORY "${TEST_DIR}"
    ENCODING UTF-8
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
check_temporal_results(0 1)
message(STATUS "Three-method temporal comparison CLI passed")
