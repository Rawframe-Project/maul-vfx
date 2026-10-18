# mvfx-0001. Library profile

Status: Accepted

## Context

Every Maul library states in one record what its domain adds to the
family rulebook (family record 0005). Maul VFX simulates visual
effects: particles from emitters with spawn rules, shapes, forces,
noise, collision, events and values over their lives; ribbons, trails
and beams; and it hands a renderer the data it draws, in the layout the
renderer asks for, with sorting, bounds and draw order. The core
simulates on the CPU; an optional part runs the same effects on the GPU
through Maul RHI, and the core does not depend on it. Effects are built
in code or read from a versioned binary file. A game that replays,
rolls back or shares effects across machines needs particles placed
the same way everywhere, so the results must not depend on the
platform, the compiler, the host's threads or how the host splits its
frames.

## Decision

- **Determinism:** bit-exact on the CPU path. The same effects, seeds,
  host inputs and sequence of step lengths give the same bits on every
  platform, compiler and architecture the family builds for, whatever
  the host's thread count and however its task system splits the work,
  for every particle attribute, count, event, bound, ribbon point and
  output record. Time is an integer count of ticks of 2^-20 seconds, so
  spawn counts and birth times do not depend on how a span of time was
  split into steps. Random numbers come from a counter-based hash of the
  seed, the emitter, the particle's serial and a counter, never from a
  generator whose state follows the order of draws. No fused
  multiply-add, no contraction, no fast math; `FLT_EVAL_METHOD` is 0;
  minimum and maximum are compare and select; only the `<math.h>`
  functions IEEE 754 rounds exactly are called
  (`tools/libm-allowed.txt`), and the library writes its own sine,
  cosine, exponential and power. Each task the library runs saves,
  sets (round to nearest, subnormals kept) and restores the thread's
  floating-point control register on x86-64 and AArch64, so a host's
  flush-to-zero setting does not change results; WebAssembly has one
  floating-point environment. The GPU part is reproducible on one
  device and driver, equals the CPU path in every integer output, and
  is compared with the CPU path within a measured tolerance for floats.
- **Threads:** none. Parallel work runs on the host's task system
  through two hooks in the context's def, an enqueue and a finish
  function (both null: serial); every parallel item writes only its own
  slots, so the split and the schedule never change results. A context
  is used by one thread at a time; outputs for different views of a
  stepped context may be written from different threads at once.
- **Host code calling back in:** the allocator, the task hooks and host
  modules call none of the context's functions that take it mutable;
  only those taking it read-only (`const`) are allowed. Such a call is
  refused with `mvfx_errorState` and counted as misuse: the context
  counts its calls of host code (a step or a prewarm, whose tasks and
  host modules are the host's, and each allocation).
  The GPU part's allocators call none of its functions either: a
  simulation refuses its changing calls the same way (counting nothing,
  the part having no misuse count) while its destruction releases its
  memory, the one time its host code can reach a whole object.
- **Memory:** the owner object is the context, which takes the host's
  allocator. Creating an effect allocates its compiled form in one
  block; creating an instance allocates all of its particle, ribbon,
  event and scratch storage in one block at capacities fixed then.
  Stepping and output allocate nothing, nothing grows by itself, and
  every particle, point or event a capacity refuses is counted. Output
  functions write into the host's buffers and take scratch memory from
  the host.
- **Platform dependencies:** the C standard library's `<stddef.h>`,
  `<stdint.h>`, `<string.h>`, `<float.h>` and the exactly rounded
  functions of `<math.h>`; the floating-point control register
  intrinsics of x86-64 (`<xmmintrin.h>`) and AArch64. No operating system
  call: no clock, file or thread. The GPU part depends on Maul RHI.
- **Commit areas:** `api`, `base`, `bench`, `build`, `ci`, `collide`,
  `docs`, `effect`, `events`, `file`, `forces`, `gpu`, `math`,
  `modules`, `output`, `ribbon`, `samples`, `spawn`, `step`, `tests`,
  `tools`.

## Consequences

A host gets the same effect on every machine from the same calls, and
decides when time passes, which threads do the work and where every
byte lives. The library pays for this with its own math functions, a
clock in ticks, and a floating-point control register set and restored
around each task; the GPU part pays with a tolerance instead of
bit-equality for floats, which its tests measure.
