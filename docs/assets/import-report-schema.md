# Import report schema

Schema identifier: `daedalus.import-report/1`.

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
