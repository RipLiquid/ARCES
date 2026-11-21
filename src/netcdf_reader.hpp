#pragma once

#include "analysis.hpp"
#include <cstddef>
#include <utility>

namespace arces {

struct LoadedFile {
    std::vector<Record> records;
    std::vector<std::pair<std::string, std::size_t>> dimensions;
    std::size_t latitudeCount = 0;
    std::size_t longitudeCount = 0;
    std::size_t latitudeIndex = 0;
    std::size_t longitudeIndex = 0;
    std::string timeUnits;
    std::string calendar;
    double firstTime = 0;
};

// Inclusive geographic bounds, in degrees.
// Boxes crossing the antimeridian are not supported.
struct GeographicBounds {
    double south = 0;
    double north = 0;
    double west = 0;
    double east = 0;
};

struct GridCell {
    std::size_t latitudeIndex = 0;
    std::size_t longitudeIndex = 0;
    double latitude = 0;
    double longitude = 0;
    std::vector<Record> records;
};

struct LoadedRegion {
    // Full coordinate arrays support consistency checks between files.
    std::vector<double> latitudes;
    std::vector<double> longitudes;
    std::vector<GridCell> cells;
    std::string timeUnits;
    std::string calendar;
};

LoadedFile readFile(const std::filesystem::path& path,
                    double latitude, double longitude);

// Read cells whose centres fall inside the geographic bounds.
void validateBounds(const GeographicBounds& bounds);

LoadedRegion readRegion(const std::filesystem::path& path,
                        const GeographicBounds& bounds);

std::vector<std::filesystem::path> findInputs(
    const std::filesystem::path& path);

}