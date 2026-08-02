# Campaign B acceptance matrix

## Evidence boundary

The delivered snapshot was implemented and validated in a Linux x86-64 environment. Portable C++20 targets were built with GNU and Clang in Debug and Release with warnings as errors. A separate Clang AddressSanitizer/UndefinedBehaviorSanitizer configuration also passed. All CTest executions reported 3/3 executables passed. The real validator accepted all 6 valid/degraded controlled fixtures and rejected all 18 invalid fixtures with their expected statuses. Fixture regeneration was byte-identical, and deterministic reports matched byte-for-byte from distinct absolute roots with report SHA-256 `3da68544ed48beeb7198e9a7d99b67bd7219e93f89aa3864514bdf849ac1213c`.

The environment did not contain Windows headers, a Windows SDK, MSVC, DXC, a D3D12 runtime, an RTX adapter, or WARP. Therefore the Windows application, HLSL, staging uploads, descriptors, interactive controls, live reload/resize behavior, and DirectX debug-layer cleanliness were source-reviewed but not compiled or executed. Those checks are not promoted to PASS.

The Campaign A acceptance record remains authoritative for its historical evidence. Its exact closure snapshot still requires Windows Debug/Release and runtime revalidation, so Campaign B's formal entry gate remains unresolved.

## Commands actually run before packaging

```text
cmake --preset portable-debug
cmake --build --preset portable-debug -j2
ctest --preset portable-debug --output-on-failure

cmake --preset portable-release
cmake --build --preset portable-release -j2
ctest --preset portable-release --output-on-failure

CC=clang CXX=clang++ cmake -S . -B build/clang-debug -G Ninja \
  -DDAEDALUS_BUILD_APP=OFF -DDAEDALUS_BUILD_TESTS=ON \
  -DDAEDALUS_BUILD_TOOLS=ON -DDAEDALUS_WARNINGS_AS_ERRORS=ON \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/clang-debug -j2
ctest --test-dir build/clang-debug --output-on-failure

CC=clang CXX=clang++ cmake -S . -B build/clang-release -G Ninja \
  -DDAEDALUS_BUILD_APP=OFF -DDAEDALUS_BUILD_TESTS=ON \
  -DDAEDALUS_BUILD_TOOLS=ON -DDAEDALUS_WARNINGS_AS_ERRORS=ON \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/clang-release -j2
ctest --test-dir build/clang-release --output-on-failure

CC=clang CXX=clang++ cmake -S . -B build/sanitize -G Ninja \
  -DDAEDALUS_BUILD_APP=OFF -DDAEDALUS_BUILD_TESTS=ON \
  -DDAEDALUS_BUILD_TOOLS=ON -DDAEDALUS_WARNINGS_AS_ERRORS=ON \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/sanitize -j2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  ctest --test-dir build/sanitize --output-on-failure
```

The asset validator was then run over every `tests/assets/manifest.json` entry using `--expect-success` or `--expect-failure`. Location independence was checked by importing copied external-resource fixtures from two independently created directories and comparing report bytes. The fixture generator was rerun and all generated-file hashes were compared before and after.

## Matrix

| ID | Requirement | Status | Evidence | Exact command or procedure | Actually run on delivered source? | Remaining limitation |
|---|---|---|---|---|---|---|
| B-01 | Canonical scene independence from D3D12 | PASS | `daedalus_scene` and `daedalus_assets` build on Linux; canonical public headers contain no D3D/COM/WIC/parser types. | GNU/Clang builds plus contamination scan. | Yes | Windows consumer still needs compilation. |
| B-02 | Canonical ownership and stable identifiers | PASS | Typed handles, owned vectors/strings/bytes, invalid-handle tests, and no raw ownership. | `Daedalus.Scene`; source audit. | Yes | Handles are scene-local, not persistent database IDs. |
| B-03 | glTF loading | PASS | External and data-URI `.gltf` fixtures import with exact counts/metadata. | Validator and `Daedalus.Assets`. | Yes | Declared subset only. |
| B-04 | GLB loading | PASS | Minimal and embedded-image GLB fixtures pass; corrupted framing fails. | Validator and `Daedalus.Assets`. | Yes | One JSON and optional BIN chunk supported. |
| B-05 | External buffers/images | PASS | Relative buffer/PNG dependencies load, hash, and report; missing dependencies have typed status; unsafe URI/root escape cases reject. | External, missing-dependency, network, and traversal fixtures. | Yes | HTTP/network resources intentionally unsupported. |
| B-06 | Embedded buffers/images | PASS | GLB BIN, buffer-view PNG, data-URI buffer/PNG/JPEG cases pass. | `minimal.glb`, `embedded_image.glb`, `data_uri_scene.gltf`, `jpeg_image.gltf`. | Yes | PNG/JPEG subset only; WIC runtime decode unrun. |
| B-07 | Multiple scenes/roots/primitives | PASS | Two scenes, two selected roots, two primitives, and scene-name selection are tested. | `Daedalus.Assets`. | Yes | Triangle topology only. |
| B-08 | Hierarchy and transform propagation | PASS | Parent/child propagation, multiple-parent/cycle rejection, negative scale, TRS/matrix paths. | `Daedalus.Scene`; invalid-parent fixture. | Yes | Live renderer placement not visually checked. |
| B-09 | Coordinate/unit/winding/tangent policy | PASS | Documented right-handed/CCW/metre convention; tangent W and negative determinant tested. | Scene/asset tests and source review. | Yes | D3D visual orientation remains unrun. |
| B-10 | Accessor decoding and validation | PASS | Float and normalized integer attributes, VEC3/VEC4 colour, interleaving, 8/16-bit test indices, semantic constraints, checked ranges/strides, `KHR_mesh_quantization`, and invalid-index cases. | `Daedalus.Assets`; controlled fixtures. | Yes | Sparse accessors remain unsupported and documented. |
| B-11 | Material/texture/sampler metadata | PASS | Core metallic-roughness references/factors, alpha, double-sided, colour-space intent, sampler modes, finite/range validation. | Data-URI and invalid-material fixtures. | Yes | Diagnostic shader consumes only a Campaign B subset. |
| B-12 | Camera and supported light metadata | PASS | Perspective, orthographic, and directional punctual-light metadata are tested; point/spot paths are implemented and source-reviewed. | Valid fixture imports. | Yes | Dedicated point/spot fixtures remain future strengthening. |
| B-13 | Bounds | PASS | Primitive/mesh/node/scene bounds recomputed and transformed; stale declared bounds generate a repair. | Scene tests and `stale_bounds.gltf`. | Yes | Live bounds overlay remains unrun. |
| B-14 | Import report | PASS | Parseable stable JSON with counts, bounds, extensions, dependencies, diagnostics, status, and schema/tool versions. | Report test and validator output. | Yes | Schema version is intentionally v1. |
| B-15 | Deterministic identity/cache key | PASS | Same content/settings and different checkout roots produce byte-identical reports/keys; dependency/settings changes alter key. | `cmp report-a.json report-b.json`; asset tests. | Yes | No persistent cache, by design. |
| B-16 | Malformed-input handling | PASS | 18 invalid fixtures reject with structured status/code/location/rule evidence; resource limit has a distinct status. | Manifest-wide validator run; resource-limit test. | Yes | Coverage-guided fuzzing was not performed. |
| B-17 | Controlled fixture corpus | PASS | 6 valid/degraded and 18 invalid self-authored CC0 fixtures, manifest, and byte-reproducible generator. | Generator hash comparison and validator run. | Yes | No copyrighted production asset is required. |
| B-18 | Portable CPU tests | PASS | GNU/Clang Debug/Release and Clang ASan/UBSan pass, warnings as errors, 3/3 CTest executables each. | Commands above. | Yes | MSVC portable tests remain unrun. |
| B-19 | GPU staging upload | BLOCKED | Default-heap geometry/textures, upload heaps, footprints, barriers, checked API sizes, and immediate fence wait are implemented and source-reviewed. | Windows build/debug-layer procedure. | No | Requires Windows compile and live D3D execution. |
| B-20 | Diagnostic asset rendering | BLOCKED | Canonical draw path, depth, indexed/multi-primitive rendering, texture SRVs, normal/UV/bounds modes implemented. | Hardware/WARP fixture smoke tests. | No | No screenshot or live output evidence. |
| B-21 | Orbit camera | PASS | Portable camera has finite empty-bounds fallback, clamped input, pan/dolly/reset, and CPU tests; Win32 input plumbing is present. | `Daedalus.Scene`. | CPU portion yes; interaction no | Interactive mouse behavior needs Windows validation. |
| B-22 | Hierarchy/report inspection | PASS | Validator and application dump canonical hierarchy; deterministic report file output is implemented, including built-in fallback scene. | Validator `--dump-scene --report`; source review. | Portable tool yes | In-window UI intentionally not added. |
| B-23 | Unload/reload and resize safety | BLOCKED | `F5` implements GPU-idle renderer/import replacement; resize recreates depth after context wait; per-frame constants prevent overwrite. | Repeated live reload, resize/minimize/restore, shutdown stress. | No | Requires live Windows/D3D12 stress and debug-layer evidence. |
| B-24 | DirectX validation cleanliness | BLOCKED | No blanket suppression; Campaign A info-queue behavior preserved. | Debug hardware and WARP runs with debug layer. | No | DirectX messages unavailable. |
| B-25 | Debug and Release builds | BLOCKED | Portable GNU/Clang Debug/Release pass. | Portable commands plus documented Windows presets. | Portable yes; Windows no | VS2022/v143 and VS2026/v145 builds required. |
| B-26 | CI | NOT RUN | Workflow defines portable and Windows jobs. | Push/open PR and inspect hosted results. | No | No hosted run evidence. |
| B-27 | Documentation | PASS | README, architecture, schema, subset, conventions, report, lifetime, fixtures, controls, acceptance, audit, experiment log, and handoff are present and reconciled. | Documentation/source consistency review. | Yes | Must append future Windows evidence without rewriting history. |
| B-28 | Source hygiene and archive reproducibility | PASS | Deterministic source-only ZIP is scanned, extracted twice, rebuilt/tested, manifest-validated, and location-determinism checked; final hash is reported externally. | Final packaging/re-extraction sequence in source manifest. | Yes | PowerShell packager itself was not executable on Linux; an equivalent stricter deterministic procedure was used. |
| B-29 | AI experiment evidence | PASS | Agent log records task, decisions, failures, repairs, validation counts, intervention level, and unrun evidence. | Review agent log. | Yes | Future Windows rerun becomes additional Level-2 evidence. |
| B-30 | Adversarial audit and Campaign C handoff | PASS | Hostile review found and repaired importer and D3D12 source defects; explicit stable/unstable handoff is included. | Audit and handoff review. | Yes | Overall Campaign B exit remains blocked by GPU/Windows/CI rows and Campaign A entry gate. |

## Conclusion

The canonical asset pipeline is implemented and the portable evidence is strong. **Campaign B is not finally accepted** because its formal Campaign A entry gate and required Windows/D3D12 evidence remain unresolved. This delivery is the most complete honestly validated snapshot supported by the available environment, not an all-green campaign claim.
