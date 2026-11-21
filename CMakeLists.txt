cmake_minimum_required(VERSION 3.24)
project(ARCES VERSION 0.4.0 LANGUAGES CXX)

find_package(netCDF CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(BOOST_GEOMETRY_INCLUDE_DIR boost/geometry.hpp REQUIRED)
find_path(BOOST_MATH_INCLUDE_DIR boost/math/distributions/students_t.hpp REQUIRED)

function(arces_warnings target)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(arces_core STATIC src/analysis.cpp src/netcdf_reader.cpp src/boundary.cpp src/regional.cpp src/regional_map.cpp)
target_compile_features(arces_core PUBLIC cxx_std_20)
target_include_directories(arces_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(arces_core SYSTEM PRIVATE "${BOOST_MATH_INCLUDE_DIR}" "${BOOST_GEOMETRY_INCLUDE_DIR}")
target_link_libraries(arces_core PRIVATE netCDF::netcdf nlohmann_json::nlohmann_json)
arces_warnings(arces_core)

add_executable(arces src/main.cpp)
target_link_libraries(arces PRIVATE arces_core)
arces_warnings(arces)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME cli_errors COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:arces>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_errors.cmake")

    add_executable(create_fixture tests/create_fixture.cpp)
    target_compile_features(create_fixture PRIVATE cxx_std_20)
    target_link_libraries(create_fixture PRIVATE netCDF::netcdf)
    arces_warnings(create_fixture)
    set(fixture_path "${CMAKE_CURRENT_BINARY_DIR}/synthetic_temperature.nc4")
    add_test(NAME create_netcdf_fixture COMMAND create_fixture "${fixture_path}")
    set_tests_properties(create_netcdf_fixture PROPERTIES FIXTURES_SETUP temperature_data)
    add_test(NAME read_netcdf_fixture COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:arces>" "-DINPUT=${fixture_path}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/read_fixture.cmake")
    set_tests_properties(read_netcdf_fixture PROPERTIES FIXTURES_REQUIRED temperature_data)

    add_executable(time_utils_test tests/time_utils_test.cpp)
    target_compile_features(time_utils_test PRIVATE cxx_std_20)
    target_include_directories(time_utils_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    arces_warnings(time_utils_test)
    add_test(NAME timestamp_conversion COMMAND time_utils_test)

    add_executable(analysis_test tests/analysis_test.cpp)
    target_link_libraries(analysis_test PRIVATE arces_core)
    arces_warnings(analysis_test)
    add_test(NAME annual_and_trend_calculations COMMAND analysis_test)

    add_executable(create_series tests/create_series.cpp)
    target_link_libraries(create_series PRIVATE netCDF::netcdf)
    target_compile_features(create_series PRIVATE cxx_std_20)
    arces_warnings(create_series)
    add_test(NAME create_series_fixture COMMAND create_series "${CMAKE_CURRENT_BINARY_DIR}/series-fixture")
    set_tests_properties(create_series_fixture PROPERTIES FIXTURES_SETUP series_data)
    add_test(NAME series_csv_and_trend COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:arces>" "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/series-fixture"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/series_test.cmake")
    set_tests_properties(series_csv_and_trend PROPERTIES FIXTURES_REQUIRED series_data TIMEOUT 60)
endif()

if(BUILD_TESTING)
    add_executable(regional_test tests/regional_test.cpp)
    target_link_libraries(regional_test PRIVATE arces_core netCDF::netcdf nlohmann_json::nlohmann_json)
    arces_warnings(regional_test)
    add_test(NAME regional_reader_boundary_and_trends COMMAND regional_test "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/data/boundaries/ontario.geojson")
    set_tests_properties(regional_reader_boundary_and_trends PROPERTIES TIMEOUT 60)
endif()

if(BUILD_TESTING)
    add_test(NAME regional_cli COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:arces>" "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/series-fixture"
        "-DBOUNDARY=${CMAKE_CURRENT_SOURCE_DIR}/data/boundaries/ontario.geojson"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/regional_cli.cmake")
    set_tests_properties(regional_cli PROPERTIES FIXTURES_REQUIRED series_data TIMEOUT 120)
endif()
