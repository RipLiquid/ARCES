# Changelog

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
