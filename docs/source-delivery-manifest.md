# Source delivery manifest — Campaign B Windows JPEG entropy hardening

- Baseline archive: `Project-Daedalus-main(7).zip`
- Baseline SHA-256: `b9aff71fda6e1823beec46d3cb0be7d7d641d063279b796975b60f52dd51c7e0`
- Suggested branch/PR: `fix/campaign-b-strict-jpeg-entropy`
- Output archive: `Project-Daedalus-Campaign-B-strict-jpeg-entropy-source.zip`
- Final output SHA-256: reported externally after the archive is frozen; not embedded into the bytes it identifies.
- Canonical archive root: `Project-Daedalus/`
- Canonical entry timestamp: `2000-01-01T00:00:00Z`

## Repair scope

1. Retain the project-owned PNG zlib/DEFLATE envelope validation added after Windows WIC accepted the malformed PNG fixture.
2. Add a platform-independent strict entropy walk for baseline sequential JPEG scans. The validator parses DHT/SOF0/SOS/DRI data, decodes the declared Huffman syntax for the expected MCU/block count, validates restart sequencing, coefficient-category/run bounds, marker boundaries, and JPEG one-bit padding, and rejects premature markers/truncated entropy before WIC/libjpeg pixel decode.
3. Remove the ineffective WIC `SetIndexing(GenerateOnLoad)` hardening attempt; developer evidence proved WIC still accepted the controlled malformed-JPEG fixture.
4. Keep progressive JPEG support on the existing marker-validation + full backend decode path; no unsupported claim is made that the new baseline entropy walker validates progressive refinement scans.
5. Update the Campaign B acceptance/audit/subset/fixture/experiment documentation so B-16 remains BLOCKED until this exact patch passes on Windows.

## Validation performed in the delivery environment

- `python3 scripts/source-health.py`: PASS.
- GNU C++20 warnings-as-errors portable Debug configure/build/CTest: PASS, 3/3.
- GNU C++20 warnings-as-errors portable Release configure/build/CTest: PASS, 3/3.
- Direct validator: `valid/jpeg_image.gltf`: accepted.
- Direct validator: `invalid/corrupt_entropy_jpeg.gltf --expect-failure`: PASS with structured `invalid_image` caused by premature marker before the expected MCU blocks completed.

Windows/MSVC/WIC/D3D12 execution is not available in this delivery environment. Developer-side VS2026 validation is required before the malformed-input acceptance row can be promoted to PASS.

## Packaging hygiene

`scripts/package-source.py` is the canonical source-only packager. It rejects build trees, IDE state, generated projects, binaries, shader outputs, logs, caches, nested archives, and `.git`; entries are sorted with fixed timestamps and verified after creation.
