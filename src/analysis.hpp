#pragma once

#include <filesystem>
#include <compare>
#include <optional>
#include <string>
#include <vector>

namespace arces {

struct Month {
    int year = 0;
    int month = 0;
    auto operator<=>(const Month&) const = default;
};

struct Record {
    std::string timestamp;
    Month date;
    double latitude = 0;
    double longitude = 0;
    std::optional<double> kelvin;
    std::string source;
    std::string status = "valid";
};

struct Annual {
    int year = 0;
    int observedMonths = 0;
    int validMonths = 0;
    int validDays = 0;
    std::optional<double> mean;
    std::optional<double> partialMean;
    std::string missingMonths;
};

struct Trend {
    bool available = false;
    std::string reason;
    int n = 0;
    int lag = 0;
    double meanYear = 0;
    double meanTemperature = 0;
    double slope = 0;
    double standardError = 0;
    double lower = 0;
    double upper = 0;
    double p = 1;
    std::optional<double> rSquared;
    std::optional<double> residualCorrelation;
};

struct Options {
    std::optional<Month> start;
    std::optional<Month> end;
    int minimumYears = 10;
    std::optional<int> hacLag;
};

struct Report {
    std::vector<Record> monthly;
    std::vector<Annual> annual;
    std::vector<Month> absentMonths;
    Month start;
    Month end;
    Trend trend;
};

Month parseMonth(const std::string& text);
std::string monthText(Month date);
int daysInMonth(Month date);
Trend fitTrend(const std::vector<Annual>& annual, int minimumYears,
               std::optional<int> hacLag = std::nullopt);
Report analyze(std::vector<Record> records, const Options& options = {});
void writeReport(const Report& report, const std::filesystem::path& directory,
                 double requestedLatitude, double requestedLongitude,
                 std::size_t inputFiles, const Options& options);
void writeMonthlyCsv(const std::vector<Record>& records,
                     const std::filesystem::path& path);
std::string csvField(const std::string& value);
std::string jsonString(const std::string& value);

}
