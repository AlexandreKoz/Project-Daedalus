[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$DxcPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Preset = if ($Configuration -eq "Debug") { "windows-msvc-debug" } else { "windows-msvc-release" }

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
