# Campaign C performance baseline methodology

Status: instrumentation implemented; target RTX 4060 Ti and WARP measurements are `NOT RUN` in the delivery environment.

Use the Release build for published hardware baselines and keep the asset, resolution, frame count, warm-up count, exposure, environment intensity, shadow state/bias and diagnostic mode fixed. Example:

```powershell
.\scripts\run.ps1 -Configuration Release -VisualStudioVersion 2026 `
  -Asset .\tests\assets\valid\c2_architectural_interior.gltf `
  -Frames 600 -BenchmarkWarmup 120 -BenchmarkOutput .\evidence\campaign-c-interior-release.json
```

The JSON distinguishes CPU frame work from D3D12 GPU timestamp intervals. GPU timestamps are delayed through frame-owned query/readback slots and converted with the command queue timestamp frequency. The report includes separate shadow, opaque, transparent, tone-map and total GPU means when valid samples exist.

Resource data distinguishes canonical geometry/retained payload from the estimated committed size of renderer-owned D3D12 resources. `committed_allocation_estimate_bytes` sums `GetResourceAllocationInfo` sizes for tracked committed resources; it is not total process working set, residency, or driver VRAM consumption.

For a baseline report preserve the JSON output together with adapter identity, build configuration, capture metadata, exact source revision/archive checksum, and any debug-layer observations. Do not compare WARP timing to RTX hardware as a performance claim; WARP is a correctness/fallback validation path.
