#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <netcdf.h>
#include <stdexcept>
#include <string>
#include <vector>

// Read a one-dimensional coordinate variable.
// The caller remains responsible for closing the file.
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

    // Parse the requested coordinates.
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

    // Open the input file read-only.
    int fileId = -1;
    const int openStatus = nc_open(path, NC_NOWRITE, &fileId);

    if (openStatus != NC_NOERR)
    {
        std::cerr << "Could not open file: "
                  << nc_strerror(openStatus) << '\n';
        return 1;
    }

    std::cout << "Successfully opened:\n" << path << '\n';

    // Inspect the dimensions defined in the file.
    int dimensionCount = 0;
    int inspectionStatus = nc_inq_ndims(fileId, &dimensionCount);

    if (inspectionStatus != NC_NOERR)
    {
        std::cerr << "Could not inspect dimensions: "
                  << nc_strerror(inspectionStatus) << '\n';
        nc_close(fileId);
        return 1;
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
            std::cerr << "Could not read dimension: "
                      << nc_strerror(inspectionStatus) << '\n';
            nc_close(fileId);
            return 1;
        }

        std::cout << "  " << name << ": " << length << '\n';
    }

    // Find the 2-metre air temperature variable.
    int temperatureId = -1;
    inspectionStatus = nc_inq_varid(fileId, "T2M", &temperatureId);

    if (inspectionStatus != NC_NOERR)
    {
        std::cerr << "Could not find T2M: "
                  << nc_strerror(inspectionStatus) << '\n';
        nc_close(fileId);
        return 1;
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
        std::cerr << "Could not inspect T2M: "
                  << nc_strerror(inspectionStatus) << '\n';
        nc_close(fileId);
        return 1;
    }

    // The reading code expects [time, latitude, longitude].
    if (temperatureDimensionCount != 3)
    {
        std::cerr << "Expected T2M to have three dimensions.\n";
        nc_close(fileId);
        return 1;
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
            std::cerr << "Could not inspect T2M dimension: "
                      << nc_strerror(inspectionStatus) << '\n';
            nc_close(fileId);
            return 1;
        }

        std::cout << "  " << name << ": " << length << '\n';

        if (std::string(name) != expectedDimensions[i] || length == 0)
        {
            std::cerr << "Unexpected T2M dimension structure.\n";
            nc_close(fileId);
            return 1;
        }
    }

    // Read and validate the temperature units.
    std::size_t unitsLength = 0;
    inspectionStatus =
        nc_inq_attlen(fileId, temperatureId, "units", &unitsLength);

    if (inspectionStatus != NC_NOERR)
    {
        std::cerr << "Could not find T2M units: "
                  << nc_strerror(inspectionStatus) << '\n';
        nc_close(fileId);
        return 1;
    }

    std::string units(unitsLength, '\0');

    inspectionStatus =
        nc_get_att_text(fileId, temperatureId, "units", units.data());

    if (inspectionStatus != NC_NOERR)
    {
        std::cerr << "Could not read T2M units: "
                  << nc_strerror(inspectionStatus) << '\n';
        nc_close(fileId);
        return 1;
    }

    std::cout << "\nT2M units: " << units << '\n';

    if (units != "K")
    {
        std::cerr << "Expected kelvin but found units: " << units << '\n';
        nc_close(fileId);
        return 1;
    }

    // Load the geographic coordinates.
    std::vector<double> latitudes;
    std::vector<double> longitudes;

    inspectionStatus = readCoordinate(fileId, "lat", latitudes);

    if (inspectionStatus != NC_NOERR)
    {
        std::cerr << "Could not read latitudes: "
                  << nc_strerror(inspectionStatus) << '\n';
        nc_close(fileId);
        return 1;
    }

    inspectionStatus = readCoordinate(fileId, "lon", longitudes);

    if (inspectionStatus != NC_NOERR)
    {
        std::cerr << "Could not read longitudes: "
                  << nc_strerror(inspectionStatus) << '\n';
        nc_close(fileId);
        return 1;
    }

    std::cout << "\nCoordinates loaded:\n"
              << "  Latitudes: " << latitudes.size() << '\n'
              << "  Longitudes: " << longitudes.size() << '\n';

    if (latitudes.empty() || longitudes.empty())
    {
        std::cerr << "Cannot locate a point: coordinate arrays are empty.\n";
        nc_close(fileId);
        return 1;
    }

    // Find the nearest latitude on this regular grid.
    std::size_t nearestLatitudeIndex = 0;

    for (std::size_t i = 0; i < latitudes.size(); ++i)
    {
        if (!std::isfinite(latitudes[i]))
        {
            std::cerr << "Latitude array contains an invalid coordinate.\n";
            nc_close(fileId);
            return 1;
        }

        if (std::abs(latitudes[i] - targetLatitude) <
            std::abs(latitudes[nearestLatitudeIndex] - targetLatitude))
        {
            nearestLatitudeIndex = i;
        }
    }

    // Use circular distance so -180 and +180 are treated equivalently.
    std::size_t nearestLongitudeIndex = 0;
    double nearestLongitudeDistance = 361.0;

    for (std::size_t i = 0; i < longitudes.size(); ++i)
    {
        if (!std::isfinite(longitudes[i]))
        {
            std::cerr << "Longitude array contains an invalid coordinate.\n";
            nc_close(fileId);
            return 1;
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

    // Read one temperature from the first time step.
    const std::size_t position[3] = {
        0,
        nearestLatitudeIndex,
        nearestLongitudeIndex};

    double temperatureKelvin = 0.0;

    inspectionStatus = nc_get_var1_double(
        fileId,
        temperatureId,
        position,
        &temperatureKelvin);

    if (inspectionStatus != NC_NOERR)
    {
        std::cerr << "Could not read temperature: "
                  << nc_strerror(inspectionStatus) << '\n';
        nc_close(fileId);
        return 1;
    }

    // Check for missing data before converting the temperature.
    double fillValue = 0.0;
    const int fillStatus =
        nc_get_att_double(fileId, temperatureId, "_FillValue", &fillValue);

    if (fillStatus != NC_NOERR && fillStatus != NC_ENOTATT)
    {
        std::cerr << "Could not inspect missing-data value: "
                  << nc_strerror(fillStatus) << '\n';
        nc_close(fileId);
        return 1;
    }

    if (!std::isfinite(temperatureKelvin) ||
        (fillStatus == NC_NOERR && temperatureKelvin == fillValue))
    {
        std::cerr << "Temperature is missing at this grid point.\n";
        nc_close(fileId);
        return 1;
    }

    const double temperatureCelsius = temperatureKelvin - 273.15;

    std::cout << "\nMonthly mean temperature at selected grid point:\n"
              << "  Kelvin: " << temperatureKelvin << '\n'
              << "  Celsius: " << temperatureCelsius << '\n';

    const int closeStatus = nc_close(fileId);

    if (closeStatus != NC_NOERR)
    {
        std::cerr << "Could not close file: "
                  << nc_strerror(closeStatus) << '\n';
        return 1;
    }

    return 0;
}