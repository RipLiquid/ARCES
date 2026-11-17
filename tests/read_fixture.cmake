if(NOT DEFINED PROGRAM OR NOT DEFINED INPUT)
    message(FATAL_ERROR "PROGRAM and INPUT are required.")
endif()

execute_process(
    COMMAND "${PROGRAM}" "${INPUT}" 43.65 -79.38
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 10
)

if(NOT "${exit_code}" STREQUAL "0")
    message(FATAL_ERROR
        "Expected exit code 0, got '${exit_code}'.\n"
        "Output: ${output}\nError: ${error}"
    )
endif()

set(expected_messages
    "Latitudes: 2"
    "Longitudes: 2"
    "Latitude: 43.5 (index 0)"
    "Longitude: -79.375 (index 1)"
    "Kelvin: 280"
    "Celsius: 6.85"
    "Time metadata:"
    "First value: 0"
    "Units: minutes since 1995-01-01 00:30:00"
    "Decoded timestamp: 1995-01-01T00:30:00"
)

foreach(expected IN LISTS expected_messages)
    string(FIND "${output}" "${expected}" position)

    if(position EQUAL -1)
        message(FATAL_ERROR
            "Missing expected output: '${expected}'.\n"
            "Actual output:\n${output}"
        )
    endif()
endforeach()

message(STATUS "Coordinates, temperature, and time metadata passed.")
