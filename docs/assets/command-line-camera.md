# Command line and orbit camera

`--asset` selects a `.gltf` or `.glb`. `--scene` accepts an exact scene name or decimal index. `--dump-scene` writes the canonical hierarchy to standard output and the session log. `--import-report` writes deterministic JSON. `--diagnostic` selects `shaded`, `normals`, `uv`, or `bounds`. `--warp` explicitly selects WARP. `--frames` permits deterministic limited-frame shutdown.

The camera frames selected-scene bounds. Empty or degenerate bounds use a finite fallback target and radius. Pitch is clamped away from the poles, radius is bounded above zero, and resize updates only aspect-dependent projection/depth resources.

Controls: left drag orbit, right drag pan in camera-relative axes, wheel dolly, `R` reset/reframe, `F5` fence-safe asset reload.
