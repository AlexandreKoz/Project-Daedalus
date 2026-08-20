# Campaign B Windows validation follow-up — 2026-08-20

This record summarizes developer-supplied Windows evidence captured against the immediate predecessor of this patch. The tested tree already contained the strict malformed-PNG and baseline-JPEG validation repair. The final patch in this archive changes only DXGI live-object acquisition, shutdown idempotence, and PowerShell relative-output-path handling; it does not change importer semantics, diagnostic shading, camera math, staging/upload, or stress sequencing.

## Environment and results

- Windows x64, Visual Studio 2026 / MSVC 19.51 / v145, Windows SDK 10.0.26100.0, x64 DXC.
- Hardware: NVIDIA GeForce RTX 4060 Ti, D3D feature level 12.1.
- Explicit WARP: Microsoft Basic Render Driver, feature level 12.1.
- Debug source-health, configure, build, DXC and CTest: PASS (3/3).
- Direct `corrupt_entropy_jpeg.gltf --expect-failure`: PASS with structured `invalid_image` and exit code 0.
- Release configure, build, DXC and CTest: PASS (3/3).
- Hardware and WARP frame-limited smoke tests: PASS.
- `shaded`, `normals`, `uv`, `tangents`, and `bounds`: executed on hardware and WARP.
- Developer-supplied visual captures confirmed UV variation, two-instance tangent diagnostic response, and aggregate bounds placement.
- Interactive orbit, pan, dolly and reframe were confirmed by the developer during runtime validation.
- Hardware and WARP stress completed 100 alternating reloads of `uv1_scene.gltf` and `instanced_tangents.gltf` plus small/large resize, minimize/restore and maximize/restore with clean exit.

## Defects exposed by the Windows evidence

1. `DXGIGetDebugInterface1` was searched in `dxgidebug.dll`, so `--report-live-objects` could not obtain the DXGI 1.3 debug interface.
2. `scripts/run.ps1` used the one-argument `.NET Path.GetFullPath()` overload for `--import-report`; in Developer PowerShell a relative report path resolved against the host process environment current directory instead of PowerShell's current location.
3. `Application::shutdown()` could be entered again from the destructor after successful explicit shutdown, causing a duplicate DXGI report attempt.

This patch repairs all three issues. It also applies the same caller-relative output-path rule to an explicitly relative `package-source.ps1 -OutputPath`.

## Remaining exact-final-snapshot evidence

Because this patch changes Windows-only application/shutdown code, one short VS2026 Debug rebuild and `--report-live-objects` run is still required on the exact extracted final archive. The detailed D3D12/DXGI live-object listing remains debugger output and must be inspected there. Hosted GitHub Actions evidence remains NOT RUN unless retained separately.
