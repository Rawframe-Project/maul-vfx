# Changelog

All notable changes to this project are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project uses [Semantic Versioning](https://semver.org/). Before
1.0.0, any minor release may change the API and the ABI.

## [Unreleased]

### Added

- The library skeleton: the build, the family rules and tools, the
  version and result API (`mvfxGetVersion`, `mvfxResultName`) and the
  library profile.
- The context: `mvfxCreateContext` from a def stamped with the headers'
  version and carrying the host's allocator and task hooks,
  `mvfxDestroyContext` and `mvfxContext_GetMisuse`. Work handed to the
  host's tasks runs in round to nearest with subnormals kept, whatever
  floating-point mode the host's threads are in.
- `mvfxHash64`, the family's 64-bit hash, which the determinism checks
  and effect identities are built on.
- Curves and gradients as effects state them (`maul-vfx/curve.h`): keys
  with constant, linear or cubic segments, tangents given, smooth or
  monotone, and color and alpha keys.
- Effects (`maul-vfx/effect.h`): `mvfxCreateEffect` checks an effect def
  and its emitter defs (capacity, spawn rate, lifetime) and compiles them
  into one block, `mvfxDestroyEffect` and `mvfxEffect_IsValid` take its
  generation-checked id; the context's def gains a limit of effects.
- Instances and the step (`maul-vfx/instance.h`): `mvfxCreateInstance`
  plays an effect at a double-precision origin with a seed,
  `mvfxContext_Step` and `mvfxContext_StepTicks` advance every instance
  on a clock of 2^-20 s ticks, births at exact ticks and deaths from each
  particle's birth tick, so spans whose ticks sum alike reach the same
  state; `mvfxInstance_GetEmitterStats` reads live, owed and refused
  counts. An effect an instance plays refuses destruction
  (`mvfx_errorBusy`); the context's def gains a limit of instances.
- Emitter lives: a start delay, a duration and a loop count; a rate
  curve over each loop's normalized age; bursts with counts drawn from a
  range, cycles, an interval and a chance (`mvfxBurst`). Emitter states
  (`mvfxEmitterStatus` in `mvfxEmitterStats`), `mvfxInstance_IsFinished`,
  `mvfxInstance_Stop` and `mvfxInstance_Restart`.
- Changed: the rate's k-th birth is where its integral reaches k + 1, so
  a rate of 0 spawns nothing and nothing is born at the emitter's start;
  the golden state hashes change with it.
- Spawn shapes and initial velocity (`maul-vfx/shape.h`): point, line,
  circle and arc, sphere, hemisphere, box and cone, on their surface, in
  a shell or in their volume, turned and moved within the emitter; arc
  modes random, loop, ping-pong and burst spread; a direction from the
  shape's normal, its center, a cone or a fixed vector, randomized by a
  share, times a speed range. Particles now store a velocity.
- Meshes (`maul-vfx/mesh.h`): `mvfxCreateMesh` copies vertices, normals,
  triangles and tetrahedra; a mesh shape spawns at its vertices, on its
  edges by length, on its surface by area or in its volume by volume.
  `mvfxMesh_SetPositions` moves a skinned mesh between steps, and
  `mvfxInstance_SetEmitterMesh` binds an instance's own mesh. The
  context's def gains a limit of meshes.
- Value sources (`maul-vfx/value.h`): ranges, curves over the emitter's
  age, gradients at random or over age, and instance parameters
  (`mvfxInstance_SetParameter`, `mvfxInstance_GetParameter`; an effect
  declares their defaults). Optional initial values: size, color, a
  sprite's angle and spin, a mesh particle's orientation and angular
  velocity, a flipbook start frame and custom attributes, each stored
  only when set.
- Changed: an emitter's `lifetime` is a value source
  (`mvfxRangeSource(min, max)` for the former range).
- Moving instances: an instance def gains a rotation and a scale, and
  `mvfxInstance_SetTransform` moves it over the next step (or teleports
  it). Emitters simulate in world space (offsets from double anchors
  that rebase after 1,024 units) or local space, spawn by distance
  travelled (`ratePerDistance`) and inherit a share of the instance's
  velocity.
- Host spawns and prewarm: `mvfxInstance_Spawn` queues particles for an
  emitter (places, and optional velocities, colors and sizes) into a
  queue each instance reserves (`hostSpawnCapacity`), born first at the
  next step's start; `mvfxInstance_Prewarm` steps one instance in steps
  of a given length, at most a number a call, returning the span left.
- The integrator: an emitter def gains `motion` (gravity in the world's
  or the emitter's space, linear drag with an optional multiplier per
  particle, angular drag). Each step moves particles exactly under
  gravity and drag, births from their own ticks, and turns sprites and
  mesh particles by their spin and angular velocity.
- Forces: an emitter def gains up to eight `forces` (a constant, wind,
  an attractor, a vortex), each in the world's or the emitter's space,
  with an optional falloff (sphere, tube, cone) and a strength that may
  come from a parameter. Wind acts as exact drag toward its velocity.
- Turbulence: a force of curl noise (three improved gradient noises with
  analytic gradients, octaves, a scroll over time, a seed that may be
  mixed per instance), acting as an acceleration or, like wind, as a
  velocity particles are pulled toward.
- Grids: `mvfxCreateGrid`, `mvfxDestroyGrid`, `mvfxGrid_IsValid`,
  `mvfxGrid_SetValues` and `mvfxDefaultGridDef` keep host values at a
  lattice's points (binary32 or binary16), and a context gains a grid
  limit. A vector field force samples a grid of three components in a
  turned box, trilinearly, with a none, clamp or repeat border, as an
  acceleration or a velocity pulled toward.
- Velocity modules: an emitter def gains `velocityModules` (a new
  header, `maul-vfx/modules.h`): a scale of each particle's motion, an
  added velocity, an orbit exact on its circle, radial velocity and a
  speed limit whose excess decays independently of the steps, each a
  value times a curve over the particle's normalized age.
- Values over life: an emitter def gains `overLife` (colour by a
  gradient over age or speed and alpha over age, size uniform or per
  axis over age or speed, the flipbook over the life or at a rate,
  looping or held, rotation speed over life), and a custom attribute may
  be scaled over life. All but rotation are taken where a value is read
  and never written back.
- Kill rules: an emitter def gains up to eight `killRules` (through a
  sphere or a turned box, ending outside one, past a plane, slower than
  a speed after an age), each in the world's or the emitter's space; an
  attractor gains a `killRadius`; emitter stats count the `killed`.
- Host modules: an emitter def gains `hostModules`, functions over its
  particles' arrays run before motion or after it (where they may kill),
  and `mvfxModule_Random` gives a module a random stream fixed by its
  inputs.
- Collision with primitives: an emitter def gains `collision` (restitution,
  friction, life loss, kill on collision, a rest speed, a radius) and
  `colliders` (planes, spheres, turned boxes, inside or outside, in the
  world's or the emitter's space), `mvfxInstance_SetColliders` sets an
  instance's own, a kill rule kills at a collision count, and host
  modules see the count.
- Collision with grids: colliders gain heightfields and distance grids (a
  `grid` of one component spanning a turned box), sampled along each
  step every half cell; effects and instances hold their grids.
- Collision with the host's scene: a collision gains `sceneQueries`,
  `queryCapacity` and `lookahead`; each step writes a query a moving
  particle (`mvfxInstance_GetSceneQueries`), the host answers with
  contact planes (`mvfxInstance_AnswerSceneQueries`), and emitter stats
  count the `queriesDropped`.
- Fixed: a particle born in the slot of an ended one took its count of
  collisions.
- Collision with a depth image: `mvfxInstance_SetDepthImage` borrows a
  view's depth (binary32, unorm16 or unorm24, any projection, optional
  normals), and a collision gains `depthImage`, `depthThickness`,
  `depthSamples` and `killOutsideView`.
- Events: an emitter def gains `events` (births, deaths, collisions)
  and `eventCapacity`; `mvfxInstance_GetEvents` reads a step's events,
  each with its tick, place, velocity, normal, colour and size, and
  emitter stats count the `eventsDropped`.
- Sub-emitters: an emitter def gains `eventSpawn`, spawning a drawn
  count of particles per event of a kind another emitter writes, at its
  tick and place, inheriting a share of its velocity and optionally its
  colour and size, up to its own `capacity` a step; an effect's
  emitters step in an order their links make, and a cycle is refused.
- Output: `include/maul-vfx/output.h` with layouts (`mvfxCreateLayout`,
  `mvfxDestroyLayout`, `mvfxLayout_IsValid`, `mvfxDefaultLayoutDef`) of
  particle fields in binary32, binary16, normalized, sRGB and uint32
  formats, and `mvfxInstance_WriteParticles` writing an emitter's
  records for a view; the context gains `limits.layouts`.
- Bounds and draw order: each emitter's box of its particles measured
  every step (or `fixedBounds` from its def), read in the world with
  `mvfxInstance_GetBounds`, and `mvfxContext_OrderEmitters` listing a
  view's emitters by `drawPriority`, distance, instance and emitter,
  skipping those outside an optional frustum.
- Mesh indices and lights: an emitter's `meshIndex` (one or any of a
  range, chosen at birth) written by the record field `mvfx_recordMesh`,
  and its `light` making every n-th particle by serial carry a light
  that `mvfxInstance_WriteLights` writes as `mvfxLight` records.
- Sorting: an emitter's `sortMode` and `sortMeasure` order its records
  for a view (youngest first, or back to front or front to back by
  distance or depth) through a stable radix sort on scratch the host
  passes, sized by `mvfxInstance_GetSortScratchBytes`. Outputs take an
  `mvfxView` (origin, eye, forward) in place of a view origin.
- Ribbons: an emitter's `ribbon` (ids: one ribbon, one per batch, one
  per event's particle; width and colour over length) and
  `mvfxInstance_WriteRibbons`, writing each chain's points in a layout
  with the ribbon fields (tangent, width, length, coordinate along,
  ribbon) and the ribbon list.
- Ribbon shape: `ribbon.pieces` and `bendAngle` tessellate along a
  centripetal Catmull-Rom spline; `ribbon.texture` (stretch, per
  segment, by a distance stored at birth) with `tileLength` and
  `scroll`; `ribbon.facing` (camera or a normal) written by the record
  fields `mvfx_recordSide` and `mvfx_recordFaceNormal`.
- Trails: a ribbon counter per emitter that `mvfxInstance_BreakRibbons`,
  a teleport and `ribbon.breakDistance` advance, so trails end and
  begin; `ribbon.pinHead` pins the current ribbon's head to its source.
- Beams: an emitter's `beam` strikes `segments + 1` points along a line
  or a quadratic or cubic Bezier curve between points fixed or from
  instance parameters, with noise across it faded at both ends, struck
  again when a strike's lifetime ends; drawn as a ribbon.
- Expansion: an emitter's `sprite` (billboards: camera, rotated, Y axis,
  fixed, directional with velocity stretch) and
  `mvfxInstance_ExpandSprites`, writing four vertices and six indices a
  particle with the corner field `mvfx_recordCorner`; `mvfxView` gains
  `up`.
- Ribbon strips: `mvfxInstance_ExpandRibbons`, two vertices a ribbon
  point across its width and six indices a segment.
- Quality and budgets: an effect's `qualityLevels` (capacity and rate
  scales, emitters left out), `mvfxInstanceDef.quality`, the context's
  `limits.particles` budget choosing the first level that fits,
  `mvfxInstance_GetQuality` and `mvfxContext_GetParticles`; particle
  arrays sized per instance.
- Off-screen policies: `mvfxInstance_SetVisible`, and per instance
  simulate, throttle (every n-th unseen step by the summed length, in
  the seed's phase) or pause (`mvfxInstanceDef.offscreen`,
  `mvfxInstance_SetOffscreen`).
- The neighbor grid: an emitter's `neighbors` (its own particles or
  another emitter's, a radius, a cap counted in `neighborsCapped`),
  hashed cells and a stable counting sort; each particle's neighbor
  count and offsets to their mean place and velocity, for host modules
  and the records `mvfx_recordNeighborCount`, `mvfx_recordNeighborCenter`
  and `mvfx_recordNeighborVelocity`.
- Flocking and steering: an emitter's `steering` (separation, alignment
  and cohesion from its neighbors within a view cone; seeking with
  arrival, fleeing within a radius, following an open or closed path;
  the sum held to a maximum force).
- Particle contacts: an emitter's `contacts` (radii from sizes,
  position-based Jacobi passes averaged over each particle's contacts,
  then impulses with restitution and friction; capped contacts counted
  in `contactsCapped`).
- Forces between particles: an emitter's `pairForce` (each neighbor
  within a radius pulls or pushes at a strength curved over their
  distance).
- Plexus links: an emitter's `plexus` (each pair of its particles within
  a distance once, caps per particle and per emitter counted in
  `linksDropped`, a width and a fade by length), written as two-point
  ribbons by `mvfxInstance_WritePlexus` and as strips by
  `mvfxInstance_ExpandPlexus`.
- Chains by XPBD: an emitter's `unit` (each birth a chain of points
  along a direction at a spacing, counted in units), solved in substeps
  with stretch and bending compliance, damping along each constraint
  and an optional pinned first point; each chain a ribbon.
- Cloth sheets by XPBD: `mvfx_unitSheet` units of points across by rows
  on two directions, with stretch, shear and bending compliance and an
  optional pinned first row; `mvfxInstance_ExpandSheets` writes each
  sheet as a mesh (normals, tangents and texture coordinates from the
  grid, two counterclockwise triangles a quad).
- Units' attachments: pinned points held to the instance, to frames the
  host moves (`mvfxInstance_SetAttachFrames`, `attachFrames` in a spawn
  request) or to the particle that spawned the unit, eased over the
  substeps, let go when that particle dies.
- Fixed: a stray pin on a particle emitter's unit no longer zeroes its
  particles' velocities at birth.
- Tearing: a unit's stretch constraints tear past `tearStrain`, taking
  the bending and shear across them; sheets' meshes drop torn quads,
  torn chains split into ribbons, `mvfx_eventTear` events and
  `mvfxEmitterStats.torn` report each tear.
- Sheets' aerodynamics: `normalDrag` and `tangentDrag` push each whole
  triangle against the air the emitter's winds and velocity fields move,
  so tilted sheets take lift.
- Self-collision: `selfThickness` keeps a unit emitter's points apart
  (within a unit, those apart at rest), with `selfFriction`, candidates
  found once a step and parted each substep, capped by `selfContacts`.
- Tubes: `mvfxInstance_ExpandTubes` draws an emitter's ribbons (chains,
  trails, beams) as tubes of rings turned by rotation-minimizing frames,
  with optional caps.
- Rigid particles: `mvfxRigidDef` makes an emitter's particles rigid
  spheres, capsules or boxes by XPBD rigid bodies in substeps, with the
  gyroscopic term, meeting planes and spheres at their surfaces with
  restitution and patch friction (static and dynamic), counted and
  evented by strikes; `mvfxEmitterStats.rigidContactsCapped`.
- Fix: a kill in any of a unit emitter's substeps now holds (only the
  last substep's counted before).
- Rigid particles meet box colliders, outside (face, edge and corner
  contacts) and inside, the emitter's and the host's; restitution of
  contacts struck at once is shared, and depth from a moving collider is
  pushed out without becoming velocity.
- Rigid particles meet heightfields and distance grids (sampled), the
  host's scene answers and the depth image, killed outside its view
  with `killOutsideView`.
- Rigid particles meet each other: `bodyContacts` (up to
  `MVFX_MAX_BODY_CONTACTS`) pools an emitter's contacts between its
  bodies, found by the neighbor grid, solved with both bodies' masses;
  past the pool counted in `rigidContactsCapped`.
- Rigid friction holds: friction resists a patch's turning about its
  normal (a spun box stops), its static test is of the bodies' slide by
  their masses and the whole load, dynamic friction is taken by velocity
  from the net normal impulse, and friction acts level with a body's
  center while its contacts can balance its tipping; contacts are kept
  within a small margin, solved in alternating order, and box faces
  clipped edge to edge no longer meet at the same corner twice. A box
  held on a gentle slope no longer creeps, and stacks rest.
- Rigid hulls: `mvfx_rigidHull` makes rigid particles the convex hull
  (Quickhull, at most `MVFX_MAX_HULL_VERTICES`) of a mesh's vertices
  (`mvfxRigidDef.hull`), scaled by size along the mesh's axes, their mass
  and full inertia from the hull's volume integrals, placed at its
  centroid (`mvfxMesh_HullCenter`); they meet planes, grids, the insides
  of spheres and boxes, scene answers and the depth image.
- Rigid hulls meet the outsides of spheres and boxes, the host's
  colliders and each other (`bodyContacts`): nearest points by GJK,
  overlaps by EPA, touching faces clipped into contacts.
- Rigid particles sleep (`mvfxRigidDef.sleepSpeed`, `sleepTime`; 0.05
  and 0.5 in `mvfxDefaultEmitterDef`): bodies joined by contacts sleep
  together once all have been still, keep their pose and meet nothing
  while they still age, and wake when an awake body meets them, when
  their forces change, when the emitter's colliders or depth image
  change, or by `mvfxInstance_WakeBodies`; `mvfxEmitterStats.asleep`
  counts them.
