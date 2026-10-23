# Collision

How particles meet colliders, the host's scene and the depth image, and why each test follows the whole step. The user's view is [collision.md](../user/collision.md).

## Along the step, not at its end

A particle's step is a segment from where it was to where the integrator put it ([src/collide.c](../../src/collide.c)). Collision finds the segment's first contact with any collider, moves the particle there, bounces its velocity by restitution and friction, and reflects the rest of the segment likewise, up to four contacts a step. Testing only the end place would let a fast particle, or any particle in a long step, pass through a thin wall; testing the segment makes the result independent of the step length, which determinism needs anyway.

Colliders are compiled when the effect is made and resolved each step into the space the emitter's particles are stored in, with the instance's transform at the step's start. A particle's radius grows every collider by it (a box's corners stay square, a conservative and cheap growth), so particles rest on a surface rather than in it. A particle slower than the rest speed after a contact stops where it hit, which ends the endless tiny bounces a restitution near 1 would make.

A run of particles is first tested against the colliders its bounds can reach; a particle with no contact there has none at all, and one with a contact is tested again against all of them, so the shortcut never changes a contact.

## Grids

Heightfields and signed distance grids sample one of the context's grids ([src/grid.c](../../src/grid.c)) trilinearly over the collider's box. A contact along a segment is found by marching the segment against the sampled surface and bisecting the crossing, a bounded number of times, so its cost per particle is fixed. The host may give a grid new values between steps; colliders read them from the next step.

## The host's scene

The library has no view of the host's world, and asking the host during a step would put host code, possibly a physics engine's locks, inside the step's tasks. Scene queries ([src/scene_query.c](../../src/scene_query.c)) turn that around: at the end of an emitter's step each moving particle writes a query, its place and the segment it will cover in the lookahead, and the host answers each with a plane between steps. The next step collides the particle's segment with its plane, as with a plane collider. A particle keeps its last plane until an answer replaces it, so a host may answer a share of the queries each frame and the particles still meet something. A step's answers are inputs like any other: the same answers give the same particles.

## The depth image

[src/depth.c](../../src/depth.c) collides particles with the camera's depth buffer, as screen-space collision on a GPU does, on the CPU path and the GPU path alike. The image is checked once when the host sets it and its projection inverted. A particle's step is projected into the image and sampled at its end and at up to a number of points along it, one a pixel it crosses, nearest pixel each; a particle behind the surface by less than the thickness has met it, at a plane from the pixel's normal (given by the host, or made from the depth). The thickness is what tells a surface the particle passed in front of from one it went behind. Outside the image there is no depth, so particles there either pass freely or die, as the host chooses.

## Kill rules

Kill rules ([src/kill.c](../../src/kill.c)) test the same segment against their shapes, so a particle crossing a kill plane within a step dies even if it ends the step on the other side. A killed particle is taken out at the end of the step, after the host modules, so modules after motion still see it.

## Rigid particles

Rigid bodies do not use this path: they meet colliders at their shapes' surfaces in their own substeps ([interaction.md](interaction.md#rigid-bodies-by-xpbd)), against the same compiled colliders, scene planes and depth image.
