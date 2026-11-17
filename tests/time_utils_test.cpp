#include "time_utils.hpp"

#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

void expectTimestamp(
    double offset,
    const std::string& units,
    const std::string& calendar,
    const std::string& expected)
{
    const std::string actual =
        decodeTimestamp(offset, units, calendar);

    if (actual != expected)
    {
        throw std::runtime_error(
            "Expected " + expected + ", got " + actual);
    }
}

void expectRejected(
    double offset,
    const std::string& units,
    const std::string& calendar)
{
    std::string actual;

    try
    {
        actual = decodeTimestamp(offset, units, calendar);
    }
    catch (const std::invalid_argument&)
    {
        return;
    }
    catch (const std::out_of_range&)
    {
        return;
    }

    throw std::runtime_error(
        "Invalid time input was accepted."
        "\nOffset: " + std::to_string(offset) +
        "\nUnits: " + units +
        "\nCalendar: " + calendar +
        "\nReturned timestamp: " + actual);
}

int main()
{
    try
    {
        expectTimestamp(
            0,
            "minutes since 1995-01-01 00:30:00",
            "",
            "1995-01-01T00:30:00");

        expectTimestamp(
            60,
            "minutes since 1995-01-31 23:30:00",
            "standard",
            "1995-02-01T00:30:00");

        expectTimestamp(
            -60,
            "minutes since 1995-01-01 00:30:00",
            "gregorian",
            "1994-12-31T23:30:00");

        expectTimestamp(
            1440,
            "minutes since 2000-02-28 00:00:00",
            "standard",
            "2000-02-29T00:00:00");

        expectTimestamp(
            1440,
            "minutes since 1900-02-28 00:00:00",
            "standard",
            "1900-03-01T00:00:00");

        expectTimestamp(
            0.5,
            "minutes since 1995-01-01 00:00:00",
            "proleptic_gregorian",
            "1995-01-01T00:00:30");

        expectRejected(
            0, "minutes since 1995-02-30 00:00:00", "");

        expectRejected(
            0, "minutes since 1995-01-01 24:00:00", "");

        expectRejected(
            0, "hours since 1995-01-01 00:00:00", "");

        expectRejected(
            0, "minutes since 1995-01-01 00:00:00", "360_day");

        expectRejected(
            std::numeric_limits<double>::infinity(),
            "minutes since 1995-01-01 00:00:00",
            "");

        expectRejected(
            -1, "minutes since 1583-01-01 00:00:00", "");

        expectRejected(
            1, "minutes since 9999-12-31 23:59:00", "");

        expectTimestamp(
            0, "minutes since 9999-12-31 23:59:00", "",
            "9999-12-31T23:59:00");

        expectRejected(
            std::numeric_limits<double>::quiet_NaN(),
            "minutes since 1995-01-01 00:00:00", "");

        expectRejected(
            0.001, "minutes since 1995-01-01 00:00:00", "");

        std::cout << "All timestamp checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Timestamp test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
