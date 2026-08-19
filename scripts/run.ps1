[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [ValidateSet("2022", "2026")]
    [string]$VisualStudioVersion = "2022",
    [switch]$Warp,
    [UInt64]$Frames = 0,
    [string]$Asset = "",
    [string]$Scene = "",
    [string]$ImportReport = "",
    [switch]$DumpScene,
    [ValidateSet("shaded", "normals", "uv", "tangents", "bounds")]
    [string]$Diagnostic = "shaded",
    [UInt64]$StressReloads = 0,
    [string]$StressAlternateAsset = "",
    [switch]$StressResize,
    [switch]$ReportLiveObjects,
    [switch]$NoErrorDialog
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildPrefix = if ($VisualStudioVersion -eq "2026") { "windows-vs2026" } else { "windows-msvc" }
$BuildFolder = "$BuildPrefix-$($Configuration.ToLowerInvariant())"
$Executable = Join-Path $RepositoryRoot "build/$BuildFolder/$Configuration/Daedalus.exe"

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Daedalus executable was not found at '$Executable'. Configure and build $Configuration with Visual Studio $VisualStudioVersion first."
}
if ($PSBoundParameters.ContainsKey("Frames") -and $Frames -eq 0) {
    throw "Frames must be a positive integer when specified."
}
if ($PSBoundParameters.ContainsKey("StressReloads") -and $StressReloads -eq 0) {
    throw "StressReloads must be a positive integer when specified."
}
if ($Scene -and -not $Asset) { throw "Scene requires Asset." }
if ($ImportReport -and -not $Asset) { throw "ImportReport requires Asset." }
if ($StressAlternateAsset -and -not $Asset) { throw "StressAlternateAsset requires Asset." }
if ($StressAlternateAsset -and $StressReloads -eq 0) { throw "StressAlternateAsset requires StressReloads." }

$Arguments = @("--diagnostic", $Diagnostic)
if ($Warp) { $Arguments += "--warp" }
if ($Frames -gt 0) { $Arguments += @("--frames", $Frames.ToString()) }
if ($Asset) { $Arguments += @("--asset", (Resolve-Path $Asset).Path) }
if ($Scene) { $Arguments += @("--scene", $Scene) }
if ($ImportReport) { $Arguments += @("--import-report", [System.IO.Path]::GetFullPath($ImportReport)) }
if ($DumpScene) { $Arguments += "--dump-scene" }
if ($StressReloads -gt 0) { $Arguments += @("--stress-reloads", $StressReloads.ToString()) }
if ($StressAlternateAsset) { $Arguments += @("--stress-alternate-asset", (Resolve-Path $StressAlternateAsset).Path) }
if ($StressResize) { $Arguments += "--stress-resize" }
if ($ReportLiveObjects) { $Arguments += "--report-live-objects" }
if ($NoErrorDialog) { $Arguments += "--no-error-dialog" }

Write-Host "Launching '$Executable' $($Arguments -join ' ')"
& $Executable @Arguments
if ($LASTEXITCODE -ne 0) { throw "Daedalus exited with code $LASTEXITCODE." }
