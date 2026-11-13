file(MAKE_DIRECTORY "${TEST_DIR}")
file(REMOVE "${TEST_DIR}/sample.txt" "${TEST_DIR}/sample_SCCs.txt")
file(WRITE "${TEST_DIR}/raw.txt" "# u v t\n% comment\n40 10 -5\n10 40 -7\n10 40 -7 # repeated event\n99 99 8\n40 90 2\n")
execute_process(
    COMMAND "${PROCESS_EXE}" -i "${TEST_DIR}/raw.txt" -d sample -o "${TEST_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Dataset preprocessing failed: ${output}\n${error}")
endif()
file(READ "${TEST_DIR}/sample.txt" dataset)
file(READ "${TEST_DIR}/sample_SCCs.txt" components)
if(NOT dataset STREQUAL "3 4 10\n0 1 0\n0 1 0\n1 0 2\n1 2 9\n" OR NOT components STREQUAL "1\n2 0 1\n")
    message(FATAL_ERROR "Incorrect normalized dataset or SCCs: ${dataset}\n${components}")
endif()
execute_process(
    COMMAND "${PROCESS_EXE}" -i "${TEST_DIR}/raw.txt" -d sample -o "${TEST_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "Preprocessing overwrote an existing dataset")
endif()
file(READ "${TEST_DIR}/sample.txt" preserved)
if(NOT preserved STREQUAL dataset)
    message(FATAL_ERROR "Failed preprocessing changed the existing dataset")
endif()
file(REMOVE "${TEST_DIR}/invalid.txt" "${TEST_DIR}/invalid_SCCs.txt")
file(WRITE "${TEST_DIR}/bad_raw.txt" "1 2\n")
execute_process(
    COMMAND "${PROCESS_EXE}" -i "${TEST_DIR}/bad_raw.txt" -d invalid -o "${TEST_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR EXISTS "${TEST_DIR}/invalid.txt" OR EXISTS "${TEST_DIR}/invalid_SCCs.txt")
    message(FATAL_ERROR "Malformed raw input was accepted")
endif()
message(STATUS "Dataset preprocessing CLI passed")
