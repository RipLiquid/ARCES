#pragma once
#include "boundary.hpp"

namespace arces {
struct CellReport {
    std::size_t latitudeIndex = 0, longitudeIndex = 0;
    double latitude = 0, longitude = 0;
    Report report;
};
struct RegionalReport {
    std::vector<CellReport> cells;
    GeographicBounds bounds;
    Month start, end;
    std::size_t inputFiles = 0;
    std::optional<Boundary> boundary;
};
RegionalReport analyzeRegion(const std::filesystem::path& input,
                             const GeographicBounds& bounds,
                             const Options& options = {},
                             const Boundary* boundary = nullptr);
void writeRegionalReport(const RegionalReport& report,
                         const std::filesystem::path& output,
                         const Options& options,
                         const std::filesystem::path& boundaryPath = {},
                         const std::string& feature = "");
}
