# GPU upload and resource lifetime

The Campaign B Windows path uses a deliberately synchronous staging model:

1. Create a default-heap destination in `COPY_DEST`.
2. Create and map an upload-heap staging resource.
3. Copy canonical RGBA8 rows using `GetCopyableFootprints` and the required row pitch. Image decoding has already completed in the assets layer.
4. Record the copy and explicit transition to vertex/index/pixel-shader state.
5. Execute, signal, and wait through `D3D12Context::execute_immediate`.
6. Release staging resources only after the fence wait returns.

Geometry remains in default heaps. Texture resources use a typeless RGBA8 resource format so legal linear and sRGB SRVs can be selected from canonical colour-space metadata; Campaign B uploads one mip. Checked conversions guard D3D12 byte sizes, descriptor counts, and other 32-bit API limits.

The diagnostic renderer owns SRV, sampler, DSV, constant-buffer, geometry, texture, and depth resources. Constant-buffer storage is partitioned by frame-in-flight so a later CPU frame cannot overwrite constants still referenced by an earlier queued frame. Descriptor heaps are renderer-owned and are replaced only after a GPU-idle fence wait.

Resize waits through the context before swap-chain-buffer replacement, then recreates renderer depth. `F5` reload performs a GPU-idle wait, destroys the old renderer/import result, imports again, creates fresh resources/descriptors, and reframes the camera. Shutdown first proves queue idleness. If proof fails during fatal termination, resources are intentionally retained until operating-system process cleanup rather than being released while referenced by queued work.

These source-level contracts still require Windows compilation, DirectX debug-layer execution, repeated reload/resize stress, and live-object inspection before the GPU rows can be accepted.

## Audit-closure stress and teardown evidence path

`--stress-reloads N` waits for GPU idle, destroys the renderer, reparses/revalidates the source, recreates default-heap geometry/textures and descriptors, and repeats. `--stress-alternate-asset` alternates two real assets. `--stress-resize` queues small/large/minimized/restored/maximized/restored window states through the real Win32 and swap-chain path. These modes are acceptance tools, not a streaming architecture.

`--report-live-objects` requests a Debug DXGI report only after renderer, D3D12 context, and window resources have been released. It logs explicit begin/end markers. The report output must still be inspected in the debugger; the application does not claim it can reliably classify every runtime-internal DXGI object programmatically.


## Windows live-object acquisition follow-up

`DXGIGetDebugInterface1` is obtained through the linked DXGI 1.3 API, not by searching `dxgidebug.dll` for that symbol. The application releases renderer/context/window resources first, requests the DXGI report once, and uses an idempotent shutdown guard so destructor entry cannot duplicate the report. The detailed D3D12/DXGI object listing is still debugger output and must be inspected during acceptance; a successful API return alone is not treated as proof of zero unexpected live objects.
