# Campaign C1 acceptance matrix — raster PBR core and direct lighting

Allowed statuses are only `PASS`, `FAIL`, `BLOCKED`, and `NOT RUN`.

This is a **Campaign C1** record. It does not assert that Campaign C has exited. Campaign C2 still owns shadows, image-based lighting, automated screenshot/image-difference infrastructure, final reference scenes/evidence, performance baselines, and final Campaign C adversarial acceptance.

Portable validation available in the delivery environment is separated from Windows/D3D12 runtime validation. Historical Campaign B rows B-24/B-25 remain BLOCKED and B-26 remains NOT RUN in `campaign-b-acceptance.md`; Campaign C1 does not rewrite them.

| ID | Requirement | Status | Evidence / limitation |
|---|---|---|---|
| C1-01 | Raster architecture decision | PASS | Conventional forward decision and tradeoffs documented in `campaign-c1-raster-architecture.md`. |
| C1-02 | Canonical scene remains renderer/D3D12 independent | PASS | New PBR math/preparation compile in portable library; D3D12 ownership remains in `graphics`. |
| C1-03 | Base colour factor/texture/vertex colour | PASS | Production HLSL path plus portable factor/fixture tests; sRGB SRV selected by slot. |
| C1-04 | Metallic G/B mapping and factors | PASS | Shader uses G roughness/B metallic; independent CPU test and controlled pixel fixture prove channels. |
| C1-05 | Normal mapping/tangent handedness/non-uniform normal transform | PASS | TBN path, inverse-transpose, source `w`, determinant sign, back-face sign; no-tangent policy tested. Windows visual proof NOT RUN in this environment. |
| C1-06 | Emissive factor/texture | PASS | sRGB emissive sampling and independent addition; fixture metadata and CPU factor tests. |
| C1-07 | AO R/strength without darkening direct punctual light | PASS | Shader restricts AO to provisional indirect term; R/strength fixture and CPU test. |
| C1-08 | Default material sentinel/source material 0 distinction | PASS | Stable diagnostic ID 0 vs source 0 -> ID 1; preparation and fixture tests. |
| C1-09 | GGX/Smith/Schlick metallic-roughness BRDF | PASS | CPU reference cases + HLSL implementation; numerical regularization documented. Shader execution on Windows NOT RUN. |
| C1-10 | Directional/point/spot canonical lights | PASS | Renderer-neutral preparation and controlled fixtures; local -Z convention documented/tested. |
| C1-11 | Distance/range/spot attenuation | PASS | Portable numeric tests for inverse-square, smooth finite range, and cone falloff. |
| C1-12 | Light-count boundary is explicit | PASS | 32-light bound; preparation throws instead of truncating; boundary behavior tested. |
| C1-13 | Linear HDR scene-colour target | BLOCKED | Source implements `R16G16B16A16_FLOAT` target and explicit state transitions; D3D12 runtime creation/validation not executable here. |
| C1-14 | Exposure/tone map/display transfer | PASS | Portable EV and finite/monotonic tone-map tests; source implements one explicit linear-to-sRGB output pass. Windows visual/runtime check NOT RUN. |
| C1-15 | OPAQUE/MASK/BLEND state and ordering | PASS | Portable alpha semantics and deterministic partition/sort tests; D3D12 blend/depth runtime validation NOT RUN. |
| C1-16 | Double-sided back-face shading | PASS | Source uses cull policy + `SV_IsFrontFace` shading-frame reversal; portable handedness tests. D3D12 visual check NOT RUN. |
| C1-17 | Explicit C++/HLSL ABI | PASS | `static_assert`/portable size-offset tests/source-health sentinels. Final DXC/Windows build NOT RUN. |
| C1-18 | Per-slot sRGB/linear texture interpretation | PASS | Dual SRVs per canonical texture, portable colour-space tests and controlled fixtures. |
| C1-19 | Canonical sampler use/mip safety | PASS | Wrap/min/mag base-level behavior mapped; `MinLOD=MaxLOD=0` prevents nonexistent-mip access. Full mip-filter fidelity explicitly unsupported. |
| C1-20 | Existing diagnostics retained through production draw path | PASS | normals, UV, tangents, bounds remain; no legacy competing diagnostic renderer. Windows captures NOT RUN. |
| C1-21 | New material diagnostics | PASS | base colour, metallic, roughness, emissive, stable material ID in production shader. Windows captures NOT RUN. |
| C1-22 | Deterministic controlled C1 fixtures | PASS | 12 new self-authored top-level fixtures generated from `generate_fixtures.py`; manifest validator accepts the full 22-valid/25-invalid corpus. |
| C1-23 | Portable automated tests | PASS | Core/Scene/Assets/Rendering CTest suite passes with project warnings as errors in the delivery environment. |
| C1-24 | Source health | PASS | `python3 scripts/source-health.py`. |
| C1-25 | Windows Debug/Release compile + DXC | NOT RUN | Delivery environment has no Windows/MSVC/DXC. Exact commands below. |
| C1-26 | Hardware-adapter smoke/material/light diagnostics | NOT RUN | No Windows/D3D12 GPU runtime in delivery environment. |
| C1-27 | Explicit WARP smoke | NOT RUN | No Windows D3D12/WARP runtime in delivery environment. |
| C1-28 | Resize/reload/camera stress with new renderer | NOT RUN | Requires Windows application execution. Existing Campaign B lifetime discipline is preserved in source. |
| C1-29 | D3D12/DXGI live-object report on exact C1 snapshot | NOT RUN | Must be run after C1 Windows rebuild; this also does not retroactively close Campaign B B-24/B-25 without its prescribed exact-snapshot evidence. |
| C1-30 | Shadows | NOT RUN | Intentionally reserved for C2. |
| C1-31 | Image-based lighting | NOT RUN | Intentionally reserved for C2. |
| C1-32 | Screenshot regression/reference image pipeline | NOT RUN | Intentionally reserved for C2. |
| C1-33 | Performance baseline/final Campaign C acceptance | NOT RUN | Intentionally reserved for C2. |

## Exact Windows validation commands

From a VS2026-capable PowerShell at repository root:

```powershell
cmake --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-debug --clean-first
ctest --preset windows-vs2026-debug --output-on-failure

cmake --preset windows-vs2026-release
cmake --build --preset windows-vs2026-release --clean-first
ctest --preset windows-vs2026-release --output-on-failure
```

Representative C1 material/light runs:

```powershell
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 -Asset .\tests\assets\valid\c1_metallic_roughness.gltf -Diagnostic shaded -Frames 120
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 -Asset .\tests\assets\valid\c1_normal_map.gltf -Diagnostic normals -Frames 120
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 -Asset .\tests\assets\valid\c1_alpha_modes.gltf -Diagnostic base-color -Frames 120
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 -Asset .\tests\assets\valid\c1_spot_light.gltf -Diagnostic shaded -Exposure 0 -Frames 120
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 -Asset .\tests\assets\valid\c1_tangent_negative_scale.gltf -Diagnostic tangents -Frames 120
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 -Asset .\tests\assets\valid\material_default.gltf -Diagnostic material-id -Frames 120
```

Repeat representative runs with `-Warp`. Then execute reload/resize/live-object stress, for example:

```powershell
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 -Asset .\tests\assets\valid\c1_point_light.gltf -StressReloads 100 -StressAlternateAsset .\tests\assets\valid\c1_alpha_modes.gltf -StressResize -ReportLiveObjects -NoErrorDialog -Frames 650
```

Inspect debugger/debug-layer output for unexpected D3D12/DXGI warnings or live objects. A plausible image is not sufficient evidence.

## Disposition

C1 source/portable acceptance is established for the implemented scope. C1 Windows-specific rows remain NOT RUN/BLOCKED by environment and must be executed by the developer before treating the D3D12 implementation as runtime-proven. Full Campaign C remains open for C2.
