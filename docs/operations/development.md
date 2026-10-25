# Development

Setting up a machine to build and test Maul VFX, and regenerating what is generated.

## Toolchains

| Target | Needs |
|---|---|
| Linux | CMake 3.25, Ninja, GCC 14 and Clang 20 (`gcc-14`, `clang-20`), `clang-tidy-20`, clang-format 22.1.5 (`pip install clang-format==22.1.5`); for the GPU part's suites, lavapipe (`mesa-vulkan-drivers`) and the Vulkan validation layers. |
| Windows | Visual Studio with "C++ Clang tools for Windows" (`clang-cl`). |
| macOS | Xcode's Apple Clang. |
| The web | Emscripten (`EMSDK` set, as `emsdk activate` does) and Node. |

The organization's CI image has everything the Linux cells need. On the shared development machine, run full checks, large builds and fuzzing through `/home/rawframe-project/tools/heavy.sh`.

## Building

Every CI cell is a preset of `CMakePresets.json`, built in `out/<preset>`:

```sh
cmake --preset linux-clang-20-asan
cmake --build --preset linux-clang-20-asan
ctest --preset linux-clang-20-asan
ctest --preset linux-clang-20-asan -L effect   # one suite, by its label
```

A plain build works too:

```sh
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-20
cmake --build build
ctest --test-dir build
```

The options a developer uses besides the user's: `MAUL_VFX_WERROR`, `MAUL_VFX_SANITIZE` (AddressSanitizer and UBSan), `MAUL_VFX_TSAN`, `MAUL_VFX_COVERAGE`, `MAUL_VFX_FUZZ` (the libFuzzer targets, Clang only) and `MAUL_VFX_RHI` (the GPU part, which fetches Maul RHI at configure time unless it is installed).

## The GPU part's tests

The GPU part's suites need a device. Those that run on Maul RHI's test driver (`rhi_object`, `rhi_uploads`, `rhi_host_calls`, `rhi_mesh_image`) need none, and the `linux-rhi` preset runs them, with the GPU sample (`sample_gpu`) on lavapipe as a smoke test. Those that compare a planned instance with the CPU path on a real device carry the `integration` and `nightly` labels and run in the `linux-rhi-lavapipe` presets, which the nightly check runs:

```sh
cmake --preset linux-rhi-lavapipe
cmake --build --preset linux-rhi-lavapipe
/home/rawframe-project/tools/heavy.sh ctest --preset linux-rhi-lavapipe -R rhi_events
```

A device suite and the GPU sample exit 77 and count as skipped when the adapter search succeeds and finds no adapter, unless `MVFX_RHI_REQUIRED` is set; a failed search, device or GPU object fails them. Those presets set it, to `vulkan`, or `d3d12` in the `windows-rhi-d3d12` preset; the device suites and the sample then also refuse an adapter of another driver. On D3D12 only `rhi_kernels` runs: WARP, the runners' D3D12 adapter, faults compiling the simulation's kernels, and its in-place 64-bit remainders are an expected failure there that fails the check if it ever passes ([#65](https://github.com/Rawframe-Project/maul-vfx/issues/65)). The first frame of a run compiles every kernel it dispatches: minutes on lavapipe with an empty shader cache, a fraction of a second with a warm one. lavapipe keys its cache on the CPU, its count included, so pinning a run to other cores (`taskset`) starts it cold.

## Generated files

| Files | Generator | Inputs | In CI |
|---|---|---|---|
| `src/file_fields.c` | `python3 tools/gen_file.py` | the public headers | checked with `--check` |
| `src/marching_table.c`, `rhi/src/marching_words.c` and `.h`, `rhi/kernels/shared/marching_words.wgsl` | `python3 tools/gen_marching_cubes.py` | none; the cases are derived | checked with `--check` |
| `rhi/kernels/*_container.h`, `test/kernels/*_container.h` | `python3 tools/gen_kernels.py <maul-rhi tree>` | the WGSL sources and JSON bindings beside them | committed; `--check` checks the WGSL they carry |
| `test/fixtures/*.mvfx` | `python3 tools/effect_json.py --write test/fixtures` | the JSON forms beside them | checked with `--check`, and against the library's own writing |
| `test/corpus/*.tar.gz` | minimized from local fuzzing runs | | seeds only |

`tools/math_constants.py` derives the constants of `src/elementary.c`, and `tools/pcg4d_vectors.py` the reference values `test/test_random.c` checks; both print what the source holds, for review.

Run a generator again whenever its inputs change, and commit what it writes. [kernels.md](kernels.md) covers the kernel containers, which need tools CI does not have.

## Family files

The files listed under "Editing shared family files" in [AGENTS.md](../../AGENTS.md#ways-to-hurt-yourself) are synced from the organization's `.github` repository. Take a new family revision with

```sh
python3 /home/rawframe-project/.github/maul/family_sync.py --write maul-vfx
python3 /home/rawframe-project/.github/maul/family_sync.py --check maul-vfx
```

in a pull request of its own.
