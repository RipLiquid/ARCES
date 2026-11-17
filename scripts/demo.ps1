param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $executable = Join-Path $projectRoot "build/windows-static/$Configuration/arces.exe"
    $generator = Join-Path $projectRoot "build/windows-static/$Configuration/create_series.exe"
    if (-not (Test-Path -LiteralPath $executable) -or -not (Test-Path -LiteralPath $generator)) {
        throw 'Build with testing enabled first: .\scripts\build.ps1'
    }
    $demoRoot = Join-Path $projectRoot 'build/synthetic-demo'
    & $generator $demoRoot
    if ($LASTEXITCODE -ne 0) { throw 'Synthetic data generation failed.' }
    $resultRoot = Join-Path $projectRoot ('results/synthetic-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    & $executable analyze (Join-Path $demoRoot 'full') 43.65 -79.38 --output $resultRoot
    if ($LASTEXITCODE -ne 0) { throw 'Demo analysis failed.' }
    Write-Host "`nSYNTHETIC DATA: the 1 C/decade trend is deliberately constructed, not a NASA finding."
    Write-Host "Open the report: $(Join-Path $resultRoot 'report.html')"
} finally { Pop-Location }
