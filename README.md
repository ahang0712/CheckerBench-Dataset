# CheckerBench Dataset

**300 static-analysis tasks with complete runtime assets: 159 CSA tasks and 141 CodeQL tasks.**

| Backend / language | Tasks |
|---|---:|
| CSA (C/C++) | 159 |
| CodeQL Go | 30 |
| CodeQL Java | 30 |
| CodeQL JavaScript | 30 |
| CodeQL Python | 51 |
| **Total** | **300** |

`dataset/tasks.jsonl` is the authoritative portable roster. `tasks/` contains browsable task inputs. The release attachments provides complete task bundles, reference implementations, source snapshots, CodeQL databases, CSA build assets, refinement environments, and a frozen Docker image.

## Download and restore

Requirements: Linux x86-64, Docker, Python 3.10+, GitHub CLI, and at least 100 GB of available disk space. Additional task workspaces require additional storage. Authenticate with `gh auth login` if access is required.

```bash
git clone <REPOSITORY_URL> CheckerBench-Dataset
cd CheckerBench-Dataset
python3 scripts/download.py --output downloads
python3 scripts/restore.py --assets downloads --data data --load-image
```

The scripts verify SHA-256 checksums for every part and the complete compressed stream. Split archives are extracted or loaded directly without writing an extra combined copy. Verified downloads and completed restores can be reused.

## Create a task workspace

The `checkerbench-runtime:review-v1` image targets `linux/amd64` and includes LLVM/Clang 18.1.8, CodeQL 2.26.4, language toolchains, and Python dependencies. Model-service credentials are supplied separately by the operator.

Start a container with stable paths:

```bash
mkdir -p work
docker run --rm -it --network none --entrypoint /bin/bash \
  -v "$PWD:/release:ro" -v "$PWD/data:/data:ro" \
  -v "$PWD/work:/work" checkerbench-runtime:review-v1
```

Prepare a CSA task:

```bash
python /release/scripts/prepare.py csa pair_134943 \
  --data-root /data --output /work/pair_134943
cd /work/pair_134943
# Write checker.cpp, then compile and scan.
bash compile.sh
CHECKER_NAME=YOUR_CHECKER_NAME bash scan/scan_before.sh
CHECKER_NAME=YOUR_CHECKER_NAME bash scan/scan_after.sh
```

Prepare a CodeQL task:

```bash
python /release/scripts/prepare.py codeql morefixes-32ed35a8bd8aba7b47ad \
  --language go --data-root /data --output /work/go-example
cd /work/go-example/tasks/morefixes-32ed35a8bd8aba7b47ad
# Edit query/vulnerability.ql, then compile and scan.
bash scripts/compile_query.sh
bash scripts/run_before.sh
bash scripts/run_after.sh
```

Only tasks in the fixed 300-task roster are accepted. Maintainers can add `--reference` to reproduce a bundled reference implementation. Keep the same container paths after preparing a workspace. CSA uses shared Git objects from the read-only dataset; CodeQL databases are copied into the task workspace.

## Dataset contents

- **CSA:** 159 tasks with source snapshots, captured build assets, reference implementations, and refinement environments. Shared runtime assets preserve restoration dependencies.
- **CodeQL:** four task bundles containing 30 Go, 30 Java, 30 JavaScript, and 51 Python tasks, with their captured analysis databases.
- `scripts/prepare.py` restores before/after scanning workspaces. The CSA bundle also includes `scripts/rehydrate_benchmark_env.py` for full refinement worktrees. Historical refinement assets contain absolute paths and symbolic links; see `docs/environment.md` for the restoration boundary.
- References and evaluator assets are maintainer material. During evaluation, expose only a prepared single-task workspace and its required runtime assets to the evaluated agent.

## Integrity and validation

`release-manifest.json` records archive and part sizes, SHA-256 checksums, and the image fingerprint. `environment/versions.json` records the toolchain versions; the exported image is the direct restoration entry point. `reports/validation.json` records the checks actually performed and their limits.

Third-party source and tools retain their own license files. This repository does not assign a new blanket license to all bundled third-party content.
