# Command line, diagnostics, exposure, and orbit camera

`--asset` selects a `.gltf` or `.glb`. `--scene` accepts an exact scene name or decimal index. `--dump-scene` writes the canonical hierarchy. `--import-report` writes deterministic JSON. When invoked through `scripts/run.ps1`, relative report paths are resolved against the caller's PowerShell directory.

## Campaign C1 rendering options

`--diagnostic` accepts:

- `shaded`
- `normals`
- `uv`
- `tangents`
- `bounds`
- `base-color`
- `metallic`
- `roughness`
- `emissive`
- `material-id`

All material diagnostics use the same production material sampling/intermediate values used by the PBR path. `material-id` is motion-independent: canonical default material uses ID 0 while source material 0 maps to ID 1.

`--exposure <ev>` sets shaded-output exposure in EV stops over `[-24,+24]`; default is 0. Diagnostics bypass exposure and the ACES display fit. The equivalent PowerShell wrapper parameter is `-Exposure`.

`--warp` explicitly selects WARP. `--frames` permits deterministic limited-frame shutdown. Frame-limited and stress runs suppress modal fatal-error dialogs so unattended validation cannot hang; `--no-error-dialog` requests the same behavior explicitly.

## Camera

The orbit camera frames selected-scene bounds. Empty or degenerate bounds use a finite fallback target/radius. Pitch is clamped away from the poles and radius is bounded above zero.

Controls: left drag orbit, right drag pan in camera-relative axes, wheel dolly, `R` reframe, `F5` fence-safe asset reload.

## Stress/evidence options

- `--stress-reloads <count>`
- `--stress-alternate-asset <path>` (requires `--asset` and reload stress)
- `--stress-resize`
- `--report-live-objects`
- `--no-error-dialog`

A representative C1 validation run is:

```powershell
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 `
  -Asset .\tests\assets\valid\c1_point_light.gltf `
  -Diagnostic shaded -Exposure 0 `
  -StressReloads 100 `
  -StressAlternateAsset .\tests\assets\valid\c1_alpha_modes.gltf `
  -StressResize -ReportLiveObjects -NoErrorDialog -Frames 650
```

The exact C1 evidence matrix and material-specific commands are in `docs/campaigns/campaign-c1-acceptance.md`.

## Tangent diagnostic encoding

`--diagnostic tangents` displays the tangent basis transported by the real raster draw. Invalid/missing source tangent basis remains visibly distinguishable and, independently, disables normal-map evaluation for that primitive. Negative world determinant and source tangent sign are retained at the renderer boundary; the PBR normal-map path also accounts for back-facing double-sided fragments.
