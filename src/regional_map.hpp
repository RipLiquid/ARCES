#pragma once
#include "regional.hpp"
namespace arces {
// Static centre-point map; colours encode per-cell trends, not cell areas.
std::string regionalMapSvg(const RegionalReport& report, const std::string& attribution = "");
std::string regionalMapHtml(const RegionalReport& report, const std::string& svg,
                            bool hasBoundarySource, const std::string& attribution);
}
