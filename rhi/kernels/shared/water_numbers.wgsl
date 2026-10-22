// A water surface's numbers (rhi/src/sim_water.c writes them), which
// water.wgsl and water_read.wgsl read: spliced where a kernel names it,
// after shared/collider_numbers.wgsl.

const MODEL_WAVES: u32 = 0u;
const MODEL_SHALLOW: u32 = 1u;
// The head's flags: shallow water fills at this step; it stands on a
// terrain.
const FLAG_FILLS: u32 = 1u;
const FLAG_TERRAIN: u32 = 2u;

// Its cells along X and Z and in all, its closed walls; a substep's
// factors: for waves, the stiffness (dt c^2 / h^2), the rate's decay,
// Mur's factor and dt; for shallow water, gravity, the flows' decay, the
// fill level and dt; the pushes, the model, the sources and the flags;
// the cell's edge, the even lift what waves' pushes sank gives, the
// grid's center's Y, shallow water's volume into the world's (its scale
// cubed); shallow water's faces across X and across Z, the
// emitters coupled to it (their crossings' lists, water.wgsl's
// crossings). Then
// the pushes in its frame (X, Z, radius, depth); for shallow water each
// push's inner and ring weights summed, each source's footprint (its
// center's X and Z, its reach along X and Z; its shape, the depth it adds
// this step, its radius) and its terrain resolved into its frame.
struct Water {
    size: vec4<u32>,
    factors: vec4<f32>,
    head: vec4<u32>,
    sink: vec4<f32>,
    faces: vec4<u32>,
    pushes: array<vec4<f32>, 64>,
    spills: array<vec4<f32>, 64>,
    sources: array<vec4<f32>, 16>,
    terrain: Collider,
}
