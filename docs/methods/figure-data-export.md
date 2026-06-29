# Method Figure Data Export

`Extrinsic.Runtime.MethodFigureExport` is the runtime-owned data export seam for
method figure inputs. Geometry and method packages compute results and metrics;
runtime serializes the copied numeric data for external plotting scripts.

The seam writes deterministic text only. It does not plot, rasterize, allocate
graphics resources, or change the benchmark result JSON schema.

## Numeric Formatting

All floating-point fields use C locale scientific notation with 17 digits after
the decimal point, for example `1.25000000000000000e-01`. CSV column order and
JSON property order are stable. Manifest key groups are sorted by key before
serialization so equivalent inputs produce byte-identical output.

## Metric Bundle CSV

Schema id: `intrinsic.method_figure_export.metric_bundle.csv.v1`

Columns:

```text
schema,kind,series,key,x,y,value,unit
```

- `kind=series` rows carry one `(x, y)` sample from a named metric series such
  as RDF, RAPS, periodogram bins, nearest-neighbor histograms, per-level counts,
  minimum-distance curves, or throughput curves.
- `kind=summary` rows carry scalar summaries either for a named series or for
  the whole bundle using `series=bundle`.

## Metric Bundle JSON

Schema id: `intrinsic.method_figure_export.metric_bundle.json.v1`

Top-level fields are `dataset_id`, `method_id`, `backend_id`, `run_id`,
`series`, and `summaries`. Each series records `name`, axis labels/units, a
`samples` array of `{x, y}` records, and sorted scalar summaries.

The metric definitions and units for the point-cloud quality metrics are
documented in [geometry.md](../architecture/geometry.md).

## Run Manifest JSON

Schema id: `intrinsic.method_figure_export.run_manifest.json.v1`

The manifest captures reproducibility metadata:

- `dataset_id`
- `method_id`
- `backend_id`
- `run_id`
- `engine_version`
- `point_count`
- `sampler_config`
- `seeds`
- `artifacts`

`sampler_config`, `seeds`, and `artifacts` are sorted key/value groups. Use them
to record every sampler knob and seed needed to regenerate a dump.

## Point Sets

Point-set CSV schema id:
`intrinsic.method_figure_export.point_set.csv.v1`

Columns:

```text
schema,x,y,z,level,phase,splat_radius
```

Point-set PLY schema id:
`intrinsic.method_figure_export.point_set.ply.v1`

The PLY writer emits ASCII `vertex` rows with `x`, `y`, `z`, `level`, `phase`,
and `splat_radius` properties for external renderers.

## Failure Model

Writers validate array sizes, finite numeric values, duplicate manifest keys,
target paths, and point radii before opening output files. Writes go through a
same-directory temporary file and commit by rename; failed validation or write
failures return explicit diagnostics and do not leave a committed partial file.
