# Import report schema

Schema identifier: `daedalus.import-report/2` (Campaign H). `/2` is a strict superset of `/1`: every `/1` field keeps its name, type and meaning; `/2` adds the `provenance` object described below.

The deterministic JSON report contains tool and canonical-schema versions, source display name/format/hash, settings, selected scene, asset key, object/vertex/index counts, local/world bounds, normalized dependency paths and hashes, extension inventories, ordered diagnostics, and final status.

Statuses are `success`, `success_with_warnings`, `success_with_repairs`, `unsupported_feature`, `invalid_source`, `missing_dependency`, `io_failure`, `resource_limit`, and `internal_error`.

Each diagnostic records severity, stable code, disposition, asset-relative location, message, and optional expected/observed values. Dispositions distinguish rejected, ignored, defaulted, converted, repaired, and observed behavior.

The asset key hashes canonical schema version, tool interpretation settings, source content hash, normalized dependency identities/hashes, and stable extension-relevant inputs. Absolute checkout paths, timestamps, pointer values, random state, and unordered iteration are excluded. JSON objects use sorted keys and report arrays are deterministically ordered.

## Resource-usage object

Audit closure adds a stable `resource_usage` object:

```json
{
  "source_payload_bytes": 0,
  "buffer_payload_bytes": 0,
  "encoded_image_bytes": 0,
  "canonical_geometry_bytes": 0,
  "decoded_image_bytes": 0,
  "retained_bytes": 0,
  "conservative_peak_bytes": 0
}
```

Budget failures use `status: resource_limit` and `code: resource_budget_exceeded`, with category/limit information in `expected` and the observed cumulative or peak byte count in `observed`. Full decode failures use `invalid_image` at `json.images[n]`. Repairs use `attribute_normalized` or `rotation_normalized` and remain deterministically sorted.

## Provenance object (`/2`, Campaign H)

`provenance` is a flat object of string values with sorted keys. Keys are only present when they apply:

| key | meaning |
| --- | --- |
| `loader` | `daedalus-asset-loader/1` when the report came through `AssetLoader` |
| `source_kind` | `gltf`, `compatibility` or `blender` |
| `adapter` | `gltf-native`, `assimp/<major>.<minor>.<patch>+<rev>`, `blender-worker` or `unavailable` |
| `cache`, `cache_key` | `hit`, `stored`, `store-failed`, `not-stored (import failed)`, `unavailable` or `disabled`; the content-addressed key |
| `axis_policy`, `metres_per_unit`, `unit_source` | compatibility importer axis/unit conversion actually applied |
| `uv_policy`, `tangent_policy`, `light_cone_policy`, `assimp_read_flags`, `assimp_normal_flags` | declared compatibility conversion policies |
| `blender_executable`, `blender_version`, `blender_script_version`, `intermediate_glb`, `intermediate_glb_sha256`, `conversion_manifest`, `blender_log`, `blender_exit_code`, `blender_timed_out` | `.blend` conversion record (version read from the manifest written by Blender itself) |
| `blocked_reason` | why an optional adapter is unavailable in this build |

For `.blend` sources `source_format` is `blend` and `source_sha256` hashes the `.blend`; the intermediate GLB is recorded only in provenance.

## Campaign H diagnostic codes

Appended (existing numeric values unchanged): `unsupported_source_format`, `compatibility_importer_unavailable`, `material_approximated`, `axis_converted`, `unit_converted`, `tangents_generated`, `normals_generated`, `texture_resolved_by_search`, `ambiguous_dependency`, `external_converter_failed`, `external_converter_warning`, `cache_hit`, `cache_corrupt`, `cache_stale`, `feature_discarded`, `malformed_source`.

A cache hit returns the stored original report (parsed back with `import_report_from_json`) plus a `cache_hit` diagnostic; corrupt or stale entries are deleted and reported with `cache_corrupt`/`cache_stale` (disposition `repaired`) before the asset is re-imported.
