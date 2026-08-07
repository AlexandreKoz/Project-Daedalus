# Campaign A architecture

## Purpose

Campaign A establishes the smallest maintainable spine that can own a Win32 window, submit correct Direct3D 12 graphics work, survive client-size changes, and shut down without releasing resources still referenced by the GPU. It deliberately avoids systems whose requirements belong to later campaigns.

## Ownership diagram

```text
wmain
  |
  +-- parses CommandLineOptions
  |
  +-- Application::execute
        |
        +-- Application
              |
              +-- Win32Window
              |     owns HWND and registered class lifetime
              |
              +-- D3D12Context
              |     owns factory, adapter, device, queue, swap chain,
              |     RTV heap, two frame resources, command list,
              |     fence, and fence event
              |
              +-- TriangleRenderer
                    owns root signature, PSO, and vertex buffer
```

`Application` asks `D3D12Context` to prove the graphics queue idle before destroying `TriangleRenderer`, then destroys the graphics context and finally the window. COM objects use `Microsoft::WRL::ComPtr`. The fence event uses the narrow `UniqueWin32Handle` RAII wrapper, so it is released even when `D3D12Context` construction fails after event creation.

## Application lifecycle

1. `wmain` parses arguments without creating a window or D3D12 object.
2. `--help` prints usage and exits immediately.
3. `Application::initialize` opens the session log, creates the hidden native window, creates D3D12 infrastructure against its `HWND`, loads the build-produced shader binaries, and creates the triangle pipeline.
4. The window is shown only after graphics initialization succeeds.
5. The main loop drains all pending messages, consumes a coalesced nonzero resize, skips rendering while minimized, records one frame, presents, and checks the optional frame limit.
6. Normal exit waits for queued GPU work.
7. All cleanup paths call `prepare_for_shutdown` before renderer destruction. This performs a no-throw fence flush and records whether GPU idleness was proven.
8. The application boundary logs fatal exceptions, writes to standard error and the debugger, displays a message box, and returns a failure code.

## Frame lifecycle

```text
CPU begin_frame
  |
  +-- read current swap-chain index
  +-- wait only when that frame resource's fence value is incomplete
  +-- reset that frame's command allocator
  +-- reset the shared command list
  |
TriangleRenderer::record
  |
  +-- set root signature, PSO, viewport, and scissor
  +-- PRESENT -> RENDER_TARGET barrier
  +-- bind RTV and clear
  +-- bind vertex buffer and draw three vertices
  +-- RENDER_TARGET -> PRESENT barrier
  |
D3D12Context::end_frame
  |
  +-- close and execute command list
  +-- Present(1, 0)
  +-- signal a monotonically increasing fence value
  +-- store that value on the submitted frame resource
  +-- query the next current back-buffer index
```

The normal path does not wait after every submitted frame. CPU waiting occurs only before reusing a frame resource whose previous fence has not completed. This allows up to two swap-chain frames to remain in flight.

## Command allocator ownership

Each `FrameResource` owns exactly one direct command allocator and one swap-chain back-buffer reference. A frame resource also records the fence value signaled after its latest submission. `begin_frame` verifies that value before resetting the allocator. The graphics command list is shared because it is reset only after the selected allocator is safe.

## Fence protocol

`next_fence_value_` is monotonic. After each execute/present sequence, the queue signals one new value and stores it on the frame just submitted. `wait_for_fence` uses `SetEventOnCompletion` and an auto-reset event. Full flushes used by resize and shutdown signal a fresh value and wait for it, proving all earlier queue work is complete.

Normal `wait_for_gpu` is throwing so a requested clean exit cannot silently claim success after a synchronization failure. Cleanup uses `try_wait_for_gpu`, a no-throw path that logs signal, event-registration, and wait failures. `gpu_idle_proven_` avoids redundant flushes after a successful wait. The application performs this check while renderer resources still exist; only a successful result permits normal renderer and context destruction. If GPU idleness cannot be proven during fatal process shutdown, Daedalus intentionally detaches the renderer, graphics context, window, COM objects, and fence handle so operating-system process cleanup reclaims them instead of risking release of objects still referenced by queued work.

## Swap-chain ownership

`D3D12Context` owns an `IDXGISwapChain3` configured as:

- Two buffers.
- `DXGI_FORMAT_R8G8B8A8_UNORM`.
- `DXGI_SWAP_EFFECT_FLIP_DISCARD`.
- Render-target usage.
- One sample.
- Vertical synchronization through `Present(1, 0)`.

The current index always comes from `GetCurrentBackBufferIndex`; it is not advanced by arithmetic assumptions.

## Resize sequence

The window class records the latest nonzero client dimensions and exposes them as one pending resize. A minimized or zero-area window never requests swap-chain work. Class registration ownership is tracked separately from class availability: an object unregisters only a class registration it created. Constructor failures destroy any partially created window and unregister owned class state, while `WM_NCDESTROY` clears the stored instance pointer.

The graphics resize sequence is:

1. Ignore zero dimensions or an unchanged size.
2. Signal and wait for all outstanding queue work.
3. Release every old back-buffer `ComPtr`.
4. Clear per-frame fence associations.
5. Call `ResizeBuffers` with the existing swap-chain flags.
6. Store the new width and height.
7. query the current back-buffer index.
8. reacquire each buffer and recreate its RTV.

The command allocators, command list, queue, fence, and PSO survive resizing because they do not reference the old back-buffer resources after the flush.

## Adapter selection

Normal mode enumerates high-performance adapters through `IDXGIFactory6::EnumAdapterByGpuPreference` when DXGI 1.6 is available, otherwise `EnumAdapters1`. Software adapters are excluded. Each candidate is probed with `D3D12CreateDevice` at feature level 11.0, and the pure policy selects suitable hardware by dedicated memory with enumeration order as a tie-breaker.

Device creation then attempts feature levels 12.1, 12.0, 11.1, and 11.0 in descending order and logs the successful level. Hardware failure never triggers an implicit software fallback. `--warp` obtains the factory WARP adapter directly and reports that mode.

## Debug validation

A Debug build requests the D3D12 debug interface before device creation and passes the DXGI debug factory flag when available. Missing Graphics Tools produces an actionable warning rather than an unexplained failure. When the information queue is available and a debugger is attached, corruption and error severities break execution. Campaign A installs no broad message filter and contains no message-ID suppression.

## Shader build process

`cmake/CompileShaders.cmake` locates the official DXC executable and produces two binary outputs beneath the active build directory:

```text
generated/shaders/TriangleVS.dxil
generated/shaders/TrianglePS.dxil
```

Both entry points come from project-owned HLSL and target Shader Model 6.0. The current Debug policy is `-Zi -O3 -Qembed_debug`; other configurations use `-O3`; all shader warnings are errors. Campaign A originally used `-Od`, but the Campaign B Windows investigation demonstrated that Windows SDK 10.0.26100 WARP access-violated during PSO creation when either shader stage contained unoptimized DXIL. The `Daedalus` target depends on the generated outputs and copies them into an executable-relative `shaders` directory. HLSL is an explicit custom-command dependency, so source edits request recompilation.

## Error flow

`DAEDALUS_THROW_IF_FAILED` converts a failing result code into `ResultError`, preserving:

- The 32-bit result value.
- The operation or expression.
- Source file and line through `std::source_location`.
- A formatted system message on Windows when available.

Recoverable environmental conditions, such as missing Graphics Tools, are logged as warnings. Initialization, command recording, presentation, synchronization, and resize failures propagate to the application boundary. Rendering does not continue after an unrecoverable graphics failure.

## Intentional simplifications

- One upload-heap vertex buffer is retained for the lifetime of the renderer. It is acceptable for three immutable demonstration vertices but is not the future asset transfer architecture.
- There is no index buffer, depth buffer, texture, descriptor table, constant buffer, or scene data.
- The root signature contains no parameters.
- The renderer receives a narrow `FrameRecordingContext` rather than the whole graphics context.
- The viewport and scissor are derived directly from the current client dimensions on each frame.
- A single thread owns window messages and rendering.

## Interfaces intended to remain stable for Campaign B

The following boundaries are expected to survive the next campaign with additive changes:

- `Application` owns and sequences platform and graphics lifetimes.
- `Win32Window` reports native handle, dimensions, minimize state, and resize events without renderer ownership.
- `D3D12Context` owns device, queue, presentation, frame resources, and synchronization.
- Render code records into a frame context supplied by `D3D12Context`.
- Build-produced shaders remain external generated files copied beside the executable.
- Core result diagnostics and command-line parsing remain dependency-light and CPU-testable.

## Decisions postponed

Campaign A intentionally leaves asset formats, default-heap upload staging, descriptor allocation policy, depth conventions, camera data, material representation, PBR shader layout, resource lifetime tracking beyond swap-chain frames, render-pass scheduling, shader reflection, pipeline caching, asynchronous copy queues, multithreaded command recording, and vendor-specific reconstruction features for separately reviewed campaigns.

## Campaign B audit-closure amendment (2026-08-07)

Campaign A's D3D12 ownership direction remains unchanged. Campaign B adds validated decoded image ownership to the assets layer and renderer-neutral `DiagnosticPreparation`; neither alters application/window/context ownership. Debug HLSL no longer uses `-Od`. Local Windows 11 / VS2026 investigation on the pre-closure snapshot proved that unoptimized DXIL from either shader stage caused Windows SDK 10.0.26100 WARP to access-violate during `CreateGraphicsPipelineState`. The retained policy is `-Zi -O3 -Qembed_debug`, trading unoptimized shader stepping for a stable explicit WARP path. This chronology does not retroactively rewrite Campaign A acceptance evidence.
