# Maul VFX

Maul VFX is a C library that simulates visual effects and hands a renderer what to draw as data: particles from emitters, ribbons and beams, ropes, cloth and rigid bodies, gases, liquids and water surfaces. A context holds the host's allocator and task hooks; effects compiled from defs or read from files play as instances, and each step advances them. An optional part, `maul-vfx-rhi` in `rhi/`, runs planned instances on a GPU through Maul RHI. It is part of the Maul family. The family rules are in [docs/internals/maul-conventions.md](docs/internals/maul-conventions.md), and [docs/internals/overview.md](docs/internals/overview.md) explains how the parts fit.

It never compromises on:

1. **The same bits everywhere.** The CPU path gives the same bits on every platform, compiler and thread count for the same effects, seeds, inputs and steps, whatever length the steps have. [determinism.md](docs/internals/determinism.md) lists what that takes.
2. **No allocation after creation.** Everything comes from the host's allocator when an object is made; stepping and output allocate nothing, and whatever a capacity refuses is counted.
3. **Threads are the host's.** The library starts none; parallel work runs on the host's task hooks, and the split never changes a result.
4. **Hostile input.** Effect files and every argument are validated and refused with a status in every build.
5. **The GPU part decides nothing.** It executes the plans the CPU path writes; integers equal the CPU path's, floats within stated tolerances.

## Commands

```sh
tools/check.sh changed          # before every push: rules, generated files, format, the asan preset (< 2 min)
tools/check.sh pr               # the Linux part of the pull request check: clang-tidy, asan, gcc and linux-rhi presets, short fuzzing
tools/check.sh full             # adds TSan, the shared library, Clang Release, longer fuzzing
tools/check.sh extended         # adds the GPU part's suites on lavapipe (up to 2 hours)
cmake --preset linux-clang-20-asan && cmake --build --preset linux-clang-20-asan   # one CI cell
ctest --preset linux-clang-20-asan -L effect                   # one suite, by label
out/linux-clang-20/test_golden --print                          # the golden hashes this build computes
python3 tools/gen_file.py       # regenerate src/file_fields.c after changing a def struct
```

Run the `pr`, `full` and `extended` tiers, the lavapipe presets and fuzzing through `/home/rawframe-project/tools/heavy.sh` on the shared machine. Every CI cell is a preset in `CMakePresets.json`, built in `out/<preset>`. Each suite `test/test_<name>.c` has the label `<name>`. Tests that need a device carry `integration`; the GPU part's suites among them also carry `nightly` and run only in the `linux-rhi-lavapipe` presets, while `linux-rhi` runs `sample_gpu` on lavapipe as a smoke test.

## Ways to hurt yourself

- **Killing by pattern.** Never `pkill -f`, `pgrep | kill` or `kill` a PID found by matching a name or path: other sessions build, fuzz and run the same test binaries on the same machine. Kill only a PID you started.
- **Endless fuzzing.** A libFuzzer target runs until it finds something. Always pass `-max_total_time`.
- **lavapipe on a loaded machine.** A GPU suite's first frame compiles every kernel it dispatches, single-threaded, for minutes when the Mesa shader cache is cold; the cache is keyed on the CPU and its core count, so `taskset` with another count starts it cold. Run them through heavy.sh, one at a time, and do not read a slow first frame as a hang.
- **Editing generated files.** `src/file_fields.c` (`tools/gen_file.py`), `src/marching_table.c`, `rhi/src/marching_words.c` and `.h` and `rhi/kernels/shared/marching_words.wgsl` (`tools/gen_marching_cubes.py`) are written by tools, and CI fails on any difference. Edit the inputs and regenerate.
- **Kernel containers.** `rhi/kernels/*_container.h` and `test/kernels/*_container.h` are compiled WGSL committed because CI has no naga, SPIRV-Cross or DXC. Edit the `.wgsl`, then rebuild with `tools/gen_kernels.py` where those tools are installed ([kernels.md](docs/operations/kernels.md)); `--check` in CI fails a container that no longer carries its sources.
- **Effect fixtures.** `test/fixtures/*.mvfx` are written from the JSON beside them (`tools/effect_json.py --write test/fixtures`), and the golden test checks the library writes the same bytes.
- **Editing shared family files.** These are synced from the canonical copies in the organization's `.github` repository and fail CI when they differ: `docs/internals/maul-conventions.md`; `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.gitignore`; `cmake/CompilerFlags.cmake`, `cmake/Install.cmake`, `cmake/Library.cmake`, `cmake/ReadVersion.cmake`, `cmake/Testing.cmake`, `cmake/Config.cmake.in`, `cmake/pkg-config.pc.in`, `cmake/HeaderCheck/CMakeLists.txt`; `bench/CMakeLists.txt`; `test/test_harness.h`, `test/test_fenv.h`, `test/test_fpenv.c`; `src/hash.c`, `src/fpenv.c`, `src/fpenv.h`; `tools/build_docs.py`, `check_docs.py`, `check_exports.py`, `check_float_env.py`, `check_guide.py`, `check_lengths.py`, `check_modules.py`, `check_source.py`, `check_test_hygiene.py`, `gen_api.py`; and `tools/test_hygiene/`. Change the canonical copy first.
- **The network.** `MAUL_VFX_RHI=ON` fetches Maul RHI from GitHub at configure time unless it is installed. Builds without it never touch the network.

## Invariants

- **Determinism.** Only the `<math.h>` functions in `tools/libm-allowed.txt`; sine, cosine, exponential and power come from `src/elementary.c`. No contraction or fast math. Random numbers come from pcg4d of a seed, a salt, a serial and a counter, never from state that follows draw order. Every parallel item writes only its own slots; solver passes are gathers. A change meant to change results updates `test/test_golden.c` and the changelog.
- **Time.** Ticks of 2^-20 seconds. A def's times are rounded to ticks once, when its effect is made.
- **The effect file.** `MVFX_FILE_VERSION` rises with any change to a file's bytes. A def struct's fields are the file's fields: changing one means `tools/gen_file.py`, the fixtures and, before 1.0, nothing else.
- **Versions.** The library version lives in `include/maul-vfx/base.h` between the release-please markers; only release-please changes it. Maul RHI's version is pinned in `cmake/Rhi.cmake`.
- **ABI stamp.** Only the root def, `mvfxContextDef`, carries `MVFX_ABI_VERSION`; `mvfxCreateContext` checks its cookie first and its version second, before it reads anything else. Other defs carry only a cookie.
- **Assertions:** `MVFX_ASSERT` (`src/invariant.h`) checks internal invariants only. It traps in builds without `NDEBUG` (Debug, the sanitizers, fuzzing) and compiles out of Release. Never assert on caller input, effect file bytes or what a host's hook or a GPU returns; validate it and return a status. `rhi/src` includes no core header, so the GPU part has no assertions.
- **Modules:** `tools/modules.txt` lists which module may include which. A new include needs a line there. The core never includes `rhi/`; `rhi/src` includes only its own headers and the public ones.
- **Host code calling back in.** The allocator, task hooks and host modules may call only functions taking a `const` context; a changing call while host code runs is refused and counted as misuse. Keep that check in any new function that changes a context or a simulation. It returns `mvfx_errorState` until #118 moves it to busy, as the family conventions now say.
- **Capacities.** Nothing grows after creation. Work a capacity refuses is counted in `mvfxEmitterStats` (or the GPU part's statistics record), never dropped silently.
- **Workflows:** every action is pinned by commit SHA with its version in a comment, and each workflow and job declares the least `permissions` it needs.

## Testing

Follow the [repository standard's Tests section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#tests); in this repository:

- `test/test_golden.c` is the determinism specification: a few scenes hashed after every step on every CI platform. Keep it a handful of scenes; a module's behavior is tested in its own suite through the public API.
- `test/test_twins.c` steps the golden scene serially and on four threads with random splits; a module that adds parallel work must keep it equal.
- A test of the GPU part compares it with the CPU path through `test/rhi_harness.c`, not with numbers of its own.
- Every C snippet of `docs/user/` appears unchanged in `test/test_guide.c` or `test/test_guide_rhi.c`; `tools/check_guide.py` enforces it.
- A fuzz finding becomes a test in the module's suite with the minimized input.

## Documentation

Follow the [repository standard's Documentation section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#documentation). A change in what a host must do or can rely on updates its page of `docs/user/` and, if it is in a snippet, the matching code in `test/test_guide*.c`. Measured limits of a solver live in the header comment of the field they concern. The API reference is generated into the documentation site; never commit it.

## Pull requests

Titles are conventional commits. Scopes: `api`, `base`, `step`, `spawn`, `forces`, `collide`, `modules`, `events`, `effect`, `file`, `interaction`, `gas`, `water`, `output`, `ribbon`, `math`, `gpu`, `kernels`, `samples`, `bench`, `tools`, `build`, `family`. Example: `fix(collide): keep a resting particle on a moving box`.

A change to results, the effect file format or the GPU part's tolerances says so in its body.
