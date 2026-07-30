[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Warp,
    [UInt64]$Frames = 0
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildFolder = if ($Configuration -eq "Debug") { "windows-msvc-debug" } else { "windows-msvc-release" }
$Executable = Join-Path $RepositoryRoot "build/$BuildFolder/$Configuration/Daedalus.exe"

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Daedalus executable was not found at '$Executable'. Configure and build $Configuration first."
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
