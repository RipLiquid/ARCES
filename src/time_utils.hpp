#pragma once

#include <chrono>
#include <cmath>
#include <format>
#include <regex>
#include <stdexcept>
#include <string>

// Supports "minutes since YYYY-MM-DD HH:MM:SS".
// Dates are restricted to Gregorian years 1583 through 9999.
// Fractional seconds and other calendar types are not supported yet.
inline std::string decodeTimestamp(
    double offsetMinutes,
    const std::string &units,
    const std::string &calendar)
{
    if (!calendar.empty() &&
        calendar != "standard" &&
        calendar != "gregorian" &&
        calendar != "proleptic_gregorian")
    {
        throw std::invalid_argument("Unsupported calendar: " + calendar);
    }

    const std::regex pattern(
        R"(^minutes since ([0-9]{4})-([0-9]{2})-([0-9]{2}) ([0-9]{2}):([0-9]{2}):([0-9]{2})$)");

    std::smatch match;

    if (!std::regex_match(units, match, pattern))
    {
        throw std::invalid_argument("Unsupported time units: " + units);
    }

    const int yearValue = std::stoi(match[1].str());
    const unsigned monthValue =
        static_cast<unsigned>(std::stoi(match[2].str()));
    const unsigned dayValue =
        static_cast<unsigned>(std::stoi(match[3].str()));

    const int hourValue = std::stoi(match[4].str());
    const int minuteValue = std::stoi(match[5].str());
    const int secondValue = std::stoi(match[6].str());

    using namespace std::chrono;

    const year_month_day referenceDate{
        year{yearValue}, month{monthValue}, day{dayValue}};

    if (yearValue < 1583 || !referenceDate.ok() ||
        hourValue > 23 || minuteValue > 59 || secondValue > 59)
    {
        throw std::invalid_argument("Invalid or unsupported reference date.");
    }

    // Convert to the wider seconds representation before doing arithmetic.
    const sys_seconds referenceMidnight{sys_days{referenceDate}};
    const sys_seconds reference =
        referenceMidnight +
        hours{hourValue} +
        minutes{minuteValue} +
        seconds{secondValue};

    const double offsetSeconds = offsetMinutes * 60.0;

    if (!std::isfinite(offsetSeconds))
    {
        throw std::invalid_argument("Time offset must be finite.");
    }

    const double roundedSeconds = std::round(offsetSeconds);

    if (std::abs(offsetSeconds - roundedSeconds) > 0.000001)
    {
        throw std::invalid_argument("Fractional seconds are not supported.");
    }

    const sys_seconds earliest =
        sys_days{year{1583} / January / 1};

    const sys_seconds latest =
        sys_days{year{10000} / January / 1};

    if (roundedSeconds <
            static_cast<double>((earliest - reference).count()) ||
        roundedSeconds >=
            static_cast<double>((latest - reference).count()))
    {
        throw std::out_of_range("Decoded timestamp is outside supported years.");
    }

    const sys_seconds timestamp =
        reference + seconds{static_cast<long long>(roundedSeconds)};

    return std::format("{:%FT%T}", timestamp);
}