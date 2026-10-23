# Releasing

Maul VFX follows Semantic Versioning with tags `vX.Y.Z`. Before 1.0.0 any release may change the API, the ABI, the effect file format and the simulation's results.

## How a release happens

1. Pull requests merge to `main` with conventional commit titles (`feat`, `fix`, `perf` and the rest; see [AGENTS.md](../../AGENTS.md#pull-requests)).
2. release-please (`.github/workflows/release.yml`, configured by `release-please-config.json` and `.release-please-manifest.json`) keeps a draft release pull request open, titled `chore: release X.Y.Z`. It moves the version macros between the release-please markers in `include/maul-vfx/base.h` and writes a changelog entry from the commit titles.
3. Before merging it, rewrite its `CHANGELOG.md` entry in user language: what changed for someone using the library, grouped as Added, Changed, Fixed, Removed and Security. Leave out refactors, tests, CI and documentation. Say when the simulation's results change (the golden hashes moved), when the effect file format's version rises, and when the GPU part's tolerances change.
4. Merge it. The release workflow tags the version, publishes the GitHub release with the same notes, and attaches the source archive, a `SHA256SUMS` file, an SPDX SBOM and a build provenance attestation.

## Version bumps that go with a change

- **The ABI version** (`MVFX_ABI_VERSION`) follows the library version by itself; nothing is bumped by hand.
- **The effect file format** (`MVFX_FILE_VERSION` in `include/maul-vfx/file.h`) rises with any change to a file's bytes, in the change that makes it, together with the fixtures (`tools/effect_json.py --write test/fixtures`).
- **The golden hashes** in `test/test_golden.c` change with any change to results, in the change that makes it.
- **Maul RHI** is pinned by version in `cmake/Rhi.cmake` (`MAUL_VFX_RHI_VERSION`); a new Maul RHI release is taken in a pull request of its own, with the kernel containers rebuilt by its tools when their format changed.

Each of these is part of the change that needs it, not of the release pull request.
