# Source delivery manifest — Campaign B Windows hardening

- Baseline archive: `Project-Daedalus-main(5).zip`
- Baseline SHA-256: `6acb9714c676b32657b3cafe4ed375a5ad5d843b48772853211f4dbf8de2b16d`
- Suggested branch/PR: `fix/campaign-b-windows-hardening`
- Output archive: `Project-Daedalus-Campaign-B-windows-hardening-source.zip`
- Final output SHA-256: reported externally after the archive is frozen; not embedded into the bytes it identifies.
- Canonical archive root: `Project-Daedalus/`
- Canonical entry timestamp: `2000-01-01T00:00:00Z`

## Determinism scope

`scripts/package-source.py` is the canonical delivery tool. It sorts source-relative paths by UTF-8 bytes, creates every ZIP entry explicitly, normalizes separators to `/`, fixes timestamps, strips extra/comment metadata, and uses CPython `zipfile` DEFLATE level 9. Byte identity is claimed for repeated runs with the same source bytes, CPython implementation, and zlib implementation. `scripts/package-source.ps1` remains the Windows companion.

## Included

CMake/presets/workflows, project-owned C++20/HLSL, controlled fixtures and generator, validation/packaging/source-health scripts, architecture and acceptance documentation, handoff documents, README, notices, and version metadata.

## Rejected/excluded

`.git`, IDE state, build/output/runtime trees, CMake cache/generated projects, objects/libraries/executables/DLLs, PDB/ILK, generated DXIL, logs, Python caches, nested archives, secrets, restricted SDKs, and unauthorized assets.

## Revalidation performed for this delivery

1. `python3 scripts/source-health.py`.
2. GNU portable Debug and Release configure/build/CTest with warnings-as-errors.
3. Clang 17 Release configure/build/CTest with warnings-as-errors.
4. Clang 17 ASan+UBSan configure/build/CTest.
5. Validator over all 10 valid/degraded and 25 invalid fixtures.
6. Fixture regeneration and identical tree-hash verification: `4a327e4c2a7ea1a4d275548fc539ccb0f5dd9c2acf7feb60c88da2e307ba4bc9`.
7. Generated build trees removed before packaging.
8. Canonical package generated twice and SHA-256 compared.
9. Final ZIP extracted to a fresh directory and portable Debug/Release tests plus source-health preflight rerun.
10. Final source/archive hygiene scan and final SHA-256 calculation.

Windows/MSVC/DXC/D3D12 execution is not claimed by this delivery environment. Developer-side VS2026 validation remains required.
