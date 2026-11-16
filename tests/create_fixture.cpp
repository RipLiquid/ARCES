#include <exception>
#include <iostream>
#include <netcdf.h>
#include <stdexcept>
#include <string>

void check(int status)
{
    if (status != NC_NOERR)
    {
        throw std::runtime_error(nc_strerror(status));
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: create_fixture <output-file>\n";
        return 1;
    }

    int fileId = -1;

    try
    {
        check(nc_create(argv[1], NC_NETCDF4 | NC_CLOBBER, &fileId));

        // One month, two latitudes, and two longitudes.
        int timeDimension = -1;
        int latitudeDimension = -1;
        int longitudeDimension = -1;

        check(nc_def_dim(fileId, "time", 1, &timeDimension));
        check(nc_def_dim(fileId, "lat", 2, &latitudeDimension));
        check(nc_def_dim(fileId, "lon", 2, &longitudeDimension));

        int timeVariable = -1;
        int latitudeVariable = -1;
        int longitudeVariable = -1;
        int temperatureVariable = -1;

        check(nc_def_var(
            fileId, "time", NC_DOUBLE, 1,
            &timeDimension, &timeVariable));

        check(nc_def_var(
            fileId, "lat", NC_DOUBLE, 1,
            &latitudeDimension, &latitudeVariable));

        check(nc_def_var(
            fileId, "lon", NC_DOUBLE, 1,
            &longitudeDimension, &longitudeVariable));

        const int temperatureDimensions[] = {
            timeDimension,
            latitudeDimension,
            longitudeDimension
        };

        check(nc_def_var(
            fileId, "T2M", NC_DOUBLE, 3,
            temperatureDimensions, &temperatureVariable));

        const std::string temperatureUnits = "K";
        const std::string timeUnits =
            "minutes since 1995-01-01 00:30:00";

        check(nc_put_att_text(
            fileId, temperatureVariable, "units",
            temperatureUnits.size(), temperatureUnits.c_str()));

        check(nc_put_att_text(
            fileId, timeVariable, "units",
            timeUnits.size(), timeUnits.c_str()));

        const double fillValue = -9999.0;

        check(nc_put_att_double(
            fileId, temperatureVariable, "_FillValue",
            NC_DOUBLE, 1, &fillValue));

        check(nc_enddef(fileId));

        const double times[] = {0.0};
        const double latitudes[] = {43.5, 44.0};
        const double longitudes[] = {-80.0, -79.375};

        // Stored in [time, latitude, longitude] order.
        // The point nearest Toronto contains 280 K = 6.85 C.
        const double temperatures[] = {
            270.0, 280.0,
            290.0, 300.0
        };

        check(nc_put_var_double(fileId, timeVariable, times));
        check(nc_put_var_double(fileId, latitudeVariable, latitudes));
        check(nc_put_var_double(fileId, longitudeVariable, longitudes));
        check(nc_put_var_double(fileId, temperatureVariable, temperatures));

        const int closeStatus = nc_close(fileId);
        fileId = -1;
        check(closeStatus);

        std::cout << "Created synthetic NetCDF fixture.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        if (fileId != -1)
        {
            nc_close(fileId);
        }

        std::cerr << "Fixture creation failed: " << error.what() << '\n';
        return 1;
    }
}