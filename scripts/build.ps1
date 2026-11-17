param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    if (-not $env:VCPKG_ROOT) {
        $siblingVcpkg = Join-Path (Split-Path $projectRoot -Parent) 'vcpkg'
        $userVcpkg = Join-Path $env:USERPROFILE 'vcpkg'
        if (Test-Path -LiteralPath (Join-Path $siblingVcpkg 'vcpkg.exe')) { $env:VCPKG_ROOT = $siblingVcpkg }
        elseif (Test-Path -LiteralPath (Join-Path $userVcpkg 'vcpkg.exe')) { $env:VCPKG_ROOT = $userVcpkg }
        else { throw 'Set VCPKG_ROOT to your bootstrapped vcpkg folder first.' }
    }
    $expectedCommit = (Get-Content vcpkg-commit.txt -Raw).Trim()
    $actualCommit = (git -C "$env:VCPKG_ROOT" rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect vcpkg version.' }
    if ($actualCommit -ne $expectedCommit) {
        throw "vcpkg is not at the project revision. In your vcpkg checkout, use: git checkout --detach $expectedCommit (preserve any local vcpkg work first)."
    }
    cmake --preset windows-static
    if ($LASTEXITCODE -ne 0) { throw 'Configuration failed.' }
    $preset = if ($Configuration -eq 'Debug') { 'static-debug' } else { 'static-release' }
    cmake --build --preset $preset --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    ctest --preset $preset
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
} finally { Pop-Location }
