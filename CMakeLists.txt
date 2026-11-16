cmake_minimum_required(VERSION 3.24)

project(ARCES VERSION 0.1.0 LANGUAGES CXX)

find_package(netCDF CONFIG REQUIRED)

add_executable(arces src/main.cpp)
target_link_libraries(arces PRIVATE netCDF::netcdf)
target_compile_features(arces PRIVATE cxx_std_20)
set_target_properties(arces PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(arces PRIVATE /W4 /permissive-)
else()
    target_compile_options(arces PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

if(BUILD_TESTING)
    add_test(
        NAME cli_errors
        COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:arces>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_errors.cmake"
    )
endif()
if(BUILD_TESTING)
    add_executable(create_fixture tests/create_fixture.cpp)
    target_compile_features(create_fixture PRIVATE cxx_std_20)
    target_link_libraries(create_fixture PRIVATE netCDF::netcdf)

    set(fixture_path "${CMAKE_CURRENT_BINARY_DIR}/synthetic_temperature.nc4")

    add_test(
        NAME create_netcdf_fixture
        COMMAND create_fixture "${fixture_path}"
    )

    set_tests_properties(create_netcdf_fixture PROPERTIES
        FIXTURES_SETUP temperature_data
    )

    add_test(
        NAME read_netcdf_fixture
        COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:arces>"
        "-DINPUT=${fixture_path}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/read_fixture.cmake"
    )

    set_tests_properties(read_netcdf_fixture PROPERTIES
        FIXTURES_REQUIRED temperature_data
    )
endif()
