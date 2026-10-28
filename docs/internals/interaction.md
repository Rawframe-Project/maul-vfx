# Interaction

How particles find and meet each other, and how units and rigid bodies are solved, with the orderings that keep every result independent of threads and step splits. The user's view is [interaction.md](../user/interaction.md).

## The neighbor grid

[src/neighbor.c](../../src/neighbor.c) builds a grid once a step from the particles alive at the step's start: each particle's cell (of the largest radius that queries the grid) is hashed into a table, and a stable counting sort orders the particles by table slot, keeping the pool's order within a slot. A query visits the 27 cells around a place in a fixed order and their particles in the grid's order. A hashed table bounds memory by the particle count rather than the space the particles cover, which an effect cannot know ahead.

Because the build and the visit orders are fixed, everything summed over neighbors (steering, pair forces, contacts, liquids, plexus links) sums in the same order everywhere. Contacts and rigid bodies build their grids again from the places after motion or the predicted poses, by the same rules. A query that reaches its `maxNeighbors` stops at the same neighbor on every platform, and the stop is counted.

The grid is built from the step's start, so the births of a step meet the others from the next step. An emitter may read another emitter's grid in the same space; the effect orders their steps so the source is built first, and refuses a cycle.

## Steering, pair forces and contacts

[src/steer.c](../../src/steer.c) turns neighbors, points and a path into an acceleration held to a most force, applied over the step like any force. Pair forces add equal and opposite accelerations by a curve of the distance. [src/contact.c](../../src/contact.c) follows Macklin et al. 2014: a grid of the places after motion, then passes in which each particle reads the last pass's places and moves by half of each overlap averaged over its contacts (Jacobi, not Gauss-Seidel, so the result does not depend on which particle goes first), then one pass of impulses for restitution and friction on the pairs still closing. A projection parts a pair to the middle of a small slop past the touching distance, so rounding alone never decides whether it touches next step.

## Units by XPBD

Chains and sheets ([src/unit.c](../../src/unit.c)) are solved by XPBD (Macklin, Muller and Chentanez 2016) in substeps: each substep predicts every point under gravity, drag and forces, projects the distance, shear and bending constraints with their compliances, and takes velocities from the change. Small substeps with one projection each converge better than one step with many iterations (Macklin et al. 2019), and compliance gives a stiffness that does not change with the substep count, so an author's numbers mean the same at any frame rate.

- **Pins** ([src/attach.c](../../src/attach.c)) hold pinned points to what the unit is attached to. The attachment's frame is eased over the step's substeps, so a fast-moving hand drags a rope rather than snapping it.
- **Tearing** checks each stretch constraint after a substep's projections and removes it for good, with the bending and shear it carried.
- **Aerodynamics** ([src/aero.c](../../src/aero.c)) pushes each whole triangle of a sheet by drag along and across its normal against the air the emitter's winds and fields make; a tilted triangle takes lift from it. A triangle takes at most a share of its relative speed a substep, which keeps light cloth stable in strong wind.
- **Self-collision** ([src/self.c](../../src/self.c)) finds each point's candidates once a step, within the thickness plus the farthest a point can travel, then parts close pairs in every substep. Finding candidates once keeps the cost of the grid out of the substeps.

## Rigid bodies by XPBD

Rigid particles ([src/rigid.c](../../src/rigid.c)) follow Muller et al. 2020: each substep a body's pose is predicted, its contacts are found at the predicted pose, positional impulses push it out, and velocities are taken from the pose's change, then restitution and friction act on them. Friction is solved per patch, the contacts with one collider or one face together, so a box on a plane holds still instead of creeping corner by corner.

Within a substep a body turns by its free flow ([src/rigid_flow.c](../../src/rigid_flow.c)), the method of maul3d's [free rotation](https://github.com/Rawframe-Project/maul3d/blob/main/docs/internals/rotation.md): a Casimir splitting of its kinetic energy (McLachlan 1993; Fasso 2003). In its principal frame (cyclic Jacobi rotations, its moments largest first), it takes the least moment's residual turn for half the substep, the largest's for all of it and the least's again, in as many equal pieces as its spin needs (counted by comparing cubes, not by a cube root), then a turn about its momentum by `h |L| / I_mid`. Each part is an exact turn, so a body keeps its angular momentum to rounding and its energy within a bounded error, and a symmetric top turns exactly. Angular drag slows the spin before the flow. The flow replaces both the gyroscopic term and the turn of the orientation by the spin, so a spinning body keeps its momentum where the two, split, lost it.

Where it differs from maul3d's page:

- The CPU path turns in binary64, from mvfxSinCos's binary32 half turns made unit pairs, so it needs neither of the page's length corrections. The GPU path follows the page in binary32, operation by operation, with its own sine and cosine for the page's `cossin` (WGSL has no `remainderf`).
- A body touched by a contact takes its angular velocity from its pose's change after the solve, as XPBD does. maul3d never re-derives it.
- A body's size can change over its life, and its principal frame with it, so the frame is taken each substep rather than once per mass update.
- A body of one moment about every axis, or of no volume, turns about its spin by the integrator's exact turn.

Contacts are found by shape: spheres and capsules directly; boxes against boxes by separating axes (Gottschalk, Lin and Manocha 1996) and face clipping; hulls (Quickhull of a mesh's vertices, [src/hull.c](../../src/hull.c)) by GJK for the nearest points and EPA for the depth ([src/convex.c](../../src/convex.c)). GJK and EPA stop after a fixed number of steps, and a stop is counted; a capped pair is kept touching with its depth at most the true one.

Bodies meeting each other are found by the neighbor grid at the predicted poses and solved in coloured runs ([src/rigid_colour.c](../../src/rigid_colour.c)): no two runs of a colour share a body, so a colour's runs give the same result in any order. The CPU path solves them in order; the GPU path solves a colour's runs at once.

Sleeping ([src/rigid_sleep.c](../../src/rigid_sleep.c)) groups bodies into islands by their contacts; an island sleeps when all of its bodies have been still for the sleep time, and wakes as a whole. A sleeping body costs nothing but its age, which is what lets a pile of debris rest for minutes.

## Known limits

The headers state each solver's measured limits next to the parameter they come from: how far a body under a much heavier one sinks into its support, how a box held near its tipping point creeps until it sleeps, how many passes a liquid of many substeps adds on the GPU. They are kept there, by the field they concern, rather than here, so they change with the code.
