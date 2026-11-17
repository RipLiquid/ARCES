#include "analysis.hpp"
#include "netcdf_reader.hpp"

#include <charconv>
#include <cmath>
#include <exception>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

void usage(std::ostream& out) {
    out << "Usage: arces <netcdf-file> <latitude> <longitude> [output.csv]\n"
        << "       arces analyze <file-or-directory> <latitude> <longitude> --output <new-directory>\n"
        << "             [--start YYYY-MM] [--end YYYY-MM] [--min-years N] [--hac-lag N]\n"
        << "       arces --help | --version\n\n"
        << "Input records must be monthly T2M means in kelvin on a regular latitude-longitude grid.\n"
        << "Analysis exports monthly.csv, annual.csv, gaps.csv, and trend.json.\n"
        << "All 12 valid months are required for an annual mean; default minimum is 10 complete years.\n"
        << "Trend uncertainty uses approximate Newey-West HAC / Student-t inference.\n"
        << "Existing output files/directories are never intentionally overwritten.\n";
}

double coordinate(const char* text) {
    try {
        std::size_t end = 0;
        const std::string value(text);
        const double number = std::stod(value, &end);
        if (end != value.size()) throw std::invalid_argument("Trailing characters");
        return number;
    } catch (const std::exception&) {
        throw std::invalid_argument("Latitude and longitude must be valid numbers.");
    }
}

int integer(const std::string& text, const std::string& name) {
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::invalid_argument(name + " must be an integer.");
    return value;
}

void validateCoordinates(double latitude, double longitude) {
    if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
        latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180)
        throw std::invalid_argument("Latitude must be between -90 and 90; longitude must be between -180 and 180.");
}

int extract(const char* input, double latitude, double longitude, const char* output) {
    const auto loaded = arces::readFile(input, latitude, longitude);
    const auto& first = loaded.records.front();
    if (!first.kelvin) throw std::runtime_error("Temperature is missing at this grid point: " + first.status);
    std::cout << "Successfully opened:\n" << input << "\n\nDimensions:\n";
    for (const auto& [name, count] : loaded.dimensions) std::cout << "  " << name << ": " << count << '\n';
    std::cout << "\nT2M dimension order:\n  time: " << loaded.records.size()
              << "\n  lat: " << loaded.latitudeCount << "\n  lon: " << loaded.longitudeCount
              << "\n\nT2M units: K\n\nCoordinates loaded:\n  Latitudes: " << loaded.latitudeCount
              << "\n  Longitudes: " << loaded.longitudeCount
              << "\n\nRequested location:\n  Latitude: " << latitude << "\n  Longitude: " << longitude
              << "\n\nNearest grid point to requested location:\n  Latitude: " << first.latitude
              << " (index " << loaded.latitudeIndex << ")\n  Longitude: " << first.longitude
              << " (index " << loaded.longitudeIndex << ")\n"
              << "\nMonthly mean temperature at selected grid point:\n  Kelvin: " << *first.kelvin
              << "\n  Celsius: " << *first.kelvin - 273.15
              << "\n\nTime metadata:\n  Count: " << loaded.records.size() << "\n  First value: " << loaded.firstTime
              << "\n  Units: " << loaded.timeUnits << "\n  Calendar: "
              << (loaded.calendar.empty() ? "not specified" : loaded.calendar)
              << "\n\nDecoded timestamp: " << first.timestamp << '\n';
    if (loaded.records.size() > 1) std::cout << "Console shows the first record; CSV includes all time steps.\n";
    if (output) {
        arces::writeMonthlyCsv(loaded.records, output);
        std::cout << "CSV saved to: " << output << '\n';
    }
    return 0;
}

}

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") { usage(std::cout); return 0; }
        if (argc == 2 && std::string(argv[1]) == "--version") { std::cout << "ARCES 0.3.0\n"; return 0; }
        const bool analysisMode = argc > 1 && std::string(argv[1]) == "analyze";
        if ((!analysisMode && argc != 4 && argc != 5) || (analysisMode && argc < 7)) {
            usage(std::cerr); return 1;
        }
        const int base = analysisMode ? 2 : 1;
        const double latitude = coordinate(argv[base + 1]);
        const double longitude = coordinate(argv[base + 2]);
        validateCoordinates(latitude, longitude);
        if (!analysisMode) return extract(argv[1], latitude, longitude, argc == 5 ? argv[4] : nullptr);

        arces::Options options;
        std::filesystem::path output;
        bool haveOutput = false, haveStart = false, haveEnd = false, haveMinimum = false, haveLag = false;
        for (int i = 5; i < argc; i += 2) {
            const std::string flag = argv[i];
            if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + flag);
            const std::string value = argv[i + 1];
            if (flag == "--output" && !haveOutput) { output = value; haveOutput = true; }
            else if (flag == "--start" && !haveStart) { options.start = arces::parseMonth(value); haveStart = true; }
            else if (flag == "--end" && !haveEnd) { options.end = arces::parseMonth(value); haveEnd = true; }
            else if (flag == "--min-years" && !haveMinimum) { options.minimumYears = integer(value, flag); haveMinimum = true; }
            else if (flag == "--hac-lag" && !haveLag) { options.hacLag = integer(value, flag); haveLag = true; }
            else throw std::invalid_argument("Unknown or repeated option: " + flag);
        }
        if (!haveOutput || output.empty()) throw std::invalid_argument("Analysis requires --output <new-directory>.");
        if (options.minimumYears < 3) throw std::invalid_argument("Minimum complete years must be at least 3.");
        if (options.hacLag && *options.hacLag < 0) throw std::invalid_argument("HAC lag must not be negative.");
        if (options.start && options.end && *options.start > *options.end)
            throw std::invalid_argument("Start month must not follow end month.");
        if (std::filesystem::exists(output)) throw std::runtime_error("Output directory already exists: " + output.string());
        const auto inputs = arces::findInputs(argv[2]);
        std::vector<arces::Record> records;
        for (const auto& input : inputs) {
            try {
                auto loaded = arces::readFile(input, latitude, longitude);
                records.insert(records.end(), std::make_move_iterator(loaded.records.begin()), std::make_move_iterator(loaded.records.end()));
            } catch (const std::exception& error) {
                throw std::runtime_error(input.string() + ": " + error.what());
            }
        }
        const auto report = arces::analyze(std::move(records), options);
        arces::writeReport(report, output, latitude, longitude, inputs.size(), options);
        std::cout << "Processed " << inputs.size() << " file(s), " << report.monthly.size() << " monthly records.\n"
                  << "Window: " << arces::monthText(report.start) << " to " << arces::monthText(report.end) << '\n'
                  << "Absent months: " << report.absentMonths.size() << "; complete years: " << report.trend.n << '\n';
        if (report.trend.available) {
            const auto& trend = report.trend;
            std::cout << "Trend: " << trend.slope * 10 << " C/decade\n"
                      << "Approximate 95% HAC interval: [" << trend.lower * 10 << ", " << trend.upper * 10 << "] C/decade\n"
                      << "Approximate p-value: " << trend.p << "; HAC lag: " << trend.lag << " calendar years\n"
                      << "Exploratory result; review coverage and assumptions in trend.json.\n";
        } else std::cout << "Trend unavailable: " << report.trend.reason << '\n';
        std::cout << "Results: " << output.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
