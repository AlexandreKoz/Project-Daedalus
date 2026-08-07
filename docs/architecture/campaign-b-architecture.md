# Campaign B architecture

## Purpose

Campaign B introduces a renderer-independent data contract and a bounded glTF/GLB importer. It preserves Campaign A's rule that the reference application owns the window and renderer lifecycle, while `D3D12Context` owns device creation, presentation, command submission, per-frame allocators, fences, and shutdown synchronization.

## Targets and dependency direction

- `daedalus_core`: JSON, SHA-256, command line, errors, adapter policy.
- `daedalus_scene`: math, typed handles, canonical schema, hierarchy and bounds.
- `daedalus_assets`: glTF/GLB parsing, dependency loading, accessor decoding, validation, diagnostics, deterministic reports.
- `DaedalusAssetValidator`: portable command-line acceptance path.
- `Daedalus` on Windows: application, Win32 input, orbit camera, D3D12 upload and diagnostic draw path. Supported image pixels are already owned by the canonical scene before renderer construction.

`daedalus_scene` does not link `daedalus_assets`; neither portable target links Direct3D 12. Importer-private JSON and buffer-view structures are translation-unit local.

## Import flow

```text
source bytes
  → GLB framing or JSON parse
  → extension and reference inventory
  → URI/dependency resolution and hashing
  → checked bufferView/accessor decoding
  → canonical arrays and stable handles
  → graph validation and transform propagation
  → recomputed local/world bounds
  → stable diagnostics, report, and asset key
```

All byte ranges use checked addition/multiplication before allocation or pointer arithmetic. External dependencies are restricted to normalized relative URIs beneath the asset directory by default.

## Runtime flow

The application imports before creating D3D12 resources. The diagnostic renderer receives a const canonical scene, builds default-heap vertex/index/texture resources through temporary upload resources, waits for the copy fence through `D3D12Context::execute_immediate`, then discards staging resources. Descriptor heaps are renderer-owned. Scene destruction happens only after `prepare_for_shutdown()` proves the GPU idle; otherwise process-exit retention is preferred over unsafe release.

## Scope boundary

The renderer deliberately supports only diagnostic base-colour shading, normals, UVs, and selected-scene bounds. Metallic, roughness, normal-map, occlusion, emissive, alpha, and light values are imported for Campaign C but are not claimed as correctly shaded here.

## Audit-closure architecture

The closure introduces two deliberate boundaries:

1. `assets/ImageDecoder` owns full supported-image validation. Import success requires actual pixel decode. Canonical `Image` owns encoded provenance bytes plus tightly packed, top-left-origin RGBA8 pixels, dimensions, source component metadata, and row stride. Windows uses WIC and portable builds use libpng/libjpeg behind this interface.
2. `rendering/DiagnosticPreparation` converts selected canonical node instances into renderer-neutral draw records. It resolves the invalid-material sentinel to `CanonicalScene::default_material`, preserves UV-set selection, and emits one draw per node/primitive instance. D3D12 consumes these records rather than duplicating semantic resolution.

Resource accounting is cumulative and checked. `source.resource_usage.retained_bytes` includes source bytes, buffer payloads, encoded images, canonical vertex/index storage, and decoded RGBA8 pixels. `conservative_peak_bytes` additionally observes the largest simultaneous image/accessor decode scratch estimate. Both configured limits are part of deterministic import settings and therefore of the asset key.
