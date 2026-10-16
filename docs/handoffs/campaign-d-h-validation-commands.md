# Campaigns D–H: validation commands (NOT RUN)

None of these commands have been executed. They are the minimum evidence needed before any row in `docs/campaigns/campaign-d-h-acceptance.md` can change status. Run them from a Developer PowerShell for VS on Windows x64 unless a command says otherwise. `$D` is `build/windows-msvc-<config>/<Config>/Daedalus.exe`.

## 0. Build and portable tests (all campaigns)

```powershell
cmake --preset windows-msvc-debug                      # DXC found via Windows SDK or -DDXC_PATH
cmake --build --preset windows-msvc-debug
cmake --preset windows-msvc-release; cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-debug                      # incl. RendererOptions/RayTracing/PathTracing/Temporal/AssetPipeline
ctest --preset windows-msvc-release
# Portable CPU subset (Linux/macOS, app off):
cmake --preset portable-debug && cmake --build --preset portable-debug && ctest --preset portable-debug
python scripts/source-health.py
python tests/assets/generate_fixtures.py               # creates compat/textures/checker.png + compat/quad_equivalent.gltf
git status --short tests/assets                        # regenerated fixtures must be byte-identical except new compat files
```

Check that every new `.dxil` (`RasterPbrGBufferPS`, `HybridRayTracing`, `PathTracing`, and the 5 `*CS`) lands in `<exe dir>/shaders`, and that `scripts/blender/daedalus_blend_export.py` is copied next to `Daedalus.exe` and `DaedalusAssetBatch.exe`.

## D — DXR and hybrid

```powershell
& $D --renderer hybrid --frames 300                                  # hardware
& $D --renderer hybrid --warp --frames 60                            # WARP (expect DXR fallback reason or WARP DXR support)
& $D --renderer hybrid --builtin-scene alpha-instancing --rt-debug shadow-visibility --capture d_shadow.png --capture-frame 60
foreach ($v in "reflection","reflection-hit-distance","direct-lighting","instance-id") {
  & $D --renderer hybrid --builtin-scene alpha-instancing --rt-debug $v --capture "d_$v.png" --capture-frame 60 }
& $D --renderer hybrid --asset tests/assets/valid/c2_architectural_interior.gltf --stress-reloads 100 --stress-resize --report-live-objects
& $D --renderer raster --frames 120 --capture c_regression.png       # raster output must still match the C2 goldens
```
Run with the D3D12 debug layer **and GPU-based validation** enabled (PIX or `d3dconfig`). Expected: zero errors, zero live objects at exit, no TDR. Exercise 50–100 mode switches with keys 1/2/3 while resizing.

## E — Path tracer

```powershell
foreach ($n in 1,4,16,64,256) {
  & $D --renderer pathtrace --builtin-scene cornell-box --pt-seed 1 --capture-samples $n --capture-hdr "e_$n.pfm" --capture "e_$n.png" --capture-metadata "e_$n.json" }
& $D --renderer pathtrace --builtin-scene cornell-box --pt-seed 1 --pt-max-depth 16 --capture-samples 4096 --capture-hdr e_ref.pfm
DaedalusConvergence --reference e_ref.pfm --sample 1=e_1.pfm --sample 4=e_4.pfm --sample 16=e_16.pfm --sample 64=e_64.pfm --sample 256=e_256.pfm --output e_convergence.json
# Determinism: repeat one capture with the same seed and compare (expect identical PFM bytes on the same GPU/driver):
& $D --renderer pathtrace --builtin-scene cornell-box --pt-seed 1 --capture-samples 64 --capture-hdr e_64_repeat.pfm
foreach ($v in "sample-count","variance","direct","indirect","path-length","material-id","nonfinite") {
  & $D --renderer pathtrace --builtin-scene cornell-box --pt-debug $v --capture-samples 16 --capture "e_dbg_$v.png" }
& $D --renderer pathtrace --pt-preset interactive --asset tests/assets/valid/c2_pbr_production_asset.gltf --frames 600 --benchmark-output e_bench.json
```
Inspect the counters for non-finite samples in the capture metadata (renderer status). Expected: an RMSE slope near −0.5, and zero non-finite samples on the controlled scenes.

## F — Temporal

```powershell
& $D --renderer hybrid --jitter on --camera-path orbit:0.5 --temporal-debug motion --capture f_motion.png --capture-frame 120
& $D --renderer hybrid --jitter on --animate-node 1:translate-x:0.5 --temporal-debug disocclusion --capture f_disocc.png --capture-frame 120 --builtin-scene alpha-instancing
& $D --renderer pathtrace --pt-preset interactive --camera-path teleport:90 --temporal-debug history-length --capture f_cut.png --capture-frame 91
& $D --renderer pathtrace --pt-preset interactive --animate-emissive 60 --builtin-scene cornell-box --frames 600
```
Ghosting cases to review frame by frame: a thin object moving over a high-contrast background, a camera orbit around the alpha cards, an emissive pulse, a camera cut (history must be reset exactly at the cut frame), and a resize while orbiting.

## G — DLSS (BLOCKED until a local SDK + glue exists)

```powershell
cmake --preset windows-msvc-release -DDAEDALUS_ENABLE_DLSS=ON `
  -DDAEDALUS_DLSS_VENDOR_GLUE_SOURCE=<glue.cpp> -DDAEDALUS_DLSS_INCLUDE_DIRECTORY=<sdk/include> `
  -DDAEDALUS_DLSS_LIBRARIES=<sdk libs> -DDAEDALUS_DLSS_RUNTIME_FILES=<runtime dlls>
& $D --renderer hybrid --jitter on --upscaler dlss --dlss-quality quality --capture g_dlss.png --capture-frame 300
& $D --renderer hybrid --jitter on --capture g_native.png --capture-frame 300   # same display resolution
DaedalusImageCompare g_native.png g_dlss.png g_diff.png g_compare.json
& $D --upscaler dlss --frames 60     # without glue: must log BLOCKED and render native
```

## H — Assets

```powershell
cmake --preset windows-msvc-release -DDAEDALUS_ENABLE_ASSIMP=ON -DCMAKE_PREFIX_PATH=<assimp install>
DaedalusAssetBatch validate tests/assets/compat --output-dir h_out
DaedalusAssetBatch convert <obj/fbx/3ds corpus> --recursive --output-dir h_conv --cache h_cache --texture-search-root <maps>
DaedalusAssetBatch convert <corpus> --recursive --output-dir h_conv2 --cache h_cache     # expect cache hits, identical .dscene bytes
# Corruption/staleness: flip a byte in one h_cache/*.dcache, touch one texture, re-run -> expect cache_corrupt / cache_stale then re-import
DaedalusAssetBatch convert scene.blend --output-dir h_blend --blender "C:/Program Files/Blender Foundation/Blender 4.x/blender.exe"
DaedalusAssetBatch convert scene.blend --output-dir h_blend_missing --blender C:/nope/blender.exe   # expect external_converter_failed
& $D --asset scene.fbx --asset-cache h_cache --import-report h_report.json
```
Check that `provenance` names the Assimp/Blender versions; that the axis/unit conversions match the authoring tool; and that the Blender timeout (`--blender-timeout 5` on a heavy file) kills the whole process tree.
