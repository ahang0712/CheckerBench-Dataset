# Runtime environment

The release contains **300 tasks: CSA 159 and CodeQL 141**. All provided archives use neutral names and reproducible file metadata.

## Toolchain

Import the provided Docker archive with `scripts/restore.py --load-image`. The image is tagged `checkerbench-runtime:review-v1` and targets Linux amd64. Toolchain versions are listed in `environment/versions.json`.

The image contains LLVM/Clang, CodeQL, language dependencies, the Python environment, and the MCP adapter needed for task preparation and scanning. Model-service credentials are supplied separately by the operator.

## Task preparation

`scripts/prepare.py` accepts only tasks in the release roster. It creates writable workspaces from the bundled task inputs and captured runtime assets. It rewrites the scan paths for the requested workspace and applies the source-evidenced compiler compatibility adjustments recorded in `runtime-adjustments.json`.

CSA workspaces share immutable Git objects with the extracted dataset. Keep `/data` mounted at the same path when reopening them. CodeQL copies its before, after, and refinement databases into the writable output directory.

## Refinement assets

CSA source snapshots, refinement overlays, compilation databases, and restoration scripts are included. Full refinement restoration is available through:

```bash
python /data/csa159/scripts/rehydrate_benchmark_env.py \
  --pair-id 134943 --output-workdir /work/refinement-example
```

Some captured refinement assets use absolute symbolic links or generated build files. Their original source-relative mappings must be preserved when restoring those environments. Before/after workspace checks and full refinement replay have separate validation scopes; `reports/validation.json` identifies the checks actually performed.

## Integrity

Each dataset archive contains `integrity.json`, which hashes regular files and records symbolic-link targets. `release-manifest.json` additionally records complete archive and split-part SHA-256 checksums.

The reviewer distribution retains task inputs, executable runtime assets, reference implementations, and evaluation evidence. Operator run logs, account configurations, and trajectory administration records are omitted. Upstream project source files and licenses retain their original content.
