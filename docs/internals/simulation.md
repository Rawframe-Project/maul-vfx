# Simulation

How a context's step advances its instances, how an emitter's step stays exact whatever its length, and where each kind of particle moves. Determinism's reasons are in [determinism.md](determinism.md); memory's in [memory.md](memory.md).

## The context's step

`mvfxContext_StepTicks` ([src/step.c](../../src/step.c)) hands the context's instance slots to the task hooks, one instance an item. Each instance, by its off-screen policy, steps the ticks it is given, holds them (a throttled instance steps every n-th time with what it held), or lets them pass (a paused one). An instance with a liquid gas may split its ticks into equal substeps, each a whole step of the instance, when the liquid's fastest particle asks for them; the count is read from what the last step left, so it is the same on every path.

Once every instance's task is done, the gases and water surfaces of every instance step, in slot order, each spreading its cells over the task hooks. They step after the emitters because particles feed them (splats, liquid velocities, pours, pushes) and read them a step behind (a gas force reads the velocity the last step left). That lag is part of the model on both paths.

## An instance's step

An instance's step moves its clock and its transform: the instance travels in a line from where it was to where it is set to be, and its velocity is that travel over the step. Its emitters step in a fixed order (`stepOrder`): an emitter that spawns from another's events, or reads another's neighbors, steps after it. A finished instance (stopped, with nothing alive) skips its emitters entirely.

## An emitter's step

[src/emitter_step.c](../../src/emitter_step.c) reaches the state that stepping one tick at a time would reach, each tick's deaths before its births, without walking the ticks:

1. **Plan the step's births.** The spawn schedule ([src/spawn.c](../../src/spawn.c)) lists every birth the step owes, in tick order: the rate's (where the integral of the rate since the emitter began reaches the next whole count, in binary64, so a fraction carries across steps and loops), the bursts' (at their times plus intervals, within each loop), the distance births (along the instance's line where the distance travelled times the distance rate reaches the next count), the program's queued births, and births from the step's events of the source emitters.
2. **Check whether they fit.** Counting the deaths the step brings and the births it owes against the capacity decides the path. When everything fits, the order of deaths and births within the step cannot matter, and the fast path runs: remove the particles whose life ended, find neighbors and steer, run the host modules before motion, move the live particles over the whole step, then append the births, each moved from its own tick to the step's end.
3. **Otherwise replay in tick order.** When the emitter may fill within the step, a min-heap of death ticks ([src/death_heap.c](../../src/death_heap.c)) replays the step: at each birth's tick the deaths up to it free their slots first, and a birth that finds no room is refused and counted. This is the only way the refused count, and which births survive, can be the same whatever the step length.
4. **After motion:** new units are attached, contacts are solved, host modules after motion run, killed particles are removed, beams are struck and placed, plexus links are found, scene queries are written, and the bounds are measured.

Removal keeps the survivors in their order (a stable compaction), so the pool's order is birth order, which output, ribbons and the GPU path all rely on.

## Moving particles

A plain particle moves exactly under constant acceleration and linear drag over any span: `v1 = v0 e + a h phi1`, `x1 = x0 + v0 h phi1 + a h^2 phi2` with `e = exp(-k h)` and the phi functions of exponential integrators (Hochbruck and Ostermann 2010), each a series near `k h = 0` ([src/integrate.h](../../src/integrate.h)). Forces that vary with place (attractors, vortices, turbulence, fields, gases) are evaluated at the step's start and held over it, so a long step is an approximation for them but not for gravity and drag. Velocity modules and kill rules act after; collision follows the step's segment.

Other kinds of particles move in their own solvers, called from the same place in the step:

- **Units** (chains and sheets) by XPBD in substeps ([interaction.md](interaction.md)).
- **Rigid particles** by XPBD rigid bodies in substeps.
- **Liquid particles** by position-based fluids, or advected by a liquid gas's velocity ([fluids.md](fluids.md)).

## Values a particle carries

An emitter stores only the attributes its def uses: its attribute plan, made when the effect is compiled, lists them, and each is an array of its own ([memory.md](memory.md)). Initial values are drawn at birth from pcg4d at the particle's serial ([src/birth.c](../../src/birth.c)). Values over life (colour, size, frame, rotation speed) are never stored: [src/appearance.c](../../src/appearance.c) computes them where they are read, from the initial values, the age and the speed. The step stays cheaper for it, and output, events and ribbons agree on what a particle shows.

## Prewarm, restart and planned instances

`mvfxInstance_Prewarm` steps one instance alone, outside the context's step, a bounded number of steps a call; it is the same steps as the context's, so a prewarmed instance is in the state those steps reach. A restart returns the clock, the counters and the cursors to 0 and keeps the seed. A planned instance runs steps 1 and 2 above and stops there: it writes the births it owes, with their serials and ticks, into plans for a simulation elsewhere, which decides room itself ([gpu.md](gpu.md)).
