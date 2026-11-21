#include "version.hpp"
#include "regional.hpp"
#include "regional_map.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <stdexcept>

namespace arces {
using Json = nlohmann::ordered_json;
RegionalReport analyzeRegion(const std::filesystem::path& input, const GeographicBounds& bounds,
                             const Options& options, const Boundary* boundary) {
    validateBounds(bounds);
    if (options.minimumYears < 3 || (options.hacLag && *options.hacLag < 0))
        throw std::invalid_argument("Invalid minimum years or HAC lag.");
    if (options.start && options.end && *options.start > *options.end)
        throw std::invalid_argument("Start month must not follow end month.");
    const auto inputs = findInputs(input);
    RegionalReport result;
    result.bounds = bounds;
    if (boundary) result.boundary = *boundary;
    result.inputFiles = inputs.size();
    std::vector<double> latitudes, longitudes;
    std::vector<GridCell> accumulated;
    std::vector<bool> selected;
    bool first = true;
    for (const auto& path : inputs) {
        try {
            auto loaded = readRegion(path, bounds);
            if (first) {
                latitudes = loaded.latitudes;
                longitudes = loaded.longitudes;
            } else if (latitudes != loaded.latitudes || longitudes != loaded.longitudes) {
                throw std::runtime_error("Regional grid coordinates change between files.");
            }
            if (first) {
                for (const auto& cell : loaded.cells)
                    selected.push_back(!boundary || boundary->covers(cell.latitude, cell.longitude));
            }
            if (selected.size() != loaded.cells.size()) throw std::runtime_error("Regional selection changes between files.");
            std::size_t selectionIndex = 0;
            std::erase_if(loaded.cells, [&](const GridCell&) {
                return !selected[selectionIndex++];
            });
            if (loaded.cells.empty()) throw std::runtime_error("No grid-cell centres selected by bounds/boundary.");
            for (auto& cell : loaded.cells) {
                std::erase_if(cell.records, [&](const Record& row) {
                    return (options.start && row.date < *options.start) || (options.end && row.date > *options.end);
                });
            }
            if (first) {
                accumulated = std::move(loaded.cells);
                first = false;
            } else {
                if (loaded.cells.size() != accumulated.size()) throw std::runtime_error("Regional selection changes between files.");
                for (std::size_t i = 0; i < accumulated.size(); ++i) {
                    const auto& a = accumulated[i];
                    const auto& b = loaded.cells[i];
                    if (a.latitudeIndex != b.latitudeIndex || a.longitudeIndex != b.longitudeIndex)
                        throw std::runtime_error("Regional selection changes between files.");
                    auto& target = accumulated[i].records;
                    auto& source = loaded.cells[i].records;
                    target.insert(target.end(), std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
                }
            }
        } catch (const std::exception& error) {
            throw std::runtime_error(path.string() + ": " + error.what());
        }
    }
    for (auto& cell : accumulated) {
        try {
            auto report = analyze(std::move(cell.records), options);
            result.cells.push_back({cell.latitudeIndex, cell.longitudeIndex, cell.latitude, cell.longitude, std::move(report)});
        } catch (const std::exception& error) {
            throw std::runtime_error("Cell (" + std::to_string(cell.latitude) + ", " + std::to_string(cell.longitude) + "): " + error.what());
        }
    }
    result.start = result.cells.front().report.start;
    result.end = result.cells.front().report.end;
    return result;
}
namespace {
std::ofstream outputFile(const std::filesystem::path& path) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not create output: " + path.string());
    stream.imbue(std::locale::classic());
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << std::setprecision(17);
    return stream;
}
Json number(std::optional<double> value) { return value ? Json(*value) : Json(nullptr); }
}
void writeRegionalReport(const RegionalReport& result, const std::filesystem::path& output,
                         const Options& options, const std::filesystem::path& boundaryPath,
                         const std::string& feature) {
    if (result.cells.empty()) throw std::runtime_error("No regional cells to export.");
    if (std::filesystem::exists(output)) throw std::runtime_error("Output directory already exists: " + output.string());
    if (!std::filesystem::create_directories(output)) throw std::runtime_error("Could not create output directory.");
    auto summary = outputFile(output / "regional.csv");
    summary << "cell_id,latitude,longitude,status,reason,observed_months,absent_months,missing_value_months,complete_years,hac_lag_calendar_years,slope_c_per_decade,ci95_lower_c_per_decade,ci95_upper_c_per_decade,approximate_p_value,r_squared\n";
    auto annual = outputFile(output / "annual.csv");
    annual << "cell_id,latitude,longitude,year,observed_months,valid_months,valid_days,complete,annual_mean_c,partial_mean_c,missing_months\n";
    auto gaps = outputFile(output / "gaps.csv");
    gaps << "cell_id,month,reason\n";
    Json features = Json::array();
    std::size_t fitted = 0;
    for (const auto& cell : result.cells) {
        const auto id = std::to_string(cell.latitudeIndex) + "_" + std::to_string(cell.longitudeIndex);
        const auto& r = cell.report;
        const auto& t = r.trend;
        const std::string status = t.available ? "ok" : "insufficient_data";
        const auto missing = std::count_if(r.monthly.begin(), r.monthly.end(), [](const auto& row) { return !row.kelvin; });
        summary << id << ',' << cell.latitude << ',' << cell.longitude << ',' << status << ',' << csvField(t.reason)
                << ',' << r.monthly.size() << ',' << r.absentMonths.size() << ',' << missing << ',' << t.n << ',';
        if (t.available) {
            ++fitted;
            summary << t.lag << ',' << t.slope * 10 << ',' << t.lower * 10 << ',' << t.upper * 10 << ',' << t.p << ',';
        } else summary << ",,,,,";
        if (t.rSquared) summary << *t.rSquared;
        summary << '\n';
        for (const auto& row : r.annual) {
            annual << id << ',' << cell.latitude << ',' << cell.longitude << ',' << row.year << ',' << row.observedMonths << ','
                   << row.validMonths << ',' << row.validDays << ',' << (row.mean ? "true" : "false") << ',';
            if (row.mean) annual << *row.mean;
            annual << ',';
            if (row.partialMean) annual << *row.partialMean;
            annual << ',' << csvField(row.missingMonths) << '\n';
        }
        for (auto date : r.absentMonths) gaps << id << ',' << monthText(date) << ",no_observation\n";
        for (const auto& row : r.monthly) if (!row.kelvin) gaps << id << ',' << monthText(row.date) << ',' << row.status << '\n';
        features.push_back({{"type", "Feature"}, {"id", id},
            {"geometry", {{"type", "Point"}, {"coordinates", {cell.longitude, cell.latitude}}}},
            {"properties", {{"cell_id", id}, {"status", status}, {"reason", t.reason}, {"complete_years", t.n},
                {"observed_months", r.monthly.size()}, {"absent_months", r.absentMonths.size()}, {"missing_value_months", missing},
                {"hac_lag_calendar_years", t.available ? Json(t.lag) : Json(nullptr)},
                {"slope_c_per_decade", t.available ? Json(t.slope * 10) : Json(nullptr)},
                {"ci95_lower_c_per_decade", t.available ? Json(t.lower * 10) : Json(nullptr)},
                {"ci95_upper_c_per_decade", t.available ? Json(t.upper * 10) : Json(nullptr)},
                {"approximate_p_value", t.available ? Json(t.p) : Json(nullptr)}, {"r_squared", number(t.rSquared)}}}});
    }
    summary.close(); annual.close(); gaps.close();
    auto geojson = outputFile(output / "cells.geojson");
    geojson << Json{{"type", "FeatureCollection"}, {"features", features}}.dump(2) << '\n';
    geojson.close();
    bool hasBoundarySource = false;
    std::string attribution = "No boundary provenance sidecar was supplied. Consult the original boundary provider for attribution and terms.";
    Json metadata = {{"schema_version", 1}, {"software_version", ARCES_VERSION}, {"variable", "T2M"},
        {"start_month", monthText(result.start)}, {"end_month", monthText(result.end)},
        {"input_file_count", result.inputFiles}, {"selected_cells", result.cells.size()}, {"fitted_cells", fitted},
        {"bounds", {{"south", result.bounds.south}, {"north", result.bounds.north}, {"west", result.bounds.west}, {"east", result.bounds.east}}},
        {"minimum_complete_years", options.minimumYears}, {"requested_hac_lag", options.hacLag ? Json(*options.hacLag) : Json(nullptr)},
        {"boundary_file", boundaryPath.empty() ? Json(nullptr) : Json("boundary.geojson")}, {"boundary_feature", feature},
        {"selection", "Grid-cell centres in inclusive bounds, optionally covered by the boundary; polygon edges included, hole interiors excluded."},
        {"annual_aggregation", "Calendar-day weighted; 12 valid months required; no imputation."},
        {"method", "Per-cell OLS and approximate Newey-West HAC / Student-t inference, as in single-point analysis."},
        {"warnings", {"Exploratory estimates; not causal attribution or forecasts.",
                      "Point selection is not area clipping, land masking, or a province-wide average.",
                      "Per-cell intervals and p-values are approximate and not adjusted for multiple comparisons.",
                      "Boundary simplification can change selection near borders; input source identity is user supplied."}}};
    if (!boundaryPath.empty()) {
        std::filesystem::copy_file(boundaryPath, output / "boundary.geojson");
        auto source = boundaryPath;
        source.replace_extension(".source.json");
        if (std::filesystem::is_regular_file(source)) {
            std::filesystem::copy_file(source, output / "boundary.source.json");
            metadata["boundary_attribution_file"] = "boundary.source.json";
            hasBoundarySource = true;
            std::ifstream sourceStream(source);
            const auto provenance = Json::parse(sourceStream, nullptr, false);
            if (provenance.is_object() && provenance.contains("attribution") && provenance["attribution"].is_string())
                attribution = provenance["attribution"].get<std::string>();
        }
    }
    const auto map = regionalMapSvg(result, attribution);
    auto svg = outputFile(output / "regional-trend.svg");
    svg << map; svg.close();
    auto html = outputFile(output / "report.html");
    html << regionalMapHtml(result, map, hasBoundarySource, attribution); html.close();
    metadata["map"] = {{"file", "regional-trend.svg"}, {"geometry", "cell centres"},
        {"projection", "Local equirectangular at midpoint latitude"}, {"colour_scale", "Symmetric around zero, run-specific extent"},
        {"uncertainty_marker", "Dark outline when approximate 95% HAC interval includes zero"}};
    auto json = outputFile(output / "regional.json");
    json << metadata.dump(2) << '\n';
    json.close();
}
}
