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
    [ValidateSet("shaded", "normals", "uv", "bounds")]
    [string]$Diagnostic = "shaded"
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
if ($Scene -and -not $Asset) { throw "Scene requires Asset." }
if ($ImportReport -and -not $Asset) { throw "ImportReport requires Asset." }

$Arguments = @("--diagnostic", $Diagnostic)
if ($Warp) { $Arguments += "--warp" }
if ($Frames -gt 0) { $Arguments += @("--frames", $Frames.ToString()) }
if ($Asset) { $Arguments += @("--asset", (Resolve-Path $Asset).Path) }
if ($Scene) { $Arguments += @("--scene", $Scene) }
if ($ImportReport) { $Arguments += @("--import-report", [System.IO.Path]::GetFullPath($ImportReport)) }
if ($DumpScene) { $Arguments += "--dump-scene" }

Write-Host "Launching '$Executable' $($Arguments -join ' ')"
& $Executable @Arguments
if ($LASTEXITCODE -ne 0) { throw "Daedalus exited with code $LASTEXITCODE." }
