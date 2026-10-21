// A water surface's numbers (rhi/src/sim_water.c writes them), which
// water.wgsl and water_read.wgsl read: spliced where a kernel names it.

// Its cells along X and Z and in all, its closed walls; a substep's
// stiffness (dt c^2 / h^2), rate decay, Mur's factor and dt; the step's
// pushes; the cell's edge, the even lift what the pushes sank gives, the
// grid's center's Y; the pushes in its frame (X, Z, radius, depth).
struct Water {
    size: vec4<u32>,
    factors: vec4<f32>,
    head: vec4<u32>,
    sink: vec4<f32>,
    pushes: array<vec4<f32>, 64>,
}
