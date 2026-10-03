# Source delivery manifest — Campaign C2 raster validation

- Input archive: `Project-Daedalus-main(11).zip`
- Campaign scope: C2 source implementation — shadows, deterministic analytic IBL, complete Campaign C diagnostics, capture/regression, reference scenes, HDR finite validation, timing/resource instrumentation, evidence tooling and Campaign D handoff.
- Suggested branch/PR: `feat/campaign-c2-raster-validation`
- Output archive: `Project-Daedalus-Campaign-C2-Raster-source.zip`
- Final output SHA-256: reported externally after the archive is frozen; not embedded into the bytes it identifies.
- Canonical archive root: `Project-Daedalus/`
- Canonical entry timestamp: `2000-01-01T00:00:00Z`

## Packaging contract

`scripts/package-source.py` is authoritative. It rejects generated build trees, IDE state, generated projects, binaries, DLL/EXE/PDB/OBJ/LIB/DXIL outputs, logs, caches, nested archives, `.git`, obvious secret material and restricted SDK content; entries are sorted/fixed-timestamped and the resulting archive is inspected.

All Campaign C2 reference assets are generated/self-authored and covered by `tests/assets/LICENSE.md`. No third-party HDR environment or restricted SDK is included.

## Validation policy

Portable Debug and Release tests, fixture regeneration/validation, image-comparison smoke validation and source-health are run before packaging and repeated from a clean extraction where possible. The delivery environment cannot execute the final MSVC/DXC/D3D12 C2 graph, so those rows remain `NOT RUN`/`BLOCKED` in `docs/campaigns/campaign-c-acceptance.md` and Campaign C is not declared exited by this archive.
