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