# Campaign B AI agent and experiment log

## Task identity

- Campaign: B — Canonical Asset Pipeline.
- Input archive SHA-256: `f79b0218c903ab4bda2d79566d35b992f9b8924933ba44aa375cc0c9557ed49d`.
- Suggested branch/PR: `feat/campaign-b-canonical-asset-pipeline`.
- Intervention level: Level 0 specification-only for implementation; no human root-cause repair was supplied during this task.

## Repository inspection

The supplied archive contained a Campaign A Win32/D3D12 triangle baseline. The application owned the window and renderer; `D3D12Context` owned adapter/device/swap-chain/command/fence state; rendering recorded through `FrameRecordingContext`. Campaign A documentation explicitly separated prior Windows evidence from its modified closure snapshot and did not claim the latter fully accepted.

## Major decisions

1. Implement a project-owned strict JSON parser and bounded glTF 2.0 importer instead of vendoring a parser dependency. This keeps the archive offline and prevents parser-specific types from crossing module boundaries.
2. Preserve glTF right-handed/CCW canonical data and isolate API-specific projection/depth handling at the graphics boundary.
3. Store encoded PNG/JPEG bytes and portable source metadata in canonical images; perform full Windows decode through WIC only in graphics code.
4. Use synchronous direct-queue staging for Campaign B. Auditable fence correctness was preferred over speculative streaming infrastructure.
5. Make the validator and generated CC0 fixture corpus first-class portable acceptance paths.
6. Replace the triangle-only path with canonical-scene rendering and remove legacy renderer/shader sources.

## Implementation sequence

- Added core JSON and SHA-256 utilities and expanded command-line parsing.
- Added canonical math/schema, typed handles, transform propagation, bounds, and hierarchy dump.
- Added structured diagnostics, stable reports, deterministic identity, and glTF/GLB import.
- Added valid/degraded/invalid fixture generation and CTest coverage.
- Added a portable validator, GNU/Clang presets, sanitizer execution, and CI updates.
- Added orbit camera, Win32 input, WIC decode, D3D12 staging, depth, descriptors, reload, and diagnostic shaders.
- Rewired `Application` to import before GPU setup and reject requested-asset failures without silent fallback.
- Added architecture, schema, subset, lifetime, controls, acceptance, audit, experiment, and handoff documentation.

## Failures and repairs observed

- An early normalized/data-URI test expected an implicit default material alongside an explicit material. The test was corrected to the actual canonical contract instead of distorting implementation.
- Missing external files initially returned generic `io_failure`; dependency reads were separated into `missing_dependency` while root open failures remain `io_failure`.
- Orbit-camera code was initially Windows-only; it was promoted to `daedalus_rendering` and tested portably.
- Hostile review found stale-source-bounds trust gaps, permissive base64/image/URI checks, missing semantic accessor/material-range constraints, a duplicate renderer function definition, a per-frame constant-buffer race, and an illegal typed-resource/sRGB-view combination. Each was repaired before final validation.

No failing test was disabled. No mock replaced real parsing. No Windows result was inferred.

## Validation metrics

- Portable compiler/configuration combinations: 4 (GNU Debug/Release and Clang Debug/Release), warnings as errors.
- Sanitizer configuration: Clang Debug with AddressSanitizer and UndefinedBehaviorSanitizer.
- CTest executions before packaging: 5; each reported 3/3 executables passed.
- Controlled top-level assets: 6 valid/degraded and 18 invalid.
- Fixture regeneration comparisons: 1; byte-identical.
- Determinism roots before packaging: 2; byte-identical report SHA-256 `3da68544ed48beeb7198e9a7d99b67bd7219e93f89aa3864514bdf849ac1213c`.
- Windows builds: 0, environment unavailable.
- Runtime D3D12 runs/screenshots/debug captures: 0, environment unavailable.
- Human Level-3 interventions: 0.

## Experimental observation

Explicit data contracts and machine-readable fixtures gave strong leverage over the portable importer. The highest residual uncertainty remains where the project charter predicted: explicit D3D12 resource/view contracts, shader compatibility, synchronization under live interaction, and visual correctness. Source review caught several credible GPU defects, demonstrating why compilation or a plausible screenshot alone would not have been adequate evidence.
