#include "analysis.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double actual, double expected, double tolerance, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
        throw std::runtime_error(std::string(message) + ": " + std::to_string(actual));
}
template<class F> void rejects(F function, const char* message) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}

std::vector<arces::Record> monthly(int first, int last) {
    std::vector<arces::Record> result;
    for (int year = first; year <= last; ++year) for (int month = 1; month <= 12; ++month) {
        arces::Record record;
        record.date = {year, month};
        record.timestamp = arces::monthText(record.date) + "-01T00:30:00";
        record.latitude = 43.5; record.longitude = -79.375;
        record.kelvin = 280 + 0.1 * (year - first);
        record.source = "synthetic";
        result.push_back(record);
    }
    return result;
}

int main() {
    try {
        require(arces::daysInMonth({2000, 2}) == 29, "Leap-year February");
        require(arces::daysInMonth({1900, 2}) == 28, "Century February");
        rejects([] { arces::parseMonth("2000-13"); }, "Invalid month accepted");
        rejects([] { arces::parseMonth("2000-1"); }, "Noncanonical month accepted");

        auto data = monthly(2000, 2014);
        std::reverse(data.begin(), data.end());
        auto report = arces::analyze(data);
        require(report.monthly.front().date == arces::Month{2000, 1}, "Metadata sorting");
        require(report.trend.n == 15 && report.trend.available, "Complete-year count");
        near(report.trend.slope * 10, 1, 1e-11, "Known warming slope");
        near(report.annual.front().mean.value(), 6.85, 1e-11, "Kelvin conversion");

        auto weighted = monthly(2000, 2000);
        for (auto& row : weighted) row.kelvin = row.date.month == 2 ? 283.15 : 273.15;
        const auto weightedReport = arces::analyze(weighted);
        near(weightedReport.annual[0].mean.value(), 290.0 / 366, 1e-12, "Day-weighted leap-year annual mean");
        require(!weightedReport.trend.available, "Insufficient data must not produce a trend");

        data = monthly(2000, 2014);
        data.erase(data.begin() + 2);
        report = arces::analyze(data);
        require(report.absentMonths.size() == 1 && !report.annual[0].mean, "Missing month excludes annual mean");
        require(report.trend.n == 14, "Incomplete year entered regression");
        data[0].kelvin.reset(); data[0].status = "missing_value";
        report = arces::analyze(data);
        require(report.annual[0].validMonths == 10, "Missing values entered annual mean");
        data.push_back(data.back());
        rejects([&] { arces::analyze(data); }, "Duplicate month accepted");

        data = monthly(2000, 2001);
        data.back().latitude = 44;
        rejects([&] { arces::analyze(data); }, "Changing selected grid point accepted");
        arces::Options options;
        options.start = arces::Month{2002, 1}; options.end = arces::Month{2001, 1};
        rejects([&] { arces::analyze(monthly(2000, 2001), options); }, "Reversed window accepted");
        options.start = arces::Month{2000, 2}; options.end = arces::Month{2001, 11};
        report = arces::analyze(monthly(2000, 2001), options);
        require(report.trend.n == 0 && report.monthly.size() == 22, "Partial-window years fitted");
        options = {}; options.start = arces::Month{1999, 12}; options.end = arces::Month{2002, 1};
        report = arces::analyze(monthly(2000, 2001), options);
        require(report.absentMonths.size() == 2 && report.annual.size() == 4, "Explicit range coverage");

        // Independent reference: tools/reference_stats.py uses exact rational
        // covariance and integrates the t density, rather than Boost Math.
        const double values[] = {10, 11, 10, 13, 12, 15, 14, 16, 15, 18, 17, 20};
        std::vector<arces::Annual> annual;
        for (int i = 0; i < 12; ++i) {
            arces::Annual row; row.year = 2000 + i; row.mean = values[i]; annual.push_back(row);
        }
        const auto trend = arces::fitTrend(annual, 10, 2);
        near(trend.slope, 0.8426573426573427, 1e-13, "Reference slope");
        near(trend.standardError, 0.05174655941608503, 1e-13, "Reference HAC standard error");
        near(trend.lower, 0.7273588231657477, 1e-11, "Reference lower interval");
        near(trend.upper, 0.9579558621489377, 1e-11, "Reference upper interval");
        near(trend.p, 1.583822667537093e-08, 1e-17, "Reference p-value");
        near(trend.rSquared.value(), 0.9045898422290405, 1e-13, "Reference R squared");
        auto gappedYears = annual;
        for (std::size_t i = 5; i < gappedYears.size(); ++i) ++gappedYears[i].year;
        const auto gappedTrend = arces::fitTrend(gappedYears, 10, 2);
        near(gappedTrend.slope, 0.7503454629203132, 1e-13, "Calendar-gap reference slope");
        near(gappedTrend.standardError, 0.0500609624212318, 1e-13, "Calendar-gap HAC reference");
        rejects([&] { arces::fitTrend(annual, 2); }, "Minimum below three accepted");
        rejects([&] { arces::fitTrend(annual, 10, 100); }, "Excessive HAC lag accepted");
        for (auto& row : annual) row.mean = 7;
        const auto constant = arces::fitTrend(annual, 10);
        near(constant.slope, 0, 0, "Constant-series slope");
        near(constant.p, 1, 0, "Constant series incorrectly significant");
        require(!constant.rSquared, "Constant response R squared should be undefined");
        for (std::size_t i = 0; i < annual.size(); ++i) annual[i].mean = 20 - 0.2 * static_cast<double>(i);
        near(arces::fitTrend(annual, 10).slope * 10, -2, 1e-12, "Cooling slope");

        require(arces::csvField("a,b\"c") == "\"a,b\"\"c\"", "CSV escaping");
        require(arces::jsonString("a\n\"b") == "\"a\\n\\\"b\"", "JSON escaping");
        std::cout << "Annual coverage, weights, duplicates, regression, HAC inference, and reference checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
