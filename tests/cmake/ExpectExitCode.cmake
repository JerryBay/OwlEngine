if(NOT DEFINED PROGRAM)
    message(FATAL_ERROR "PROGRAM is required")
endif()

if(NOT DEFINED EXPECTED_EXIT_CODE)
    message(FATAL_ERROR "EXPECTED_EXIT_CODE is required")
endif()

if(NOT DEFINED PROGRAM_ARGUMENT)
    message(FATAL_ERROR "PROGRAM_ARGUMENT is required")
endif()

execute_process(
    COMMAND "${PROGRAM}" "${PROGRAM_ARGUMENT}" ${PROGRAM_EXTRA_ARGUMENTS}
    RESULT_VARIABLE actual_exit_code
)

if(NOT "${actual_exit_code}" STREQUAL "${EXPECTED_EXIT_CODE}")
    message(FATAL_ERROR
        "Expected exit code ${EXPECTED_EXIT_CODE}, got ${actual_exit_code}")
endif()
