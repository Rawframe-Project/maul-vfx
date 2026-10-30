# Releasing

Maul VFX follows Semantic Versioning with tags `vX.Y.Z`. Before 1.0.0 any release may change the API, the ABI, the effect file format and the simulation's results.

## How a release happens

1. Pull requests merge to `main` with conventional commit titles (`feat`, `fix`, `perf` and the rest; see [AGENTS.md](../../AGENTS.md#pull-requests)).
2. release-please (`.github/workflows/release.yml`, configured by `release-please-config.json` and `.release-please-manifest.json`) keeps a draft release pull request open, titled `chore: release X.Y.Z`. It moves the version macros between the release-please markers in `include/maul-vfx/base.h` and writes a changelog entry from the commit titles.
3. Before merging it, rewrite its `CHANGELOG.md` entry in user language: what changed for someone using the library, grouped as Added, Changed, Fixed, Removed and Security. Leave out refactors, tests, CI and documentation. Say when the simulation's results change (the golden hashes moved), when the effect file format's version rises, and when the GPU part's tolerances change.
4. Merge it. The release workflow tags the version, publishes the GitHub release with the same notes, and attaches the source archive `maul-vfx-X.Y.Z.tar.gz` (the tagged tree with the compiled kernel containers), a `SHA256SUMS` file, an SPDX SBOM and a build provenance attestation. Check that they are on the release; a user verifies a download with `sha256sum -c SHA256SUMS` and `gh attestation verify maul-vfx-X.Y.Z.tar.gz -R Rawframe-Project/maul-vfx`.

## When the release workflow fails

- If release-please itself failed in the push run, run the release workflow again by hand (`workflow_dispatch`, with no input).
- If only `assets` failed, re-run that job from its run. A second release-please run finds the release made and starts no `assets`.
- When the release app cannot publish the release at all (it cannot create one on a commit whose workflows differ from `main`'s head), a maintainer creates the tag and the release by hand, with the curated notes. They then move the release pull request's label from `autorelease: pending` to `autorelease: tagged`, and run the release workflow from `main` with that tag as its `tag` input. `assets` then builds, attests and attaches the files of that release.

`assets` refuses:

- a `tag` input given to a run that is not on `main`; the job does not start;
- a tag that is not `vX.Y.Z`;
- a run that creates one release and is given another tag;
- a tag whose commit `main` does not hold, since the attestation names the workflow on `main`;
- a release that already has files: nothing replaces a published file.

So a run that failed after attaching some files is re-run only after a maintainer deletes those files from the release by hand (`gh release delete-asset vX.Y.Z <file>`).

## Version bumps that go with a change

- **The ABI version** (`MVFX_ABI_VERSION`) follows the library version by itself; nothing is bumped by hand.
- **The effect file format** (`MVFX_FILE_VERSION` in `include/maul-vfx/file.h`) rises with any change to a file's bytes, in the change that makes it, together with the fixtures (`tools/effect_json.py --write test/fixtures`).
- **The golden hashes** in `test/test_golden.c` change with any change to results, in the change that makes it.
- **Maul RHI** is pinned by version in `cmake/Rhi.cmake` (`MAUL_VFX_RHI_VERSION`); a new Maul RHI release is taken in a pull request of its own, with the kernel containers rebuilt by its tools when their format changed.

Each of these is part of the change that needs it, not of the release pull request.
