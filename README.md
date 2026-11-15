# ARCES
=======

**Analysis of Regional Change in Earth Systems**

A C++ geospatial analysis project for exploring environmental change using NASA Earth science data.

Inspired by the [NASA Space Apps 2026 Earth System Trend Detective challenge](https://www.spaceappschallenge.org/2026/challenges/earth-system-trend-detective/).


## Current Status

ARCES currently provides a command-line tool that reads a monthly NASA MERRA-2 dataset and extracts air temperature at the grid point nearest a requested location.

Implemented features:

- Read NetCDF-4 files using the NetCDF C library from C++.
- Inspect dataset dimensions and temperature metadata.
- Accept an input file, latitude, and longitude.
- Validate coordinates and report input errors.
- Find the nearest position on the dataset's latitude-longitude grid.
- Check temperature values for non-finite values and the `_FillValue` marker.
- Convert temperature from kelvin to Celsius.

Trend analysis, uncertainty estimates, and mapping are planned features.

## Example

Run from the repository root:

```powershell
.\build\windows-static\Debug\arces.exe "data/raw/MERRA2_200.tavgM_2d_slv_Nx.199501.nc4" 43.65 -79.38
```

Selected output from the January 1995 sample:

```text
Requested location:
  Latitude: 43.65
  Longitude: -79.38

Nearest grid point to requested location:
  Latitude: 43.5 (index 267)
  Longitude: -79.375 (index 161)

Monthly mean temperature at selected grid point:
  Kelvin: 271.409
  Celsius: -1.7414
```

This is a model-grid monthly mean near the requested location, not a weather-station measurement.

## Technology

- C++20
- CMake
- Microsoft Visual C++ compiler
- vcpkg
- NetCDF C library with NetCDF-4 support
- HDF5, installed as a dependency

The current build instructions target Windows x64.

## Build

### Requirements

Install:

- Visual Studio 2022 Build Tools with the C++ workload and Windows SDK
- CMake 3.24 or newer
- Git
- vcpkg

VS Code with Microsoft's C/C++ and CMake Tools extensions is recommended.

### Clone

```powershell
git clone https://github.com/RipLiquid/ARCES.git
cd ARCES
```

### Set Up vcpkg

If vcpkg is not already installed:

```powershell
git clone https://github.com/microsoft/vcpkg.git "$env:USERPROFILE\vcpkg"
& "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat" -disableMetrics
```

Set its location for the current PowerShell session:

```powershell
$env:VCPKG_ROOT = "$env:USERPROFILE\vcpkg"
```

If you already installed vcpkg elsewhere, set `VCPKG_ROOT` to that folder instead.

### Configure

Run from the repository root:

```powershell
cmake --preset windows-msvc -B build/windows-static "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>'
```

CMake uses the project's `vcpkg.json` manifest to install dependencies. The initial configuration may take several minutes.

### Compile

```powershell
cmake --build build/windows-static --config Debug
```

The resulting executable is:

```text
build/windows-static/Debug/arces.exe
```

## Get Sample Data

ARCES uses this NASA collection:

| Field | Value |
|---|---|
| Dataset | MERRA-2 monthly mean single-level diagnostics |
| Collection | `M2TMNXSLV` |
| Version | `5.12.4` |
| Variable | `T2M`: air temperature at 2 metres |
| Units | Kelvin |
| Grid spacing | 0.5° latitude × 0.625° longitude |

1. Create a free [Earthdata account](https://urs.earthdata.nasa.gov/).
2. Open the [dataset page](https://www.earthdata.nasa.gov/data/catalog/ges-disc-m2tmnxslv-5.12.4).
3. Select **Data Access → Earthdata Search**.
4. Filter to January 1995 and download the monthly `.nc4` file.
5. Save it under `data/raw/`, retaining its original filename.

Create the local folder if needed:

```powershell
New-Item -ItemType Directory -Force -Path .\data\raw
```

Downloaded data is not included in this repository. Do not commit account credentials or access tokens.

## Usage

```text
arces <netcdf-file> <latitude> <longitude>
```

- Latitude must be between `-90` and `90`.
- Longitude must be between `-180` and `180`.
- Coordinates must be finite numeric values.
- Relative file paths are resolved from the terminal's current directory.
- Exit code `0` indicates success; `1` indicates an error.

## Validation

Manually verified with the January 1995 sample:

- Successful extraction near Toronto.
- Identical temperature before and after the coordinate-reader refactor.
- Missing filename and nonexistent-file errors.
- Rejection of out-of-range and nonnumeric coordinates.

Automated tests and independent scientific validation are planned.

## Data Attribution

Global Modeling and Assimilation Office, & Pawson, S. (2015).
*MERRA-2 tavgM_2d_slv_Nx: Monthly mean single-level diagnostics, V5.12.4.*
NASA Goddard Earth Sciences Data and Information Services Center.

https://doi.org/10.5067/AP1B0BA5PD2K
>>>>>>> 4ab80e9 (Add C++ reader for NASA MERRA-2 temperature data v0.01)
