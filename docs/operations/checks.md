# Checks

What to run before pushing, what each tier covers, and what the test presets expect of the machine.

## The tiers

`tools/check.sh` has four tiers; each includes the one before it.

| Tier | Runs | Budget |
|---|---|---|
| `changed` | The source, module, length, documentation and snippet rules; the generated files' checks; the organization's documentation check; clang-format on the changed files; the `linux-clang-20-asan` preset. A change to documentation alone builds nothing. | under 2 minutes |
| `pr` | Every rule; clang-format on every source but the generated tables and kernel containers; clang-tidy on `src/` and `rhi/src/` but `rhi/src/kernels.c`; the `linux-clang-20-asan`, `linux-gcc-14` and `linux-rhi` presets; 10 seconds of fuzzing per target. | under 10 minutes |
| `full` | Also ThreadSanitizer, the shared library, Clang's Release build, clang-tidy on `rhi/src/kernels.c` (it includes 70 MB of containers and takes over three minutes), and 60 seconds of fuzzing per target. | under 20 minutes |
| `extended` | Also the GPU part's suites on lavapipe: under the Vulkan validation layer, and under AddressSanitizer. | up to 2 hours |

Run `changed` before every push. Run the other tiers through the workspace's heavy-job wrapper on the shared machine:

```sh
/home/rawframe-project/tools/heavy.sh tools/check.sh full
```

The macOS, Windows and web cells run in CI only. The organization's documentation check runs when the `.github` repository is checked out beside the workspace (`ORG_DEFAULTS` overrides where).

## The rules

| Check | Tool |
|---|---|
| Formatting | `clang-format` 22.1.5, `.clang-format` |
| Naming, complexity | `clang-tidy-20`, `.clang-tidy` |
| SPDX lines, comments, banned calls, the `<math.h>` allow list, file-scope state, file names | `tools/check_source.py`, `tools/libm-allowed.txt` |
| The module graph | `tools/check_modules.py`, `tools/modules.txt` |
| File and function lengths, WGSL files included | `tools/check_lengths.py` |
| Public documentation blocks and thread safety statements | `tools/check_docs.py` |
| Documentation snippets run by `test/test_guide*.c` | `tools/check_guide.py` |
| The effect file's field tables match the headers | `tools/gen_file.py --check` |
| The marching cubes tables | `tools/gen_marching_cubes.py --check` |
| The kernel containers carry their sources | `tools/gen_kernels.py --check` |
| The effect fixtures' JSON and binary forms agree | `tools/effect_json.py --check test/fixtures` |
| Allowed Markdown files, em dash, record numbers, links | the organization's `docs_check.py` |

Each tool's header comment and message state its rule and why.

## Labels

Every suite `test/test_<name>.c` carries the label `<name>`; `ctest -L <label>` runs one suite. The samples carry `sample`. The GPU part's suites that drive a device carry `integration` and run in the `linux-rhi-lavapipe` presets; `-LE integration` leaves them out. A device suite that finds no adapter exits 77 and counts as skipped, unless `MVFX_RHI_REQUIRED` is set, as the lavapipe presets set it; CI fails a cell with a skipped test.

The lavapipe presets run one suite at a time, since lavapipe uses every core for one suite, and fail a test whose output holds a message of the Vulkan validation layer (`Validation Error` or a `VUID-`).

## Manual tests

`test_elementary_sweep` and `test_half_sweep` are built but not registered: they compare the library's own sine, cosine, exponential, power and half conversion against references over every binary32 input of their domains, which takes minutes. Run them by hand after changing `src/elementary.c` or `src/half.c`:

```sh
/home/rawframe-project/tools/heavy.sh out/linux-clang-20/test_elementary_sweep
```
