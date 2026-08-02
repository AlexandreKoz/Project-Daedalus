# Source delivery manifest

- Input archive: `Project-Daedalus-main(1)(1).zip`
- Input SHA-256: `f79b0218c903ab4bda2d79566d35b992f9b8924933ba44aa375cc0c9557ed49d`
- Suggested branch/PR: `feat/campaign-b-canonical-asset-pipeline`
- Output archive: `Project-Daedalus-Campaign-B-source.zip`
- Output SHA-256: recorded externally in the delivery response; intentionally not embedded in the archive whose bytes it identifies.

## Included source domains

CMake/presets/workflows, project-owned C++20/HLSL, generated CC0 fixtures and generator, scripts, Campaign A historical documents, Campaign B architecture/schema/subset/conventions/report/lifetime/controls/fixtures/acceptance/audit/agent-log/handoff documents, README, notices, and version metadata.

## Excluded

`.git`, build directories, CMake caches, Ninja logs, objects/libraries/executables/DLLs, PDB/ILK, generated DXIL, runtime logs, screenshots, IDE state, Python caches, nested archives, restricted SDKs, secrets, and unauthorized assets.

## Exact delivery revalidation sequence

1. Remove generated directories from the working repository.
2. Scan all source-relative paths against prohibited directory/file patterns and absolute local-path markers.
3. Create a deterministic source-only ZIP under a single `Project-Daedalus/` root with sorted entries and fixed timestamps.
4. Test ZIP integrity and inspect every entry.
5. Extract independently into two fresh roots.
6. Configure/build/test portable GNU Debug from extraction A.
7. Configure/build/test portable Clang Release from extraction B.
8. Run all 6 valid/degraded and 18 invalid fixtures with explicit expectations from extraction A.
9. Compare deterministic external-resource reports across both extraction roots.
10. Scan both extracted trees for prohibited artifacts and local paths.
11. Calculate and report the final archive SHA-256 without modifying the archive afterward.

Windows/D3D12 revalidation was unavailable and is not represented as performed.
