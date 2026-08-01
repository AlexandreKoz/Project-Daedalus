[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [ValidateSet("2022", "2026")]
    [string]$VisualStudioVersion = "2022",
    [switch]$Warp,
    [UInt64]$Frames = 0
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

$Arguments = @()
if ($Warp) {
    $Arguments += "--warp"
}
if ($Frames -gt 0) {
    $Arguments += @("--frames", $Frames.ToString())
}

Write-Host "Launching '$Executable' $($Arguments -join ' ')"
& $Executable @Arguments
if ($LASTEXITCODE -ne 0) {
    throw "Daedalus exited with code $LASTEXITCODE."
}
