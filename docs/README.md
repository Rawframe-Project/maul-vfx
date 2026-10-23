# Maul VFX documentation

## Using the library

- [Getting started](user/getting-started.md): building, linking, the rules every call follows, a first effect, the samples.
- [Effects](user/effects.md): emitters, spawning, shapes, values, motion and forces, values over life, parameters, quality levels, host modules.
- [Effect files](user/effect-files.md): writing and reading effects as bytes, assets, the JSON form.
- [Instances](user/instances.md): placing, moving, seeds, stopping, off screen, inputs between steps.
- [Output](user/output.md): layouts, order, quads, lights, bounds and draw order.
- [Ribbons, trails and beams](user/ribbons.md): trails, tubes, beams and plexus links.
- [Events](user/events.md): events, sub-emitters and the program's own births.
- [Interaction](user/interaction.md): neighbors, flocking, contacts, ropes and cloth, rigid bodies.
- [Fluids](user/fluids.md): smoke and fire, liquids, surfaces, water.
- [Collision](user/collision.md): colliders, the program's scene, the depth image.
- [The GPU part](user/gpu.md): running effects on a device through Maul RHI.
- [Troubleshooting](user/troubleshooting.md): results, missing particles, results that differ, slow steps.

The [documentation site](https://rawframe-project.github.io/maul-vfx/) adds the API reference, generated from the headers.

## How it works

- [Overview](internals/overview.md) and [glossary](internals/glossary.md).
- [Determinism](internals/determinism.md), [simulation](internals/simulation.md), [memory](internals/memory.md).
- [The effect file](internals/effect-file.md), [output](internals/output.md).
- [Interaction](internals/interaction.md), [fluids](internals/fluids.md), [collision](internals/collision.md).
- [The GPU part](internals/gpu.md).
- [Maul conventions](internals/maul-conventions.md): the rules every Maul library follows.

## Working on it

- [Development](operations/development.md): toolchains, building, the GPU part's tests, generated files.
- [Checks](operations/checks.md): the check tiers, the rules, labels and manual tests.
- [Kernel containers](operations/kernels.md): rebuilding the GPU part's kernels.
- [Debugging](operations/debugging.md): golden hashes, sanitizers, the GPU part, fuzzing.
- [Releasing](operations/release.md).
