// A water surface's cells as its reads and samples take them, spliced
// after shared/water_numbers.wgsl into a kernel that binds the surface's
// numbers as `water` and its fields as `fields` and gives the buffer its
// fields are read from as current(): shallow water's faces and ground,
// and each cell's surface, normal and velocity (src/water_api.c's
// SurfaceAt, NormalAt and VelocityAt).

// include: shared/length.wgsl

// Shallow water's fields: depths, X faces and Z faces, two buffers each,
// the givers' scales, the ground.
fn faceAt(axis: u32) -> u32 {
    let b = current();
    let c = water.size.z;
    return select(2u * c + 2u * water.faces.x + b * water.faces.y, 2u * c + b * water.faces.x,
                  axis == 0u);
}

fn groundAt(i: u32) -> f32 {
    return fields[3u * water.size.z + 2u * (water.faces.x + water.faces.y) + i];
}

fn shallow() -> bool {
    return water.head.y == MODEL_SHALLOW;
}

// The surface's height at cell (x, z) over the grid's center: a wave's
// displacement, or shallow water's ground and depth.
fn surfaceAt(x: u32, z: u32) -> f32 {
    let i = z * water.size.x + x;
    let u = fields[current() * water.size.z + i];
    return select(u, groundAt(i) + u, shallow());
}

// The surface's unit normal at cell (x, z): its heights' central
// differences, one-sided at the grid's edges.
fn normalAt(x: u32, z: u32) -> vec3<f32> {
    let h = water.sink.x;
    let x0 = select(x, x - 1u, x > 0u);
    let x1 = select(x, x + 1u, x + 1u < water.size.x);
    let z0 = select(z, z - 1u, z > 0u);
    let z1 = select(z, z + 1u, z + 1u < water.size.y);
    let dx = (surfaceAt(x1, z) - surfaceAt(x0, z)) / (f32(x1 - x0) * h);
    let dz = (surfaceAt(x, z1) - surfaceAt(x, z0)) / (f32(z1 - z0) * h);
    // Measured scaled where a slope's square passes binary32's range.
    let length = lengthOf(vec3<f32>(dx, 1.0, dz));
    return vec3<f32>(-dx / length, 1.0 / length, -dz / length);
}

// The surface's velocity at cell (x, z): a wave's rate along Y, or shallow
// water's flow along X and Z, the mean of the cell's two faces' along each.
fn velocityAt(x: u32, z: u32) -> vec3<f32> {
    let n0 = water.size.x;
    if (!shallow()) {
        return vec3<f32>(0.0, fields[(2u + current()) * water.size.z + z * n0 + x], 0.0);
    }
    let across = faceAt(0u) + z * (n0 + 1u) + x;
    let along = faceAt(1u) + z * n0 + x;
    return vec3<f32>(0.5 * (fields[across] + fields[across + 1u]), 0.0,
                     0.5 * (fields[along] + fields[along + n0]));
}
