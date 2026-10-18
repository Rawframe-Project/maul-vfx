// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's step on the GPU: update, room, then the survivors and the
// births kept in order by a scan in three passes (reduce, offsets,
// scatter), and their box measured. Particles are arrays of the capacity
// in one buffer of words: the emitter's values as the CPU path stores
// them, then the death tick (2) and whether it outlives the step, each
// value's first word given by the step's numbers (Words).

// A word of a value the emitter does not store.
const NONE: u32 = 0xFFFFFFFFu;
const CHUNK: u32 = 256u;
// The elements an invocation of the scan's reduce and scatter takes, in a
// row; a workgroup's block is CHUNK times as many.
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;
const TWO_PI: f32 = 6.28318530717958647692;

// include: shared/exact.wgsl

const SHAPE_SALT: u32 = 0x53484150u;
const VELOCITY_SALT: u32 = 0x56454C4Fu;
const SIZE_SALT: u32 = 0x53495A45u;
const COLOR_SALT: u32 = 0x434F4C52u;
const FRAME_SALT: u32 = 0x46524D45u;
const CUSTOM_SALT: u32 = 0x43555354u;
const MESH_SALT: u32 = 0x4D455348u;
const ANGLE_SALT: u32 = 0x414E474Cu;
const SPIN_SALT: u32 = 0x5350494Eu;
const ORIENTATION_SALT: u32 = 0x4F524E54u;
const ANGULAR_SALT: u32 = 0x414E4755u;
const DRAG_SALT: u32 = 0x44524147u;
const LIFE_SALT: u32 = 0x4C494645u;
const EVENT_SALT: u32 = 0x45564E54u;

// A force as resolved for the step, in the particles' space.
struct Force {
    // its kind, its falloff's kind, its field mode, unused
    modes: vec4<u32>,
    // its vector (the constant's acceleration, the wind's velocity, the
    // vortex's axis) and strength
    vector: vec4<f32>,
    // its point (the attractor's, the vortex's) and softening
    point: vec4<f32>,
    // its falloff's center and radius, axis and cone cosine
    center: vec4<f32>,
    axis: vec4<f32>,
    // its kill radius, pull, falloff exponent and tightness
    extra: vec4<f32>,
}

// A force's field, as resolved for the plan: its map's turn into the
// particles' space; its octaves and seed; its map's scale and the octaves'
// gain; its scroll a second in its own space; each octave's frequency and
// the lattice coordinate of the particles' origin at the plan's start, a
// cell and the fraction past it.
struct Field {
    rotation: vec4<f32>,
    counts: vec4<u32>,
    scales: vec4<f32>,
    scroll: vec4<f32>,
    frequency: array<vec4<f32>, 2>,
    cell: array<vec4<u32>, 8>,
    fraction: array<vec4<f32>, 8>,
    // a vector field's box: the particles' origin mapped back less its
    // center; its size; its turn; its grid's first word in the grids and
    // size; the grid's format (1 for binary16) and the border
    box: vec4<f32>,
    boxSize: vec4<f32>,
    boxRotation: vec4<f32>,
    grid: vec4<u32>,
    gridModes: vec4<u32>,
}

// A collider as resolved for the step, in the particles' space: its
// kind and whether it keeps them inside; its center and radius; a plane's
// normal; a box's half extents and turn; a grid collider's grid (first
// word, size), its format, and the scale of its values and its grow.
struct Collider {
    modes: vec4<u32>,
    center: vec4<f32>,
    normal: vec4<f32>,
    halfExtents: vec4<f32>,
    rotation: vec4<f32>,
    grid: vec4<u32>,
    gridModes: vec4<u32>,
    scales: vec4<f32>,
}

// The depth image as resolved for the step: whether it is met, its size
// and the samples a step takes at most; whether it is reversed, whether
// leaving it kills, whether a normal image is given; the radius and the
// thickness in the particles' space; the turn of its normals into the
// particles' space; the maps between the particles' space and clip space.
struct Depth {
    head: vec4<u32>,
    modes: vec4<u32>,
    sizes: vec4<f32>,
    normalTurn: vec4<f32>,
    toClip: mat4x4<f32>,
    fromClip: mat4x4<f32>,
}

// Where a particle's values are among its words: each one's first word
// (NONE for one the emitter does not store); the emitter's arrays, after
// which come the death tick's two words and whether it outlives the step
// (death and alive).
struct Words {
    place: u32,
    velocity: u32,
    serial: u32,
    birth: u32,
    inverseLife: u32,
    collisions: u32,
    death: u32,
    alive: u32,
    size: u32,
    color: u32,
    angle: u32,
    spin: u32,
    orientation: u32,
    angular: u32,
    frame: u32,
    drag: u32,
    mesh: u32,
    count: u32,
    unused0: u32,
    unused1: u32,
    custom: array<vec4<u32>, 2>,
}

// A value source as compiled (mvfxSourceView): its kind, whether one
// draw serves every component, its components and parameter; its baked
// curve's and gradient's first words in the grids buffer (NONE for
// none); its bounds.
struct Source {
    modes: vec4<u32>,
    tables: vec4<u32>,
    low: vec4<f32>,
    high: vec4<f32>,
}

// A kill rule as resolved (mvfxPlannedKillRule): its kind, the
// collisions that kill, its least age in ticks (two words); its center
// and radius; its half sizes and speed; its rotation; its normal.
struct KillRule {
    modes: vec4<u32>,
    center: vec4<f32>,
    half: vec4<f32>,
    rotation: vec4<f32>,
    normal: vec4<f32>,
}

struct Step {
    // capacity, births, the plan's ticks, the instance's seed
    counts: vec4<u32>,
    // the plan's start (two words), the emitter's index, its space (0 for
    // the world)
    start: vec4<u32>,
    // the velocity's mode, the shape's kind, update's workgroups, unused
    modes: vec4<u32>,
    // the anchors' shift, rounded to binary32; the share of the instance's
    // velocity inherited
    shift: vec4<f32>,
    // the step's drag factors over the plan: decay, carry, push; the drag
    factors: vec4<f32>,
    // gravity in the particles' space; unused
    gravity: vec4<f32>,
    // the shape's matrix's rows, its offset and radius, its thickness
    shapeRow0: vec4<f32>,
    shapeRow1: vec4<f32>,
    shapeRow2: vec4<f32>,
    shapeOffset: vec4<f32>,
    shapeExtra: vec4<f32>,
    // the velocity's direction and cone cosine, tangents with randomize
    // and the least speed, the greatest speed
    direction: vec4<f32>,
    tangentA: vec4<f32>,
    tangentB: vec4<f32>,
    speed: vec4<f32>,
    // the instance at the plan's start and end: origin from the anchor
    // after, scale; rotations; its velocity
    fromOrigin: vec4<f32>,
    fromRotation: vec4<f32>,
    toOrigin: vec4<f32>,
    toRotation: vec4<f32>,
    instanceVelocity: vec4<f32>,
    // the forces' count, whether any kills, the drag every particle has
    // (binary32 bits), unused
    forceHead: vec4<u32>,
    forces: array<Force, 8>,
    fields: array<Field, 8>,
    // the colliders' count, whether a contact kills, whether it meets
    // anything (colliders or a depth image), unused; the response:
    // restitution, friction, life loss, rest speed
    collisionHead: vec4<u32>,
    response: vec4<f32>,
    depth: Depth,
    colliders: array<Collider, 16>,
    words: Words,
    // The instance's parameters.
    parameters: array<vec4<f32>, 16>,
    // The angular drag's factors over the plan: decay, carry; the speed
    // limit's decay over the plan; unused.
    angular: vec4<f32>,
    // The velocity modules in the particles' space: the added direction
    // and the scale into the particles' space; the orbit's axis; its
    // center; the radial center.
    modules: array<vec4<f32>, 4>,
    // The kill rules' count, the events the emitter writes (a mask of
    // EVENT_ kinds), at most how many, the events' first word in the
    // events buffer.
    fateHead: vec4<u32>,
    // For an emitter that spawns from events: the ticks of the plan whose
    // events spawn (first, last; none when first is past last), the
    // plan's host births, the spawn region's first word in the events
    // buffer; the serials its own and the host's births took after the
    // plan (two words), the plan's births' first vector in the births
    // buffer, its births' room; the source's anchor after less its own;
    // the source's anchor after less the instance's origin at the plan's
    // start; the instance's move over the plan (each from binary64,
    // rounded to binary32).
    spawnHead: vec4<u32>,
    spawnOwed: vec4<u32>,
    spawnShift: vec4<f32>,
    spawnFrom: vec4<f32>,
    spawnMove: vec4<f32>,
    // The rules.
    kills: array<KillRule, 8>,
    // The sources: the births' size, colour, angle, spin, orientation,
    // angular velocity, drag scale, custom values and lifetime.
    sources: array<Source, 16>,
    // The frame's and the mesh index's draws: first, last, whether random.
    draws: array<vec4<u32>, 2>,
    // The bounds' growth by size: the most the size over life scales each
    // axis; the margin for rounding.
    growth: vec4<f32>,
    // How particles turn and move: the orientation's mode, the angular
    // drag (binary32 bits), whether the speed is limited (1) and modules
    // move them (2), how fast the speed falls to the limit (bits).
    turns: vec4<u32>,
    // The values over life (LIFE_ROTATION and on), two words each: the
    // value (binary32 bits) and the baked curve's first word (NONE for
    // none).
    lives: array<vec4<u32>, 3>,
    // How it spawns from events: whether it does, the kind, the least
    // count and the counts' span; the most births a step, whether colour
    // (1) and size (2) are inherited and the source is in the instance's
    // space (4), the share of velocity inherited
    // (bits), the source's event capacity; its loop's start and length in
    // ticks (two words each; a length of 0 for none).
    spawnRule: array<vec4<u32>, 3>,
    // The shape beyond the sphere's numbers: its length and half extents;
    // its arc, arc speed and cone tangent, unused; its arc mode, whether a
    // cone fills its volume, and the emitter's start delay in ticks (two
    // words).
    shapeSize: vec4<f32>,
    shapeArc: vec4<f32>,
    shapeModes: vec4<u32>,
}

@group(0) @binding(0) var<uniform> params: Step;
@group(0) @binding(1) var<storage, read_write> births: array<vec4<u32>>;
@group(0) @binding(2) var<storage, read_write> source: array<u32>;
@group(0) @binding(3) var<storage, read_write> moved: array<u32>;
// The statistics record (rhi/src/record.h): counts, the growth, the box.
@group(0) @binding(4) var<storage, read_write> counts: array<u32>;
// Whether each of the plan's births has room, as the room entry decides.
@group(0) @binding(5) var<storage, read_write> accepted: array<u32>;
// The grids the forces and colliders read, binary32 values one a word,
// binary16 two.
@group(0) @binding(7) var<storage, read> grids: array<u32>;
// The host's depth image and its normals in the view's space, when the
// step's depth is met and they are given (else the simulation's own).
@group(0) @binding(9) var depthImage: texture_depth_2d;
@group(0) @binding(10) var normalImage: texture_2d<f32>;
// An emitter's events (a word of the simulation's for one that writes
// none): each live particle's facts of the step, EVENT_FACTS words (the
// flags EVENT_ENDED and EVENT_CONTACT, the first contact's share, point
// and normal, the inverse life it met); each block of the sequence's
// counts of the four groups' events, then their first places; the
// events, EVENT_WORDS each.
@group(0) @binding(8) var<storage, read_write> events: array<u32>;
// Each block of the sequence's count of kept elements, then its first
// place; after them (blockCount() words on), each block's box of the
// places scatter wrote: min then max, three binary32 words each; after
// them (7 blockCount() words on), each block's growth by size.
@group(0) @binding(6) var<storage, read_write> blocks: array<u32>;

var<workgroup> scan: array<u32, CHUNK>;
var<workgroup> carried: u32;
var<workgroup> liveCount: u32;
var<workgroup> dead: u32;
var<workgroup> taken: u32;
var<workgroup> boxLow: array<vec4<f32>, CHUNK>;
var<workgroup> boxHigh: array<vec4<f32>, CHUNK>;

// include: sim/forces.wgsl

// include: sim/shapes.wgsl

// include: sim/collide.wgsl

// include: sim/step.wgsl

// include: sim/events.wgsl

// include: sim/spawn.wgsl
