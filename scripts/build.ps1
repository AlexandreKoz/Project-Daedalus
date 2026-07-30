[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Preset = if ($Configuration -eq "Debug") { "windows-msvc-debug" } else { "windows-msvc-release" }

Push-Location $RepositoryRoot
try {
    Write-Host "Building Project Daedalus with preset '$Preset'..."
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
