#pragma once
#include "netcdf_reader.hpp"
#include <memory>

namespace arces {

// WGS84 GeoJSON Polygon/MultiPolygon. Uses cell-centre selection, not area clipping.
using BoundaryRing = std::vector<std::pair<double, double>>;
using BoundaryPolygon = std::vector<BoundaryRing>;
using BoundaryCoordinates = std::vector<BoundaryPolygon>;

class Boundary {
public:
    static Boundary load(const std::filesystem::path& path, const std::string& feature = "");
    bool covers(double latitude, double longitude) const;
    GeographicBounds bounds() const;
    BoundaryCoordinates coordinates() const;
    const std::string& name() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl;
    explicit Boundary(std::shared_ptr<const Impl> value) : impl(std::move(value)) {}
};

}
