#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <netcdf.h>
#include <stdexcept>
#include <string>
#include <vector>

// Read a one-dimensional coordinate variable.
// This function does not close the file.
int readCoordinate(
    int fileId,
    const char* name,
    std::vector<double>& coordinates)
{
    int variableId = -1;
    int status = nc_inq_varid(fileId, name, &variableId);

    if (status != NC_NOERR)
    {
        return status;
    }

    int dimensionCount = 0;
    status = nc_inq_varndims(fileId, variableId, &dimensionCount);

    if (status != NC_NOERR)
    {
        return status;
    }

    if (dimensionCount != 1)
    {
        return NC_EINVAL;
    }

    int dimensionId = -1;
    status = nc_inq_vardimid(fileId, variableId, &dimensionId);

    if (status != NC_NOERR)
    {
        return status;
    }

    std::size_t length = 0;
    status = nc_inq_dimlen(fileId, dimensionId, &length);

    if (status != NC_NOERR)
    {
        return status;
    }

    coordinates.resize(length);

    if (coordinates.empty())
    {
        return NC_NOERR;
    }

    return nc_get_var_double(fileId, variableId, coordinates.data());
}

int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cerr
            << "Usage: arces <netcdf-file> <latitude> <longitude>\n";
        return 1;
    }

    const char* path = argv[1];

    double targetLatitude = 0.0;
    double targetLongitude = 0.0;

    // Parse command-line coordinates before opening the file.
    try
    {
        const std::string latitudeText = argv[2];
        const std::string longitudeText = argv[3];

        std::size_t latitudeEnd = 0;
        std::size_t longitudeEnd = 0;

        targetLatitude = std::stod(latitudeText, &latitudeEnd);
        targetLongitude = std::stod(longitudeText, &longitudeEnd);

        if (latitudeEnd != latitudeText.size() ||
            longitudeEnd != longitudeText.size())
        {
            throw std::invalid_argument("Unexpected characters");
        }
    }
    catch (const std::exception&)
    {
        std::cerr << "Latitude and longitude must be valid numbers.\n";
        return 1;
    }

    if (!std::isfinite(targetLatitude) ||
        !std::isfinite(targetLongitude) ||
        targetLatitude < -90.0 || targetLatitude > 90.0 ||
        targetLongitude < -180.0 || targetLongitude > 180.0)
    {
        std::cerr << "Latitude must be between -90 and 90; "
                     "longitude must be between -180 and 180.\n";
        return 1;
    }

    // Open the dataset read-only.
    int fileId = -1;
    const int openStatus = nc_open(path, NC_NOWRITE, &fileId);

    if (openStatus != NC_NOERR)
    {
        std::cerr << "Could not open file: "
                  << nc_strerror(openStatus) << '\n';
        return 1;
    }

    // Shared error handler for operations on the open file.
    // Every call to this handler is followed immediately by returning.
    const auto fail = [fileId](
        const std::string& message,
        int status = NC_NOERR) -> int
    {
        std::cerr << message;

        if (status != NC_NOERR)
        {
            std::cerr << ": " << nc_strerror(status);
        }

        std::cerr << '\n';
        nc_close(fileId);
        return 1;
    };

    std::cout << "Successfully opened:\n" << path << '\n';

    // Inspect the file's dimensions.
    int dimensionCount = 0;
    int inspectionStatus = nc_inq_ndims(fileId, &dimensionCount);

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not inspect dimensions", inspectionStatus);
    }

    std::cout << "\nDimensions:\n";

    for (int dimensionId = 0; dimensionId < dimensionCount; ++dimensionId)
    {
        char name[NC_MAX_NAME + 1] = {};
        std::size_t length = 0;

        inspectionStatus =
            nc_inq_dim(fileId, dimensionId, name, &length);

        if (inspectionStatus != NC_NOERR)
        {
            return fail("Could not read dimension", inspectionStatus);
        }

        std::cout << "  " << name << ": " << length << '\n';
    }

    // Find the temperature variable.
    int temperatureId = -1;
    inspectionStatus = nc_inq_varid(fileId, "T2M", &temperatureId);

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not find T2M", inspectionStatus);
    }

    int temperatureDimensionCount = 0;
    int dimensionIds[NC_MAX_VAR_DIMS] = {};

    inspectionStatus = nc_inq_var(
        fileId,
        temperatureId,
        nullptr,
        nullptr,
        &temperatureDimensionCount,
        dimensionIds,
        nullptr);

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not inspect T2M", inspectionStatus);
    }

    if (temperatureDimensionCount != 3)
    {
        return fail("Expected T2M to have three dimensions.");
    }

    const char* expectedDimensions[] = {"time", "lat", "lon"};

    std::cout << "\nT2M dimension order:\n";

    for (int i = 0; i < temperatureDimensionCount; ++i)
    {
        char name[NC_MAX_NAME + 1] = {};
        std::size_t length = 0;

        inspectionStatus =
            nc_inq_dim(fileId, dimensionIds[i], name, &length);

        if (inspectionStatus != NC_NOERR)
        {
            return fail("Could not inspect T2M dimension", inspectionStatus);
        }

        std::cout << "  " << name << ": " << length << '\n';

        if (std::string(name) != expectedDimensions[i] || length == 0)
        {
            return fail("Unexpected T2M dimension structure.");
        }
    }

    // Read the temperature units.
    nc_type unitsType = NC_NAT;
    std::size_t unitsLength = 0;

    inspectionStatus = nc_inq_att(
        fileId,
        temperatureId,
        "units",
        &unitsType,
        &unitsLength);

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not inspect T2M units", inspectionStatus);
    }

    if (unitsType != NC_CHAR || unitsLength == 0)
    {
        return fail("Expected T2M units to be a nonempty text attribute.");
    }

    std::string units(unitsLength, '\0');

    inspectionStatus =
        nc_get_att_text(fileId, temperatureId, "units", units.data());

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not read T2M units", inspectionStatus);
    }

    std::cout << "\nT2M units: " << units << '\n';

    if (units != "K")
    {
        return fail("Expected kelvin but found units: " + units);
    }

    // Load latitude and longitude coordinates.
    std::vector<double> latitudes;
    std::vector<double> longitudes;

    inspectionStatus = readCoordinate(fileId, "lat", latitudes);

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not read latitudes", inspectionStatus);
    }

    inspectionStatus = readCoordinate(fileId, "lon", longitudes);

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not read longitudes", inspectionStatus);
    }

    std::cout << "\nCoordinates loaded:\n"
              << "  Latitudes: " << latitudes.size() << '\n'
              << "  Longitudes: " << longitudes.size() << '\n';

    if (latitudes.empty() || longitudes.empty())
    {
        return fail("Cannot locate a point: coordinate arrays are empty.");
    }

    // Find the nearest latitude.
    std::size_t nearestLatitudeIndex = 0;

    for (std::size_t i = 0; i < latitudes.size(); ++i)
    {
        if (!std::isfinite(latitudes[i]))
        {
            return fail("Latitude array contains an invalid coordinate.");
        }

        if (std::abs(latitudes[i] - targetLatitude) <
            std::abs(latitudes[nearestLatitudeIndex] - targetLatitude))
        {
            nearestLatitudeIndex = i;
        }
    }

    // Find the nearest longitude, accounting for wraparound.
    std::size_t nearestLongitudeIndex = 0;
    double nearestLongitudeDistance = 361.0;

    for (std::size_t i = 0; i < longitudes.size(); ++i)
    {
        if (!std::isfinite(longitudes[i]))
        {
            return fail("Longitude array contains an invalid coordinate.");
        }

        const double distance =
            std::abs(std::remainder(longitudes[i] - targetLongitude, 360.0));

        if (distance < nearestLongitudeDistance)
        {
            nearestLongitudeIndex = i;
            nearestLongitudeDistance = distance;
        }
    }

    std::cout << "\nRequested location:\n"
              << "  Latitude: " << targetLatitude << '\n'
              << "  Longitude: " << targetLongitude << '\n';

    std::cout << "\nNearest grid point to requested location:\n"
              << "  Latitude: " << latitudes[nearestLatitudeIndex]
              << " (index " << nearestLatitudeIndex << ")\n"
              << "  Longitude: " << longitudes[nearestLongitudeIndex]
              << " (index " << nearestLongitudeIndex << ")\n";

    // Read the temperature at the first time step.
    const std::size_t position[3] = {
        0,
        nearestLatitudeIndex,
        nearestLongitudeIndex
    };

    double temperatureKelvin = 0.0;

    inspectionStatus = nc_get_var1_double(
        fileId,
        temperatureId,
        position,
        &temperatureKelvin);

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not read temperature", inspectionStatus);
    }

    // Inspect the missing-value marker before reading it into a scalar.
    nc_type fillType = NC_NAT;
    std::size_t fillLength = 0;

    const int fillStatus = nc_inq_att(
        fileId,
        temperatureId,
        "_FillValue",
        &fillType,
        &fillLength);

    if (fillStatus != NC_NOERR && fillStatus != NC_ENOTATT)
    {
        return fail("Could not inspect missing-data value", fillStatus);
    }

    double fillValue = 0.0;

    if (fillStatus == NC_NOERR)
    {
        if (fillLength != 1)
        {
            return fail("Expected _FillValue to contain exactly one value.");
        }

        inspectionStatus =
            nc_get_att_double(fileId, temperatureId, "_FillValue", &fillValue);

        if (inspectionStatus != NC_NOERR)
        {
            return fail("Could not read missing-data value", inspectionStatus);
        }
    }

    if (!std::isfinite(temperatureKelvin) ||
        (fillStatus == NC_NOERR && temperatureKelvin == fillValue))
    {
        return fail("Temperature is missing at this grid point.");
    }

    const double temperatureCelsius = temperatureKelvin - 273.15;

    std::cout << "\nMonthly mean temperature at selected grid point:\n"
              << "  Kelvin: " << temperatureKelvin << '\n'
              << "  Celsius: " << temperatureCelsius << '\n';

    // Read time metadata while the file is still open.
    std::vector<double> times;
    inspectionStatus = readCoordinate(fileId, "time", times);

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not read time coordinates", inspectionStatus);
    }

    if (times.empty() || !std::isfinite(times.front()))
    {
        return fail("The first time coordinate is missing or invalid.");
    }

    int timeVariableId = -1;
    inspectionStatus = nc_inq_varid(fileId, "time", &timeVariableId);

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not find time variable", inspectionStatus);
    }

    nc_type timeUnitsType = NC_NAT;
    std::size_t timeUnitsLength = 0;

    inspectionStatus = nc_inq_att(
        fileId,
        timeVariableId,
        "units",
        &timeUnitsType,
        &timeUnitsLength);

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not inspect time units", inspectionStatus);
    }

    if (timeUnitsType != NC_CHAR || timeUnitsLength == 0)
    {
        return fail("Expected time units to be a nonempty text attribute.");
    }

    std::string timeUnits(timeUnitsLength, '\0');

    inspectionStatus =
        nc_get_att_text(fileId, timeVariableId, "units", timeUnits.data());

    if (inspectionStatus != NC_NOERR)
    {
        return fail("Could not read time units", inspectionStatus);
    }

    std::cout << "\nTime metadata:\n"
              << "  Count: " << times.size() << '\n'
              << "  First value: " << times.front() << '\n'
              << "  Units: " << timeUnits << '\n';

    // All reads are finished. Close the file last.
    const int closeStatus = nc_close(fileId);

    if (closeStatus != NC_NOERR)
    {
        std::cerr << "Could not close file: "
                  << nc_strerror(closeStatus) << '\n';
        return 1;
    }

    return 0;
}