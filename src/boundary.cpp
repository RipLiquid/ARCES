#include "boundary.hpp"
#include <boost/geometry.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace arces {
namespace bg = boost::geometry;
using Json = nlohmann::json;
using Point = bg::model::d2::point_xy<double>;
using Polygon = bg::model::polygon<Point>;
struct Boundary::Impl {
    bg::model::multi_polygon<Polygon> polygons;
    GeographicBounds box;
    std::vector<bg::model::box<Point>> envelopes;
    std::string label;
};
namespace {
std::string label(const Json& feature) {
    if (!feature.contains("properties") || !feature["properties"].is_object()) return {};
    for (const auto* key : {"name", "shapeName", "NAME"}) {
        const auto& props = feature["properties"];
        if (props.contains(key) && props[key].is_string()) return props[key].get<std::string>();
    }
    return {};
}
auto ring(const Json& coordinates) {
    Polygon::ring_type result;
    if (!coordinates.is_array() || coordinates.size() < 4)
        throw std::runtime_error("Boundary rings require at least four positions, including closure.");
    for (const auto& position : coordinates) {
        if (!position.is_array() || position.size() < 2 || !position[0].is_number() || !position[1].is_number())
            throw std::runtime_error("Boundary positions must be [longitude, latitude].");
        const double lon = position[0].get<double>(), lat = position[1].get<double>();
        validateBounds({lat, lat, lon, lon});
        if (!result.empty() && std::abs(lon - bg::get<0>(result.back())) > 180)
            throw std::runtime_error("Antimeridian-crossing boundaries are not supported.");
        result.emplace_back(lon, lat);
    }
    if (!bg::equals(result.front(), result.back())) throw std::runtime_error("Boundary ring is not closed.");
    return result;
}
Polygon polygon(const Json& coordinates) {
    if (!coordinates.is_array() || coordinates.empty()) throw std::runtime_error("Boundary polygon is empty.");
    Polygon result;
    result.outer() = ring(coordinates[0]);
    for (std::size_t i = 1; i < coordinates.size(); ++i) result.inners().push_back(ring(coordinates[i]));
    bg::correct(result);
    std::string reason;
    if (!bg::is_valid(result, reason)) throw std::runtime_error("Invalid boundary polygon: " + reason);
    return result;
}
}

Boundary Boundary::load(const std::filesystem::path& path, const std::string& featureName) {
    if (std::filesystem::file_size(path) > 32 * 1024 * 1024)
        throw std::runtime_error("Boundary GeoJSON exceeds the 32 MiB input limit.");
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Could not open boundary: " + path.string());
    Json root;
    stream >> root;
    if (root.contains("crs")) throw std::runtime_error("Use WGS84 GeoJSON without a legacy crs member.");
    Json selected = root;
    if (root.value("type", "") == "FeatureCollection") {
        if (!root.contains("features") || !root["features"].is_array()) throw std::runtime_error("Invalid FeatureCollection.");
        std::vector<Json> matches;
        for (const auto& item : root["features"])
            if (featureName.empty() || label(item) == featureName) matches.push_back(item);
        if (matches.size() != 1) throw std::runtime_error("Boundary must select exactly one feature; use --feature with its name or shapeName.");
        selected = matches.front();
    } else if (!featureName.empty() && label(root) != featureName) {
        throw std::runtime_error("Requested boundary feature was not found.");
    }
    if (selected.contains("crs")) throw std::runtime_error("Use WGS84 GeoJSON without a legacy crs member.");
    auto value = std::make_shared<Impl>();
    value->label = label(selected);
    Json geometry = selected.value("type", "") == "Feature" ? selected.at("geometry") : selected;
    if (!geometry.is_object() || geometry.contains("crs")) throw std::runtime_error("Invalid WGS84 boundary geometry.");
    const auto type = geometry.value("type", "");
    if (type == "Polygon") value->polygons.push_back(polygon(geometry.at("coordinates")));
    else if (type == "MultiPolygon") {
        const auto& coordinates = geometry.at("coordinates");
        if (!coordinates.is_array()) throw std::runtime_error("Invalid MultiPolygon coordinates.");
        for (const auto& item : coordinates) value->polygons.push_back(polygon(item));
    } else throw std::runtime_error("Boundary must be a Polygon or MultiPolygon.");
    if (value->polygons.empty()) throw std::runtime_error("Boundary MultiPolygon is empty.");
    // Components are individually validated by polygon(). For centre selection
    // they form a union, so overlapping components do not double-count cells.
    // Avoid the quadratic cross-component validity check for thousands of islands.
    for (const auto& component : value->polygons) {
        bg::model::box<Point> box;
        bg::envelope(component, box);
        value->envelopes.push_back(box);
    }
    bg::model::box<Point> envelope;
    bg::envelope(value->polygons, envelope);
    value->box = {bg::get<bg::min_corner, 1>(envelope), bg::get<bg::max_corner, 1>(envelope),
                  bg::get<bg::min_corner, 0>(envelope), bg::get<bg::max_corner, 0>(envelope)};
    return Boundary(value);
}

bool Boundary::covers(double latitude, double longitude) const {
    validateBounds({latitude, latitude, longitude, longitude});
    const Point point(longitude, latitude);
    for (std::size_t i = 0; i < impl->polygons.size(); ++i)
        if (bg::covered_by(point, impl->envelopes[i]) && bg::covered_by(point, impl->polygons[i])) return true;
    return false;
}
BoundaryCoordinates Boundary::coordinates() const {
    BoundaryCoordinates result;
    for (const auto& polygon : impl->polygons) {
        BoundaryPolygon item;
        const auto add = [&](const auto& ring) {
            BoundaryRing points;
            for (const auto& point : ring) points.emplace_back(bg::get<0>(point), bg::get<1>(point));
            item.push_back(std::move(points));
        };
        add(polygon.outer());
        for (const auto& hole : polygon.inners()) add(hole);
        result.push_back(std::move(item));
    }
    return result;
}
GeographicBounds Boundary::bounds() const { return impl->box; }
const std::string& Boundary::name() const { return impl->label; }
}
