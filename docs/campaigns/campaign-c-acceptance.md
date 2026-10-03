# Campaign C acceptance matrix — Rasterized Physically Based Rendering

Allowed statuses: `PASS`, `FAIL`, `BLOCKED`, `NOT RUN` only.

This matrix covers Campaign C as a whole. Campaign C1 established the material/direct-lighting foundation and was subsequently exercised by the developer on Windows (Debug/Release, DXC, 4/4 CTest, hardware/WARP smoke, fixture validation, reload/resize stress). C2 source/portable validation in this delivery environment is recorded below. New Windows/D3D12 C2 behavior is **not** promoted to PASS from source inspection.

| ID | Requirement | Status | Evidence / limitation |
|---|---|---|---|
| C-01 | Forward/forward+/deferred architecture decision | PASS | Conventional forward architecture retained; C1 and C2 architecture documents explain scope/tradeoffs. |
| C-02 | Metallic-roughness PBR core | PASS | C1 production path retained; portable numeric/material tests pass. |
| C-03 | Base colour / sRGB / vertex colour | PASS | C1 path retained and tested. |
| C-04 | Roughness G / metallic B channel mapping | PASS | C1 shader + portable tests + controlled fixture. |
| C-05 | Tangent-space normal mapping and handedness | PASS | C1 source/tests retained; developer previously exercised hardware/WARP diagnostics. |
| C-06 | Emissive semantics | PASS | C1 implementation/tests retained. |
| C-07 | AO applies only to indirect contribution | PASS | C2 replaces provisional ambient with deterministic IBL and keeps AO outside punctual direct contribution. |
| C-08 | Colour-space slot correctness | PASS | Explicit linear/sRGB SRV policy retained; portable classification tests pass. |
| C-09 | Double-sided shading | PASS | C1 back-face frame reversal retained. |
| C-10 | OPAQUE/MASK/BLEND | PASS | C1 state/ordering retained; portable semantics tested. |
| C-11 | Exposure + explicit tone mapping | PASS | C1 math tests retained; C2 capture metadata records EV/operator. |
| C-12 | Directional/point/spot direct lights | PASS | C1 preparation/math retained; controlled fixtures validate ingestion. |
| C-13 | Shadow mapping | BLOCKED | Source implements one 2048² directional-or-spot shadow map, bounds/cone fitting, alpha-mask casters, PCF and bias. Requires Windows/D3D12 runtime/visual validation. Point shadows intentionally unsupported. |
| C-14 | Diffuse/specular IBL + BRDF integration | BLOCKED | Source implements deterministic analytic environment/irradiance/specular prefilter equivalent and split-sum BRDF fit; portable reference tests pass. GPU visual validation NOT RUN here. |
| C-15 | Environment intensity/orientation contract | PASS | Fixed world-space procedural environment with configurable intensity; portable finite tests. |
| C-16 | Depth diagnostic | BLOCKED | Production shader exposes hardware normalized depth; Windows capture NOT RUN. |
| C-17 | Normal/base colour/metallic/roughness/emissive/material ID/UV diagnostics | PASS | C1 production-path diagnostics retained. |
| C-18 | Overdraw diagnostic | BLOCKED | Source uses additive no-depth fragment accumulation after alpha test; Windows capture NOT RUN. |
| C-19 | Deterministic screenshot capture | BLOCKED | Source implements explicit readback + PNG + fixed-frame CLI; Windows execution NOT RUN. |
| C-20 | Machine-readable capture metadata | PASS | Metadata schema/source path implemented and CLI tested portably; actual Windows capture metadata NOT RUN. |
| C-21 | Image comparison metrics + difference image | PASS | Portable `DaedalusImageCompare` plus tiny-array metric tests; comparisons are linear-sRGB RGB. |
| C-22 | Reference update policy | PASS | Normal regression never rewrites goldens; explicit promotion script requires `-ConfirmReferenceUpdate`. |
| C-23 | Textured Cube reference scene | PASS | Self-authored generated `c2_textured_cube.gltf`; importer validator accepts it. |
| C-24 | Material Sphere Grid reference scene | PASS | Self-authored generated 5x5 metallic/roughness sphere grid; importer validator accepts it. |
| C-25 | PBR Production Asset reference scene | PASS | Self-authored multi-material controlled fixture generated and validated. |
| C-26 | Architectural Interior reference scene | PASS | Self-authored room/occluder/multi-light controlled fixture generated and validated. |
| C-27 | Targeted image regressions | NOT RUN | Harness/cases/thresholds exist, but golden captures must be deliberately promoted on Windows first. |
| C-28 | HDR NaN/Inf validation | BLOCKED | Source performs half-float HDR readback scan before tone mapping; Windows execution NOT RUN. |
| C-29 | GPU pass timing | BLOCKED | Source implements delayed D3D12 timestamp queries/frequency conversion; Windows runtime validation NOT RUN. |
| C-30 | CPU frame timing | BLOCKED | `steady_clock` instrumentation/source implemented; Windows benchmark execution NOT RUN. |
| C-31 | Resource counts / VRAM allocation estimates | BLOCKED | Source reports counts, canonical payloads, and `GetResourceAllocationInfo` committed estimates; Windows benchmark output NOT RUN. |
| C-32 | Machine-readable benchmark output | BLOCKED | JSON source path implemented; hardware/WARP baseline generation NOT RUN. |
| C-33 | Many-material/light/transparent stress | NOT RUN | Exact Windows commands documented; new C2 stress fixtures not run here. |
| C-34 | Reload/renderer recreation/resize/minimize/restore | PASS | Existing C1 developer evidence covered 100 reloads and staged resize on hardware and WARP; C2 shadow/capture-specific repetition remains NOT RUN. |
| C-35 | Camera movement | PASS | C1 developer visual evidence; deterministic reset/framing contract retained. |
| C-36 | Windows Debug/Release + DXC after C2 | NOT RUN | This environment cannot execute MSVC/DXC. Must be rerun on final C2 snapshot. |
| C-37 | D3D12 debug/GPU validation after C2 | NOT RUN | Requires Windows target. No cleanliness claim is inferred. |
| C-38 | Hardware RTX 4060 Ti C2 captures | NOT RUN | Requires developer machine. |
| C-39 | Explicit WARP C2 captures | NOT RUN | Requires Windows WARP. |
| C-40 | Live-object reporting on final C2 snapshot | NOT RUN | Must inspect debugger output; invoking the report alone is not PASS evidence. |
| C-41 | Portable tests | PASS | Core/Scene/Assets/Rendering CTest 4/4 pass with warnings-as-errors in delivery environment. |
| C-42 | Controlled fixture corpus | PASS | Generator/manifest validator accepts 26 valid/degraded and rejects 25 invalid fixtures. |
| C-43 | Source health | PASS | `scripts/source-health.py` run before packaging. |
| C-44 | AI experiment evidence and adversarial audit | PASS | C2 agent log and source-level adversarial audit included. Independent external review is not claimed. |
| C-45 | Campaign D handoff | PASS | Stable contracts and unproven runtime assumptions documented in `docs/handoffs/campaign-d-handoff.md`. |
| C-46 | Final Campaign C exit | NOT RUN | Mandatory C2 Windows runtime, golden image regression, performance baseline, NaN/Inf, and final live-object evidence remain to be executed. |

## Required Windows closure sequence

From a VS2026 Developer PowerShell:

```powershell
cmake --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-debug --clean-first
ctest --preset windows-vs2026-debug --output-on-failure
cmake --preset windows-vs2026-release
cmake --build --preset windows-vs2026-release --clean-first
ctest --preset windows-vs2026-release --output-on-failure
```

Capture/finite-HDR/shadow/IBL proof:

```powershell
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 `
  -Asset .\tests\assets\valid\c2_architectural_interior.gltf -Diagnostic shaded `
  -Frames 8 -Capture .\evidence\interior.png -CaptureMetadata .\evidence\interior.json `
  -CaptureFrame 4 -ValidateHdr -EnvironmentIntensity 1.0

.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 `
  -Asset .\tests\assets\valid\c2_material_sphere_grid.gltf -Diagnostic shaded `
  -Frames 8 -Capture .\evidence\sphere-grid.png -CaptureMetadata .\evidence\sphere-grid.json `
  -CaptureFrame 4 -ValidateHdr

.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 `
  -Asset .\tests\assets\valid\c2_architectural_interior.gltf -Diagnostic overdraw `
  -Frames 8 -Capture .\evidence\overdraw.png -CaptureMetadata .\evidence\overdraw.json -CaptureFrame 4
```

Compare shadows on/off using identical deterministic camera/capture frame:

```powershell
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 -Asset .\tests\assets\valid\c2_architectural_interior.gltf -Frames 8 -Capture .\evidence\shadow-on.png -CaptureFrame 4
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 -Asset .\tests\assets\valid\c2_architectural_interior.gltf -Frames 8 -Capture .\evidence\shadow-off.png -CaptureFrame 4 -NoShadows
```

Benchmark:

```powershell
.\scripts\run.ps1 -Configuration Release -VisualStudioVersion 2026 `
  -Asset .\tests\assets\valid\c2_architectural_interior.gltf `
  -Frames 600 -BenchmarkWarmup 120 -BenchmarkOutput .\evidence\campaign-c-benchmark.json
```

After reviewing candidate images, establish initial goldens deliberately:

```powershell
.\scripts\update-campaign-c-references.ps1 -ConfirmReferenceUpdate -Configuration Debug -VisualStudioVersion 2026
.\scripts\validate-campaign-c.ps1 -Configuration Debug -VisualStudioVersion 2026
```

Repeat representative capture/regression and stress under `-Warp`, then perform reload/resize/live-object stress and inspect Visual Studio/DebugView output for unexpected D3D12/DXGI objects or warnings.
