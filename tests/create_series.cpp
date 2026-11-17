#include <netcdf.h>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void checked(int status) {
    if (status != NC_NOERR) throw std::runtime_error(nc_strerror(status));
}

void writeYear(const fs::path& path, int year, int mode = 0) {
    int file = -1;
    try {
        checked(nc_create(path.string().c_str(), NC_NETCDF4 | NC_CLOBBER, &file));
        std::vector<double> times, temperatures;
        using namespace std::chrono;
        const sys_seconds reference{sys_days{std::chrono::year{year} / January / 1}};
        for (unsigned m = 1; m <= 12; ++m) {
            if (mode == 1 && year == 2005 && m == 4) continue;
            const sys_seconds date{sys_days{std::chrono::year{year} / month{m} / 1}};
            times.push_back(static_cast<double>(duration_cast<minutes>(date - reference).count()));
            double target = 280.0 + 0.1 * (year - 2000);
            if (mode == 1 && year == 2006 && m == 2) target = -9999;
            if (mode == 1 && year == 2007 && m == 3) target = -8888;
            if (mode == 5) target = -9999;
            if (mode == 2) target = 100; // Unpacked: 100 * 0.1 + 270 = 280 K.
            if (mode == 7) target = 300;
            temperatures.insert(temperatures.end(), {270, target, 290, 300});
        }
        int dimensions[3], variables[4];
        checked(nc_def_dim(file, "time", times.size(), &dimensions[0]));
        checked(nc_def_dim(file, "lat", 2, &dimensions[1]));
        checked(nc_def_dim(file, "lon", 2, &dimensions[2]));
        checked(nc_def_var(file, "time", NC_DOUBLE, 1, &dimensions[0], &variables[0]));
        checked(nc_def_var(file, "lat", NC_DOUBLE, 1, &dimensions[1], &variables[1]));
        checked(nc_def_var(file, "lon", NC_DOUBLE, 1, &dimensions[2], &variables[2]));
        const int transposed[] = {dimensions[1], dimensions[2], dimensions[0]};
        checked(nc_def_var(file, "T2M", NC_DOUBLE, 3, mode == 6 ? transposed : dimensions, &variables[3]));
        const std::string units = std::format("minutes since {:04}-01-01 00:30:00", year);
        checked(nc_put_att_text(file, variables[0], "units", units.size(), units.c_str()));
        checked(nc_put_att_text(file, variables[3], "units", 1, mode == 3 ? "C" : "K"));
        const double fill = -9999, missing = -8888;
        checked(nc_put_att_double(file, variables[3], "_FillValue", NC_DOUBLE, 1, &fill));
        checked(nc_put_att_double(file, variables[3], "missing_value", NC_DOUBLE, 1, &missing));
        if (mode == 2) {
            const double scale = 0.1, offset = 270;
            checked(nc_put_att_double(file, variables[3], "scale_factor", NC_DOUBLE, 1, &scale));
            checked(nc_put_att_double(file, variables[3], "add_offset", NC_DOUBLE, 1, &offset));
        }
        if (mode == 7) {
            const double range[] = {200, 290};
            checked(nc_put_att_double(file, variables[3], "valid_range", NC_DOUBLE, 2, range));
        }
        checked(nc_enddef(file));
        const double latitudes[] = {mode == 4 ? 45.0 : 43.5, 46};
        const double longitudes[] = {-80, -79.375};
        checked(nc_put_var_double(file, variables[0], times.data()));
        checked(nc_put_var_double(file, variables[1], latitudes));
        checked(nc_put_var_double(file, variables[2], longitudes));
        checked(nc_put_var_double(file, variables[3], temperatures.data()));
        const int status = nc_close(file); file = -1; checked(status);
    } catch (...) { if (file != -1) nc_close(file); throw; }
}

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: create_series <synthetic-demo-directory>");
        const fs::path root = fs::absolute(argv[1]);
        if (fs::exists(root) && !fs::exists(root / ".arces-fixture"))
            throw std::runtime_error("Refusing to write fixtures into an existing non-fixture directory.");
        fs::create_directories(root);
        std::ofstream(root / ".arces-fixture") << "Synthetic test data, not NASA observations.\n";
        for (const auto* name : {"full", "gaps", "duplicate", "packed", "wrong-units", "shifted", "all-missing", "wrong-shape", "valid-range"})
            fs::create_directory(root / name);
        for (int year = 2000; year <= 2014; ++year) {
            // Reverse filenames deliberately: ordering must come from metadata.
            const auto name = std::format("chunk_{:02}.nc4", 2015 - year);
            writeYear(root / "full" / name, year);
            writeYear(root / "gaps" / name, year, 1);
        }
        writeYear(root / "duplicate" / "a.nc4", 2000);
        writeYear(root / "duplicate" / "b.nc4", 2000);
        writeYear(root / "packed" / "packed.nc4", 2000, 2);
        writeYear(root / "wrong-units" / "wrong.nc4", 2000, 3);
        writeYear(root / "shifted" / "a.nc4", 2000);
        writeYear(root / "shifted" / "b.nc4", 2001, 4);
        writeYear(root / "all-missing" / "missing.nc4", 2000, 5);
        writeYear(root / "wrong-shape" / "transposed.nc4", 2000, 6);
        writeYear(root / "valid-range" / "range.nc4", 2000, 7);
        std::cout << "Created synthetic 2000-2014 series with a known 1 C/decade trend in " << root << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
