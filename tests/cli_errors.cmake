if(NOT DEFINED PROGRAM)
    message(FATAL_ERROR "PROGRAM must point to the ARCES executable.")
endif()

function(expect_error case_name expected_message)
    execute_process(
        COMMAND "${PROGRAM}" ${ARGN}
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 10
    )

    if(NOT "${exit_code}" STREQUAL "1")
        message(FATAL_ERROR
            "${case_name}: expected exit code 1, got '${exit_code}'.\n"
            "Output: ${output}\nError: ${error}"
        )
    endif()

    string(FIND "${error}" "${expected_message}" message_position)

    if(message_position EQUAL -1)
        message(FATAL_ERROR
            "${case_name}: expected '${expected_message}'.\n"
            "Actual error: ${error}"
        )
    endif()

    message(STATUS "${case_name}: passed")
endfunction()

# Ensure this path does not point to an existing file.
set(missing_file "${CMAKE_CURRENT_BINARY_DIR}/arces-missing-input.nc4")

if(EXISTS "${missing_file}")
    message(FATAL_ERROR "Test requires a nonexistent file: ${missing_file}")
endif()

expect_error(
    "Missing arguments"
    "Usage: arces"
)

expect_error(
    "Invalid latitude"
    "Latitude must be between"
    "${missing_file}" 100 -79.38
)

expect_error(
    "Invalid longitude"
    "Latitude must be between"
    "${missing_file}" 43.65 200
)

expect_error(
    "Nonnumeric coordinate"
    "Latitude and longitude must be valid numbers."
    "${missing_file}" abc -79.38
)

expect_error(
    "Trailing characters"
    "Latitude and longitude must be valid numbers."
    "${missing_file}" 43.65abc -79.38
)

expect_error(
    "Missing input file"
    "Could not open file:"
    "${missing_file}" 43.65 -79.38
)