// A smoke gas's numbers (rhi/src/sim_gas.c writes them), which gas.wgsl
// and gas_read.wgsl read: spliced where a kernel names it.

// A level of the solve: its cells along each axis and in all; where its
// pressure, right-hand side, scratch and weights start; where its areas
// across each axis start, and how many fine cells a cell spans; where its
// coefficients across each axis start.
struct Level {
    size: vec4<u32>,
    at: vec4<u32>,
    areas: vec4<u32>,
    coefficients: vec4<u32>,
}

// A substep: its span in seconds, the velocity's keep over it, whether
// the sources pour; each scalar's keep.
struct Substep {
    span: vec4<f32>,
    keeps: vec4<f32>,
}

// A source in the box's frame: its center and shape, its half extents and
// radius, its rates a second, its velocity and whether it sets it.
struct Source {
    center: vec4<f32>,
    extents: vec4<f32>,
    rates: vec4<f32>,
    velocity: vec4<f32>,
}

struct Gas {
    // Its cells along each axis, its levels; its closed walls, the
    // solve's cycles, its sources, the first level of the tail.
    size: vec4<u32>,
    walls: vec4<u32>,
    // Its cell's edge, Jacobi's weight, the buoyancy's weights of density
    // and of temperature.
    lattice: vec4<f32>,
    // The gravity in the box's frame.
    gravity: vec4<f32>,
    // Where each buffer's faces across each axis start, and its density,
    // temperature and fuel.
    faces: array<vec4<u32>, 2>,
    cells: array<vec4<u32>, 2>,
    levels: array<Level, 10>,
    substeps: array<Substep, 8>,
    sources: array<Source, 8>,
}
