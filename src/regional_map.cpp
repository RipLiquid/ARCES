#include "regional_map.hpp"
#include "version.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace arces {
namespace {
std::string escape(const std::string& value) {
    std::string result;
    for (char c : value) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result += c;
        }
    }
    return result;
}
std::string fixed(double value, int precision = 2) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}
std::string colour(double slope, double limit) {
    const std::array<int,3> blue{33,102,172}, neutral{247,247,247}, red{178,24,43};
    const auto& target = slope < 0 ? blue : red;
    const double fraction = std::clamp(std::abs(slope) / limit, 0.0, 1.0);
    std::ostringstream out;
    out << '#' << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < 3; ++i)
        out << std::setw(2) << static_cast<int>(std::lround(neutral[i] + fraction * (target[i] - neutral[i])));
    return out.str();
}
struct Projection {
    double west, east, south, north, reference, scale, x0, y0;
    explicit Projection(GeographicBounds b) {
        validateBounds(b);
        // Zero-width selections still get a visible map extent.
        west = std::max(-180.0, b.west - (b.west == b.east ? 0.5 : 0.0));
        east = std::min(180.0, b.east + (b.west == b.east ? 0.5 : 0.0));
        south = std::max(-90.0, b.south - (b.south == b.north ? 0.5 : 0.0));
        north = std::min(90.0, b.north + (b.south == b.north ? 0.5 : 0.0));
        reference = (south + north) / 2;
        const double cosine = std::max(0.01, std::cos(reference * std::numbers::pi / 180.0));
        scale = std::min(790.0 / ((east - west) * cosine), 640.0 / (north - south));
        x0 = 75 + (790 - (east - west) * cosine * scale) / 2;
        y0 = 120 + (640 - (north - south) * scale) / 2;
    }
    double x(double longitude) const {
        const double cosine = std::max(0.01, std::cos(reference * std::numbers::pi / 180.0));
        return x0 + (longitude - west) * cosine * scale;
    }
    double y(double latitude) const { return y0 + (north - latitude) * scale; }
};
void checkReport(const RegionalReport& report) {
    if (report.cells.empty()) throw std::invalid_argument("Cannot map an empty region.");
    validateBounds(report.bounds);
    for (const auto& cell : report.cells) {
        validateBounds({cell.latitude,cell.latitude,cell.longitude,cell.longitude});
        if (cell.report.trend.available &&
            (!std::isfinite(cell.report.trend.slope * 10) || !std::isfinite(cell.report.trend.lower) || !std::isfinite(cell.report.trend.upper)))
            throw std::invalid_argument("Cannot map a non-finite trend.");
    }
}
}

std::string regionalMapSvg(const RegionalReport& report, const std::string& attribution) {
    checkReport(report);
    const Projection projection(report.bounds);
    double maxMagnitude = 0;
    std::size_t fitted = 0, includesZero = 0;
    int minimumYears = std::numeric_limits<int>::max(), maximumYears = 0;
    std::vector<double> xs, ys;
    for (const auto& cell : report.cells) {
        const auto& trend = cell.report.trend;
        minimumYears = std::min(minimumYears, trend.n);
        maximumYears = std::max(maximumYears, trend.n);
        xs.push_back(projection.x(cell.longitude)); ys.push_back(projection.y(cell.latitude));
        if (trend.available) {
            ++fitted;
            maxMagnitude = std::max(maxMagnitude, std::abs(trend.slope * 10));
            if (trend.lower <= 0 && trend.upper >= 0) ++includesZero;
        }
    }
    // Symmetric, zero-centred colour scale, rounded up in 0.5 C/decade steps.
    const double limit = std::max(0.5, std::ceil(maxMagnitude * 2) / 2);
    double spacing = 28;
    for (auto* axis : {&xs, &ys}) {
        std::sort(axis->begin(), axis->end());
        for (std::size_t i = 1; i < axis->size(); ++i) {
            const double distance = (*axis)[i] - (*axis)[i-1];
            if (distance > 1e-6) spacing = std::min(spacing, distance);
        }
    }
    const double radius = std::clamp(spacing * 0.30, 2.5, 7.0);
    std::string credit = "No polygon boundary; geographic-bounds selection.";
    if (report.boundary) credit = "Boundary source and licensing: see report and boundary.source.json where available.";
    if (attribution.find("geoBoundaries") != std::string::npos && attribution.find("Statistics Canada") != std::string::npos)
        credit = "Boundary: geoBoundaries / Statistics Canada. CC BY 4.0 and upstream terms; see boundary.source.json.";
    const std::string name = report.boundary && !report.boundary->name().empty() ? report.boundary->name() : "Regional";
    std::ostringstream svg;
    svg.imbue(std::locale::classic());
    svg << std::fixed << std::setprecision(3);
    svg << R"(<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="940" viewBox="0 0 1200 940" role="img" aria-labelledby="map-title map-desc">
<title id="map-title">)" << escape(name) << R"( temperature trends</title>
<desc id="map-desc">Per-cell temperature trends in degrees Celsius per decade. Coloured circles are selected grid-cell centres; grey crosses indicate unavailable trends. Outlined circles have approximate intervals including zero. No interpolation or area averaging.</desc>
<metadata>)" << escape(attribution) << R"(</metadata>
<defs><linearGradient id="trend-colours" x1="0" y1="1" x2="0" y2="0"><stop offset="0%" stop-color="#2166ac"/><stop offset="50%" stop-color="#f7f7f7"/><stop offset="100%" stop-color="#b2182b"/></linearGradient><clipPath id="map-extent"><rect x="65" y="110" width="815" height="665"/></clipPath></defs>
<rect width="1200" height="940" fill="#f8fafc"/>
<g font-family="Segoe UI,Arial,sans-serif" fill="#172b4d">
<text x="45" y="43" font-size="15" font-weight="700" letter-spacing="2">ARCES</text>
<text x="45" y="78" font-size="27" font-weight="700">)" << escape(name) << R"( temperature trends</text>
<text x="46" y="101" font-size="15" fill="#475569">)" << monthText(report.start) << " to " << monthText(report.end)
        << " | " << report.cells.size() << " selected grid centres | " << fitted << " fitted trends</text>"
        << R"(<rect x="45" y="112" width="840" height="683" rx="12" fill="#eef3f8" stroke="#d6dee8"/>)";
    // Label geographic graticules; projection preserves longitude/latitude lines.
    for (int tick = 0; tick <= 4; ++tick) {
        const double lon = projection.west + (projection.east - projection.west) * tick / 4;
        const double lat = projection.south + (projection.north - projection.south) * tick / 4;
        svg << "<path d=\"M" << projection.x(lon) << ' ' << projection.y(projection.north) << "V" << projection.y(projection.south)
            << "\" stroke=\"#d6dee8\" stroke-width=\"1\"/>"
            << "<text x=\"" << projection.x(lon) << "\" y=\"785\" text-anchor=\"middle\" font-size=\"11\">" << fixed(lon,1) << "&#176;</text>"
            << "<path d=\"M" << projection.x(projection.west) << ' ' << projection.y(lat) << "H" << projection.x(projection.east)
            << "\" stroke=\"#d6dee8\" stroke-width=\"1\"/>"
            << "<text x=\"60\" y=\"" << projection.y(lat)+4 << "\" text-anchor=\"end\" font-size=\"11\">" << fixed(lat,1) << "&#176;</text>";
    }
    svg << "<g clip-path=\"url(#map-extent)\">";
    if (report.boundary) {
        for (const auto& polygon : report.boundary->coordinates()) {
            svg << "<path class=\"boundary\" d=\"";
            for (const auto& ring : polygon) {
                bool first = true;
                for (const auto& [lon,lat] : ring) {
                    svg << (first ? 'M' : 'L') << projection.x(lon) << ' ' << projection.y(lat);
                    first = false;
                }
                svg << 'Z';
            }
            svg << "\" fill=\"#fff\" fill-rule=\"evenodd\" stroke=\"#8293a7\" stroke-width=\"0.7\"/>";
        }
    }
    for (const auto& cell : report.cells) {
        const auto& t = cell.report.trend;
        const auto id = std::to_string(cell.latitudeIndex) + "_" + std::to_string(cell.longitudeIndex);
        const double x = projection.x(cell.longitude), y = projection.y(cell.latitude);
        svg << "<g class=\"grid-cell\" data-cell-id=\"" << id << "\"><title>Cell " << id << ": " << fixed(cell.latitude,4) << ", " << fixed(cell.longitude,4)
            << "; " << t.n << " complete years; ";
        if (t.available) svg << fixed(t.slope * 10,4) << " C/decade; approximate 95% HAC interval [" << fixed(t.lower*10,4) << ", " << fixed(t.upper*10,4) << ']';
        else svg << "Trend unavailable: " << escape(t.reason);
        svg << "</title>";
        if (t.available) {
            const bool zero = t.lower <= 0 && t.upper >= 0;
            svg << "<circle class=\"trend-point\" cx=\"" << x << "\" cy=\"" << y << "\" r=\"" << radius
                << "\" fill=\"" << colour(t.slope*10,limit) << "\" stroke=\"" << (zero ? "#172b4d" : "#fff")
                << "\" stroke-width=\"" << (zero ? 1.6 : 0.6) << "\"/>";
        } else {
            svg << "<path class=\"unavailable-point\" d=\"M" << x-radius << ' ' << y-radius << 'L' << x+radius << ' ' << y+radius
                << 'M' << x-radius << ' ' << y+radius << 'L' << x+radius << ' ' << y-radius << "\" stroke=\"#64748b\" stroke-width=\"2\"/>";
        }
        svg << "</g>";
    }
    svg << R"(</g><path d="M850 151V128L845 138M850 128L855 138" stroke="#172b4d" fill="none" stroke-width="2"/><text x="850" y="123" text-anchor="middle" font-size="11">N</text>
<text x="915" y="145" font-size="18" font-weight="600">Trend</text>
<text x="915" y="169" font-size="14">&#176;C per decade</text>
<rect x="920" y="190" width="30" height="230" fill='url(#trend-colours)' stroke="#94a3b8"/>)"
        << "<text x=\"963\" y=\"199\" font-size=\"14\">+" << fixed(limit) << "</text>"
        << "<text x=\"963\" y=\"310\" font-size=\"14\">0.00</text>"
        << "<text x=\"963\" y=\"420\" font-size=\"14\">-" << fixed(limit) << "</text>"
        << R"(<text x="915" y="450" font-size="12" fill="#475569">Symmetric scale centred on zero</text>
<circle cx="925" cy="484" r="6" fill="#f7f7f7" stroke="#172b4d" stroke-width="1.6"/>
<text x="942" y="480" font-size="12">Approximate interval</text><text x="942" y="497" font-size="12">includes zero: )" << includesZero << R"(</text>
<path d="M920 522L930 532M920 532L930 522" stroke="#64748b" stroke-width="2"/>
<text x="942" y="532" font-size="12">Trend unavailable: )" << report.cells.size()-fitted << R"(</text>
<text x="915" y="577" font-size="18" font-weight="600">Coverage</text>
<text x="915" y="604" font-size="14">Complete years: )" << minimumYears;
    if (minimumYears != maximumYears) svg << " to " << maximumYears;
    svg << R"(</text><text x="915" y="630" font-size="12">All 12 valid months required.</text>
<text x="915" y="651" font-size="12">Missing months are not filled.</text>
<text x="915" y="704" font-size="13" font-weight="600">Cell centres only</text>
<text x="915" y="726" font-size="12">No interpolation or area average.</text>
<text x="45" y="829" font-size="14" font-weight="600">Exploratory per-cell estimates</text>
<text x="45" y="852" font-size="12">HAC intervals are approximate. No spatial multiple-testing adjustment; no causal attribution or forecast.</text>
<text x="45" y="875" font-size="12">Local equirectangular projection; reference latitude )" << fixed(projection.reference,2)
        << R"(&#176;. WGS84 geographic coordinates.</text>
<text x="45" y="898" font-size="12">)" << escape(credit) << R"(</text>
<text x="45" y="922" font-size="11" fill="#64748b">ARCES )" << ARCES_VERSION << R"( | Per-cell statistics: regional.csv | Selected centres: cells.geojson</text>
</g></svg>)";
    return svg.str();
}

std::string regionalMapHtml(const RegionalReport& report, const std::string& svg,
                            bool hasBoundarySource, const std::string& attribution) {
    const std::string name = report.boundary && !report.boundary->name().empty() ? report.boundary->name() : "Regional";
    std::ostringstream html;
    html << R"(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ARCES regional trend report</title><style>body{font:16px/1.6 system-ui,sans-serif;color:#172b4d;background:#f8fafc;margin:32px auto;max-width:1280px;padding:0 20px}article{background:white;border:1px solid #d6dee8;border-radius:16px;padding:24px}h1{margin:0}svg{width:100%;height:auto}a{color:#175cd3}li{margin:8px 0}.note{color:#475569}.map{margin:20px -12px}code{overflow-wrap:anywhere}</style></head><body><article><h1>)"
         << escape(name) << " temperature trend report</h1><p class=\"note\">" << monthText(report.start) << " through " << monthText(report.end)
         << " | " << report.cells.size() << " selected grid-cell centres</p><div class=\"map\">" << svg << "</div>"
         << R"(<h2>Reading the map</h2><p>Circle colour shows the fitted annual temperature trend in degrees Celsius per decade. Blue indicates cooling, red indicates warming, and white is zero. The scale is symmetric around zero and chosen separately for each run; compare numeric legends when comparing maps.</p><p>A dark circle outline means the approximate 95% HAC interval includes zero. Grey crosses indicate insufficient complete years for a trend. Hover over a centre to inspect its coordinates, coverage and estimate. An unmarked location is not a selected cell; it is not a zero trend.</p><h2>Methods and limits</h2><p>Annual means weight monthly temperatures by calendar days and require all 12 valid months. Missing values are not imputed. Trends use ordinary least squares with approximate Newey-West HAC intervals. Per-cell intervals have no spatial multiple-testing adjustment. These exploratory estimates are not a forecast or causal attribution.</p><p>Markers show centres, not clipped grid-cell areas or interpolated surfaces. Boundary holes and islands affect which centres are selected. This report does not calculate an Ontario-wide temperature average or apply a separate land mask.</p><h2>Exports</h2><ul><li><a href="regional-trend.svg">Download the static trend map (SVG)</a></li><li><a href="regional.csv">Per-cell trends and coverage (CSV)</a></li><li><a href="annual.csv">Per-cell annual means (CSV)</a></li><li><a href="gaps.csv">Coverage gaps (CSV)</a></li><li><a href="cells.geojson">Selected centres and statistics (GeoJSON)</a></li><li><a href="regional.json">Run settings and limitations (JSON)</a></li>)";
    if (report.boundary) html << R"(<li><a href="boundary.geojson">Selection boundary (GeoJSON)</a></li>)";
    if (hasBoundarySource) html << R"(<li><a href="boundary.source.json">Boundary source, license and checksums</a></li>)";
    html << "</ul><h2>Attribution</h2><p>" << escape(attribution) << "</p>"
         << R"(<p>Input temperature files are user supplied; collection identity is not independently authenticated by ARCES. For MERRA-2 T2M, cite NASA GMAO and the <a href="https://doi.org/10.5067/AP1B0BA5PD2K">M2TMNXSLV dataset</a>. Synthetic fixtures are not NASA measurements.</p><p class="note">Generated with ARCES )"
         << ARCES_VERSION << "</p></article></body></html>";
    return html.str();
}
}
