# Debugging

How to find out what went wrong in a failing test, a determinism check or the GPU part.

## A golden hash that changed

`test_golden` hashes the scene's whole state after every step and compares with the values committed in `test/test_golden.c`. When a change is meant to change results, run it with `--print` to print the values this build computes, paste them in, and say in the pull request and the changelog what changed for users. When it is not meant to, the first step whose hash differs points at the change: bisect the step's modules with the suites of `test/`, which compare narrower things.

A hash that differs on one platform only is a determinism bug: a call outside `tools/libm-allowed.txt`, a contraction the compiler made, a float environment the library did not set, or an order that depends on the platform. `test_twins` differing means the order depends on the task split.

## Sanitizers

The `linux-clang-20-asan` preset builds with AddressSanitizer and UBSan, and `linux-clang-20-tsan` with ThreadSanitizer; `test_twins` and `test_tasks` are the suites ThreadSanitizer exists for. A sanitizer report names the frame; rerun the one suite with `ctest --preset <preset> -L <suite> --output-on-failure`.

## A trap

A test that stops with `SIGILL` (x86-64) or `SIGTRAP` (AArch64) in a build without `NDEBUG` hit an `MVFX_ASSERT` (`src/invariant.h`): an internal invariant the library broke, never a caller's mistake, which is refused with a status instead. Run the suite under a debugger; the trap's frame is the broken check. Release builds compile the checks away.

## The GPU part

- **A parity failure.** The lavapipe suites step each scene on both paths and compare every particle after every step through `test/rhi_harness.c`. A failure prints the step, the emitter and the largest relative differences in place, velocity and bounds; run the suite alone to see it.
- **A frame that does not finish.** The harness waits for each frame, without a limit of its own for a run's first frames, which compile kernels, and briefly for the rest, so a kernel that hangs fails in seconds. A frame that fails prints its submit and wait results and its time; each run prints its longest warm-up frame and its longest later frame.
- **The validation layer.** `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` turns it on, as `linux-rhi-lavapipe` does; any `Validation Error` or `VUID-` in a test's output fails it.
- **Slow first frames.** lavapipe compiles each kernel with LLVM on one thread at its first dispatch; with an empty shader cache a suite's first frame takes minutes. `MESA_SHADER_CACHE_DISABLE=true` reproduces a cold cache, and `MESA_SHADER_CACHE_DIR` moves it. The cache is keyed on the CPU, its count included, so a run under `taskset` with another core count starts cold. `GALLIVM_PERF=nopt` turns LLVM's optimizations off, which halves a cold first frame and slows the kernels; the `linux-rhi` preset's smoke test sets it, and the parity suites do not. Redirected output is fully buffered: `stdbuf -oL` shows a running suite's lines as they come.
- **AddressSanitizer and lavapipe.** LeakSanitizer cannot suppress lavapipe's own allocations, which its module frees after the report, so the GPU part's sanitizer runs set `ASAN_OPTIONS=detect_leaks=0`; the suites count the part's own allocations instead.

## Effect files

`python3 tools/effect_json.py to-json <file> <out>` shows a file whole, or names why it is refused. A file refused by the library comes with the offset of the first byte at fault.

## Fuzzing

Build with `-DMAUL_VFX_FUZZ=ON` and Clang, then run `fuzz_file` (the effect file reader) or `fuzz_step` (stepping whatever effects fuzzed files make) over a corpus directory, always with `-max_total_time`. `test/corpus/<target>.tar.gz` holds each target's seeds, and `test_golden --seeds <dir>` writes the golden scene's effects as files, the reader's first seeds. A crash leaves its input in the working directory; run the target on that file alone to reproduce it, then add the input as a test in the module's suite.
