#include "analysis.hpp"

#include <boost/math/distributions/students_t.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <numeric>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace arces {
namespace {

Month nextMonth(Month date) {
    if (++date.month == 13) { date.month = 1; ++date.year; }
    return date;
}

std::ofstream outputFile(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) throw std::runtime_error("Output file already exists: " + path.string());
    std::ofstream stream(path, std::ios::binary);
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10);
    if (!stream) throw std::runtime_error("Could not create output: " + path.string());
    return stream;
}

void finish(std::ofstream& stream) {
    stream.close();
    if (!stream) throw std::runtime_error("Could not finish writing output.");
}

void number(std::ostream& stream, std::optional<double> value) {
    if (value) stream << *value;
}

void jsonNumber(std::ostream& stream, std::optional<double> value) {
    if (value && std::isfinite(*value)) stream << *value;
    else stream << "null";
}

std::string annualChart(const Report& report) {
    std::ostringstream svg;
    svg.imbue(std::locale::classic());
    svg << std::setprecision(10);
    svg << R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 960 440" role="img" aria-label="Complete annual temperature means and fitted linear trend"><rect width="960" height="440" fill="#fff"/><g font-family="Segoe UI,Arial,sans-serif" font-size="14" fill="#344054">)";
    std::vector<const Annual*> valid;
    for (const auto& row : report.annual) if (row.mean) valid.push_back(&row);
    if (valid.empty()) {
        svg << R"(<text x="60" y="210">No complete annual means: supply all 12 valid monthly observations for a year.</text></g></svg>)";
        return svg.str();
    }
    double low = *valid.front()->mean, high = low;
    for (const auto* row : valid) { low = std::min(low, *row->mean); high = std::max(high, *row->mean); }
    if (report.trend.available) {
        for (const auto* row : valid) {
            const double fitted = report.trend.meanTemperature + report.trend.slope * (row->year - report.trend.meanYear);
            low = std::min(low, fitted); high = std::max(high, fitted);
        }
    }
    const double padding = std::max(0.2, (high - low) * 0.12);
    low -= padding; high += padding;
    const double first = report.start.year - 0.5, last = report.end.year + 0.5;
    const auto px = [&](double year) { return 85 + 820 * (year - first) / (last - first); };
    const auto py = [&](double value) { return 345 - 280 * (value - low) / (high - low); };
    svg << R"(<text x="85" y="28" font-size="20" font-weight="600">Annual mean temperature at the selected grid point</text><text x="85" y="51" font-size="13">Complete years only; monthly means weighted by calendar days</text>)";
    for (int tick = 0; tick <= 4; ++tick) {
        const double value = low + (high - low) * tick / 4;
        const double y = py(value);
        svg << "<path d=\"M85 " << y << "H905\" stroke=\"#e4e7ec\"/><text x=\"73\" y=\"" << y + 5
            << "\" text-anchor=\"end\">" << std::fixed << std::setprecision(2) << value << "</text>" << std::defaultfloat << std::setprecision(10);
    }
    for (int tick = 0; tick <= 4; ++tick) {
        const int year = report.start.year + (report.end.year - report.start.year) * tick / 4;
        if (tick && year == report.start.year + (report.end.year - report.start.year) * (tick - 1) / 4) continue;
        svg << "<text x=\"" << px(year) << "\" y=\"370\" text-anchor=\"middle\">" << year << "</text>";
    }
    svg << R"svg(<path d="M85 65V345H905" fill="none" stroke="#667085"/><text transform="translate(22 220) rotate(-90)" text-anchor="middle">Temperature (degrees Celsius)</text>)svg";
    if (report.trend.available) {
        const auto& trend = report.trend;
        const int a = valid.front()->year, b = valid.back()->year;
        svg << "<path d=\"M" << px(a) << ' ' << py(trend.meanTemperature + trend.slope * (a - trend.meanYear))
            << 'L' << px(b) << ' ' << py(trend.meanTemperature + trend.slope * (b - trend.meanYear))
            << "\" stroke=\"#d97706\" stroke-width=\"3\" stroke-dasharray=\"8 5\" fill=\"none\"/>";
    }
    for (const auto* row : valid) {
        svg << "<circle cx=\"" << px(row->year) << "\" cy=\"" << py(*row->mean)
            << "\" r=\"4.5\" fill=\"#2563eb\"><title>" << row->year << ": " << *row->mean << " C</title></circle>";
    }
    svg << R"(<circle cx="95" cy="410" r="4" fill="#2563eb"/><text x="109" y="415">Annual mean</text>)";
    if (report.trend.available) svg << R"(<path d="M240 410H275" stroke="#d97706" stroke-width="3" stroke-dasharray="8 5"/><text x="285" y="415">OLS trend (uncertainty reported separately)</text>)";
    svg << "</g></svg>";
    return svg.str();
}

}

Month parseMonth(const std::string& text) {
    static const std::regex pattern(R"(^([0-9]{4})-([0-9]{2})$)");
    std::smatch match;
    if (!std::regex_match(text, match, pattern)) throw std::invalid_argument("Expected date YYYY-MM: " + text);
    Month date{std::stoi(match[1].str()), std::stoi(match[2].str())};
    if (date.year < 1583 || date.year > 9999 || date.month < 1 || date.month > 12)
        throw std::invalid_argument("Date outside supported range: " + text);
    return date;
}

std::string monthText(Month date) { return std::format("{:04}-{:02}", date.year, date.month); }

int daysInMonth(Month date) {
    using namespace std::chrono;
    const year_month_day_last lastDay{year{date.year}, month_day_last{month{static_cast<unsigned>(date.month)}}};
    return static_cast<int>(static_cast<unsigned>(lastDay.day()));
}

Trend fitTrend(const std::vector<Annual>& annual, int minimumYears, std::optional<int> hacLag) {
    if (minimumYears < 3) throw std::invalid_argument("Minimum complete years must be at least 3.");
    if (hacLag && *hacLag < 0) throw std::invalid_argument("HAC lag must not be negative.");
    std::vector<double> x, y;
    int previousYear = 0;
    for (const auto& item : annual) {
        if (item.year <= previousYear) throw std::invalid_argument("Annual records must have unique, ascending years.");
        previousYear = item.year;
        if (!item.mean) continue;
        if (!std::isfinite(*item.mean)) throw std::invalid_argument("Annual mean is not finite.");
        x.push_back(item.year);
        y.push_back(*item.mean);
    }
    Trend result;
    result.n = static_cast<int>(x.size());
    if (result.n < minimumYears) {
        result.reason = "Need at least " + std::to_string(minimumYears) + " complete years; found " + std::to_string(result.n) + ".";
        return result;
    }
    const double n = static_cast<double>(x.size());
    result.meanYear = std::accumulate(x.begin(), x.end(), 0.0) / n;
    result.meanTemperature = std::accumulate(y.begin(), y.end(), 0.0) / n;
    double sxx = 0, sxy = 0, syy = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double dx = x[i] - result.meanYear;
        const double dy = y[i] - result.meanTemperature;
        sxx += dx * dx; sxy += dx * dy; syy += dy * dy;
    }
    if (!(sxx > 0)) throw std::runtime_error("Cannot fit a trend without distinct years.");
    if (!std::isfinite(sxy) || !std::isfinite(syy)) throw std::runtime_error("Temperature magnitudes overflow regression arithmetic.");
    result.slope = sxy / sxx;
    std::vector<double> residual(x.size()), score(x.size());
    double sse = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        residual[i] = y[i] - result.meanTemperature - result.slope * (x[i] - result.meanYear);
        score[i] = (x[i] - result.meanYear) * residual[i];
        sse += residual[i] * residual[i];
    }
    if (syy > 0) result.rSquared = std::clamp(1 - sse / syy, 0.0, 1.0);
    const int span = static_cast<int>(x.back() - x.front());
    result.lag = hacLag.value_or(static_cast<int>(std::floor(4 * std::pow(n / 100.0, 2.0 / 9.0))));
    if (result.lag > span) throw std::invalid_argument("HAC lag exceeds the complete-year time span.");

    // Newey-West slope covariance with Bartlett weights. Lags refer to calendar
    // years, so a missing year is never treated as an adjacent observation.
    double meat = std::inner_product(score.begin(), score.end(), score.begin(), 0.0);
    for (std::size_t i = 0; i < x.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            const double lag = x[i] - x[j];
            if (lag <= result.lag)
                meat += 2 * (1 - lag / (result.lag + 1.0)) * score[i] * score[j];
        }
    }
    const double variance = std::max(0.0, meat) * n / (n - 2) / (sxx * sxx);
    if (!std::isfinite(variance)) throw std::runtime_error("Trend covariance is not finite.");
    result.standardError = std::sqrt(variance);
    const boost::math::students_t distribution(n - 2);
    const double critical = boost::math::quantile(distribution, 0.975);
    result.lower = result.slope - critical * result.standardError;
    result.upper = result.slope + critical * result.standardError;
    if (result.standardError > 0) {
        const double statistic = std::abs(result.slope / result.standardError);
        result.p = 2 * boost::math::cdf(boost::math::complement(distribution, statistic));
    } else result.p = result.slope == 0 ? 1 : 0;

    double cross = 0, left = 0, right = 0;
    for (std::size_t i = 1; i < x.size(); ++i) {
        if (x[i] - x[i - 1] != 1) continue;
        cross += residual[i] * residual[i - 1];
        left += residual[i] * residual[i];
        right += residual[i - 1] * residual[i - 1];
    }
    if (left > 0 && right > 0) result.residualCorrelation = cross / std::sqrt(left * right);
    result.available = true;
    return result;
}

Report analyze(std::vector<Record> records, const Options& options) {
    if (options.minimumYears < 3) throw std::invalid_argument("Minimum complete years must be at least 3.");
    if (options.start && options.end && *options.start > *options.end)
        throw std::invalid_argument("Start month must not follow end month.");
    records.erase(std::remove_if(records.begin(), records.end(), [&](const Record& record) {
        return (options.start && record.date < *options.start) || (options.end && record.date > *options.end);
    }), records.end());
    if (records.empty()) throw std::runtime_error("No records remain in the selected date window.");
    std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) { return a.date < b.date; });
    const auto& location = records.front();
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& item = records[i];
        if (i && item.date == records[i - 1].date)
            throw std::runtime_error("Duplicate month " + monthText(item.date) + ": " + records[i - 1].source + " and " + item.source);
        if (std::abs(item.latitude - location.latitude) > 1e-8 ||
            std::abs(std::remainder(item.longitude - location.longitude, 360.0)) > 1e-8)
            throw std::runtime_error("Selected grid location changes between input files; cannot combine series.");
        if (item.kelvin && (!std::isfinite(*item.kelvin) || *item.kelvin < 0))
            throw std::runtime_error("Record contains an invalid temperature.");
    }
    Report report;
    report.start = options.start.value_or(records.front().date);
    report.end = options.end.value_or(records.back().date);
    report.monthly = std::move(records);
    std::map<Month, const Record*> index;
    for (const auto& record : report.monthly) index.emplace(record.date, &record);
    for (Month date = report.start; date <= report.end; date = nextMonth(date))
        if (!index.contains(date)) report.absentMonths.push_back(date);

    for (int year = report.start.year; year <= report.end.year; ++year) {
        Annual annual;
        annual.year = year;
        double weighted = 0;
        for (int month = 1; month <= 12; ++month) {
            const Month date{year, month};
            const auto found = index.find(date);
            if (found != index.end()) ++annual.observedMonths;
            if (found != index.end() && found->second->kelvin) {
                const int days = daysInMonth(date);
                weighted += (*found->second->kelvin - 273.15) * days;
                annual.validDays += days;
                ++annual.validMonths;
            } else {
                if (!annual.missingMonths.empty()) annual.missingMonths += ';';
                annual.missingMonths += monthText(date);
            }
        }
        if (annual.validDays) annual.partialMean = weighted / annual.validDays;
        if (annual.validMonths == 12) annual.mean = annual.partialMean;
        report.annual.push_back(std::move(annual));
    }
    report.trend = fitTrend(report.annual, options.minimumYears, options.hacLag);
    return report;
}

std::string csvField(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string escaped = "\"";
    for (char c : value) { escaped += c; if (c == '"') escaped += c; }
    return escaped + '"';
}

std::string jsonString(const std::string& value) {
    std::ostringstream stream;
    stream << '"';
    for (unsigned char c : value) {
        switch (c) {
        case '"': stream << "\\\""; break;
        case '\\': stream << "\\\\"; break;
        case '\n': stream << "\\n"; break;
        case '\r': stream << "\\r"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (c < 32) stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
            else stream << c;
        }
    }
    stream << '"';
    return stream.str();
}

void writeMonthlyCsv(const std::vector<Record>& records, const std::filesystem::path& path) {
    auto stream = outputFile(path);
    stream << "timestamp,latitude,longitude,temperature_k,temperature_c,status,source\n";
    for (const auto& record : records) {
        stream << csvField(record.timestamp) << ',' << record.latitude << ',' << record.longitude << ',';
        number(stream, record.kelvin); stream << ',';
        if (record.kelvin) stream << *record.kelvin - 273.15;
        stream << ',' << csvField(record.status) << ',' << csvField(record.source) << '\n';
    }
    finish(stream);
}

void writeReport(const Report& report, const std::filesystem::path& directory,
                 double requestedLatitude, double requestedLongitude,
                 std::size_t inputFiles, const Options& options) {
    if (std::filesystem::exists(directory)) throw std::runtime_error("Output directory already exists: " + directory.string());
    if (!std::filesystem::create_directories(directory)) throw std::runtime_error("Could not create output directory.");
    writeMonthlyCsv(report.monthly, directory / "monthly.csv");
    auto annual = outputFile(directory / "annual.csv");
    annual << "year,observed_months,valid_months,valid_days,complete,annual_mean_c,partial_mean_c,missing_months,fitted_c\n";
    for (const auto& row : report.annual) {
        annual << row.year << ',' << row.observedMonths << ',' << row.validMonths << ',' << row.validDays << ',' << (row.mean ? "true" : "false") << ',';
        number(annual, row.mean); annual << ','; number(annual, row.partialMean);
        annual << ',' << csvField(row.missingMonths) << ',';
        if (row.mean && report.trend.available)
            annual << report.trend.meanTemperature + report.trend.slope * (row.year - report.trend.meanYear);
        annual << '\n';
    }
    finish(annual);
    auto gaps = outputFile(directory / "gaps.csv");
    gaps << "month,reason\n";
    std::map<Month, std::string> issues;
    for (const auto& date : report.absentMonths) issues[date] = "no_observation";
    for (const auto& record : report.monthly) if (!record.kelvin) issues[record.date] = record.status;
    for (const auto& [date, reason] : issues) gaps << monthText(date) << ',' << reason << '\n';
    finish(gaps);

    const auto& trend = report.trend;
    auto json = outputFile(directory / "trend.json");
    json << "{\n  \"schema_version\": 1,\n  \"software_version\": \"0.3.0\",\n"
         << "  \"variable\": \"T2M\",\n  \"source_description\": \"User-supplied monthly NetCDF files; file collection identity is not independently authenticated\",\n"
         << "  \"requested_latitude\": " << requestedLatitude << ",\n  \"requested_longitude\": " << requestedLongitude
         << ",\n  \"selected_latitude\": " << report.monthly.front().latitude
         << ",\n  \"selected_longitude\": " << report.monthly.front().longitude
         << ",\n  \"input_file_count\": " << inputFiles
         << ",\n  \"start_month\": " << jsonString(monthText(report.start))
         << ",\n  \"end_month\": " << jsonString(monthText(report.end))
         << ",\n  \"observed_months\": " << report.monthly.size()
         << ",\n  \"absent_months\": " << report.absentMonths.size()
         << ",\n  \"missing_value_months\": " << std::count_if(report.monthly.begin(), report.monthly.end(), [](const Record& r) { return !r.kelvin; })
         << ",\n  \"minimum_complete_years\": " << options.minimumYears
         << ",\n  \"complete_years\": " << trend.n
         << ",\n  \"status\": " << jsonString(trend.available ? "ok" : "insufficient_data")
         << ",\n  \"reason\": " << jsonString(trend.reason)
         << ",\n  \"annual_aggregation\": \"Calendar-day-weighted mean; all 12 valid months required; no imputation\",\n"
         << "  \"method\": \"OLS on complete annual means; Newey-West HAC slope covariance with Bartlett weights, n/(n-2) correction, and approximate Student-t(n-2) inference\",\n"
         << "  \"hac_lag_calendar_years\": ";
    if (trend.available) json << trend.lag; else json << "null";
    json << ",\n  \"slope_c_per_decade\": "; jsonNumber(json, trend.available ? std::optional(trend.slope * 10) : std::nullopt);
    json << ",\n  \"standard_error_c_per_decade\": "; jsonNumber(json, trend.available ? std::optional(trend.standardError * 10) : std::nullopt);
    json << ",\n  \"ci95_lower_c_per_decade\": "; jsonNumber(json, trend.available ? std::optional(trend.lower * 10) : std::nullopt);
    json << ",\n  \"ci95_upper_c_per_decade\": "; jsonNumber(json, trend.available ? std::optional(trend.upper * 10) : std::nullopt);
    json << ",\n  \"approximate_p_value\": "; jsonNumber(json, trend.available ? std::optional(trend.p) : std::nullopt);
    json << ",\n  \"significant_at_0_05\": ";
    if (trend.available) json << (trend.p < 0.05 ? "true" : "false"); else json << "null";
    json << ",\n  \"r_squared\": "; jsonNumber(json, trend.rSquared);
    json << ",\n  \"residual_lag1_correlation\": "; jsonNumber(json, trend.residualCorrelation);
    json << ",\n  \"mean_year\": "; jsonNumber(json, trend.available ? std::optional(trend.meanYear) : std::nullopt);
    json << ",\n  \"temperature_at_mean_year_c\": "; jsonNumber(json, trend.available ? std::optional(trend.meanTemperature) : std::nullopt);
    json << ",\n  \"warnings\": [\"Exploratory estimate, not causal attribution or a forecast.\","
         << "\"HAC intervals and p-values are approximate, particularly with short records or persistent autocorrelation. Inspect annual.csv, coverage, and sensitivity to HAC lag.\","
         << "\"Only complete years are fitted; missing years can bias results.\","
         << "\"Nearest grid coordinate, no interpolation or station-level interpretation.\"";
    if (trend.n < 10) json << ",\"Fewer than ten complete years; inference is especially unreliable.\"";
    json << "]\n}\n";
    finish(json);

    const auto chart = annualChart(report);
    auto svg = outputFile(directory / "annual-trend.svg");
    svg << chart;
    finish(svg);
    auto html = outputFile(directory / "report.html");
    html << R"(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ARCES trend report</title><style>body{font:16px/1.6 system-ui,sans-serif;color:#182230;background:#f5f7fb;max-width:1050px;margin:40px auto;padding:0 24px}article{background:white;padding:32px;border:1px solid #e4e7ec;border-radius:16px}h1{margin:0;color:#13376b}svg{width:100%;height:auto}table{border-collapse:collapse;width:100%}td,th{text-align:left;border-bottom:1px solid #e4e7ec;padding:10px}a{color:#175cd3}.note{color:#475467}.result{font-size:24px;font-weight:600}</style><article><h1>ARCES</h1><p class="note">Environmental temperature trend analysis | )"
         << monthText(report.start) << " through " << monthText(report.end) << "</p><p>Selected grid coordinate: "
         << report.monthly.front().latitude << ", " << report.monthly.front().longitude << ". Complete years: " << trend.n << ".</p>";
    if (trend.available) {
        html << "<p class=\"result\">" << std::fixed << std::setprecision(3) << trend.slope * 10 << " &deg;C per decade</p>"
             << "<p>Approximate 95% HAC interval: [" << trend.lower * 10 << ", " << trend.upper * 10 << "] &deg;C per decade. "
             << "Approximate p-value: " << std::scientific << trend.p << std::defaultfloat << ".</p>";
    } else html << "<p class=\"result\">Insufficient complete years for trend analysis</p><p>" << trend.reason << "</p>";
    html << chart << R"(<h2>Data and assumptions</h2><p>Monthly means are weighted by the number of calendar days. All 12 valid months are required for an annual mean. Missing values are not filled. Trends use ordinary least squares on complete annual means; confidence intervals use Newey-West covariance and approximate Student-t inference.</p><p class="note">This is an exploratory estimate, not a forecast or causal attribution. Short records, missing years, and persistent autocorrelation can affect inference. Source identity is user-supplied: synthetic demo inputs are not NASA observations.</p><h2>Exports</h2><ul><li><a href="monthly.csv">Monthly records and source paths</a></li><li><a href="annual.csv">Annual means and coverage</a></li><li><a href="gaps.csv">Missing months and values</a></li><li><a href="trend.json">Trend statistics and method</a></li><li><a href="annual-trend.svg">Standalone annual chart</a></li></ul></article></html>)";
    finish(html);
}

}
