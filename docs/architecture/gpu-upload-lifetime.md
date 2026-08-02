# GPU upload and resource lifetime

The Campaign B Windows path uses a deliberately synchronous staging model:

1. Create a default-heap destination in `COPY_DEST`.
2. Create and map an upload-heap staging resource.
3. Copy canonical bytes or WIC-decoded RGBA8 rows using `GetCopyableFootprints` and the required row pitch.
4. Record the copy and explicit transition to vertex/index/pixel-shader state.
5. Execute, signal, and wait through `D3D12Context::execute_immediate`.
6. Release staging resources only after the fence wait returns.

Geometry remains in default heaps. Texture resources use a typeless RGBA8 resource format so legal linear and sRGB SRVs can be selected from canonical colour-space metadata; Campaign B uploads one mip. Checked conversions guard D3D12 byte sizes, descriptor counts, and other 32-bit API limits.

The diagnostic renderer owns SRV, sampler, DSV, constant-buffer, geometry, texture, and depth resources. Constant-buffer storage is partitioned by frame-in-flight so a later CPU frame cannot overwrite constants still referenced by an earlier queued frame. Descriptor heaps are renderer-owned and are replaced only after a GPU-idle fence wait.

Resize waits through the context before swap-chain-buffer replacement, then recreates renderer depth. `F5` reload performs a GPU-idle wait, destroys the old renderer/import result, imports again, creates fresh resources/descriptors, and reframes the camera. Shutdown first proves queue idleness. If proof fails during fatal termination, resources are intentionally retained until operating-system process cleanup rather than being released while referenced by queued work.

These source-level contracts still require Windows compilation, DirectX debug-layer execution, repeated reload/resize stress, and live-object inspection before the GPU rows can be accepted.
