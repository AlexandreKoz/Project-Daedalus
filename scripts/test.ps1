[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [ValidateSet("2022", "2026")]
    [string]$VisualStudioVersion = "2022"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ConfigurationName = $Configuration.ToLowerInvariant()
$PresetPrefix = if ($VisualStudioVersion -eq "2026") { "windows-vs2026" } else { "windows-msvc" }
$Preset = "$PresetPrefix-$ConfigurationName"

Push-Location $RepositoryRoot
try {
    Write-Host "Running CTest with preset '$Preset'..."
    & ctest --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CTest failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
