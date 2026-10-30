# Glossary

The words this library uses in its headers, code and documentation, with the meaning each has here.

| Word | Meaning |
|---|---|
| **anchor** | The point a world-space emitter's particle places are offsets from, near its instance; moved to the instance when it has gone far. |
| **attribute plan** | The list of particle attributes an emitter stores, made when its effect is compiled from the values its def uses. |
| **birth** | A particle coming into being at a tick, with a serial. A birth that finds the emitter full is refused and counted. |
| **budget** | The context's cap on the sum of its instances' emitter capacities (`mvfxContextLimits.particles`). |
| **capacity** | The most of something an object holds at once, fixed when it is made: particles, events, scene queries, births from events. |
| **context** | The owner object: the host's allocator and task hooks, and the tables of everything made through it. |
| **def** | The struct a creation takes, started from its default function, which sets its cookie. |
| **effect** | A compiled def: emitters, gases, water surfaces, parameters, quality levels. Never changes after creation. |
| **emitter** | The part of an effect that spawns particles and decides how they live. |
| **emitter view** | An effect's compiled emitter as a simulation elsewhere reads it (`mvfxEffect_GetEmitterView`). |
| **event** | A record of what a particle did in a step: born, died, collided, tore, crossed water. |
| **gas** | A box of cells holding a velocity and scalar fields, stepped with its instance: smoke and fire, a liquid's grid, or a surface. |
| **host** | The program using the library. |
| **host module** | A function of the host's run over an emitter's particles at a stage of the step. |
| **instance** | One playing of an effect, at a place, with a seed. |
| **layout** | The host's description of a record: fields, formats, offsets, stride. |
| **misuse** | A call refused for an argument a correct program never passes, against a live context, counted on it. Bad untrusted data (an effect file's contents, a mesh's vertices) is refused with its status and counts none. |
| **parameter** | Four floats per instance that value sources may read at births, and a force's strength at each step. |
| **plan** | What a planned instance's step writes for a simulation elsewhere: births owed with serials and ticks, the resolved forces and colliders. |
| **planned instance** | An instance that holds no particles and writes plans each step, for the GPU part or the host's own GPU path. |
| **pool** | An emitter's live particles in their arrays, in birth order. |
| **quality level** | A cheaper version of an effect (capacities and rates scaled, emitters left out) an instance may play at. |
| **record** | One particle (or vertex) written in a layout. |
| **ribbon** | A chain of an emitter's particles drawn as a strip or a tube. |
| **scene query** | A particle's question to the host at a step's end: where it is and where it is going, answered with a plane. |
| **serial** | A particle's number within its emitter, counted in birth order; with the seed, it fixes every random draw for the particle. |
| **simulation** | The GPU part's object holding a planned instance's particles, gases and water surfaces on a device (`mvfxGpuSim`). |
| **space** | Where an emitter's particles are stored: the world's, or the instance's (local). |
| **statistics record** | The words a simulation leaves on the device for each emitter after a step: live count, bounds, capacity counts. |
| **step** | Advancing the context's instances by a number of ticks. |
| **substep** | A part of a step a solver takes alone: units, rigid bodies, gases, water, and liquid splits of an instance's step. |
| **tick** | The unit of time, 2^-20 seconds (`MVFX_TICKS_PER_SECOND`). |
| **unit** | A chain or a sheet of particles held by constraints, spawned together as one birth. |
| **water surface** | A grid of heights across an instance's frame: waves, or shallow water over terrain. |
