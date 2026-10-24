# Determinism

The CPU path is bit-exact: the same effects, seeds, inputs and sequence of step lengths give the same bits on every platform, compiler and architecture the family builds for, whatever the host's thread count and however its task system splits the work. That covers every particle attribute, count, event, bound, ribbon point and output record. A game that replays, rolls back, or shows the same effect to several players needs particles placed the same way everywhere; a host that renders on different machines needs bounds and draw orders that agree. This page explains each piece that makes it hold, and what breaks it.

## Time in ticks

Time is an integer count of ticks, 2^20 a second (`MVFX_TICKS_PER_SECOND`). A def's times (bursts, delays, durations, lifetimes) are rounded to ticks once, when the effect is compiled, and the simulation reasons in ticks from then on. A float of seconds summed step by step drifts; a tick count does not, so a burst every 0.1 s fires at the same tick on the thousandth loop as on the first.

Each birth happens at its own tick, and an emitter's step reaches exactly the state that stepping one tick at a time would reach, each tick's deaths before its births. [simulation.md](simulation.md) shows how the step does that without walking the ticks. The consequence is that two steps of 1/60 s and one step of 1/30 s reach the same particles: the state does not depend on how time was split, only on the inputs between the steps.

## Random numbers by coordinates

Every random draw is pcg4d (Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT 2020) of four coordinates: the instance's seed, a salt naming the emitter and the purpose (a lifetime, a place on the shape, a velocity, a burst count), the particle's serial, and a counter ([src/random.h](../../src/random.h)). A value depends only on those, never on how many values were drawn before it or in what order. Parallel work can draw in any order, a module added to an emitter does not shift the draws of the others, and the GPU path, which runs pcg4d in 32-bit integer arithmetic, draws the same words. `tools/pcg4d_vectors.py` computes the reference values from the published listing in Python, and `test/test_random.c` checks the C against them.

Serials are counted per emitter in birth order, and births on the same tick are ordered by a fixed rule: bursts first by index, then the rate, then distance. A particle's serial is therefore part of its identity across platforms and across the CPU and GPU paths.

## Floating point

- **No contraction, no fast math.** `cmake/CompilerFlags.cmake` builds with `-ffp-contract=off` and refuses fast-math flags: a fused multiply-add rounds once where the source rounds twice, and compilers choose differently.
- **Exactly rounded library functions only.** `<math.h>` functions other than those IEEE 754 rounds exactly may differ by an ulp between C libraries. The library calls only the ones in `tools/libm-allowed.txt` (`sqrt`, `floor`, `frexp`, `ldexp` and the like), and `tools/check_source.py` refuses any other.
- **Its own transcendental functions.** Sine, cosine, exponential and power are written in [src/elementary.c](../../src/elementary.c): each reduces its argument and sums its series in binary64, then rounds once to binary32, so the result is within one ulp of the true value and the same everywhere. `tools/math_constants.py` derives the reduction constants exactly, and the manual sweeps compare every binary32 input of their domains against a reference.
- **Minimum and maximum** are a compare and a select, not `fminf`, whose treatment of signed zeros and NaNs varies.
- **The rounding mode.** A host may run its threads with flush-to-zero, denormals-are-zero or another rounding mode. Every public call that computes floats enters the family's floating-point environment ([src/fpenv.h](../../src/fpenv.h)) and leaves the thread as it found it: round to nearest, subnormals kept, no traps, the control register put back on leaving, sticky flags included. That covers the steps, effect creation and files, meshes, instance inputs, every output, read, sample and plan, and the context-free bakes; the task ranges it hands the host enter it again on whatever thread runs them, and entries nest. The calls that compute no float (defaults, counts, views of stored values, destruction, integer draws, inputs stored as given) are named in `tools/float-env-allowed.txt` under the reason, and `tools/check_float_env.py` fails a public function that neither enters nor is named. A comparison counts as computing: denormals-are-zero reads a subnormal as 0. Host modules run inside the environment, since their results feed the simulation; the allocator and the task hooks compute nothing of the library's and run in the host's own. This holds on x86 and AArch64; WebAssembly has one environment and needs no guard. `test_fpenv` checks the module; `test_hosts` runs every computing call once in the default setting and under each rounding mode with the flush bits off and on, and requires the same bytes; `test_host_calls` checks where host code runs; `test_float_mode` steps a gas scene in flush-to-zero and round toward zero.

## Threads that cannot change results

The library starts no thread. A context's step hands its instances to the host's task hooks, one instance per item: the host splits the items into ranges of its choosing and runs them on any thread in any order. Each item writes only its own instance, and nothing an item reads is written by another, so the split and the schedule cannot change a bit. Gases and water surfaces spread their cells over tasks the same way: every pass of their solvers is a gather, each cell reading the previous buffer and writing its own, never a scatter whose sums would depend on order. Where a scatter is inherent (particles splatting into a gas), it runs serially in the particles' order.

## Wide coordinates

Instance origins are `double`. A world-space emitter stores its particles as `float` offsets from an anchor near its instance. When the instance has gone `MVFX_REBASE_DISTANCE` from the anchor, the step moves the anchor to it and shifts every offset once. Output writes places relative to the view's eye. The precision of a particle therefore does not depend on how far from the world's origin it plays, and the anchor's moves are part of the deterministic state.

## What proves it

- `test/test_golden.c` steps a scene (`test/vfx_scene.h`: endless, looping and mesh emitters over seven instances, full and churning pools, rigid particles, gases and liquids) and hashes the whole state after every step against committed values. Every CI platform runs it; a hash that differs anywhere fails. A change meant to change results updates the values and says so in the changelog. The scene runs a second time through effect files, to the same hashes.
- `test/test_twins.c` steps the same scene serially and through a task system that cuts every step into random ranges run in a shuffled order on four threads, and compares the states after every step, under ThreadSanitizer in CI.
- Each module's tests compare a step split into pieces with the step whole.

## What breaks it

Determinism is a property of the inputs as well as the library. A host breaks it by giving different inputs: another seed, another sequence of step lengths, transforms set at other steps, a host module that is not deterministic, scene answers from a physics engine that is not. The GPU part is reproducible on one device and driver, equals the CPU path in its integer outputs, and is compared with it within measured tolerances for floats ([gpu.md](gpu.md)).
