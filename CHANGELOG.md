# Changelog

## 0.4.0-dev — Regional analysis and static map

- Shared point/regional NetCDF validation and decoding; one open per input file.
- Inclusive geographic bounds, descending axes and normalized longitudes.
- Per-cell annual aggregation and OLS/HAC trends with coverage and null results
  for insufficient data; duplicate-month and changing-grid rejection.
- WGS84 GeoJSON polygon selection with holes, islands and boundary-edge inclusion;
  individual polygons validated with Boost.Geometry, components treated as a union.
- Versioned Ontario boundary with source/license attribution and checksums.
- Regional CSV, annual/gap tables, cell-centre GeoJSON and run metadata.
- Static SVG trend map with a symmetric zero-centred scale, coverage, unavailable
  markers, approximate-interval outlines, and boundary geometry.
- Offline regional HTML report with methods, attribution and export links.
- Reader, boundary, map and end-to-end regional CLI tests.

Interactive maps remain future work. This development milestone does not calculate
a province-wide average or perform area clipping or multiple-testing correction.


## 0.3.0 — Trend-analysis development milestone

- Modular C++ core separating NetCDF access, analysis, and the CLI.
- Dated CSV export with full numeric precision and overwrite checks.
- Directory or multi-time-step input, sorted by decoded metadata.
- Date windows, duplicate-month rejection, grid-location consistency checks.
- Missing-value markers, raw valid ranges, and packed temperature decoding.
- Missing-month reports and day-weighted annual means requiring 12 valid months.
- OLS trends in degrees Celsius per decade, approximate Newey-West HAC intervals,
  approximate two-sided p-values, R squared, and a residual correlation diagnostic.
- Machine-readable JSON, CSV outputs, an offline HTML report, and an SVG chart.
- Synthetic multi-year demo, regression-reference checks, and expanded CI tests.

This milestone stops at a single selected grid location. GIS clipping, province-wide
spatial analysis, and interactive maps remain future work.
