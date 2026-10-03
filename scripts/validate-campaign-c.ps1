[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [ValidateSet("2022", "2026")][string]$VisualStudioVersion = "2026",
    [switch]$Warp
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $Root
$BuildPrefix = if ($VisualStudioVersion -eq "2026") { "windows-vs2026" } else { "windows-msvc" }
$Compare = Join-Path $Root "build/$BuildPrefix-$($Configuration.ToLowerInvariant())/$Configuration/DaedalusImageCompare.exe"
if (-not (Test-Path $Compare)) { throw "DaedalusImageCompare.exe not found: $Compare" }
$ReferenceRoot = Join-Path $Root "tests/rendering/references"
$ResultRoot = Join-Path $Root "tests/rendering/results"
New-Item -ItemType Directory -Force -Path $ResultRoot | Out-Null

$Cases = @(
    @{Name="textured_cube"; Asset="c2_textured_cube.gltf"; Diagnostic="shaded"},
    @{Name="sphere_grid"; Asset="c2_material_sphere_grid.gltf"; Diagnostic="shaded"},
    @{Name="production_asset"; Asset="c2_pbr_production_asset.gltf"; Diagnostic="shaded"},
    @{Name="architectural_interior"; Asset="c2_architectural_interior.gltf"; Diagnostic="shaded"},
    @{Name="depth"; Asset="c2_textured_cube.gltf"; Diagnostic="depth"},
    @{Name="overdraw"; Asset="c2_architectural_interior.gltf"; Diagnostic="overdraw"},
    @{Name="normal"; Asset="c1_normal_map.gltf"; Diagnostic="normals"},
    @{Name="material_id"; Asset="material_default.gltf"; Diagnostic="material-id"}
)
foreach ($Case in $Cases) {
    $Candidate = Join-Path $ResultRoot "$($Case.Name).candidate.png"
    $Metadata = Join-Path $ResultRoot "$($Case.Name).candidate.json"
    $Reference = Join-Path $ReferenceRoot "$($Case.Name).png"
    $Diff = Join-Path $ResultRoot "$($Case.Name).diff.png"
    $Metrics = Join-Path $ResultRoot "$($Case.Name).metrics.json"
    $Args = @{
        Configuration=$Configuration; VisualStudioVersion=$VisualStudioVersion;
        Asset=(Join-Path $Root "tests/assets/valid/$($Case.Asset)"); Diagnostic=$Case.Diagnostic;
        Frames=8; Capture=$Candidate; CaptureMetadata=$Metadata; CaptureFrame=4; ValidateHdr=$true
    }
    if ($Warp) { $Args.Warp=$true }
    & "$Root/scripts/run.ps1" @Args
    if (-not (Test-Path $Reference)) {
        throw "Reference missing: $Reference. Review and promote it explicitly; normal validation never creates goldens."
    }
    $ReferenceMetadata = Join-Path $ReferenceRoot "$($Case.Name).json"
    & $Compare $Reference $Candidate $Diff $Metrics --mae 0.004 --rmse 0.008 --max 0.08 --pixel-threshold 0.02 --fraction 0.01 `
        --reference-metadata $ReferenceMetadata --candidate-metadata $Metadata
    if ($LASTEXITCODE -ne 0) { throw "Image regression failed for $($Case.Name)" }
}
Write-Host "Campaign C image regression PASS" -ForegroundColor Green
