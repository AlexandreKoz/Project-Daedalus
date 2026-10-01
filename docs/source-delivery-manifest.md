# Source delivery manifest — Campaign C1 raster PBR core

- Input archive: `Project-Daedalus-main(9)(3).zip`
- Campaign scope: C1 only — raster PBR/material correctness/direct punctual lighting/HDR output foundation.
- Suggested branch/PR: `feat/campaign-c1-forward-pbr`
- Output archive: `Project-Daedalus-Campaign-C1-PBR-source.zip`
- Final output SHA-256: reported externally after the archive is frozen; not embedded into the bytes it identifies.
- Canonical archive root: `Project-Daedalus/`
- Canonical entry timestamp: `2000-01-01T00:00:00Z`

## Packaging contract

`scripts/package-source.py` is authoritative. It rejects generated build trees, IDE state, generated projects, binaries, DLL/EXE/PDB/OBJ/LIB/DXIL outputs, logs, caches, nested archives, and `.git`; entries are sorted and fixed-timestamped, CRC-tested, and inspected after creation. The PowerShell companion applies the same source-only intent.

The delivery archive contains no restricted SDK content. Campaign C1 does not integrate an external restricted SDK.

## Validation policy

Portable validation is run before source packaging and again from a clean extraction of the frozen archive. Windows/MSVC/DXC/D3D12 checks are not inferred from Linux results and remain NOT RUN where documented in `docs/campaigns/campaign-c1-acceptance.md`.

Campaign C is not declared complete by this delivery.
