// A neighbor grid's cells and keys, as the core's (src/neighbor.h), which
// the grid and contacts kernels share (tools/gen_kernels.py splices it
// where a kernel names it).

// include: shared/pcg4d_hash.wgsl

// include: shared/cell_floor.wgsl

// The salt that sets grid keys apart (the core's MVFX_GRID_SALT).
const GRID_SALT: u32 = 0x4E474244u;

fn keyOf(cell: vec3<i32>, mask: u32) -> u32 {
    let c = bitcast<vec3<u32>>(cell);
    return pcg4d(vec4<u32>(c, GRID_SALT)).x & mask;
}
