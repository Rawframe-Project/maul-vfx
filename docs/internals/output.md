# Output

How the library turns particles into what a renderer draws, and why each piece is built so that the same particles give the same bytes everywhere. The user's view is [output.md](../user/output.md).

## Records

[src/output.c](../../src/output.c) writes one record per live particle in a layout's fields. A field's value is taken where it is read: initial values from the particle's arrays, values over life from [src/appearance.c](../../src/appearance.c), places mapped from the emitter's storage space to the world and then made relative to the view's eye. Writing relative to the eye is what lets a `float32` record carry a particle a hundred kilometres from the world's origin at full precision.

Formats are converted by the library's own integer arithmetic ([src/layout.c](../../src/layout.c), [src/half.h](../../src/half.h)), not by casts or conversion instructions. A float-to-half instruction, an `lrintf` and a saturating cast round, clamp and treat NaN differently across compilers and CPUs, and output records are part of the deterministic result. Half floats round to nearest even; normalized integers clamp, map NaN to zero and round. An sRGB colour is encoded by comparing against the exact binary32 thresholds where each byte steps up, which `mvfxSrgbSteps` exports so the GPU part writes the same bytes.

## Sorting

A sort by place ([src/sort.c](../../src/sort.c)) turns each particle's depth or distance into a key, a binary32 made order-preserving as a `uint32`, and sorts keys with their indices by a least-significant-digit radix sort in passes of 8 bits, skipping a pass whose byte is the same in every key. Radix sorting is stable and its cost does not depend on the data, so particles at equal depth keep the pool's order and a frame never flickers between two orders; a comparison sort guarantees neither. It runs in scratch the host gives, 16 bytes a particle of the emitter's capacity. The GPU part sorts by the same keys in the same passes.

## Quads, ribbons and tubes

- **Quads** ([src/expand.c](../../src/expand.c)) are four vertices a particle in the order the records take, turned by the sprite def's billboard against the view's axes.
- **Ribbons** ([src/ribbon.c](../../src/ribbon.c)) chain an emitter's live particles by ribbon id, oldest first; the pool's birth order makes the chain order free. A segment is cut into pieces along a centripetal Catmull-Rom spline (alpha 1/2), which never forms cusps or self-intersections within a segment when points bunch up, as a uniform spline does. With a bend angle the pieces adapt to the turn. Width and colour along the ribbon are curves over its normalized length.
- **Tubes** ([src/tube.c](../../src/tube.c)) place a ring about each point turned by rotation-minimizing frames (the double reflection method), carried from point to point, so a tube does not twist about its curve as Frenet frames do at inflections.

## Bounds and draw order

[src/bounds.c](../../src/bounds.c) measures each emitter's box of live particles at the step's end and grows it by the largest size component any of them can have over its life (its stored size times the most its size over life scales it), so a renderer that culls by it never clips a particle that grows. A fixed box from the def skips the measure. [src/view.c](../../src/view.c) orders a context's emitters for a view by draw priority, then farther first, then by instance slot and emitter index, so the order is a function of the inputs and never of hash tables or pointers.

## Reads of gases and water

A gas's or a water surface's field is written into a texture's format over a box of cells with the same conversions as records ([src/gas_read.c](../../src/gas_read.c), [src/water_api.c](../../src/water_api.c)), at a row and slice pitch the host gives, so the bytes go straight to a texture upload.

## Thread safety

Output reads a stepped context and writes only the host's buffers and scratch, so a host may write different views, or different emitters, from different threads at once. Nothing an output call writes is kept: two calls with the same inputs write the same bytes.
