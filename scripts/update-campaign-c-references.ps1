[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][switch]$ConfirmReferenceUpdate,
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [ValidateSet("2022", "2026")][string]$VisualStudioVersion = "2026",
    [switch]$Warp
)
if (-not $ConfirmReferenceUpdate) { throw "Reference promotion requires -ConfirmReferenceUpdate." }
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $Root
$ReferenceRoot = Join-Path $Root "tests/rendering/references"
New-Item -ItemType Directory -Force -Path $ReferenceRoot | Out-Null
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
    $Png = Join-Path $ReferenceRoot "$($Case.Name).png"
    $Meta = Join-Path $ReferenceRoot "$($Case.Name).json"
    $Args = @{
        Configuration=$Configuration; VisualStudioVersion=$VisualStudioVersion;
        Asset=(Join-Path $Root "tests/assets/valid/$($Case.Asset)"); Diagnostic=$Case.Diagnostic;
        Frames=8; Capture=$Png; CaptureMetadata=$Meta; CaptureFrame=4; ValidateHdr=$true
    }
    if ($Warp) { $Args.Warp=$true }
    & "$Root/scripts/run.ps1" @Args
}
Write-Host "Reference captures promoted. Commit only after visual/metadata review." -ForegroundColor Yellow
