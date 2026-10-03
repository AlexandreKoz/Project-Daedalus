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
    [ValidateSet("shaded", "normals", "uv", "tangents", "bounds", "base-color", "metallic", "roughness", "emissive", "material-id", "depth", "overdraw")]
    [string]$Diagnostic = "shaded",
    [ValidateRange(-24.0, 24.0)]
    [double]$Exposure = 0.0,
    [ValidateRange(0.0, 64.0)]
    [double]$EnvironmentIntensity = 1.0,
    [switch]$NoShadows,
    [ValidateRange(0.0, 0.05)][double]$ShadowBias = 0.001,
    [ValidateRange(0.0, 0.05)][double]$ShadowNormalBias = 0.002,
    [string]$Capture = "",
    [string]$CaptureMetadata = "",
    [UInt64]$CaptureFrame = 0,
    [switch]$ValidateHdr,
    [string]$BenchmarkOutput = "",
    [UInt64]$BenchmarkWarmup = 0,
    [UInt64]$StressReloads = 0,
    [string]$StressAlternateAsset = "",
    [switch]$StressResize,
    [switch]$ReportLiveObjects,
    [switch]$NoErrorDialog
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$InvocationDirectory = (Get-Location).Path
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
if ($PSBoundParameters.ContainsKey("CaptureFrame") -and $CaptureFrame -eq 0) { throw "CaptureFrame must be positive when specified." }
if ($CaptureMetadata -and -not ($Capture -or $ValidateHdr)) { throw "CaptureMetadata requires Capture or ValidateHdr." }
if ($BenchmarkOutput -and $Frames -eq 0) { throw "BenchmarkOutput requires Frames." }
if ($Scene -and -not $Asset) { throw "Scene requires Asset." }
if ($ImportReport -and -not $Asset) { throw "ImportReport requires Asset." }
if ($StressAlternateAsset -and -not $Asset) { throw "StressAlternateAsset requires Asset." }
if ($StressAlternateAsset -and $StressReloads -eq 0) { throw "StressAlternateAsset requires StressReloads." }

function Resolve-OutputPath([string]$Path, [string]$BaseDirectory) {
    if ([System.IO.Path]::IsPathFullyQualified($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

$Arguments = @("--diagnostic", $Diagnostic, "--exposure", $Exposure.ToString([System.Globalization.CultureInfo]::InvariantCulture),
    "--environment-intensity", $EnvironmentIntensity.ToString([System.Globalization.CultureInfo]::InvariantCulture))
if ($NoShadows) { $Arguments += "--no-shadows" }
$Arguments += @("--shadow-bias", $ShadowBias.ToString([System.Globalization.CultureInfo]::InvariantCulture))
$Arguments += @("--shadow-normal-bias", $ShadowNormalBias.ToString([System.Globalization.CultureInfo]::InvariantCulture))
if ($Capture) { $Arguments += @("--capture", (Resolve-OutputPath $Capture $InvocationDirectory)) }
if ($CaptureMetadata) { $Arguments += @("--capture-metadata", (Resolve-OutputPath $CaptureMetadata $InvocationDirectory)) }
if ($CaptureFrame -gt 0) { $Arguments += @("--capture-frame", $CaptureFrame.ToString()) }
if ($ValidateHdr) { $Arguments += "--validate-hdr" }
if ($BenchmarkOutput) { $Arguments += @("--benchmark-output", (Resolve-OutputPath $BenchmarkOutput $InvocationDirectory)) }
if ($BenchmarkWarmup -gt 0) { $Arguments += @("--benchmark-warmup", $BenchmarkWarmup.ToString()) }
if ($Warp) { $Arguments += "--warp" }
if ($Frames -gt 0) { $Arguments += @("--frames", $Frames.ToString()) }
if ($Asset) { $Arguments += @("--asset", (Resolve-Path $Asset).Path) }
if ($Scene) { $Arguments += @("--scene", $Scene) }
if ($ImportReport) { $Arguments += @("--import-report", (Resolve-OutputPath $ImportReport $InvocationDirectory)) }
if ($DumpScene) { $Arguments += "--dump-scene" }
if ($StressReloads -gt 0) { $Arguments += @("--stress-reloads", $StressReloads.ToString()) }
if ($StressAlternateAsset) { $Arguments += @("--stress-alternate-asset", (Resolve-Path $StressAlternateAsset).Path) }
if ($StressResize) { $Arguments += "--stress-resize" }
if ($ReportLiveObjects) { $Arguments += "--report-live-objects" }
if ($NoErrorDialog) { $Arguments += "--no-error-dialog" }

Write-Host "Launching '$Executable' $($Arguments -join ' ')"
& $Executable @Arguments
if ($LASTEXITCODE -ne 0) { throw "Daedalus exited with code $LASTEXITCODE." }
