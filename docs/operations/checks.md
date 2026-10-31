# Checks

What to run before pushing, what each tier covers, and what the test presets expect of the machine.

## The tiers

`tools/check.sh` has four tiers; each includes the one before it.

| Tier | Runs | Budget |
|---|---|---|
| `changed` | The source, module, length, documentation and snippet rules; the generated files' checks; the organization's documentation check; clang-format on the changed files; clang-tidy on the changed library sources (`src/*.c` and `rhi/src/*.c`, as the `pr` tier reads them, in a Release and a Debug tree); the `linux-clang-20-asan` preset. A change to documentation alone builds nothing. | under 2 minutes |
| `pr` | Every rule; clang-format on every source but the generated tables and kernel containers; clang-tidy on `src/` and `rhi/src/` but `rhi/src/kernels.c`, in a Release tree, where an assertion's condition is not evaluated, and in a Debug one, where the analyzer reads it; both parts built at `-O0`, where no function may take a stack frame over 64 KB; the `linux-clang-20-asan`, `linux-gcc-14` and `linux-rhi` presets; 10 seconds of fuzzing per target. | under 10 minutes |
| `full` | Also ThreadSanitizer, the shared library, Clang's Release build, clang-tidy on `rhi/src/kernels.c` (it includes 70 MB of containers and takes over three minutes), and 60 seconds of fuzzing per target. | under 20 minutes |
| `extended` | Also the GPU part's suites on lavapipe: under the Vulkan validation layer, and under AddressSanitizer. | up to 2 hours |

Run `changed` before every push. Run the other tiers through the workspace's heavy-job wrapper on the shared machine:

```sh
/home/rawframe-project/tools/heavy.sh tools/check.sh full
```

The macOS, Windows and web cells run in CI only, `windows-d3d12` among them. The organization's documentation check runs when the `.github` repository is checked out beside the workspace (`ORG_DEFAULTS` overrides where).

## The rules

| Check | Tool |
|---|---|
| Formatting | `clang-format` 22.1.5, `.clang-format` |
| Naming, complexity | `clang-tidy-20`, `.clang-tidy` |
| No stack frame over 64 KB in either part | `-Wframe-larger-than=65536` (`mvfx_limit_frames` in `CMakeLists.txt`), shown by the unoptimized build |
| SPDX lines, comments, banned calls, the `<math.h>` allow list, file-scope state, file names | `tools/check_source.py`, `tools/libm-allowed.txt` |
| The module graph | `tools/check_modules.py`, `tools/modules.txt` |
| File and function lengths, WGSL files included | `tools/check_lengths.py` |
| Public documentation blocks and thread safety statements | `tools/check_docs.py` |
| Documentation snippets run by `test/test_guide*.c` | `tools/check_guide.py` |
| The effect file's field tables match the headers | `tools/gen_file.py --check` |
| The marching cubes tables | `tools/gen_marching_cubes.py --check` |
| The kernel containers carry their sources | `tools/gen_kernels.py --check` |
| The effect fixtures' JSON and binary forms agree | `tools/effect_json.py --check test/fixtures` |
| No sleep, wall clock, retry or unexplained skip in a test, and every test labeled | `tools/check_test_hygiene.py`: its self-test, the tree, and each configured build's tests (`--ctest`) |
| Every byte array in a test states its alignment (`_Alignas(16)`), so a buffer the library checks for alignment never passes by where it lands | `tools/check_test_buffers.py` and its self-test |
| A public function whose output has an input's element type says in its header whether they may overlap | `tools/check_overlap.py` and its self-test |
| Allowed Markdown files, em dash, record numbers, links | the organization's `docs_check.py` |

Each tool's header comment and message state its rule and why.

## Labels

Every suite `test/test_<name>.c` carries the label `<name>`; `ctest -L <label>` runs one suite. The samples carry `sample`. Tests that drive a device carry `integration`; `-LE integration` leaves them out. The GPU part's suites among them also carry `nightly`: they run in the `linux-rhi-lavapipe` presets of the nightly check, while `linux-rhi` runs the GPU sample on lavapipe as the pull request's smoke test. A device suite or the GPU sample whose adapter search succeeds and finds no adapter exits 77 and counts as skipped, unless `MVFX_RHI_REQUIRED` is set, as the device presets set it; a failed search or device fails it; CI fails a cell with a skipped test. Set to `vulkan` or `d3d12`, as the lavapipe and D3D12 presets set it, the device suites and the GPU sample also fail on an adapter of another driver.

The lavapipe presets run one suite at a time, since lavapipe uses every core for one suite, and fail a test whose output holds a message of the Vulkan validation layer (`Validation Error` or a `VUID-`).

The `windows-rhi-d3d12` preset runs `rhi_kernels` on WARP, D3D12's software adapter, under the Agility SDK's debug layer and GPU-based validation, and fails on any error the layer reports, through Maul RHI's own `test/d3d12_debug.c` from the tree CMake fetches at the pinned version (`MAUL_VFX_D3D12_AGILITY` names the SDK's DLLs; `tools/ci_agility.sh` fetches them with Maul RHI's script at that version). The validation's resource state tracking is off, as in Maul RHI's own D3D12 cell, where it reports false errors. The suites that build a simulation (the harness suites, `guide_rhi`, `sample_gpu`) do not run there: WARP faults compiling the simulation's kernels, and `rhi_kernels`' in-place remainders are WARP's expected failure, which fails the check if it ever passes ([#65](https://github.com/Rawframe-Project/maul-vfx/issues/65)).

## White-box suites

A suite tests through the public headers. One that includes a header from `src/` is marked `WHITEBOX` in `CMakeLists.txt` and is skipped in the shared build, which hides the internal symbols; [#15](https://github.com/Rawframe-Project/maul-vfx/issues/15) moves those suites to the public API and names each one that stays, with the reason.

The one internal header a public suite may include is `src/test_hooks.h`. Its functions set or read a state no public call reaches within a test: an emitter's births owed, so its serials reach their 32-bit wrap; one chosen torn edge; the broadphase's rigid pairs; a gas's face velocities, scalar fields and obstacle marks; shallow water's depths and face velocities. They build into `maul-vfx-test-hooks`, which is never installed and links only into a static build, so a suite that uses them keeps `WHITEBOX` for the include path and the link alone. Each hook's comment names its state and why no public call observes it, and #15 lists them. A new hook takes its line there, and one whose state a host would plausibly read also gets an issue as a candidate public output.

## CI

`.github/workflows/ci.yml` runs on every pull request, on every push to `main`, every night and by hand. The ruleset requires one job, `required`, which passes when every other job passed or was skipped.

| Job | What it runs | When |
|---|---|---|
| `changes` | Whether anything beyond documentation changed; the build jobs skip when nothing did. | always |
| `docs`, `zizmor` | The organization's documentation check, from the commit the workflow pins (its `docs-check` action), and zizmor over the workflows. | always |
| `checks` | clang-format, the rules above (test hygiene over the library's build and the GPU part's), the generated files' checks, the documentation site's build, clang-tidy in a Release and a Debug tree (`rhi/src/kernels.c` only off pull requests), and both parts built at `-O0`, where no function may take a stack frame over 64 KB. | always |
| `build` | One cell per CMake preset through the organization's `cpp.yml`, in its CI image on Linux: `linux-gcc-14`, `linux-arm64-gcc-14`, `linux-clang-20`, `linux-clang-20-asan` (each test up to three times on a pull request), `linux-clang-20-tsan`, `linux-shared`, `linux-rhi`, `macos-clang` and `windows-clang-cl`. | code changes |
| `package` | The installed shared library used by `samples/minimal`, and the static library's size. | code changes |
| `msvc-headers` | The public headers under MSVC's `cl` as C17 and C++17 (`cmake/HeaderCheck`). | code changes |
| `web-emscripten` | The suites under Node, and the library's size. | code changes |
| `fuzz` | Both fuzz targets, 30 seconds each on a pull request and 300 elsewhere; the fixtures written again by the library. | code changes |
| `extended` | The GPU part's suites on lavapipe, `linux-rhi-lavapipe` and `linux-rhi-lavapipe-asan`, up to two hours. | nightly and by hand |
| `windows-d3d12` | `rhi_kernels` on D3D12 (WARP), `windows-rhi-d3d12`, under the Agility SDK's debug layer. | nightly and by hand |

`cpp.yml` restores ccache and the Mesa shader cache on every run and saves them only from `main`, so a pull request starts from `main`'s latest build. Each save on `main` is keyed by its commit, so main's last job, `prune-caches` (the organization's `cache-prune.yml`), deletes every entry a newer save of the same cache replaced; the 10 GB the repository has keep one entry per cache, and GitHub never evicts the fuzz corpus to make room for old builds. The shader cache is kept per CPU model, feature set and core count, since lavapipe keys its cache on them. A cell fails when a test is skipped or not run, and uploads its test logs when it fails.

The nightly `fuzz` run minimizes each target's corpus with libFuzzer's merge and saves it in an Actions cache, which every later run restores, a pull request's too, so a change that breaks a harness's promise fails on its own pull request and not later on `main`. The cache is not secret, since a fork's pull request can restore it too, and it holds nothing the seeds in `test/corpus/` would not lead to; those seeds stay every run's floor. A run that finds something saves no corpus and uploads nothing: `tools/fuzz_target.sh` keeps the input, the seed and the finding's name on the runner and reports only the kind of each finding and the sanitizers' report. A finding becomes a test in the module's suite, and an input worth keeping a seed.

`codeql.yml` builds the library, the GPU part and the tests with GCC 14 for CodeQL. `pages.yml` publishes the documentation site from `main`. `pr-title.yml` checks a pull request's title against the scopes in `AGENTS.md`, and `pr-gate.yml` closes a pull request from outside the organization unless it closes an accepted issue. `release.yml` and `scorecard.yml` are described in [release.md](release.md) and the README's badge.

## Manual tests

`test_elementary_sweep` and `test_half_sweep` are built but not registered: they compare the library's own sine, cosine, exponential, power and half conversion against references over every binary32 input of their domains, which takes minutes. Run them by hand after changing `src/elementary.c` or `src/half.c`:

```sh
/home/rawframe-project/tools/heavy.sh out/linux-clang-20/test_elementary_sweep
```
