// A collider as rhi/src/numbers.c writes it (mvfxColliderWords) and its
// kinds: spliced where a kernel names it.

const COLLIDER_PLANE: u32 = 0u;
const COLLIDER_SPHERE: u32 = 1u;
const COLLIDER_BOX: u32 = 2u;
const COLLIDER_HEIGHTFIELD: u32 = 3u;
const COLLIDER_DISTANCE: u32 = 4u;

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
