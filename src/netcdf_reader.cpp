#include "netcdf_reader.hpp"
#include "time_utils.hpp"

#include <netcdf.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace arces {
namespace {

void check(int status, const std::string& operation) {
    if (status != NC_NOERR)
        throw std::runtime_error(operation + ": " + nc_strerror(status));
}

class File {
public:
    int id = -1;
    explicit File(const std::filesystem::path& path) {
        check(nc_open(path.string().c_str(), NC_NOWRITE, &id), "Could not open file");
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { if (id != -1) nc_close(id); }
    void close() {
        const int status = nc_close(id);
        id = -1;
        check(status, "Could not close file");
    }
};

bool numeric(nc_type type) {
    return type == NC_BYTE || type == NC_UBYTE || type == NC_SHORT ||
           type == NC_USHORT || type == NC_INT || type == NC_UINT ||
           type == NC_INT64 || type == NC_UINT64 || type == NC_FLOAT || type == NC_DOUBLE;
}

std::string textAttribute(int file, int variable, const char* name, bool required = true) {
    nc_type type;
    std::size_t size = 0;
    const int status = nc_inq_att(file, variable, name, &type, &size);
    if (status == NC_ENOTATT && !required) return {};
    check(status, std::string("Could not inspect attribute ") + name);
    if (type != NC_CHAR || size == 0 || size > 65536)
        throw std::runtime_error(std::string("Expected a short text attribute: ") + name);
    std::string result(size, '\0');
    check(nc_get_att_text(file, variable, name, result.data()), "Could not read text attribute");
    while (!result.empty() && (result.back() == '\0' || result.back() == ' ')) result.pop_back();
    return result;
}

std::vector<double> numbers(int file, int variable, const char* name) {
    nc_type type;
    std::size_t size = 0;
    const int status = nc_inq_att(file, variable, name, &type, &size);
    if (status == NC_ENOTATT) return {};
    check(status, std::string("Could not inspect attribute ") + name);
    if (!numeric(type) || size == 0 || size > 1024)
        throw std::runtime_error(std::string("Invalid numeric attribute: ") + name);
    std::vector<double> values(size);
    check(nc_get_att_double(file, variable, name, values.data()), "Could not read numeric attribute");
    return values;
}

double scalar(int file, int variable, const char* name, double fallback) {
    const auto values = numbers(file, variable, name);
    if (values.empty()) return fallback;
    if (values.size() != 1 || !std::isfinite(values[0]))
        throw std::runtime_error(std::string("Expected a finite scalar: ") + name);
    return values[0];
}

int variable(int file, const char* name) {
    int id = -1;
    check(nc_inq_varid(file, name, &id), std::string("Could not find ") + name);
    return id;
}

std::vector<double> coordinate(int file, int id, int expectedDimension) {
    int rank = 0;
    nc_type type;
    check(nc_inq_varndims(file, id, &rank), "Could not inspect coordinate rank");
    check(nc_inq_vartype(file, id, &type), "Could not inspect coordinate type");
    if (rank != 1 || !numeric(type)) throw std::runtime_error("Coordinates must be one-dimensional numeric arrays.");
    int dimension = -1;
    check(nc_inq_vardimid(file, id, &dimension), "Could not inspect coordinate dimension");
    if (dimension != expectedDimension) throw std::runtime_error("Coordinate dimension does not match T2M.");
    std::size_t count = 0;
    check(nc_inq_dimlen(file, dimension, &count), "Could not read coordinate size");
    if (!count) throw std::runtime_error("Coordinate array is empty.");
    if (!numbers(file, id, "scale_factor").empty() || !numbers(file, id, "add_offset").empty())
        throw std::runtime_error("Packed coordinate arrays are not supported.");
    std::vector<double> values(count);
    check(nc_get_var_double(file, id, values.data()), "Could not read coordinate");
    for (double value : values)
        if (!std::isfinite(value)) throw std::runtime_error("Coordinate contains a non-finite value.");
    return values;
}

double defaultFill(nc_type type) {
    switch (type) {
    case NC_FLOAT: return NC_FILL_FLOAT;
    case NC_DOUBLE: return NC_FILL_DOUBLE;
    case NC_BYTE: return NC_FILL_BYTE;
    case NC_UBYTE: return NC_FILL_UBYTE;
    case NC_SHORT: return NC_FILL_SHORT;
    case NC_USHORT: return NC_FILL_USHORT;
    case NC_INT: return NC_FILL_INT;
    case NC_UINT: return NC_FILL_UINT;
    case NC_INT64: return static_cast<double>(NC_FILL_INT64);
    case NC_UINT64: return static_cast<double>(NC_FILL_UINT64);
    default: throw std::runtime_error("T2M must have a numeric type.");
    }
}

bool matches(double value, const std::vector<double>& markers) {
    return std::any_of(markers.begin(), markers.end(), [value](double marker) {
        return value == marker || (std::isnan(value) && std::isnan(marker));
    });
}


struct Input {
    File file;
    int temperature = -1;
    std::vector<double> latitudes, longitudes, times, fill, missing, validRange;
    std::vector<std::string> timestamps;
    std::string timeUnits, calendar;
    double validMin = 0, validMax = 0, scale = 1, offset = 0;

    explicit Input(const std::filesystem::path& path) : file(path) {
        temperature = variable(file.id, "T2M");
        int rank = 0;
        check(nc_inq_varndims(file.id, temperature, &rank), "Could not inspect T2M rank");
        if (rank != 3) throw std::runtime_error("Expected T2M dimensions [time, lat, lon].");
        int ids[3]{};
        check(nc_inq_vardimid(file.id, temperature, ids), "Could not inspect T2M dimensions");
        const char* expected[] = {"time", "lat", "lon"};
        for (int i = 0; i < 3; ++i) {
            char name[NC_MAX_NAME + 1]{};
            check(nc_inq_dimname(file.id, ids[i], name), "Could not read dimension name");
            if (std::string(name) != expected[i]) throw std::runtime_error("Expected T2M dimensions [time, lat, lon].");
        }
        if (textAttribute(file.id, temperature, "units") != "K")
            throw std::runtime_error("Expected T2M temperature units K (kelvin).");

        latitudes = coordinate(file.id, variable(file.id, "lat"), ids[1]);
        longitudes = coordinate(file.id, variable(file.id, "lon"), ids[2]);
        const int timeId = variable(file.id, "time");
        times = coordinate(file.id, timeId, ids[0]);

        timeUnits = textAttribute(file.id, timeId, "units");
        calendar = textAttribute(file.id, timeId, "calendar", false);

        nc_type temperatureType;
        check(nc_inq_vartype(file.id, temperature, &temperatureType), "Could not inspect T2M type");
        fill = numbers(file.id, temperature, "_FillValue");
        if (fill.empty()) fill.push_back(defaultFill(temperatureType));
        if (fill.size() != 1) throw std::runtime_error("Expected one _FillValue.");
        missing = numbers(file.id, temperature, "missing_value");
        const auto timeMissing = numbers(file.id, timeId, "missing_value");
        const auto timeFill = numbers(file.id, timeId, "_FillValue");
        validRange = numbers(file.id, temperature, "valid_range");
        if (!validRange.empty() && (validRange.size() != 2 || !std::isfinite(validRange[0]) ||
            !std::isfinite(validRange[1]) || validRange[0] > validRange[1]))
            throw std::runtime_error("Invalid T2M valid_range.");
        validMin = scalar(file.id, temperature, "valid_min", -std::numeric_limits<double>::infinity());
        validMax = scalar(file.id, temperature, "valid_max", std::numeric_limits<double>::infinity());
        scale = scalar(file.id, temperature, "scale_factor", 1);
        offset = scalar(file.id, temperature, "add_offset", 0);
        if (validMin > validMax) throw std::runtime_error("T2M valid_min exceeds valid_max.");


        for (double latitude : latitudes)
            if (latitude < -90 || latitude > 90) throw std::runtime_error("Latitude outside [-90, 90].");
        for (double longitude : longitudes)
            if (longitude < -180 || longitude > 360) throw std::runtime_error("Unsupported longitude coordinate range.");
        for (double time : times) {
            if (matches(time, timeFill) || matches(time, timeMissing))
                throw std::runtime_error("Missing time coordinate.");
            timestamps.push_back(decodeTimestamp(time, timeUnits, calendar));
        }
    }

    Record record(double raw, std::size_t t, std::size_t lat, std::size_t lon,
                  const std::string& source) const {
        Record row;
        row.timestamp = timestamps[t];
        row.date = parseMonth(row.timestamp.substr(0, 7));
        row.latitude = latitudes[lat];
        row.longitude = std::remainder(longitudes[lon], 360.0);
        row.source = source;
        const bool outside = raw < validMin || raw > validMax ||
            (!validRange.empty() && (raw < validRange[0] || raw > validRange[1]));
        if (!std::isfinite(raw) || matches(raw, fill) || matches(raw, missing)) row.status = "missing_value";
        else if (outside) row.status = "outside_valid_range";
        else {
            const double value = raw * scale + offset;
            if (!std::isfinite(value) || value < 0) row.status = "invalid_temperature";
            else row.kelvin = value;
        }
        return row;
    }
};

void uniqueAxis(const std::vector<double>& values, bool longitude) {
    std::vector<double> sorted = values;
    if (longitude)
        for (auto& value : sorted) value = std::remainder(value, 360.0);
    // +180 and -180 are the same geographic meridian.
    for (auto& value : sorted) if (longitude && value == 180) value = -180;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
        throw std::runtime_error("Duplicate grid coordinates are not supported for regional reads.");
}

}

void validateBounds(const GeographicBounds& b) {
    if (!std::isfinite(b.south) || !std::isfinite(b.north) ||
        !std::isfinite(b.west) || !std::isfinite(b.east) ||
        b.south < -90 || b.north > 90 || b.south > b.north ||
        b.west < -180 || b.east > 180 || b.west > b.east)
        throw std::invalid_argument("Invalid geographic bounds; use south <= north and west <= east in degrees (no antimeridian crossing).");
}

LoadedFile readFile(const std::filesystem::path& path, double latitude, double longitude) {
    if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180)
        throw std::invalid_argument("Invalid requested coordinates.");
    Input input(path);
    LoadedFile loaded;
    int dimensions = 0;
    check(nc_inq_ndims(input.file.id, &dimensions), "Could not inspect dimensions");
    for (int i = 0; i < dimensions; ++i) {
        char name[NC_MAX_NAME + 1]{};
        std::size_t count = 0;
        check(nc_inq_dim(input.file.id, i, name, &count), "Could not read dimension");
        loaded.dimensions.emplace_back(name, count);
    }
    loaded.latitudeCount = input.latitudes.size();
    loaded.longitudeCount = input.longitudes.size();
    loaded.timeUnits = input.timeUnits;
    loaded.calendar = input.calendar;
    loaded.firstTime = input.times.front();
    double bestLat = std::numeric_limits<double>::infinity(), bestLon = bestLat;
    for (std::size_t i = 0; i < input.latitudes.size(); ++i) {
        const double distance = std::abs(input.latitudes[i] - latitude);
        if (distance < bestLat) { bestLat = distance; loaded.latitudeIndex = i; }
    }
    for (std::size_t i = 0; i < input.longitudes.size(); ++i) {
        const double distance = std::abs(std::remainder(input.longitudes[i] - longitude, 360.0));
        if (distance < bestLon) { bestLon = distance; loaded.longitudeIndex = i; }
    }
    const auto source = path.generic_string();
    for (std::size_t t = 0; t < input.times.size(); ++t) {
        const std::size_t index[] = {t, loaded.latitudeIndex, loaded.longitudeIndex};
        double raw = 0;
        check(nc_get_var1_double(input.file.id, input.temperature, index, &raw), "Could not read temperature");
        loaded.records.push_back(input.record(raw, t, loaded.latitudeIndex, loaded.longitudeIndex, source));
    }
    input.file.close();
    return loaded;
}

LoadedRegion readRegion(const std::filesystem::path& path, const GeographicBounds& bounds) {
    validateBounds(bounds);
    Input input(path);
    uniqueAxis(input.latitudes, false);
    uniqueAxis(input.longitudes, true);
    LoadedRegion loaded;
    loaded.latitudes = input.latitudes;
    loaded.longitudes = input.longitudes;
    loaded.timeUnits = input.timeUnits;
    loaded.calendar = input.calendar;
    std::vector<std::size_t> selectedLongitudes;
    for (std::size_t j = 0; j < input.longitudes.size(); ++j) {
        const double lon = std::remainder(input.longitudes[j], 360.0);
        if (lon >= bounds.west && lon <= bounds.east) selectedLongitudes.push_back(j);
    }
    const auto source = path.generic_string();
    for (std::size_t i = 0; i < input.latitudes.size(); ++i) {
        const double lat = input.latitudes[i];
        if (lat < bounds.south || lat > bounds.north) continue;
        const auto base = loaded.cells.size();
        for (auto j : selectedLongitudes) {
            GridCell cell{i, j, lat, std::remainder(input.longitudes[j], 360.0), {}};
            cell.records.reserve(input.times.size());
            loaded.cells.push_back(std::move(cell));
        }
        // Read contiguous longitude runs, one time/latitude row at a time.
        // This avoids reopening the file for each cell or allocating a global cube.
        for (std::size_t first = 0; first < selectedLongitudes.size();) {
            std::size_t last = first + 1;
            while (last < selectedLongitudes.size() && selectedLongitudes[last] == selectedLongitudes[last - 1] + 1) ++last;
            const std::size_t count[] = {1, 1, last - first};
            std::vector<double> raw(last - first);
            for (std::size_t t = 0; t < input.times.size(); ++t) {
                const std::size_t start[] = {t, i, selectedLongitudes[first]};
                check(nc_get_vara_double(input.file.id, input.temperature, start, count, raw.data()), "Could not read regional temperature row");
                for (std::size_t k = first; k < last; ++k)
                    loaded.cells[base + k].records.push_back(input.record(raw[k - first], t, i, selectedLongitudes[k], source));
            }
            first = last;
        }
    }
    input.file.close();
    return loaded;
}

std::vector<std::filesystem::path> findInputs(const std::filesystem::path& path) {
    if (std::filesystem::is_regular_file(path)) return {path};
    if (!std::filesystem::is_directory(path)) throw std::runtime_error("Input is not a file or directory: " + path.string());
    std::vector<std::filesystem::path> result;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (!entry.is_regular_file()) continue;
        auto extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".nc4" || extension == ".nc" || extension == ".cdf") result.push_back(entry.path());
    }
    std::sort(result.begin(), result.end());
    if (result.empty()) throw std::runtime_error("No NetCDF files found in input directory.");
    return result;
}

}
