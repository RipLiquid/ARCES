#pragma once

#include "analysis.hpp"
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

LoadedFile readFile(const std::filesystem::path& path,
                    double latitude, double longitude);
std::vector<std::filesystem::path> findInputs(const std::filesystem::path& path);

}
