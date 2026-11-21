#include "regional.hpp"
#include "regional_map.hpp"
#include <netcdf.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace fs = std::filesystem;
using Json = nlohmann::json;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void rejected(const std::function<void()>& action, const std::string& expected) {
    try { action(); }
    catch (const std::exception& e) {
        require(std::string(e.what()).find(expected) != std::string::npos, std::string("Unexpected error: ") + e.what());
        return;
    }
    throw std::runtime_error("Expected rejection: " + expected);
}
void check(int status) { if (status != NC_NOERR) throw std::runtime_error(nc_strerror(status)); }
void fixture(const fs::path& path, int year, int variant = 0) {
    int file = -1;
    check(nc_create(path.string().c_str(), NC_NETCDF4 | NC_CLOBBER, &file));
    try {
        int dims[3], vars[4];
        check(nc_def_dim(file, "time", 12, &dims[0]));
        check(nc_def_dim(file, "lat", 2, &dims[1]));
        check(nc_def_dim(file, "lon", 3, &dims[2]));
        for (int i = 0; i < 3; ++i) check(nc_def_var(file, i == 0 ? "time" : i == 1 ? "lat" : "lon", NC_DOUBLE, 1, &dims[i], &vars[i]));
        check(nc_def_var(file, "T2M", NC_SHORT, 3, dims, &vars[3]));
        const std::string units = "minutes since " + std::to_string(year) + "-01-01 00:30:00";
        check(nc_put_att_text(file, vars[0], "units", units.size(), units.c_str()));
        check(nc_put_att_text(file, vars[3], "units", 1, "K"));
        const short fill = -9999, missing = -8888, range[] = {0, 1000};
        check(nc_put_att_short(file, vars[3], "_FillValue", NC_SHORT, 1, &fill));
        check(nc_put_att_short(file, vars[3], "missing_value", NC_SHORT, 1, &missing));
        check(nc_put_att_short(file, vars[3], "valid_range", NC_SHORT, 2, range));
        const double scale = 0.1, offset = 270;
        check(nc_put_att_double(file, vars[3], "scale_factor", NC_DOUBLE, 1, &scale));
        check(nc_put_att_double(file, vars[3], "add_offset", NC_DOUBLE, 1, &offset));
        check(nc_enddef(file));
        // Descending latitude and 0..360 longitudes exercise both conventions.
        const double lat[] = {44, 43.5};
        const double lon[] = {variant == 2 ? 0.0 : 280.0,
                              variant == 1 ? 280.65 : variant == 2 ? 180.0 : variant == 3 ? 280.0 : 280.625,
                              variant == 2 ? 270.0 : 281.25};
        check(nc_put_var_double(file, vars[1], lat));
        check(nc_put_var_double(file, vars[2], lon));
        std::vector<double> times;
        std::vector<short> temperatures;
        using namespace std::chrono;
        const sys_days reference{std::chrono::year{year}/January/1};
        for (unsigned m = 1; m <= 12; ++m) {
            const sys_days date{std::chrono::year{year}/month{m}/1};
            times.push_back(static_cast<double>(duration_cast<minutes>(date - reference).count()));
            for (int cell = 0; cell < 6; ++cell) {
                short raw = static_cast<short>(100 + cell * 5 + 2 * (year - 2000));
                if (cell == 1) raw = missing;
                if (cell == 0 && year == 2000 && m == 1) raw = fill;
                if (cell == 2 && year == 2000 && m == 2) raw = 1001;
                temperatures.push_back(raw);
            }
        }
        check(nc_put_var_double(file, vars[0], times.data()));
        check(nc_put_var_short(file, vars[3], temperatures.data()));
        const int status = nc_close(file); file = -1; check(status);
    } catch (...) { if (file != -1) nc_close(file); throw; }
}
void write(const fs::path& path, const std::string& text) {
    std::ofstream out(path); out << text; out.close(); require(!out.fail(), "Could not write test input.");
}
Json readJson(const fs::path& path) { std::ifstream in(path); Json value; in >> value; return value; }
int main(int argc, char* argv[]) {
    try {
        require(argc == 3, "Test requires a build-directory path.");
        const auto ontario = arces::Boundary::load(argv[2]);
        require(ontario.covers(43.65,-79.38), "Toronto should be in Ontario.");
        require(ontario.covers(45.4215,-75.6972), "Ottawa should be in Ontario.");
        require(ontario.covers(48.38,-89.25), "Thunder Bay should be in Ontario.");
        require(!ontario.covers(49.28,-123.12), "Vancouver must be outside Ontario.");
        require(!ontario.covers(40.7,-74), "New York must be outside Ontario.");
        const auto root = fs::path(argv[1]) / ("regional-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "full");
        for (int y = 2000; y < 2004; ++y) fixture(root / "full" / (std::to_string(4000-y) + ".nc4"), y);
        const arces::GeographicBounds box{43.5, 44, -80, -78.75};
        const auto input = root / "full" / "2000.nc4";
        auto loaded = arces::readRegion(input, box);
        require(loaded.cells.size() == 6, "Inclusive rectangle must select all 6 cells.");
        require(loaded.cells[0].latitude == 44 && loaded.cells[0].longitude == -80, "Incorrect coordinates/order.");
        require(loaded.cells[0].records[0].status == "missing_value", "Fill marker not preserved.");
        require(loaded.cells[1].records[0].status == "missing_value", "Missing marker not preserved.");
        require(loaded.cells[2].records[1].status == "outside_valid_range", "Raw valid range not checked before unpacking.");
        require(std::abs(*loaded.cells[3].records[0].kelvin - 281.5) < 1e-10, "Packed temperature incorrect.");
        require(loaded.cells[0].records[1].timestamp == "2000-02-01T00:30:00", "Timestamp incorrect.");
        for (const auto& cell : loaded.cells) {
            const auto point = arces::readFile(input, cell.latitude, cell.longitude);
            for (std::size_t i = 0; i < point.records.size(); ++i)
                require(point.records[i].kelvin == cell.records[i].kelvin && point.records[i].status == cell.records[i].status,
                        "Regional values differ from point extraction.");
        }
        require(arces::readRegion(input, {43.5,43.5,-79.375,-79.375}).cells.size() == 1, "Exact-edge selection failed.");
        require(arces::readRegion(input, {0,1,0,1}).cells.empty(), "Empty rectangle should select no cells.");
        rejected([&] { arces::readRegion(input, {44,43,-80,-79}); }, "Invalid geographic bounds");
        rejected([&] { arces::readRegion(input, {0,1,170,-170}); }, "Invalid geographic bounds");
        rejected([&] { arces::readRegion(input, {0,std::numeric_limits<double>::quiet_NaN(),0,1}); }, "Invalid geographic bounds");
        fixture(root / "seam.nc4", 2000, 2);
        const auto seam = arces::readRegion(root / "seam.nc4", {43.5,44,-90,0});
        require(seam.cells.size() == 4 && seam.cells[1].longitudeIndex == 2 &&
                seam.cells[1].records[1].status == "outside_valid_range", "Noncontiguous longitude runs decoded incorrectly.");
        fixture(root / "duplicate-axis.nc4", 2000, 3);
        rejected([&] { arces::readRegion(root / "duplicate-axis.nc4", box); }, "Duplicate grid coordinates");
        arces::Options options; options.minimumYears = 3; options.hacLag = 0;
        const auto region = arces::analyzeRegion(root / "full", box, options);
        require(region.cells.size() == 6 && region.inputFiles == 4, "Regional aggregation lost cells/files.");
        require(region.cells[0].report.trend.n == 3, "Incomplete year entered trend.");
        require(!region.cells[1].report.trend.available && region.cells[1].report.trend.n == 0, "Missing cell should be insufficient.");
        for (std::size_t i = 0; i < region.cells.size(); ++i) if (i != 1)
            require(std::abs(region.cells[i].report.trend.slope * 10 - 2) < 1e-10, "Expected independent known slope 2 C/decade.");
        options.start = arces::Month{2001,1}; options.end = arces::Month{2003,12};
        const auto windowed = arces::analyzeRegion(root / "full", box, options);
        require(windowed.cells[0].report.monthly.size() == 36 && windowed.cells[0].report.trend.n == 3, "Date filtering failed.");
        options.start.reset(); options.end.reset();
        fs::create_directory(root / "duplicate");
        fs::copy_file(input, root / "duplicate/a.nc4"); fs::copy_file(input, root / "duplicate/b.nc4");
        rejected([&] { arces::analyzeRegion(root / "duplicate", box, options); }, "Duplicate month");
        fs::create_directory(root / "shifted");
        fs::copy_file(input, root / "shifted/a.nc4"); fixture(root / "shifted/b.nc4", 2001, 1);
        rejected([&] { arces::analyzeRegion(root / "shifted", box, options); }, "grid coordinates change");
        rejected([&] { arces::analyzeRegion(root / "full", {0,1,0,1}, options); }, "No grid-cell centres");
        const auto boundaryPath = root / "boundary.geojson";
        write(boundaryPath, R"({"type":"Feature","properties":{"name":"Ontario test geometry"},"geometry":{"type":"MultiPolygon","coordinates":[[[[-81,43],[-78,43],[-78,45],[-81,45],[-81,43]],[[-80.2,43.7],[-80.2,44.2],[-79.8,44.2],[-79.8,43.7],[-80.2,43.7]]],[[[-77,41],[-76,41],[-76,42],[-77,42],[-77,41]]]]}})");
        const auto boundary = arces::Boundary::load(boundaryPath);
        require(boundary.covers(43.5,-80) && !boundary.covers(44,-80), "Hole interior selection incorrect.");
        require(boundary.covers(43,-81) && boundary.covers(44,-79.8), "Boundary edges must be included.");
        require(boundary.covers(41.5,-76.5) && !boundary.covers(40,-76.5), "Island selection incorrect.");
        const auto filtered = arces::analyzeRegion(root / "full", box, options, &boundary);
        require(filtered.cells.size() == 5, "Boundary must exclude exactly one fixture cell.");
        const auto map = arces::regionalMapSvg(filtered);
        require(map.find("class=\"unavailable-point\"") != std::string::npos, "Missing-data marker absent from map.");
        require(map.find("fill-rule=\"evenodd\"") != std::string::npos, "Polygon holes not preserved in SVG.");
        require(map.find("1995") == std::string::npos && map.find("2000-01 to 2003-12") != std::string::npos, "Incorrect map period.");
        auto constant = filtered;
        constant.boundary.reset();
        constant.bounds = {43.5,43.5,-80,-80};
        constant.cells.resize(1);
        constant.cells[0].latitude = 43.5; constant.cells[0].longitude = -80;
        constant.cells[0].report.trend.available = true;
        constant.cells[0].report.trend.slope = 0;
        constant.cells[0].report.trend.lower = -0.1;
        constant.cells[0].report.trend.upper = 0.1;
        const auto constantMap = arces::regionalMapSvg(constant);
        require(constantMap.find("fill=\"#f7f7f7\"") != std::string::npos, "Zero trend must use neutral colour.");
        constant.cells[0].report.trend.slope = -0.1;
        require(arces::regionalMapSvg(constant).find("fill=\"#2166ac\"") != std::string::npos, "Cooling must use blue.");
        constant.cells[0].report.trend.slope = 0.1;
        require(arces::regionalMapSvg(constant).find("fill=\"#b2182b\"") != std::string::npos, "Warming must use red.");
        constant.cells[0].report.trend.available = false;
        require(arces::regionalMapSvg(constant).find("Trend unavailable: 1") != std::string::npos, "All-insufficient map failed.");
        const auto escaped = arces::regionalMapHtml(constant, constantMap, false, "<script>&unsafe");
        require(escaped.find("&lt;script&gt;&amp;unsafe") != std::string::npos && escaped.find("<script>") == std::string::npos, "Attribution must be escaped.");
        auto empty = constant; empty.cells.clear();
        rejected([&] { arces::regionalMapSvg(empty); }, "empty region");
        arces::writeRegionalReport(filtered, root / "output", options, boundaryPath, boundary.name());
        const auto geo = readJson(root / "output/cells.geojson");
        require(geo["features"].size() == 5 && geo["features"][0]["properties"]["slope_c_per_decade"].is_null(), "Insufficient cell must export null trend.");
        require(readJson(root / "output/regional.json")["fitted_cells"] == 4, "Incorrect fitted count.");
        require(readJson(root / "output/boundary.geojson") == readJson(boundaryPath), "Boundary provenance copy missing.");
        rejected([&] { arces::writeRegionalReport(filtered, root / "output", options); }, "already exists");
        write(root / "invalid.geojson", R"({"type":"Polygon","coordinates":[[[0,0],[1,1],[1,0],[0,1],[0,0]]]})");
        rejected([&] { arces::Boundary::load(root / "invalid.geojson"); }, "Invalid boundary");
        write(root / "not-closed.geojson", R"({"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1]]]})");
        rejected([&] { arces::Boundary::load(root / "not-closed.geojson"); }, "not closed");
        const Json feature = readJson(boundaryPath);
        write(root / "collection.geojson", Json{{"type","FeatureCollection"},{"features",{feature,Json{{"type","Feature"},{"properties",{{"name","Other"}}},{"geometry",feature["geometry"]}}}}}.dump());
        rejected([&] { arces::Boundary::load(root / "collection.geojson"); }, "exactly one feature");
        require(arces::Boundary::load(root / "collection.geojson", "Other").covers(43.5,-80), "Named feature selection failed.");
        rejected([&] { arces::Boundary::load(root / "collection.geojson", "Absent"); }, "exactly one feature");
        std::cout << "Regional reader, packed/missing values, coverage/trends, bounds, grid consistency, polygon holes/islands, and exports passed.\n";
        // Keep fixture outputs in the ignored build directory for inspection.
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
