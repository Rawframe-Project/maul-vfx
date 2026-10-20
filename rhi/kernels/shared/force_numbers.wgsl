// A force and its field as rhi/src/numbers.c writes them
// (mvfxForceWords): spliced where a kernel names it.

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
    // size; the grid's format (1 for binary16) and the border. A gas's:
    // the origin in the box's frame; its cell's edge; no turn; its faces'
    // first word and its cells
    box: vec4<f32>,
    boxSize: vec4<f32>,
    boxRotation: vec4<f32>,
    grid: vec4<u32>,
    gridModes: vec4<u32>,
}
