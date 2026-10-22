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
  `mvfxDestroyContext` and `mvfxGetContextMisuse`. Work handed to the
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
  and never written back. A flipbook's frame is the floor of an exact
  integer product (the age's ticks and the cycles or the rate, as the
  binary32 values they are, times the frames), the same on every path.
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
- `mvfxMesh_HullVertexCount` counts a mesh's hull's vertices past the
  64 a rigid hull takes, for an editor to say how much to simplify.
- Gases (`maul-vfx/gas.h`): smoke and fire on a grid in a box, by
  Stam's stable fluids. `mvfxEffectDef` gains `gases` (at most
  `MVFX_MAX_GASES`), and an effect may hold gases and no emitters. A gas
  is a box of cells in 2D or 3D, local to its instance or left in the
  world, with velocities at the cells' faces and density, temperature
  and fuel at their centers; spheres and boxes pour into it and may set
  its velocity, it carries itself semi-Lagrangian (traced back by the
  midpoint rule), and each field may decay. `mvfxInstance_GetGasInfo`
  and `mvfxInstance_ReadGas` read it; an instance's bounds hold its
  gases' boxes, and it finishes once stopped and its gases are empty.
- A gas keeps from compressing: each substep after it is carried, a
  pressure projection by geometric multigrid (`pressureCycles` V cycles,
  4 by default, of weighted Jacobi) removes its velocity's divergence.
  Its box's walls (`closedWalls`, `mvfxGasWalls`) are each closed, the gas
  sliding along it, or open, the gas flowing through it.
- A gas has forces, combustion and a sharper advection: buoyancy
  (`gravity` in `gravitySpace`, `densityWeight`, `heatLift`; temperature
  is the excess over the surroundings), vorticity confinement
  (`vorticity`), fuel burning where hot enough (`ignitionTemperature`,
  `burnRate`, `burnHeat`, `burnSmoke`, `burnExpansion`, whose expansion
  the projection keeps), and MacCormack's advection (`advection`,
  `mvfxGasAdvection`). A work buffer is added to the instance's block
  only for the features that use one.
- A gas flows around obstacles: `obstacles` (colliders of any kind, in
  the world or the instance's frame, their grids held by the effect) and
  `hostObstacles` (the instance's colliders). Cells within them are solid
  and hold nothing, and the faces beside them take the obstacle's
  velocity from its motion since the step before, so a moving door
  pushes smoke. The pressure solve's weights are now stored per face.
- Forces act on a gas: `forces` takes an emitter's force kinds
  (constant, wind, attractor, vortex, turbulence, vector field) in the
  world or the instance's frame, taken at each face, a wind's or a
  velocity field's pull exact over a substep.
- Particles and gases couple: the force kind `mvfx_forceGas` makes an
  emitter's particles follow a gas of the effect (`gas`), as a velocity
  field or an acceleration; an emitter's `gasSplat` (`mvfxGasSplat`) pours
  its live particles' density, temperature and fuel into a gas and pulls
  its faces toward their velocity, in the particles' order.
- A gas steps several times faster, bit for bit the same: 4.8 to 5.9
  times for a 2D gas of 256 by 256, 3.2 times for a 3D one of 64 cubed.
  `bench/bench_main.c` times gases.
- A gas reads out in formats and at points: `mvfxInstance_ReadGasAs`
  (`mvfxGasRead`) writes a field over a box of cells as float32,
  float16, or unorm and snorm of 8 and 16 bits over a range, at the
  host's row and layer pitch; `mvfxInstance_SampleGas` samples a field
  at points in the world.
- A gas's step splits over the host's tasks, with the same bits however
  it is split: the context steps gases once every instance's own step is
  done, and each pass of the carry and the solve goes by rows (3 times
  faster on 4 threads for a 2D gas of 256 by 256).
- Liquids: an emitter's particles become a liquid with
  `mvfxEmitterDef.liquid` (`mvfxLiquidDef`), by position-based fluids in
  substeps, 3D or planar, sliding along the colliders, with XSPH
  viscosity, vorticity confinement and an optional tensile correction.
- Grid liquids: a liquid gas (`mvfxGasDef.kind`, `mvfx_gasLiquid`) moves
  the particles of the emitter whose liquid names it
  (`mvfxLiquidDef.solver`, `mvfx_liquidGrid`) by APIC, with air cells
  at pressure 0 and each particle's affine velocity as attributes; its
  density reads as the share of each cell the liquid fills.
- A grid liquid's over-full cells grow back to full over 1/60 s, or
  over the step if longer, so a liquid spawned packed spreads calmly
  instead of flying apart after its spawning tick.
- A liquid gas's obstacles hold its liquid: their cells are solid, where
  before they were taken for air and the liquid drained toward them.
- Surfaces: a surface gas (`mvfx_gasSurface`, `mvfxGasDef.surface`)
  holds the signed distance to one emitter's particles' surface
  (`mvfx_gasDistance`), Zhu and Bridson's field with Solenthaler et
  al.'s correction for concavities and gaps, made anew each step and
  smoothed; read and sampled as any gas's field, its reach outside the
  box.
- A surface whose reach is far below its cells (a tiny radius) takes
  buckets of one cell instead of dividing by zero, and holds its reach.
- Surface meshes: `mvfxInstance_WriteSurfaceMesh` writes a surface
  gas's mesh at a level (triangles by marching cubes, segments by
  marching squares in 2D) into the host's position, normal and index
  buffers, with true counts, scratch from
  `mvfxInstance_GetSurfaceMeshScratchBytes`; ambiguous faces by the
  asymptotic decider so the mesh has no cracks, its table generated by
  `tools/gen_marching_cubes.py`.
- Water surfaces (`maul-vfx/water.h`, `mvfxEffectDef.waters`): heights
  over a grid along an instance's Y, in its frame or where it began,
  carrying waves by the wave equation at the def's speed with damping,
  each wall closed (reflecting) or open (Mur's absorbing edge), in
  substeps at most 0.9 of the stable bound. The host pushes them
  (`mvfxInstance_PushWater`, the displaced water lifting the level),
  reads their heights, velocities and normals into its own formats
  (`mvfxInstance_ReadWater`), samples them at points in the world
  (`mvfxInstance_SampleWater`) and reads their place
  (`mvfxInstance_GetWaterInfo`); an instance's bounds hold them, and it
  runs until they are still.
- A gas's or water surface's sample of more than `INT64_MAX / 3` points
  is refused; the count's three coordinates overflowed before.
- A surface's largest eigenvalue is found by cyclic Jacobi rotations,
  within 1e-6 where eigenvalues repeat; Newton's method on the
  characteristic cubic was off by up to 7% at a double largest one.
- Changed: a surface's default upper threshold is 2.0 (was 2.5), which
  keeps disks two spacings apart from joining in 2D as balls in 3D; a
  flat face sits about a tenth of a spacing inside.
- A surface mesh's samples exactly at the level are outside, and the
  edges from one take its own vertex, so ties leave no zero-length edge
  (primitives on such vertices alone are left out); the scratch takes
  32 bytes a cell of a layer in 3D and 24 a cell of a row in 2D.
- A liquid gas's fill time is a def field, `mvfxGasDef.fillTime` (1/60
  s by default), and its instance steps in equal substeps when its
  fastest particle, with Bridson's term for gravity, would cross more
  than `mvfxGasDef.courant` cells (1 by default) in a step, at most
  `MVFX_MAX_LIQUID_SUBSTEPS`: a dam break stepped at 30 a second keeps
  its volume within 4% (it lost 8% in one step a frame).
- Shallow water (`mvfxWaterDef.model`, `mvfx_waterShallow`): depths over
  a ground (a heightfield collider of the effect's or the host's, met by
  the vertical however it is turned, or a flat floor) and velocities on the grid's faces, carried by Stelling and
  Duinmeijer's momentum-conserving upwind form, accelerated by the
  surface's fall and moving water by upwind fluxes so the volume is
  exact in a closed basin and no depth falls below 0; a lake at rest
  stays at rest over any ground; open walls drain; sources (disks and
  boxes) pour while the instance plays; pushes move water from their
  middle to their ring; `fillLevel`, `gravity` and `friction`; reads
  gain `mvfx_waterDepth` and the flow along X and Z. A dam break on a dry
  bed follows Ritter's depths within 7%.
- `mvfxWaterInfo.stepsSlowed` and `mvfxWaterInfo.facesHeld` count a
  water surface's steps that outran `MVFX_MAX_WATER_SUBSTEPS` and
  shallow water's faces held at the substep's bound (a fall off a
  cliff), so a scene that leans on either is seen. A waves step due
  exactly the most substeps takes them at its own length.
- Particles meet water surfaces (`mvfxEmitterDef.water`, a
  `mvfxWaterCoupling`): `buoyancy`, the water's density over theirs (at
  most `MVFX_MAX_BUOYANCY`), lifts them by that times gravity's strength
  times their share under, and `drag` times that share draws them toward
  the water's velocity, so one floats half under at 2 and sinks below 1,
  stiff bands settling (the lift is solved with the step's move); a
  step crossing down into it over water pushes the
  surface where it met it, with `pour` adds its sphere's volume to
  shallow water and dies, and writes `mvfx_eventWater`, which other
  emitters can spawn from. Every kind of particle meets it: plain,
  liquids', units' points and rigid particles at their centers.
- A gas force on a moving instance reads its gas where the gas last
  stepped, at the step's start, rather than where the instance is going.
- `mvfxWaterInfo.volumePoured` counts the volume particles poured into
  shallow water, which no host push adds.
- Effect files (`maul-vfx/file.h`): `mvfxWriteEffect` writes an effect
  def as bytes, every field of every struct it reaches, the grids,
  meshes and host modules it refers to named through a table of
  `mvfxEffectAsset`, equal defs giving equal bytes; `mvfxReadEffect`
  reads them back into a def in caller memory and
  `mvfxCreateEffectFromBytes` creates an effect from them. A file is
  hashed and checked unit by unit, and a fault names the offset of its
  byte (`mvfxFileResult`); a name the table lacks is
  `mvfx_errorMissing`, a new status.
- A sheet unit whose points times rows overflows is refused, where the
  check computed the overflowing product first.
- Changed: an emitter's measured bounds grow by the largest size any of
  its particles can have over its life (its stored size times the most
  its size over life scales it), computed once, rather than by each
  particle's size at the step's end; boxes with sizes over life may be
  larger, never too small.
- A typical sprite emitter's step takes 79 instructions a particle
  where it took 1,438: dead particles are taken out by moving runs of
  the living (one move an array), bounds no longer evaluate each
  particle's size and take their box by comparisons that vectorize, and
  an emitter moved by gravity and drag alone moves its particles an axis
  at a time, in the same operations, so the same bits.
  `bench/bench_particles.c` prints the instructions a particle beside
  each time.
- Turbulence's curl takes only the six gradient entries it reads, the
  same bits in about 16% fewer instructions a particle.
- Particle records are written 64 at a time, a field at a time: places,
  sizes, colours and angles over the run, each particle's age taken once,
  each format chosen once; the same bytes in about 61% fewer
  instructions a particle. A ribbon's records, written one at a time,
  take about 1% fewer.
- An emitter moved by gravity and the shared drag alone keeps its plain
  move when it collides: 64 particles moved an axis at a time, then each
  one's contacts from where it started, tried first against only the
  planes and spheres the 64 can reach; the same bits, in about 36% fewer
  instructions for collision.
- A view's sort counts every byte of its keys in one read and takes each
  key inline; the same order in about 26% fewer instructions.
- A ribbon keeps the world places of the last eight points it read, which
  its splines read up to five times each; the same points in about 13%
  fewer instructions.
- `bench_threads` measures the scaling over a host's threads: a million
  particles in 64 instances through a POSIX thread pool of 1 to 8
  threads, plain and with turbulence, where POSIX threads are.
- `mvfxBakeCurlNoise` bakes a turbulence's curl noise into a grid's
  values over one period of a periodic lattice, for a vector field force
  with the repeat border: the same curl without a seam where it repeats,
  about a third fewer instructions a particle than the analytic one.
- A grid's repeat border wraps in integers and its format is chosen once
  a point; quaternion turns are inline; the same bits.
- A plain run's segments are tested against its planes and outside
  spheres together, in their own contact tests' operations; only those
  that may meet one are collided. The same contacts, collision's cost
  about halved.
- An instance holds room for its own effect's gases, water surfaces and
  gas pours only, and a compiled effect for its own gases and water
  surfaces: an instance of one sprite emitter holds 2.8 KiB besides its
  particles rather than 29 KiB, its effect 12 KiB rather than 25 KiB.
- An emitter without ribbons no longer turns its unchecked bend angle,
  where an angle past 2^24 overflowed a conversion; the library's sine
  and cosine give defined values for any argument.
- `tools/effect_json.py` converts effect files to a JSON form and back,
  every field by its name, enums by their constants' names and floats
  by the shortest decimal that reads back to their bits; the golden
  scene's effects are committed in both forms as fixtures.
- Rigid pairs are found by a grid of levels (each body in cells at
  least twice its reach), so one large body among many small ones no
  longer makes every cell large, and they are met in body order, so
  results do not depend on the grid's layout or the emitter's capacity.
- GJK and EPA stop at `MVFX_CONVEX_STEPS`; a capped call answers apart
  with an upper bound on the distance, or with a lower bound on the
  depth.
- Box and hull edges meet grids and the depth image along their length,
  so an edge laid across a crest is held there instead of sinking until
  a corner meets.
- Changed: friction tips a body where its center passes over its
  support's edge (a 0.4 by 0.6 box on a slope at 33.7 degrees; it
  tipped near 41.5), the switch blended across 10% of the balance;
  within a tenth of the limit a held box can creep a few millimeters a
  second until it sleeps. The golden state hashes change with it.
- Rigid particles sleep (`mvfxRigidDef.sleepSpeed`, `sleepTime`; 0.05
  and 0.5 in `mvfxDefaultEmitterDef`): bodies joined by contacts sleep
  together once all have been still, keep their pose and meet nothing
  while they still age, and wake when an awake body meets them, when
  their forces change, when a collider they touch or come to touch
  changes or the depth image is set again, or by
  `mvfxInstance_WakeBodies`; `mvfxEmitterStats.asleep`
  counts them. Sleeping is on by default: a host whose bodies must
  never sleep sets `sleepSpeed` to 0, and one relying on that sees
  different statistics and state hashes once bodies come to rest.
- Changed: a body coming within reach of a sleeping one wakes it, and
  the sleepers it touches in turn, before that step's solve, so a
  landing meets a sleeping pile as it would an awake one, not as
  immovable ground.
- Changed: a GJK search between rigid hulls or boxes stopped at its
  most steps keeps the pair as touching, where it answered them apart
  and an overlapping pair could be missed for the substep;
  `mvfxEmitterStats.rigidSearchesCapped` counts the searches stopped.
- A rigid body resting on another no longer creeps down a slope it
  holds on, nor stands well past its tipping angle: pushing a pair out
  of the depth left at a substep's start turned the bodies and slid
  their contacts across each other unseen by friction; the slide is
  now pushed back. A 0.4 by 0.6 box on a wide one crept 13 mm in 3 s at
  30 degrees; now it holds, and it tips between 35 and 37 degrees
  (33.7 by the statics).
- GJK stops once a step comes no nearer, where some hulls about a
  millimeter apart cycled between faces until its most steps.
- `mvfxGrid_SetValuesIn` gives new values to a box of a grid's points.
  Sleeping rigid particles on a grid collider then wake only within
  reach of the cells those points change, where new values for the
  whole grid wake every one within its box.
- The optional GPU part, the static library `maul-vfx-rhi` (CMake
  option `MAUL_VFX_RHI`, off by default), built on Maul RHI 0.7.0: a
  GPU object (`mvfxCreateGpu`, `mvfxDestroyGpu`) holds the part's
  compute kernels, whose pipelines the device builds while the host
  passes it notifications (`mvfxGpu_Notify`, `mvfxGpu_IsReady`). Its
  first kernel hashes with pcg4d, every word the CPU's.
- `tools/gen_kernels.py` builds the part's kernels from WGSL into the
  containers Maul RHI loads (SPIR-V, DXIL and MSL), checked by
  spirv-val, as headers the part compiles in.
- A vector field over an unturned box samples without the turns, its
  grid of three binary32 components read in one unrolled sum, and a
  repeat border's wrap divides only for a cell past the period: the
  baked turbulence's update takes about 40% fewer instructions, with
  the same results bit for bit.
- Planned instances (`mvfxInstanceDef.planned`, `maul-vfx/plan.h`) for
  a simulation elsewhere, such as a GPU: a planned instance holds no
  particles, and each step writes its emitters' plans
  (`mvfxInstance_GetEmitterPlan`), the births the step owes with their
  serials, birth ticks, death ticks and inverse lives, as the CPU path
  computes them, the anchor before and after, and whether the instance
  was cleared before it (stopped with `mvfx_stopAndClear`, restarted or
  new), for the simulation to drop what it holds. Host spawns and
  prewarm are refused for it, and an effect with gases, water surfaces,
  beams or births from events cannot be planned yet.
- `mvfxEffect_GetEmitterView` gives an emitter as compiled, for a
  simulation elsewhere: the parts it uses beyond plain spawning and
  motion (`MVFX_FEATURE_*` bits, one for each part of an emitter def,
  for it to refuse what it does not run), its shape, velocity, space,
  gravity and drag. A plan also gives the emitter's capacity, the
  instance's place at the plan's start and end, and its velocity.
- The GPU part simulates planned instances (`maul-vfx-rhi/sim.h`):
  `mvfxCreateGpuSim` makes a simulation of one on a ready GPU object,
  refusing with the emitter and what it does not run yet; each frame
  `mvfxGpuSim_AddPasses` adds its passes to the host's frame and
  `mvfxGpuSim_Record` records them, and `mvfxGpuSim_GetParticles` gives
  an emitter's particle and count buffers. It runs emitters of spheres
  with cone, fixed or shape-normal velocities, a share of the
  instance's velocity, gravity and drag, in the world or the instance's
  space: on lavapipe, every step's particles are the CPU path's in
  count, order, serials and ticks, their places and velocities within
  1e-5 of their size, under a moving, turning instance and a clear.
- A plan also gives the instance's seed.
- A full emitter on the GPU takes the births its capacity has room for
  as the CPU path does, batch by batch at each batch's tick after the
  deaths by then: the same particles as the CPU path's when an emitter
  of 64 is owed three times as many and a burst of 150 comes at once.
- The GPU part keeps an emitter's survivors and births in order across
  many workgroups (a scan in three passes) and sizes every dispatch
  within a device's limits, for capacities up to the largest an emitter
  has (2^24). `mvfxGpuSim_Record` documents the uploads a frame takes:
  320 bytes an emitter and 32 a planned birth.
- The GPU part measures each emitter's bounds into its statistics record
  (`mvfxGpuParticles.statistics`, which replaces `counts`), which holds
  its step's map to the world: `mvfxGpuSim_BoundsOf` maps a record read
  back any frames later to the bounds `mvfxInstance_GetBounds` gives
  the CPU path, fixed boxes included. The emitter view gains the fixed
  bounds.
- A plan carries its emitter's forces as resolved for the step
  (`mvfxPlannedForce`), and the emitter view the kinds of force it has.
  The GPU part runs constant forces, winds, attractors (their kill
  radius too) and vortices with their falloffs, in either space: on
  lavapipe the particles are the CPU path's every step under a moving,
  turning, growing instance.
- The GPU part runs turbulence (curl noise, as an acceleration or a
  velocity, scrolling, seeded per instance): a plan's turbulence holds
  each octave's lattice coordinate of the particles' origin split into
  a cell and a fraction, so the GPU's lattice is the CPU path's at any
  distance from the origin.
- `mvfxGrid_GetView` gives a grid as stored, with a serial new at each
  change of its values; the emitter view names the grid each force
  reads. The GPU part runs vector fields (every border, binary32 and
  binary16 grids, as accelerations or velocities), uploading a grid at
  the first step and again whenever it changes.
- A plan carries its emitter's colliders as resolved for the step (its
  own and the instance's, `mvfxPlannedCollider`) and its collision's
  response; the emitter view gains the kinds of its colliders and
  whether it queries the scene or a depth image. The GPU part runs
  collision with planes, spheres and boxes, inside and out: bounces,
  friction, the rest speed, kills and life lost at contacts (the death
  tick recomputed), each particle counting its collisions in a word of
  its own (`MVFX_GPU_PARTICLE_WORDS` is 15).
- A planned collider carries its grid, scale and grow, a plan its own
  colliders' count (`ownColliderCount`), and the emitter view the grid
  each of its colliders reads. The GPU part runs heightfields and
  distance grids: its own read from the grids buffer, the instance's
  from room each emitter keeps (`mvfxGpuSimDef.hostGridBytes`), a step
  whose instance grids pass it refused with `mvfx_errorCapacity`.
- `mvfxGpuSim_HostGridBytes` counts the room a set of the instance's
  colliders needs for its grids. A step refused for it adds no pass for
  any emitter. Each step uploads only the numbers its kernels read (its
  forces' fields and its colliders). A full emitter whose particles lose
  life or die at contacts takes the same births on the GPU as on the
  CPU path: the births are chosen from the deaths the step began with.
  A benchmark of the GPU step (`maul-vfx_bench_gpu`, with the GPU part)
  times the worst grid collision.
- A planned instance's depth image may come without pixels, and a plan
  carries its emitter's depth image resolved (`mvfxPlannedDepth`: the
  maps between the particles' space and clip space, the normals' turn,
  the size, the range, the radius and thickness). The GPU part runs
  depth-image collision from the depth texture the host rendered and
  optionally its normals (`mvfxGpuSim_SetDepthTextures`), the kill
  outside the view included.
- The emitter view tells where an emitter's particles keep their values
  (`mvfxAttributeArrays`: each value's first array, as the CPU path
  stores them). The GPU part's particles follow it, then the death tick
  and a word of the step's: `mvfxGpuParticles.words` replaces
  `MVFX_GPU_PARTICLE_WORDS`, and the serial takes one word, as on the
  CPU path.
- A planned birth carries its source age (`mvfxPlannedBirth` is 48
  bytes), a plan the instance's parameters, and the emitter view how
  births draw their size, colour and custom values (`mvfxSourceView`),
  frame and mesh index (`mvfxIndexView`), and how sizes grow measured
  bounds (`sizeBounds`, `sizeMargin`). The GPU part draws those values
  at each birth from the effect's baked curves and gradients, uploaded
  once, and grows its bounds by size as the CPU path does.
- The emitter view tells how births draw their angle, spin, orientation
  (`orientationMode`), angular velocity and drag scale, and how
  particles turn (`angularDrag`, `rotationScale`, `rotationCurve`). The
  GPU part runs them: angles and orientations turned under the angular
  drag with the rotation speed over life, each particle's drag scaled.
  Its drag sums as the CPU path's does (the emitter's added to the
  winds').
- A plan carries its emitter's velocity modules and kill rules as
  resolved (`mvfxPlannedModules`, `mvfxPlannedKillRule`, each least age
  in ticks), and the emitter view the modules as compiled
  (`mvfxModulesView`, values over life as `mvfxLifeView`; the rotation
  speed over life is `rotationSpeed`). The GPU part runs velocity
  modules and kill rules.
- `mvfxInstance_Spawn` queues the host's particles for a planned instance
  too: its plan carries them as its first births at the step's start
  (`mvfxPlannedHostBirth`, the plan's `hostBirths`; a planned birth names
  its record in `host`), each with its place as given, its velocity as
  the CPU path gives it and the colour and size it gives. The GPU part
  runs them, rate curves and distance spawning.
- `mvfxLayout_GetView` and `mvfxSrgbSteps` (the least values at which an
  sRGB byte steps up), and the emitter view's values over life as records
  show them (`mvfxOverLifeView`). The GPU part writes an emitter's records
  in a layout into a buffer of the host's frame, and their count
  (`mvfxGpuSim_AddRecords`, `mvfxGpuRecordsDef`).
- The emitter view gives its records' order (`sortMode`, `sortMeasure`).
  The GPU part writes records youngest first, and back to front or front
  to back by distance or depth through a radix sort of its own, its keys
  within units in the last place of the CPU path's.
- The emitter view gives the events an emitter writes and their capacity
  (`events`, `eventCapacity`). The GPU part writes births, deaths by age
  and by kills, and collisions in the CPU path's order, bounded and
  counted, with colours and sizes over life; `mvfxGpuParticles` gives an
  emitter's events buffer (`events`, `eventFirst`, `eventCapacity`,
  `MVFX_GPU_EVENT_WORDS`), the statistics record its count and those
  dropped (`MVFX_GPU_EVENTS_WRITTEN`), now 34 words.
- The GPU part's inverse lives after a contact's life loss, particles'
  ages and death ticks are exact on every device (division and tick
  conversions in integers, which WGSL leaves inexact); a life past
  4,096 s takes its new death tick after a life loss, where it kept the
  old one.
- An emitter given a colour or size it stores none of (a host's particle
  with colours, a birth inheriting them from an event) keeps its
  particles' places: the value was written over the first array. The
  golden state hashes change with it (the scene's spark emitter inherits
  a colour it does not store).
- Emitters spawning from events can be planned: the plan numbers an
  emitter's own and the host's births and leaves the births events give,
  their serials and lives to the simulation (`mvfxEmitterPlan`'s
  `spawnFirst`, `spawnLast` and `owed`, `MVFX_HOST_GIVES_VELOCITY` and
  `MVFX_HOST_ADDS_VELOCITY`), bounding the emitter's last death instead;
  the emitter view gives the lifetime, the spawning and the loop
  (`lifetime`, `eventSpawn`, `delayTicks`, `loopTicks`). The GPU part
  builds those births in the CPU path's order and bound, numbers and
  draws them, and keeps the serials they took in the statistics record
  (`MVFX_GPU_EVENT_SERIALS`, now 38 words).
- `mvfxGpuSim_UploadBytes` counts the bytes the last `mvfxGpuSim_Record`
  uploaded, so a host can hold its frame's upload room to its effects;
  `mvfxGpuSim_Record` states them as measured (1,944 an emitter a step,
  1,952 more for one writing events, its sources' 1,184 at the first
  step), where it stated 1,864 and 1,072. The public record states the
  sort's parity a host may rely on and which contacts the two paths may
  decide differently.
- Births from events draw their count and values by their event (its
  particle's serial, its tick and their place among its births) instead
  of by their serials: an event that gives other births leaves every
  other event's births as they were. The golden state hashes change (the
  scene's spark emitter spawns from events).
- The GPU part decides a full emitter's births in a few linear passes
  when none of them dies within its step, and in one linear walk when
  some do, where it walked the births one batch at a time recounting
  every particle: 4,096 births a step went from 25.6 times the step
  with room to 1.7 times on lavapipe, and to 2.5 times when they die
  within it. The births taken are the same.
- Host code the library calls (the allocator, the task hooks and host
  modules) may call only the context's read-only functions: a call of
  one that takes it mutable, from a step's tasks or modules or from an
  allocation, is refused with `mvfx_errorState` and counted as misuse,
  where it could change the context in the middle of the call.
- The GPU part's simulation refuses `mvfxGpuSim_SetDepthTextures`,
  `_AddPasses`, `_AddRecords` and `_Record` with `mvfx_errorState`, and
  a second `mvfxDestroyGpuSim` does nothing, when its allocator calls
  them while the simulation's destruction releases its memory.
- An emitter writing events uploads 96 bytes a step for its events'
  values over life, the words a plan changes (the clock and the map
  into its space), where it uploaded all 1,952; the rest once.
- The GPU part runs every spawn shape but the mesh (point, line, circle,
  sphere, hemisphere, box by its thickness, cone on its base, through its
  volume or from its apex), every arc mode (looping and going and coming
  exact at any age, spread over a burst) and every velocity mode, with
  the randomize amount: `mvfxGpuRefusal.shape` names a mesh shape alone,
  and its `velocity` is gone. The first step uploads 48 bytes more of an
  emitter's shape (its sources' block 1,248 with a mesh's place).
- A mesh whose elements all have no length, area or volume (positions
  collapsed) spawns from its first element, as documented, where the
  choice ended on its last.
- `mvfxMesh_GetView` gives a mesh as stored (positions, normals, its
  triangles, edges and tetrahedra and the running sums that choose one)
  with a serial new at its creation and at each change of its positions;
  `mvfxInstance_GetEmitterMesh` gives the mesh an instance's emitter
  spawns from now, so a simulation elsewhere copies a mesh only when it
  changes.
- The GPU part runs mesh shapes (vertices, edges, surface, volume),
  choosing the CPU path's element exactly by thresholds the host computes
  from the mesh's running sums, the mesh written again only when its
  serial changes; `mvfxGpuSimDef.hostMeshBytes` keeps room for a larger
  mesh bound later and `mvfxGpuSim_MeshBytes` counts a mesh's;
  `mvfxGpuSim_SetEmitterMesh` binds a mesh only when it fits that room,
  refusing a larger one at setup rather than at every step.
  `mvfxGpuRefusal` names the features alone (its `shape` is gone: every
  shape runs), and `mvfxShapeView` gives the mesh mode.
- The misuse count is `mvfxGetContextMisuse`, the family's name for an
  object's count (conventions section 4), where it was
  `mvfxContext_GetMisuse`.
- An emitter's view gives how it is drawn as ribbons
  (`mvfxEmitterView.ribbon`, a `mvfxRibbonView`: its ids and break
  distance, pieces and bend, texture coordinate, facing, pinned head,
  width and colour over the length) and its arrays the ribbon and the
  distance along it, and a plan gives the emitter's ribbon counter after
  the host's breaks (`mvfxEmitterPlan.ribbonCounter`), for a simulation
  elsewhere to join births to ribbons and draw them as the CPU path
  does.
- By source (`mvfx_ribbonPerSource`), the counter's ribbons take ids
  with the top bit set and a source's ribbon its serial's low 31 bits:
  the emitter's own ribbon and a source's of the same number were drawn
  as one chain through both.
- The GPU part runs emitters whose births join ribbons: each particle's
  ribbon and its distance along it are the CPU path's (by the counter
  with its breaks by distance, by batch, by source), and the statistics
  record's new word `MVFX_GPU_RIBBON_BREAKS` counts the breaks the GPU
  made since the last clear, the emitter's counter being its plan's plus
  it (`MVFX_GPU_STATISTICS_WORDS` is 39). The first step uploads 16 bytes
  more of an emitter's numbers.
- The GPU part writes an emitter's ribbons as
  `mvfxInstance_WriteRibbons` does (`mvfxGpuSim_AddRibbons`,
  `mvfxGpuRibbonsDef`): its points in the host's layout, the list of the
  ribbons written and their counts (`MVFX_GPU_RIBBON_COUNTS`: the points
  and ribbons there are and those written), each segment cut into its
  pieces along the spline as the bend asks, the current ribbon ending at
  its pinned head. Its passes count among an emitter's
  `MVFX_GPU_RECORD_PASSES`.
- The GPU part expands an emitter's ribbons into triangle strips as
  `mvfxInstance_ExpandRibbons` does (`mvfxGpuSim_ExpandRibbons`,
  `mvfxGpuExpandDef`): two vertices a point across its width, six
  indices a segment, ribbons taken whole while both buffers hold them,
  the counts the vertices and indices there are and those written.
- The GPU part expands an emitter's ribbons into tubes as
  `mvfxInstance_ExpandTubes` does (`mvfxGpuSim_ExpandTubes`, the expand
  def's `tube`): a ring a point turned by rotation-minimizing frames
  carried along each ribbon, the quads between rings, optional caps.
- A plan gives an emitter's pinned head as the CPU path's ribbons find it
  (`mvfxEmitterPlan.ribbonHead`, `ribbonHeadPlace`), and the GPU part's
  bounds join it as `mvfxInstance_GetBounds` does: the statistics record
  holds it (`MVFX_GPU_STATISTICS_WORDS` is 46), the step uploading 28
  bytes more for such an emitter.
- A planned instance may hold beams: a plan gives a beam's strikes as the
  CPU path strikes them (each at the tick the last one's points die, the
  points with the first's life), their ticks, serials and lives the same.
- The GPU part runs beams (`MVFX_FEATURE_BEAM`): a pass after the step
  places each point along its curve with the noise across it, at rest,
  its distance along the beam summed in the CPU path's order, the bounds
  the beam's. A plan gives a beam as resolved (`mvfxEmitterPlan.beam`,
  `mvfxPlannedBeam`: its curve's points and the noise's time), and an
  emitter's view its def (`mvfxEmitterView.beam`); a beam's step uploads
  96 bytes more.
- The GPU part writes an emitter's lights (`mvfxGpuSim_AddLights`,
  `mvfxGpuLightsDef`) as `mvfxInstance_WriteLights` does: every particle
  whose serial is a multiple of the light def's `every`, in the pool's
  order, as `mvfxLight`, the first capacity of them and the count of all.
  Lights are no longer refused (`MVFX_FEATURE_LIGHT`), and an emitter's
  view gives its light def (`mvfxEmitterView.light`).
- The GPU part expands an emitter's particles into quads
  (`mvfxGpuSim_ExpandSprites`, taking an `mvfxGpuExpandDef`) as
  `mvfxInstance_ExpandSprites` does: four vertices and six indices a
  particle in its records' order, turned by its billboard, the first
  quads that fit both buffers, and the strips' counts. An emitter's view
  gives its sprite def (`mvfxEmitterView.sprite`).
- The GPU part finds neighbors (`MVFX_FEATURE_NEIGHBORS`, for an emitter
  without steering, a pair force or contacts) as the CPU path does. It
  builds each queried emitter's grid from the particles that outlive the
  step, with the same keys and order. Each particle's walk then gives it
  the count and offsets of its neighbors, before the step moves it. The
  statistics record counts the queries their cap stopped
  (`MVFX_GPU_NEIGHBORS_CAPPED`; `MVFX_GPU_STATISTICS_WORDS` is 47). A plan gives a grid's mask
  (`mvfxEmitterPlan.gridMask`), and an emitter's view its neighbor def,
  its grid's cell and where the neighbor attributes are
  (`mvfxEmitterView.neighbors`, `gridCell`, `mvfxAttributeArrays`'
  `neighborCount`, `neighborCenter`, `neighborVelocity`). Births start
  with no neighbors, as on the CPU path. Every emitter's step uploads 16
  bytes more (1,960), and one with a grid 8 more.
- The GPU part steers (`MVFX_FEATURE_STEERING`) and runs pair forces
  (`MVFX_FEATURE_PAIR_FORCE`) as the CPU path does: the neighbor query
  gathers flocking's sums and the pulls, and seeking, fleeing and path
  following steer toward targets a plan gives resolved
  (`mvfxEmitterPlan.steering`, `mvfxPlannedSteering`), all added to the
  velocity before the step moves it. An emitter's view gives its
  steering def and pair force (`mvfxEmitterView.steering`, `pairRadius`,
  `pairStrength`). A grid's step uploads 32 bytes, one that steers 160
  and 16 a point of its path.
- The GPU part keeps particles apart (`MVFX_FEATURE_CONTACTS`) as the
  CPU path does, for an emitter that kills nothing within a step and has
  no ribbons or beam: after the step's compaction, a grid of the places
  after motion, the projection passes, then the impulses; the bounds are
  taken of the places contacts moved. The statistics record counts the
  particles whose contacts passed the most (`MVFX_GPU_CONTACTS_CAPPED`;
  `MVFX_GPU_STATISTICS_WORDS` is now 48). An emitter's view gives its
  contact def and whether its particles may be killed within a step
  (`mvfxEmitterView.contacts`, `kills`). Its step uploads 96 bytes more.
- Changed: particle contacts part a pair to a rest distance 0.05% past
  its radii's sum rather than to the sum, so a parted pair rests clear
  of touching instead of on its edge, where rounding decided whether it
  counted in the next pass's average; the golden state hashes change
  with it.
- The GPU part finds plexus links (`MVFX_FEATURE_PLEXUS`) as the CPU
  path does: after the bounds, a grid of the places at the step's end,
  each particle's links counted, their slots scanned in pool order and
  the links written in the CPU path's order. They are in a buffer of
  the emitter's (`mvfxGpuParticles.links`, `linkCapacity`), the step's
  count and the drops in the statistics record (`MVFX_GPU_LINKS`,
  `MVFX_GPU_LINKS_DROPPED`; `MVFX_GPU_STATISTICS_WORDS` is now 50). An
  emitter's view gives its plexus def (`mvfxEmitterView.plexus`).
- The GPU part writes plexus links as two-point ribbons
  (`mvfxGpuSim_AddPlexus`) and expands them into strips
  (`mvfxGpuSim_ExpandPlexus`), as `mvfxInstance_WritePlexus` and
  `mvfxInstance_ExpandPlexus` do: through the ribbon passes, the links
  read as chains of two. An emitter with plexus links keeps 112 bytes a
  link of its most for them and their output.
- The GPU part runs chains and sheets (`mvfxUnitDef`) without pins,
  aerodynamics, self-collision or tearing, beside nothing that kills,
  collides, spawns from events, steers or touches neighbors: a workgroup
  a unit takes its substeps, the core's sixteen constraint sets in its
  order with the damping, and a unit is born, dies and is counted for
  room whole. The planner counts an emitter of units' room in units, a
  birth a unit, and `mvfxEmitterView` gains the unit, its points, its
  width and its rest lengths.
- The GPU part's units meet colliders (the life their contacts take, a
  contact that kills), kill rules and attractors' kill radii in each
  substep, a point's kill killing its unit, a newborn unit's too, and
  turn a substep at a time; a unit's death is its first point's.
- The GPU part writes the events of units: births, deaths by age and by
  kills, and a point's first contact a step at its substep, with its
  velocity then. An emitter that writes events keeps 48 bytes a particle
  of its capacity for its particles' facts, 12 more than before.
- The GPU part runs units beside neighbors, steering, a pair force,
  contacts and the host's births.
- The GPU part runs units born from another emitter's events, a birth a
  unit, their room counted in units.
- The GPU part runs pinned units: a chain's first point or a sheet's
  first row held where it was born, or to the instance, weighing nothing
  in the constraints. `mvfxAttributeArrays` gains the anchor, the unit
  frame's origin and rotation, its flags and its source.
- The GPU part runs units held to the host's frames, each unit's frame
  eased to the frame the host set over the substeps, its pinned points
  through it. `mvfxEmitterPlan` gains the host's frames at the plan's
  end (`attachFrames`, `attachFrameCount`) and `mvfxPlannedHostBirth`
  the unit's frame.
- The GPU part runs units held to a source emitter's particles: the
  source's particles are copied after its step, each unit finds its
  particle by serial and follows its place and orientation, and a unit
  whose particle is gone is let go.
- The GPU part blows sheets in the air (`mvfxUnitDef`'s `normalDrag` and
  `tangentDrag`): each point sums its triangles' drag in the CPU path's
  order, the air's velocity taken from the forces that drag.
- The GPU part tears units (`mvfxUnitDef.tearStrain`): each point tests
  its edges after the projection, a torn edge breaks its constraints and
  quads, the tears' events are written and `MVFX_GPU_TORN` counts them.
  `mvfxAttributeArrays` gains the torn edges. On both paths a unit's
  tear events now join the step's after its substeps with its contacts,
  point by point (its contact, its tear across, its tear down).
- The GPU part draws units as ribbons: each unit is a ribbon of its
  first point's serial with its points' distances along it, and a torn
  chain's ribbon splits where it tore, as on the CPU path.
- `mvfxGpuSim_ExpandSheets` expands an emitter's sheets into meshes on
  the GPU as `mvfxInstance_ExpandSheets` does: a vertex a point with its
  frame from its neighbours, six indices a whole quad, the first whole
  sheets that fit.
- Self-collision parts each substep's pairs in one Jacobi pass with
  constraint averaging, as particle contacts do: a point's candidates
  are gathered both ways and it moves by its touching pairs' mean,
  computed from the same places, instead of pair after pair. The golden
  scene's hashes change from its third step.
- The GPU part runs units' self-collision: each step a grid of the reach
  over the points and their candidates as the CPU path's, and each
  substep a pass between the unit pass's halves moving each point by its
  touching pairs' mean. `MVFX_GPU_SELF_CAPPED` counts the points whose
  candidates passed the most (`MVFX_GPU_STATISTICS_WORDS` is now 52).
- The GPU part runs rigid particles that meet nothing (not colliding, beside
  nothing that kills within a step): each body's substeps turn its angular
  velocity by the gyroscopic term with its shape's mass at its size, move
  it as a particle and hold it asleep; a newborn takes the rest of its
  step's substeps; a body alone falls asleep once still for the sleep time.
  `mvfxEmitterView` gains the rigid def and a hull's vertices, volume and
  second moments, and `mvfxAttributeArrays` a rigid body's sleep.
- The GPU part runs rigid spheres and capsules that meet the world:
  planes, spheres and boxes from outside and in, the emitter's and the
  instance's, with the CPU path's contacts, stabilizing, pressing, patch
  friction, restitution and strikes (their events, counts and life lost);
  newborns meet them in their first step. Boxes and hulls that collide,
  grids, depth images, bodies meeting each other and kills within a step
  stay refused; the instance's heightfield or distance grid refuses the
  step (`mvfx_errorState`). `MVFX_GPU_RIGID_CAPPED` counts the contacts
  dropped past a body's most (`MVFX_GPU_STATISTICS_WORDS` is now 53).
- A rigid body's strike events are held over its substeps and join the
  step's body by body, as a unit's points' contacts do.
- The GPU part runs rigid boxes that meet the world as well: their
  corners against planes, spheres kept within and boxes kept within, the
  nearest point against spheres kept out of, and the separating axes
  against boxes kept out of (a face clipped by the other's or two edges'
  nearest points), as the CPU path's. Colliding hulls stay refused.
- The GPU part's rigid bodies meet heightfields and distance grids, by
  the CPU path's samples (a sphere's center, a capsule's core, a box's
  corners, face centers and edges) probed in the grid's field. A depth
  image and colliding hulls stay refused.
- The GPU part's rigid bodies meet the depth image too, by the same
  samples along their way over each substep, within twice the shape's
  radius, as the CPU path's. Colliding hulls stay refused, and so does
  killing bodies outside the view.
- The GPU part's rigid hulls meet planes, the spheres and boxes that keep
  them within, heightfields, distance grids and the depth image, by their
  vertices, then their faces' centers and edges where deeper, as the CPU
  path's.
- The GPU part's rigid hulls kept out of spheres and boxes meet them by
  the CPU path's polytope searches in binary32: GJK for a sphere's
  nearest point, GJK and EPA for a box, then the faces' manifold. The
  searches stopped at their cap are counted (`MVFX_GPU_RIGID_SEARCHES`,
  word 53; `MVFX_GPU_STATISTICS_WORDS` is now 54).
- `mvfxEmitterView` gives a rigid hull's faces (`hullFaceCount`,
  `hullFaceStart`, `hullLoop`, `hullPlanes`).
- Rigid bodies' pair contacts are solved a colour at a time: each pair of
  bodies gets a priority from a hash of the two indices and the least
  colour none of its neighbours has, and the passes go colour by colour,
  so that the GPU part can take a colour's pairs at once in the same
  order. Stacks and piles settle by a different order than before.
- The GPU part's rigid spheres and boxes meet each other: an emitter whose
  bodies meet runs its substeps in one workgroup, its pairs found,
  coloured and solved colour by colour as the CPU path's, their strikes'
  events included. Capsules and hulls meeting each other, and bodies
  meeting each other with sleeping on, stay refused.
- The GPU part's rigid capsules and hulls meet each other too: capsules by
  their cores' nearest points and each core's ends, hulls as two
  polytopes, as the CPU path's.
- The GPU part's rigid bodies that meet each other sleep too: sleepers an
  awake body reaches wake by touch, and at a step's end the bodies joined
  by the last substep's pairs sleep or stay awake as one island, as the
  CPU path's.
- Effects with smoke gases can be planned: `mvfxInstance_GetGasPlan`
  gives each smoke gas's step (its span, gravity, forces and obstacles,
  resolved where the CPU path resolves them), and
  `mvfxInstance_SetGasHolds` tells a planned instance whether its
  simulation's gas still holds anything, which a stopped instance waits
  on. A planned instance's gases are not read on the CPU path
  (`mvfxInstance_ReadGas`, `mvfxInstance_ReadGasAs` and
  `mvfxInstance_SampleGas` refuse it). `mvfxGpuRefusal` names a gas the
  GPU part does not run yet (`gas`).
- The GPU part runs smoke gases: their sources, buoyancy and decay,
  their carries and their pressure solve by multigrid, the levels of at
  most 512 cells in one workgroup, as the CPU path's step runs them
  (forces, obstacles, MacCormack, confinement, burning and splats still
  refused). `mvfxGpuSim_AddGasRead` writes a gas's field into a buffer
  as `mvfxInstance_ReadGasAs` does, `mvfxGpuSim_GasPasses` gives the
  passes a gas's step takes at most, and `mvfxEffect_GetGasView` gives a
  gas as compiled. A GPU object now holds at most 32 pipeline requests
  unanswered, each answer `mvfxGpu_Notify` takes requesting the next
  kernel's, so a device holding fewer creations at once can still build
  every kernel.
- The GPU part runs a smoke gas's forces (shared with the particles'
  force code), MacCormack's carry, vorticity confinement and the burn,
  its expansion less its mean in a sealed box; a gas with obstacles is
  still refused.
- A smoke gas on the GPU writes a statistics record each step (whether
  it holds anything, at which tick), which `mvfxGpuSim_GetGas` names for
  the host to read back and `mvfxGpuSim_ReportGas` hands to the planned
  instance, so a stopped instance finishes when its gas empties.
  `MVFX_GAS_EMPTY` is public.
- The GPU part runs a smoke gas's obstacles, its own and the instance's
  colliders, still and moving, of every kind: their solids marked each
  step, emptied, closing the solve's faces, and pushing the gas with
  their motion. `mvfxGasPlan.obstacleMoved` says which obstacles moved
  since the plan before. A gas the GPU part runs is no longer refused.
- The GPU part runs the gas force: particles follow a smoke gas's
  velocity as its last step left it, copied each step into the
  emitter's grids. `mvfxEmitterView.forceGases` names each force's gas.
- A gas plan names the emitters pouring into the gas
  (`mvfxGasPlan.pours`, `mvfxPlannedPour`): each one's map from its
  particles' space into the box, its splat's rates and drag.
- The GPU part runs an emitter's splat into a smoke gas: each step its
  particles are sorted by gas cell, and each substep every cell and face
  gathers the particles reaching it in the pool's order, as the core's
  splat adds and pulls them. `mvfxEmitterView.splatGas` names the gas.
- The GPU part runs particle liquids (position-based fluids): each
  substep a move, a grid of the predicted places, the iterations'
  multipliers, corrections and slides along the colliders, the
  velocities' gain, viscosity and confinement, each a pass. A plan hands
  the liquid's kernels over (`mvfxEmitterPlan.liquid`,
  `mvfxPlannedLiquid`), the emitter view its def (`liquid`), and
  `mvfxGpuSim_EmitterPasses` reports the passes an emitter's step takes,
  thousands for a liquid at its most, for a host to size its device by.
- Liquid gases can be planned. A planned instance cuts its steps into
  the substeps the CPU path cuts them into, counted from the fastest
  particle speed its simulation reports (`mvfxInstance_SetLiquidSpeed`),
  and hands over a plan a substep (`mvfxInstance_GetPlanSteps`,
  `mvfxInstance_GetEmitterStepPlan`, `MVFX_MAX_PLAN_STEPS`): its span,
  the move over the first alone, its births from its start.
- The GPU part runs liquid gases (grid liquids, APIC) in steps of one
  substep: the emitter's particles sorted into the gas's cells, the
  cells of liquid marked, the particles' velocities and affine matrices
  gathered onto the faces, the solve with air held at no pressure, the
  velocity extended into the air and read back by the particles, whose
  fastest speed the gas's statistics record carries
  (`MVFX_GPU_GAS_FASTEST`, handed over by `mvfxGpuSim_ReportGas`). A
  gas plan names its liquid's map (`mvfxGasPlan.liquid`) and the
  emitter view its affine matrices (`mvfxAttributeArrays.affine`).
- A planned instance hands over a gas's plan a substep
  (`mvfxInstance_GetGasStepPlan`): its span, a clear and its obstacles'
  move in the first alone, its forces at its start.
- An effect with water surfaces can be planned: a planned instance hands
  over each surface's plan (`mvfxInstance_GetWaterPlan`, a substep at a
  time with `mvfxInstance_GetWaterStepPlan`): its substeps, the host's
  pushes in its frame, its sources' depths and its terrain resolved;
  its simulation reports shallow water's speed
  (`mvfxInstance_SetWaterSpeed`), from which the next step's substeps
  are counted, and whether the surface still moves
  (`mvfxInstance_SetWaterHolds`: a stopped planned instance runs while
  it does). Reads and samples of a planned surface are refused
  (`mvfx_errorState`), as a planned gas's are. A surface's def as
  compiled is read with `mvfxEffect_GetWaterView`.
- The GPU part runs frames that split into steps (an effect with a
  liquid gas whose courant number cuts the instance's step): each step
  every emitter's passes and then every gas's, from that step's plans,
  as the CPU path takes them; the records after the last. Such an
  effect keeps its emitters' and gases' host state once more a step it
  may split into, and its frames take their passes once a step. A split
  frame's events follow one another over its steps as the CPU path's
  (each step's after those before, aged once, up to the capacity over
  the frame), and an emitter spawning from them takes its source's step
  alone.
- The GPU part runs waves: each step the host's pushes sink the surface
  and lift it evenly, each substep a pass of its own (open walls' edges
  as the CPU path leaves them), the step's end settled into a statistics
  record (`mvfxGpuSim_GetWater`, `MVFX_GPU_WATER_*`) whose report
  (`mvfxGpuSim_ReportWater`) finishes a stopped instance; a frame takes
  at most 68 passes a surface (`mvfxGpuSim_WaterPasses`). A surface's
  fields are written into a host's buffer in its formats as
  `mvfxInstance_ReadWater` writes them (`mvfxGpuSim_AddWaterRead`).
- The GPU part runs shallow water: each step its ground read from its
  terrain (the effect's heightfield or the instance's), the first fill,
  its sources and the host's pushes spilling in order, three passes a
  substep (faces, outflows, depths), and its record's fastest speed,
  which `mvfxGpuSim_ReportWater` hands over so the next steps count
  their substeps from it (at most 196 passes a surface a step). A
  surface keeps its numbers, fields and record on the device (16 bytes
  a cell, 8 a face, and its terrain's grid; `hostGridBytes` covers the
  instance's terrain); `mvfxGpuRefusal.water` names none.
- A planned instance hands over an emitter's water coupling as resolved
  (`mvfxEmitterPlan.water`, `mvfxPlannedWaterCoupling`): its map into
  the surface's frame, the surface's up, the particles' radius and
  gravity in the surface's lengths, and the coupling's numbers; an
  emitter's view carries its coupling as compiled
  (`mvfxEmitterView.water`). The GPU
  part runs an emitter coupled to a surface: its particles' drag toward
  the surface's flow and their buoyancy, from the surface's fields as
  its last step left them, copied each step into the emitter's grids
  (8 bytes a cell, and 4 a face of shallow water); their crossings, in
  the CPU path's order, with their water events (`mvfx_eventWater`) and
  pours' kills, listed for the surface's next step, which pushes it
  (waves sink and lift evenly; shallow water spills and takes the
  poured water).
- The GPU part runs a surface gas: its emitter's particles mapped into
  the box and sorted stably into buckets of the reach, each cell's
  distance gathered from the buckets about it in the CPU path's order
  (drawn in by Solenthaler's factor, its eigenvalue in closed form in 2D
  and by three Jacobi sweeps in 3D), then the smoothing passes, within
  the device's rounding of the CPU path's. A gas read takes its distance
  (`mvfx_gasDistance`), its record says whether its emitter has
  particles, and an emitter the quality leaves out leaves the reach in
  every cell (12 passes a step and 2 a smoothing pass; 8 bytes a cell,
  30 a particle of its emitter's capacity and 8 a bucket).
  `mvfxGasView` gives a surface gas's buckets (`bucket`, `buckets`);
  `mvfxGpuRefusal.gas` names none.
- A planned instance's water surfaces count as the CPU path's do:
  `mvfxInstance_SetWaterCounts` takes a step's reach, held faces and
  poured volume once a tick (a stop that clears empties them and drops
  the reports before it), and the instance counts its steps slowed from
  its plans, so `mvfxInstance_GetWaterInfo` and the instance's bounds
  agree with the CPU path's. The GPU part's water record gains the
  faces held and the volume poured (`MVFX_GPU_WATER_FACES_HELD`,
  `MVFX_GPU_WATER_POURED`, 7 words), which `mvfxGpuSim_ReportWater`
  hands over with the reach; `mvfxWaterPlan.volumeScale` takes a poured
  volume into the world's.
- The GPU part samples a planned instance's water surface by query, as
  `mvfxInstance_SampleWater` does on the CPU path:
  `mvfxGpuSim_AddWaterSamples` takes a host's points (binary32, the world
  less an origin in binary64) from one storage buffer and writes each
  sample's place, normal, velocity and whether it lies over the grid
  into another (`MVFX_GPU_WATER_SAMPLE_WORDS`), read back when the
  host's frame is done; at most `MVFX_GPU_WATER_SAMPLES` queries a
  surface a frame and `MVFX_GPU_SAMPLE_POINTS` points a query.
  `mvfxWaterInfo.scale` and `mvfxGasInfo.scale` give a box's scale.
- The GPU part samples a planned instance's gas by query, as
  `mvfxInstance_SampleGas` does on the CPU path:
  `mvfxGpuSim_AddGasSamples` takes a field and a host's points (binary32,
  the world less an origin in binary64) from one storage buffer and
  writes a binary32 sample a point into another, read back when the
  host's frame is done; at most `MVFX_GPU_GAS_SAMPLES` queries a gas a
  frame.
- The GPU part meshes a planned instance's surface gas, as
  `mvfxInstance_WriteSurfaceMesh` does on the CPU path:
  `mvfxGpuSim_AddSurfaceMesh` writes the same vertices in the same order
  and the same indices into a host's buffers (positions, optionally
  normals, indices), the first primitives that fit, and optionally the
  counts (`MVFX_GPU_SURFACE_MESH_COUNTS`: the vertices and indices there
  are and those written); at most `MVFX_GPU_SURFACE_MESHES` a surface a
  frame. `tools/gen_marching_cubes.py` also writes the tables' words the
  GPU part uploads.

### Changed

- Shallow water counts a step's substeps at its start, from the state
  the last step left, and makes them equal; each substep was sized by
  the flow then, which only the running step knew. A flow that speeds
  up within a step is held to the substep's bound instead, and a
  surface's first step (before its fill) takes one substep.
- `mvfxInstance_GetEmitterPlan` and `mvfxInstance_GetGasPlan` refuse
  (`mvfx_errorState`) an instance whose plans hold more than one step;
  each has its own plan.
- An effect with a surface gas can be planned (`mvfxCreateInstance` no
  longer refuses it): the gas's plan maps the emitter it wraps into its
  box (`mvfxGasPlan.liquid`). Its mesh
  (`mvfxInstance_WriteSurfaceMesh`) is refused for a planned instance
  (`mvfx_errorState`), whose fields are its simulation's.
- `mvfxInstance_WriteSurfaceMesh` and
  `mvfxInstance_GetSurfaceMeshScratchBytes` clear their outputs on any
  failure (the family's rule for outputs on failure).

### Fixed

- A smoke gas on the GPU went on from the wrong fields after a frame the
  host dropped when its step flips them an odd number of times (one
  substep, the default): the passes added for the dropped frame moved
  it on. A gas's step now moves it when recorded, so a dropped frame
  leaves it as it was.
- An emitter's events gather over every step an instance takes in a
  context step (a liquid gas's substeps, the ticks an instance held off
  screen): each step began them afresh, so the host read the last
  step's alone. Its event capacity counts the context step's; an
  emitter spawning from another's events takes those of its own step.
- A grid liquid's particles keep their own affine matrices: the CPU
  step read and wrote them as nine floats a particle where they are
  nine arrays, so a birth zeroed parts of other particles' matrices and
  a pour depended on its emitter's capacity.
- A planned instance's plan holds at most twice an emitter's births at
  once: a beam struck in many substeps of a liquid gas's split wrote
  past it; the births past it are refused, still owed.
- A unit's point writes one collision event a step, its first contact's,
  as the header says and a particle does; it wrote one at each substep
  it touched. The events of a unit's points follow its substeps in point
  order, each taken at its contact.
- A unit ends whole, with its first point: a contact's life loss on a
  later point alone ended that point by itself, leaving its unit split
  (a chain of three left with two points).
- The GPU part's documented first-step upload counts an emitter's
  sources' words with their ribbon words (the bytes it gave were 16
  short).
- The GPU part's rigid bodies wake those they touch and settle their
  islands in at most a round a body; a state that never settles can no
  longer hold the workgroup in a loop.
- The GPU part's neighbor grid moves a world-space emitter's places by
  the anchor's shift before it reads them, as the CPU path's step does;
  a moving instance's grid was built from the places before the shift.
- A capsule lying flat on a box's face meets it at its core's two ends
  only; it also met it at a middle point wherever rounding placed the
  core's deepest point, so the CPU and GPU paths solved it apart. The
  middle point counts where it is clearly deeper than both ends (over an
  edge or a corner).
- The GPU part's comparison suites have 900 seconds each: on a slow CI
  runner the longest passed the 300 a run gives by default.
- A box's or a hull's face center and an edge's inner points meet a grid
  or the depth image only where deeper than their corners by more than
  1e-4 of the body's reach: on a flat surface they were as deep, and
  rounding decided whether they met, so the CPU and GPU paths gave a body
  different contacts.
