# Campaign C2 AI experiment log

## Task

Implement the second half of Campaign C without expanding into later DXR/temporal/DLSS campaigns. The repository after C1 was treated as authoritative.

## Retained architecture

- conventional forward raster renderer;
- renderer-neutral canonical scene and portable math/preparation;
- explicit D3D12 ownership in `PbrSceneRenderer`/`D3D12Context`;
- C1 GGX material/direct-light contract and alpha ordering.

## C2 implementation work

1. Added portable C2 environment/shadow/image-comparison mathematics and tests.
2. Added deterministic directional/spot shadow projection and a renderer-owned 2048² depth shadow pass with PCF.
3. Replaced provisional ambient with deterministic analytic diffuse/specular environment lighting and split-sum BRDF approximation.
4. Added depth and real additive-overdraw diagnostics.
5. Added explicit LDR/HDR GPU readback, PNG encoding, metadata and pre-tone-map NaN/Inf scan.
6. Added portable linear-sRGB image metrics/difference tool and deliberate golden promotion workflow.
7. Added four self-authored/generated Campaign C reference scenes.
8. Added D3D12 timestamps, CPU frame timing, machine-readable benchmark output and resource allocation estimates.
9. Added C2 runtime switches, PowerShell wrappers, architecture/acceptance/audit/handoff documentation.

## Validation performed in delivery environment

- portable configure/build with project warnings as errors;
- Core/Scene/Assets/Rendering CTest suites;
- regenerated controlled fixtures;
- full fixture manifest validation;
- source-health before packaging;
- deterministic source package inspection.

Windows/D3D12/DXC execution is not available in this environment and is not represented as PASS. Exact closure commands are in `docs/campaigns/campaign-c-acceptance.md`.

## Human intervention context

The developer supplied C1 Windows evidence and screenshots before C2 began. That evidence established that the post-C1 MSVC ABI fix, Debug/Release builds, portable tests, hardware/WARP smoke and C1 reload/resize paths were functioning on the target machine. C2 still requires a fresh final-snapshot Windows pass because it changes shaders, root signatures, resources and frame instrumentation.
