# Analysis methods and limits

## Input contract

ARCES expects monthly mean `T2M` values in kelvin, with dimensions named and ordered
`[time, lat, lon]`. Latitude, longitude, and time must be numeric one-dimensional
coordinate variables attached to those exact dimensions. Every time step is read.
The application does not authenticate the collection identity or verify that each
record is actually a monthly mean: selecting the right NASA collection remains the
user's responsibility. Daily, instantaneous, and seasonal products must not be mixed
into a monthly series.

Time decoding accepts `minutes since YYYY-MM-DD HH:MM:SS`, whole-second offsets,
and absent/standard/gregorian/proleptic_gregorian calendars over years 1583–9999.
Unsupported units/calendars fail explicitly. Timezone offsets and fractional-second
timestamps are not accepted. The decoded coordinate labels the record; it does not
identify the start or end of the monthly averaging interval.

The nearest coordinate is selected independently on each axis of the regular grid.
Longitude distance wraps around the antimeridian. This is coordinate-based grid
selection, not a general geodesic nearest-neighbor search or spatial interpolation.
All combined records must select the same grid coordinate.

## Missing and packed data

Raw nonfinite temperatures, `_FillValue` (or the NetCDF type's default fill marker),
and `missing_value` markers become missing observations. `valid_range`, `valid_min`,
and `valid_max` are evaluated in the raw representation. Valid values are unpacked
as `raw * scale_factor + add_offset`, then converted with `C = K - 273.15`.
Nonfinite or negative-kelvin unpacked values are flagged. There is no interpolation
or imputation. Packed coordinate arrays are rejected.

Records are sorted by decoded year and month, not by filename. More than one record
in a calendar month is rejected, including overlapping files. Month gaps within the
selected window appear in `gaps.csv`; missing values retain their source and status
in `monthly.csv`. Date filters use inclusive `YYYY-MM` endpoints.

## Annual aggregation

For each year, `annual_mean_c = sum(days_in_month * monthly_mean_c) / days_in_year`.
Gregorian leap years are respected. A valid annual mean requires all 12 months.
Incomplete years receive a blank annual mean and are excluded from regression;
their available-month average is exported separately as `partial_mean_c` and must
not be mistaken for a full annual mean. Partial years at date-window boundaries are
also excluded. Missing-month lists in annual.csv include months outside a partial
boundary window; gaps.csv reports only issues inside the selected window.

## Trend and uncertainty

OLS fits complete annual means `y_i` against their actual calendar year `x_i`:

```
xbar = mean(x); ybar = mean(y)
Sxx = sum((x_i - xbar)^2)
b = sum((x_i - xbar) * (y_i - ybar)) / Sxx
fitted_i = ybar + b * (x_i - xbar)
```

The reported slope is `10*b` in degrees Celsius per decade. The default minimum is
10 complete years, a practical guardrail rather than a scientific guarantee. Users
can set `--min-years` down to 3 for experiments, but short-record inference is weak.

Uncertainty uses a Newey-West HAC slope covariance with Bartlett weights. Let
`e_i = y_i - fitted_i` and `z_i = (x_i-xbar)*e_i`. For lag limit L:

```
V(b) = n/(n-2) / Sxx^2 * [sum(z_i^2)
       + 2 * sum_{i>j, 1<=x_i-x_j<=L}
           (1 - (x_i-x_j)/(L+1)) * z_i*z_j]
```

The automatic lag is `floor(4*(n/100)^(2/9))`; `--hac-lag` overrides it. Lags are
calendar-year distances, so records separated by a missing year are not treated as
adjacent. For consecutive years, the formula matches a common Newey-West estimator
with the n/(n-2) finite-sample correction. Compare sensitivity using several
defensible lag choices; the automatic lag is not an estimated optimal climate model.

The 95% interval is `b +/- t_(n-2, .975)*SE_HAC`. The two-sided p-value uses the same
Student-t reference. These are **approximate** HAC inference calculations, not exact
finite-sample tests. They can be misleading with few years, strong persistence,
structural breaks, or selective missingness. A p-value below .05 is not a statement
of causality or practical importance. No forecasting or attribution is performed.

`r_squared` describes the linear fit and is null for a constant response. The
residual lag-one diagnostic uses consecutive-calendar-year pairs and normalized
residual cross-products; it is not a formal autocorrelation test. Perfect constant
or exactly linear series can have zero residual variance and degenerate intervals;
synthetic demonstrations of this kind are arithmetic checks, not realistic evidence.

## Validation

The C++ tests cover complete and partial years, leap weighting, duplicates, missing
values, fixed-grid consistency, known positive/negative slopes, constant series,
invalid windows, timestamp overflow, and CSV/JSON escaping. End-to-end tests create
NetCDF-4 data and run the real CLI, including missing-value and packed-data cases.

`tools/reference_stats.py` independently obtains a reference OLS and HAC covariance
using Python rational arithmetic and numerically integrates the Student-t density.
The C++ tests compare slope, SE, interval, p-value, and R squared to these constants.
Python is only an optional validation tool; ARCES and CI tests run in C++/CMake.

These checks do not substitute for independent validation of a real climate dataset.
The included synthetic demo has an intentionally constructed 1 degree C/decade trend.

## References

- NASA MERRA-2 monthly collection: https://doi.org/10.5067/AP1B0BA5PD2K
- NetCDF packing and attribute conventions: https://docs.unidata.ucar.edu/netcdf-c/current/attribute_conventions.html
- Newey and West (1987), Econometrica 55(3), 703–708: https://doi.org/10.2307/1913610
- Reference HAC implementation and lag convention: https://www.statsmodels.org/stable/generated/statsmodels.stats.sandwich_covariance.cov_hac.html
- Boost Student-t distribution: https://www.boost.org/doc/libs/latest/libs/math/doc/html/math_toolkit/dist_ref/dists/students_t_dist.html
