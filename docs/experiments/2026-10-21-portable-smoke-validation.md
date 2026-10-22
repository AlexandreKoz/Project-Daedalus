# Portable smoke validation — 2026-10-21

## Scope and environment

Validation of the user-supplied `Project-Daedalus-main(14).zip` on Linux x86-64, GCC 14.2, CMake 3.31.6, Ninja 1.12.1. The portable Debug preset builds *CPU-only* libraries, tools and tests; it does **not** compile the Windows D3D12 application or HLSL shaders. There is no Windows RTX/DXR runtime evidence from this session.

## Original source (before fixes)

- `cmake --preset portable-debug`: **PASS**.
- `cmake --build --preset portable-debug`: **FAIL**: ambiguous unqualified `is_regular_file(path)` in `src/assets/TextureResolver.cpp`, between the local helper and `std::filesystem::is_regular_file`.
- After changing only that symbol to unblock the build, `ctest --preset portable-debug --output-on-failure`: **7/9 PASS**, **2/9 FAIL**:
  - `Daedalus.RendererOptions`: `--jitter on` was parsed as a switch with no value, then `on` failed as unknown argument.
  - `Daedalus.AssetPipeline`: the basename recovery for an absolute authoring-machine texture path failed to search nested directories.

## Corrections applied

- Rename the local `is_regular_file` helper to `is_regular_file_safe`, keeping the non-throwing `error_code` overload.
- Parse `--jitter on|off` in accordance with documented CLI usage; update CLI help.
- Add deterministic recursive basename recovery under configured texture roots. Do not follow directory symlinks; allow only canonical paths within configured roots. Prefer direct-root matches over nested ones and preserve root ordering. Original texture-path rejection rules remain.
- Strengthen regression tests for `--jitter off` and exact nested texture-path recovery.

## Checks after corrections

| Check | Status | Evidence |
|---|---|---|
| Debug portable CMake configure | PASS | GCC/Ninja generated build graph |
| Debug portable build with `-Wall -Wextra -Wpedantic -Werror` | PASS | All portable targets built |
| Debug CTest | PASS | **9/9 suites passed**: Core, Scene, Assets, Rendering, RendererOptions, RayTracing, PathTracing, Temporal, AssetPipeline |
| Fixture validation | PASS | **26 valid/degraded + 25 invalid** cases matched expectations |
| `scripts/source-health.py` | PASS | Source-health preflight passed |
| `DaedalusAssetBatch validate tests/assets/valid/c2_textured_cube.gltf` | PASS | success_with_repairs, 0 warnings, 0 errors, 1 default/repair, manifest and report written |
| Archive ZIP CRC | PASS | All entries readable in the uploaded ZIP |
| Portable Release | NOT RUN to completion | Build interrupted after execution time limit before compile finished; no release test evidence |
| Windows VS2026/DXC/D3D12/DXR/visual captures | NOT RUN | Requires the user's Windows RTX system |
| Assimp and Blender live conversion | NOT RUN | Optional dependencies disabled/not exercised |
| NVIDIA DLSS SDK integration | BLOCKED | Requires local licensed SDK + vendor glue; in-repo boundary alone is not DLSS success |

`Daedalus.RayTracing` and `Daedalus.PathTracing` above are **portable CPU math/contract tests**, not evidence that DXR shaders compile or execute correctly.

## Suggested Windows validation sequence

Run in a Visual Studio 2026 Developer PowerShell at the repository root:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
cmake --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug --output-on-failure

New-Item -ItemType Directory -Force evidence | Out-Null
.\scripts\run.ps1 -VisualStudioVersion 2026 -Renderer raster -Asset '.\tests\assets\valid\c2_pbr_production_asset.gltf' -Frames 180 -CaptureFrame 120 -Capture 'evidence\c_raster.png' -CaptureMetadata 'evidence\c_raster.json' -ReportLiveObjects
.\scripts\run.ps1 -VisualStudioVersion 2026 -Renderer hybrid -BuiltinScene alpha-instancing -Frames 180 -CaptureFrame 120 -Capture 'evidence\d_hybrid.png' -CaptureMetadata 'evidence\d_hybrid.json' -ReportLiveObjects
.\scripts\run.ps1 -VisualStudioVersion 2026 -Renderer pathtrace -BuiltinScene cornell-box -CaptureSamples 16 -Capture 'evidence\e_pt16.png' -CaptureHdr 'evidence\e_pt16.pfm' -CaptureMetadata 'evidence\e_pt16.json' -RendererArguments @('--pt-seed','1')
```

If the first build fails, stop and record the **first** compiler/HLSL error; do not assume the renderer has passed validation. Inspect D3D12 debug-layer errors, startup logs, and JSON metadata; compare the new raster result with the Campaign C baseline before trusting hybrid/pathtrace output. Follow `docs/handoffs/campaign-d-h-validation-commands.md` for full stress, convergence, and temporal validation after initial smoke tests pass.

## Acceptance consequence

This evidence upgrades only the portable Debug build and CPU test portion. Campaigns D–H remain **NOT RUN/BLOCKED** for their GPU-dependent acceptance gates; no full-campaign PASS is claimed. Release configuration remains NOT RUN to completion.
