# Regional analysis (v0.4.0 development)

This milestone reads multiple grid cells, calculates independent per-cell annual
means and trends, and selects cell centres using an Ontario polygon. Each run also exports a static SVG trend map and an offline HTML report. A
province-wide average is not calculated.

## Build and run

The Windows build helper installs pinned vcpkg dependencies, including Boost.Geometry
for polygon validation/selection and nlohmann-json for GeoJSON input/output.

```powershell
.\scripts\build.ps1 -Configuration Release
.\build\windows-static\Release\arces.exe region "C:\path\to\monthly-files" --boundary .\data\boundaries\ontario.geojson --start 1995-01 --end 2024-12 --output .\results\ontario-1995-2024
```

Use a directory containing only the intended monthly NetCDF files. Discovery is
nonrecursive. Do not include a manual sample alongside a complete collection:
duplicate months fail explicitly. All files must contain the same coordinate
arrays in the same order; regridding and stitching spatial tiles are unsupported.
The existing Toronto files can only provide Ontario coverage if their spatial
extent actually includes Ontario. Small downloaded subsets cannot reconstruct
the rest of the province.

Without a polygon, supply an inclusive rectangle instead:

```powershell
.\build\windows-static\Release\arces.exe region "C:\path\to\monthly-files" --south 43 --north 44 --west -80 --east -79 --output .\results\regional-box
```

All four bounds are required together. If bounds and a polygon are both supplied,
selection is their intersection. Otherwise the polygon envelope supplies bounds.
Input coordinates may use -180..180 or 0..360 longitude; exported centres use
-180..180. Descending axes are supported. Duplicate geographic coordinates and
antimeridian-crossing bounds/geometries are rejected.

The reader opens each input once and reads contiguous longitude runs per time
and latitude. It shares point-reader validation, missing-value handling, packed
temperature decoding and timestamp decoding. It does not allocate a global
temperature cube. Selected-cell monthly histories are retained across files, so
memory use grows with the number of selected cells and months.

## Boundary semantics

`--boundary` accepts WGS84 longitude/latitude GeoJSON Polygon, MultiPolygon,
Feature, or a FeatureCollection selecting exactly one feature. For a collection,
use `--feature Ontario` (matches `name`, `shapeName`, or `NAME` properties).
Projected coordinates and legacy CRS members are unsupported. Rings must be closed;
each polygon and its holes are validated with Boost.Geometry after winding
normalization. MultiPolygon components are treated as a union; overlaps across
components are allowed and do not double-count cells. Cross-component topology
is not validated.

Cell **centres** must be inside or on the polygon edge. Hole interiors are excluded;
hole edges and outer edges are included. Islands are supported. This is not polygon
clipping of cell areas, an area-weighted provincial average, or a land-only mask.
No interpolation occurs. The supplied geometry defines the selected domain;
some water/coastal centres may fall outside it. For example, the earlier Toronto
centre (43.5, -79.375) is outside this bundled geometry and is excluded. Simplified
boundaries can affect near-border selection.

The bundled Ontario feature is a pinned geoBoundaries gbOpen CAN ADM1 simplified
boundary, sourced from Statistics Canada. See
[`ontario.source.json`](../data/boundaries/ontario.source.json) for the download URL,
upstream revision, checksums, source dates, license information and attribution.
It represents one administrative boundary snapshot, not changing historical borders.

## Statistics and outputs

Each selected cell uses the existing calendar-day-weighted aggregation: all 12
valid monthly means are required for a complete annual mean. Missing values are
not filled. Date windows, minimum complete years (default 10) and `--hac-lag`
behave as in single-point analysis. Insufficient cells remain in the exports with
null trend statistics. Duplicate months, grid changes and malformed input fail
the run before exports are created. Existing output directories are rejected.

| File | Content |
|---|---|
| `regional-trend.svg` | Static map of centre-point trends, approximate interval markers, coverage and legend |
| `report.html` | Offline map, interpretation, attribution and export links |
| `regional.csv` | One row per cell: coordinates, coverage, status, slope, approximate HAC interval/p-value and RÃƒâ€šÃ‚Â² |
| `annual.csv` | Per-cell annual means and incomplete-year coverage |
| `gaps.csv` | Per-cell absent months and invalid temperatures |
| `cells.geojson` | Selected cell centres and their statistics; load into GIS software |
| `regional.json` | Run settings, bounds, cell counts, selection method and limitations |
| `boundary.geojson` | Exact supplied boundary, when used |
| `boundary.source.json` | Boundary provenance sidecar, if supplied alongside the source boundary |

Cell IDs use original latitude/longitude array indices (`row_column`), and are only
stable for that grid. GeoJSON coordinates are longitude first. Output JSON uses
null, and CSV uses empty fields, for unavailable statistics.

The estimates are exploratory. Per-cell p-values and confidence intervals are
approximate and **not corrected for multiple comparisons**. Do not interpret a
collection of individual p-values as province-wide significance. No regional
mean, causal attribution, or forecasting claim is produced.

## Validation

Automated tests cover packed short values, raw fill and missing markers, raw
valid ranges, decoded timestamps, descending latitude, 0..360 longitude, inclusive
bounds, empty/invalid selection, duplicate months, changing grids, missing years,
known per-cell slopes, and equality with point extraction/analysis. Polygon tests
cover holes, edges, islands, invalid topology and named feature selection. The
bundled Ontario geometry is checked against locations inside and outside Ontario.
CLI tests exercise exports, attribution copying, invalid options and overwrite
protection. Existing point-analysis tests remain enabled.

## Static map and offline report

Open `report.html` in the output folder. It embeds the map and works without a web
server, internet connection, JavaScript, or third-party map tiles. The same figure
is exported as `regional-trend.svg` for reuse in reports.

The map uses a local equirectangular projection with the reference latitude at the
midpoint of the requested bounds. Geographic graticules and a north arrow provide
orientation. It is not intended for precise distance or area measurement.

Circles show selected cell centres, not cell footprints or a continuous surface.
The blue-white-red colour scale is symmetric around zero; its numeric extent is
rounded up to a 0.5 C/decade step for each run. Cross-run comparisons must use the
numeric legends, since the same shade can represent a different value in another
run. Zero slopes use the neutral colour, including all-constant cases.

Dark outlines indicate approximate 95% HAC intervals that include zero. Grey
crosses mark cells with unavailable trends. Unmarked locations are not selected
cells and should not be interpreted as zero trends. Tooltip titles provide exact
cell coordinates, complete-year counts and rounded trend/interval values; use CSV
or GeoJSON for full numeric precision. Boundary holes and islands are retained.
