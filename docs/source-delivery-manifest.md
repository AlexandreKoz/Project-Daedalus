# Source delivery manifest — Campaign B audit closure

- Baseline archive: `Project-Daedalus-main(3)(1).zip`
- Baseline SHA-256: `5397703f3cba8f4649f46f1ed2844a14f82198a9888dde2b34303bdf2348da32`
- Suggested branch/PR: `fix/campaign-b-audit-closure`
- Output archive: `Project-Daedalus-Campaign-B-audit-closure-source.zip`
- Final output SHA-256: reported externally after the archive is frozen; not embedded into the bytes it identifies.
- Canonical archive root: `Project-Daedalus/`
- Canonical entry timestamp: `2000-01-01T00:00:00Z`

## Determinism scope

`scripts/package-source.py` is the canonical delivery tool used for this archive. It sorts source-relative paths by their UTF-8 bytes, creates each ZIP entry explicitly, normalizes separators to `/`, fixes timestamps, strips extra/comment metadata, and uses CPython `zipfile` DEFLATE level 9. Byte identity is claimed only for repeated runs with the same source bytes, CPython implementation, and zlib implementation. `scripts/package-source.ps1` implements the same root/order/timestamp/prohibited-file policy as a Windows companion, but it was source-reviewed rather than executed in this Linux delivery environment. The delivery process generates the canonical Python package twice and compares SHA-256.

## Included

CMake/presets/workflows, project-owned C++20/HLSL, fixture generator and CC0 fixtures, scripts, architecture/subset/schema/report/lifetime/control documents, acceptance matrix, experiment log, adversarial audit, Campaign C handoff, README, notices, and version metadata.

## Rejected/excluded

`.git`, IDE state, build/output/runtime trees, CMake cache/generated projects, objects/libraries/executables/DLLs, PDB/ILK, generated DXIL, logs, screenshots not intended as source evidence, Python caches, nested archives, secrets, restricted SDKs, and unauthorized assets.

## Exact delivery revalidation

1. Regenerate fixtures twice and compare the complete fixture-tree hash.
2. Run GNU portable Debug and Release builds/tests, a Clang build, and an AddressSanitizer/UndefinedBehaviorSanitizer build.
3. Run the validator over all 10 valid/degraded and 25 invalid fixtures; fixture-tree SHA-256 after two regenerations: `e5af0b17b8eb46aae3a97c279c6a3c12b275905519752f76059909d5d52811d9`.
4. Scan source paths/content for prohibited artifacts and developer-machine absolute paths.
5. Remove generated build trees.
6. Generate the archive twice and assert identical SHA-256.
7. Verify lexical entries, fixed timestamps, canonical root, and ZIP integrity.
8. Extract into a fresh directory.
9. Reconfigure, rebuild, and rerun portable tests and the full validator corpus from the extraction.
10. Rescan the extracted source and calculate the final archive SHA-256 without modifying it afterward.

Windows validation was unavailable in the delivery environment and is not represented as performed.
