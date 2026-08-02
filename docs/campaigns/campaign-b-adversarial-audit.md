# Campaign B adversarial audit

## Method

The final source snapshot was reviewed as if the campaign claims were false. The audit searched for API/importer leakage, dangling parser storage, unchecked arithmetic, semantic accessor mistakes, weak image/URI validation, nondeterministic reports, GPU lifetime races, stale parallel paths, false evidence, unusable fixtures, scope creep, and prohibited package contents.

## Checks and findings

- **Canonical contamination:** recursive searches of `src/scene` and `src/assets` public headers found no D3D12, DXGI, Windows, COM, WIC, JSON-DOM, or importer-private structures. PASS.
- **Direct rendering from parser data:** the application passes `CanonicalScene` to `DiagnosticSceneRenderer`; JSON/glTF intermediates remain translation-unit local. PASS.
- **Temporary-memory escape:** canonical strings, vectors, indices, vertices, dependencies, and encoded images are copied and owned. PASS.
- **Accessor arithmetic and semantics:** checked 64-bit offset/stride/count arithmetic is bounded against view and buffer sizes; semantic shape/component rules, normalized conversion, index ranges, VEC3/VEC4 colours, and `KHR_mesh_quantization` requirements are exercised. PASS for portable code and fixtures.
- **Bounds trust:** source `min`/`max` are compared with decoded POSITION bounds and stale values produce a visible repair. PASS.
- **Material validation:** finite values and glTF factor/range constraints are checked before canonical insertion. PASS.
- **Graph validation:** invalid references, cycles, multiple parents, non-finite transforms, and singular transforms are handled distinctly. PASS.
- **URI and base64 safety:** network/drive/absolute paths, traversal, backslashes, NUL, malformed percent escapes, and non-canonical base64 padding are rejected. Symlink-resolved external dependencies must remain inside the asset root. PASS.
- **Image validation:** PNG chunk framing, CRCs, IHDR fields, IDAT/IEND presence and dimensions are checked; JPEG frame/scan/end structure and supported component counts are checked. Base-colour/emissive references are sRGB and data textures linear. WIC decode disagreement with portable dimensions aborts. Portable PASS; runtime decode BLOCKED.
- **Handedness/winding:** importer performs no scattered conversion; canonical data remains right-handed and CCW. Tangent W and negative determinant survive import; Campaign B disables culling. Portable/source PASS; visual proof BLOCKED.
- **Upload lifetime:** staging resources survive until synchronous `execute_immediate` signals and waits. Explicit barriers and row-pitch copies are present. Source review PASS; execution BLOCKED.
- **Cross-frame constants:** per-frame constant-buffer slices prevent CPU overwrite while another frame may still be in flight. Source review PASS; execution BLOCKED.
- **Texture view legality:** typeless RGBA8 resources back both UNORM and UNORM_SRGB SRVs instead of creating incompatible views of a typed resource. Source review PASS; execution BLOCKED.
- **Descriptor lifetime/reload:** renderer-owned heaps and resources are replaced only after a GPU-idle wait; `F5` exposes real in-process reload. Runtime stress remains BLOCKED rather than inferred.
- **Determinism:** reports and keys matched byte-for-byte from different absolute roots; dependency/report ordering is stable. PASS.
- **Fixture independence:** all 6 valid/degraded and 18 invalid manifest entries were run through the real importer; fixture regeneration was byte-identical. PASS.
- **Legacy parallel path:** `TriangleRenderer.*` and `Triangle.hlsl` were removed; the fallback triangle is a canonical scene consumed by the same renderer. PASS.
- **Campaign scope:** no production PBR, DXR, path tracing, DLSS, animation, persistent cache, editor, ECS, render graph, or compatibility-format claim was introduced. PASS.
- **Unsupported claims:** Windows/D3D12 rows remain BLOCKED or NOT RUN. PASS.
- **Package contents:** the delivery procedure rejects build trees, binaries, DXIL, logs, caches, IDE state, nested archives, restricted SDKs, secrets, and unauthorized assets. PASS after extracted-archive scan.

## Defects found and repaired during hostile review

1. Split root I/O failure from missing external dependency status.
2. Promoted orbit-camera logic into a portable tested library.
3. Removed the legacy triangle renderer/shader to eliminate a parallel runtime path.
4. Added portable CI/presets so importer tests do not require D3D12.
5. Added source accessor `min`/`max` auditing and a stale-bounds repair fixture.
6. Added semantic accessor rules, VEC3 colour handling, `KHR_mesh_quantization`, and material-range validation.
7. Replaced permissive base64 and shallow image sniffing with strict base64, PNG CRC/chunk validation, and JPEG structural validation.
8. Hardened URI resolution against network/drive paths, traversal, and symlink escape.
9. Removed a duplicate `DiagnosticSceneRenderer::record` definition discovered by source inspection.
10. Partitioned constant-buffer memory per frame to remove a possible queued-frame overwrite race.
11. Changed texture resources to typeless RGBA8 so linear and sRGB SRVs are legal.
12. Added checked conversions for D3D12 descriptor and byte-size limits.
13. Ensured the built-in canonical scene can also emit a requested import report.

No failing production test was disabled, no acceptance path was replaced by a mock, and no Windows success was inferred from source review.

## Residual risks

The Windows translation units and HLSL could not be compiled in this environment. Root-signature compatibility, row-pitch behavior on a live device, descriptor binding, visual orientation, WIC decode, debug-layer cleanliness, hardware/WARP behavior, reload/resize stress, and shutdown/live-object behavior remain the principal unresolved risks. The acceptance matrix classifies them accordingly.
