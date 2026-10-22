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
    [switch]$NoErrorDialog,
    # Campaigns D-H. Every other renderer option (e.g. --rt-debug, --pt-max-depth, --temporal-debug,
    # --camera-path) can be passed verbatim through -RendererArguments.
    [ValidateSet("", "raster", "hybrid", "pathtrace")]
    [string]$Renderer = "",
    [ValidateSet("", "triangle", "cornell-box", "alpha-instancing")]
    [string]$BuiltinScene = "",
    [string]$CaptureHdr = "",
    [UInt64]$CaptureSamples = 0,
    [ValidateSet("", "none", "bilinear", "dlss")]
    [string]$Upscaler = "",
    [string]$AssetCache = "",
    [string[]]$TextureSearchRoot = @(),
    [string]$Blender = "",
    [string[]]$RendererArguments = @()
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
if ($CaptureMetadata -and -not ($Capture -or $ValidateHdr -or $CaptureHdr)) { throw "CaptureMetadata requires Capture, CaptureHdr or ValidateHdr." }
if ($BenchmarkOutput -and $Frames -eq 0) { throw "BenchmarkOutput requires Frames." }
if ($Scene -and -not $Asset) { throw "Scene requires Asset." }
if ($ImportReport -and -not $Asset) { throw "ImportReport requires Asset." }
if ($StressAlternateAsset -and -not $Asset) { throw "StressAlternateAsset requires Asset." }
if ($StressAlternateAsset -and $StressReloads -eq 0) { throw "StressAlternateAsset requires StressReloads." }
if ($BuiltinScene -and $Asset) { throw "BuiltinScene and Asset are mutually exclusive." }
if ($PSBoundParameters.ContainsKey("CaptureSamples") -and $CaptureSamples -eq 0) { throw "CaptureSamples must be positive when specified." }
if ($CaptureSamples -gt 0 -and $Renderer -ne "pathtrace") { throw "CaptureSamples requires -Renderer pathtrace." }

function Resolve-OutputPath([string]$Path, [string]$BaseDirectory) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
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
if ($Renderer) { $Arguments += @("--renderer", $Renderer) }
if ($BuiltinScene) { $Arguments += @("--builtin-scene", $BuiltinScene) }
if ($CaptureHdr) { $Arguments += @("--capture-hdr", (Resolve-OutputPath $CaptureHdr $InvocationDirectory)) }
if ($CaptureSamples -gt 0) { $Arguments += @("--capture-samples", $CaptureSamples.ToString()) }
if ($Upscaler) { $Arguments += @("--upscaler", $Upscaler) }
if ($AssetCache) { $Arguments += @("--asset-cache", (Resolve-OutputPath $AssetCache $InvocationDirectory)) }
foreach ($Root in $TextureSearchRoot) { $Arguments += @("--texture-search-root", (Resolve-Path $Root).Path) }
if ($Blender) { $Arguments += @("--blender", (Resolve-Path $Blender).Path) }
if ($RendererArguments.Count -gt 0) { $Arguments += $RendererArguments }

Write-Host "Launching '$Executable' $($Arguments -join ' ')"
& $Executable @Arguments
if ($LASTEXITCODE -ne 0) { throw "Daedalus exited with code $LASTEXITCODE." }
