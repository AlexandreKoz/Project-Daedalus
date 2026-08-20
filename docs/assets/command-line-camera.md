# Command line and orbit camera

`--asset` selects a `.gltf` or `.glb`. `--scene` accepts an exact scene name or decimal index. `--dump-scene` writes the canonical hierarchy to standard output and the session log. `--import-report` writes deterministic JSON. When invoked through `scripts/run.ps1`, a relative report path is resolved against the PowerShell caller's current directory rather than the Visual Studio host process directory. `--diagnostic` selects `shaded`, `normals`, `uv`, `tangents`, or `bounds`. `--warp` explicitly selects WARP. `--frames` permits deterministic limited-frame shutdown. Frame-limited and stress runs suppress modal fatal-error dialogs so unattended validation cannot hang; `--no-error-dialog` explicitly requests the same behavior for any run.

The camera frames selected-scene bounds. Empty or degenerate bounds use a finite fallback target and radius. Pitch is clamped away from the poles, radius is bounded above zero, and resize updates only aspect-dependent projection/depth resources.

Controls: left drag orbit, right drag pan in camera-relative axes, wheel dolly, `R` reset/reframe, `F5` fence-safe asset reload.

## Audit-closure command-line additions

- `--diagnostic tangents`
- `--stress-reloads <count>`
- `--stress-alternate-asset <path>` (requires `--asset` and `--stress-reloads`)
- `--stress-resize`
- `--report-live-objects`
- `--no-error-dialog`

A deterministic acceptance run should allocate at least five presented frames per requested reload. Example:

```powershell
Daedalus.exe --asset tests/assets/valid/uv1_scene.gltf `
  --stress-reloads 20 `
  --stress-alternate-asset tests/assets/valid/instanced_tangents.gltf `
  --stress-resize --report-live-objects --no-error-dialog --frames 140
```

## Tangent diagnostic encoding

`--diagnostic tangents` maps normalized tangent XYZ to RGB. Positive handedness uses full luminance; negative handedness uses 35% luminance. This preserves all three direction components while making the signed `w` channel visible without implementing Campaign C normal mapping.
