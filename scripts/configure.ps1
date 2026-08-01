[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [ValidateSet("2022", "2026")]
    [string]$VisualStudioVersion = "2022",
    [string]$DxcPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ConfigurationName = $Configuration.ToLowerInvariant()
$PresetPrefix = if ($VisualStudioVersion -eq "2026") { "windows-vs2026" } else { "windows-msvc" }
$Preset = "$PresetPrefix-$ConfigurationName"

Push-Location $RepositoryRoot
try {
    Write-Host "Configuring Project Daedalus with preset '$Preset'..."
    $Arguments = @("--preset", $Preset)
    if ($DxcPath) {
        $Arguments += "-DDXC_PATH=$DxcPath"
    }
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
