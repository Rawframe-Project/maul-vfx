// An emitter's coupling to its water surface, as the core's water_couple.c
// couples a particle: the surface's fields as its last step left them,
// copied each step to a region of the grids buffer (rhi/src/sim_follow.c):
// waves' heights and rates, or shallow water's depths and ground, then its
// faces across X and across Z.

// A particle at a step's start against the surface (mvfxWaterTouch):
// whether it is over the water (over the grid, on shallow water its
// center above the ground), its place in the surface's frame and the
// surface's height over it.
struct Touch {
    over: bool,
    at: vec3<f32>,
    height: f32,
}

fn isShallowWater() -> bool {
    return params.water.w != 0u;
}

// A field's value at cell (x, z): k 0 the heights or depths, 1 the rates
// or ground.
fn waterField(k: u32, x: u32, z: u32) -> f32 {
    let cells = params.water.y * params.water.z;
    return bitcast<f32>(grids[params.water.x + k * cells + z * params.water.y + x]);
}

// mvfxWaterCorners: the lowest of the four cells about (x, z) of the
// surface's frame and the fractions toward the others; false off the grid.
fn waterCorners(x: f32, z: f32, lo: ptr<function, vec2<u32>>,
                t: ptr<function, vec2<f32>>) -> bool {
    let n = vec2<f32>(f32(params.water.y), f32(params.water.z));
    let h = params.waterNumbers.w;
    let p = vec2<f32>(x / h + 0.5 * n.x, z / h + 0.5 * n.y);
    if (!(p.x >= 0.0 && p.x <= n.x && p.y >= 0.0 && p.y <= n.y)) {
        return false;
    }
    let cells = params.water.yz;
    let c = min(max(p - 0.5, vec2<f32>(0.0)), n - 1.0);
    let below = vec2<u32>(c);
    *lo = select(cells - 2u, below, below < cells - 1u);
    *t = c - vec2<f32>(*lo);
    return true;
}

// mvfxWaterShare: a corner's weight (1 the X, 2 the Z neighbor's).
fn waterShare(t: vec2<f32>, corner: u32) -> f32 {
    return select(1.0 - t.x, t.x, (corner & 1u) != 0u) *
           select(1.0 - t.y, t.y, (corner >> 1u) != 0u);
}

// mvfxWaterHeightOf: the surface's height and shallow water's ground,
// bilinear about lo.
fn waterHeightOf(lo: vec2<u32>, t: vec2<f32>, height: ptr<function, f32>,
                 ground: ptr<function, f32>) {
    *height = 0.0;
    *ground = 0.0;
    let shallow = isShallowWater();
    for (var corner = 0u; corner < 4u; corner++) {
        let x = lo.x + (corner & 1u);
        let z = lo.y + (corner >> 1u);
        let w = waterShare(t, corner);
        let g = select(0.0, waterField(1u, x, z), shallow);
        // SurfaceAt: waves' displacement, shallow water's ground and depth.
        *height += w * select(waterField(0u, x, z), g + waterField(0u, x, z), shallow);
        *ground += w * g;
    }
}

// The surface's velocity at cell (x, z): a wave's rate along Y, or shallow
// water's flow, the mean of the cell's two faces along each axis.
fn waterVelocityAt(x: u32, z: u32) -> vec3<f32> {
    if (!isShallowWater()) {
        return vec3<f32>(0.0, waterField(1u, x, z), 0.0);
    }
    let n0 = params.water.y;
    let faces = params.water.x + 2u * n0 * params.water.z;
    let across = faces + z * (n0 + 1u) + x;
    let along = faces + (n0 + 1u) * params.water.z + z * n0 + x;
    return vec3<f32>(0.5 * (bitcast<f32>(grids[across]) + bitcast<f32>(grids[across + 1u])), 0.0,
                     0.5 * (bitcast<f32>(grids[along]) + bitcast<f32>(grids[along + n0])));
}

fn waterVelocityOf(lo: vec2<u32>, t: vec2<f32>) -> vec3<f32> {
    var velocity = vec3<f32>(0.0);
    for (var corner = 0u; corner < 4u; corner++) {
        velocity += waterShare(t, corner) * waterVelocityAt(lo.x + (corner & 1u), lo.y + (corner >> 1u));
    }
    return velocity;
}

// mvfxWaterDrag: the surface's drag on a particle at x (in the particles'
// space) added to its acceleration a and drag, the coupling's drag times
// its share under (its center from its radius above the surface, 0, to
// its radius below, 1) toward the water's velocity; what it touches.
fn waterDrag(x: vec3<f32>, a: ptr<function, vec3<f32>>, drag: ptr<function, f32>) -> Touch {
    var touch = Touch(false, vec3<f32>(0.0), 0.0);
    let q = params.waterRotation;
    let sigma = params.waterOrigin.w;
    let turned = rotate(vec4<f32>(-q.xyz, q.w), x - params.waterOrigin.xyz);
    touch.at = select(turned / sigma, turned, sigma == 1.0);
    var lo = vec2<u32>(0u);
    var t = vec2<f32>(0.0);
    if (!waterCorners(touch.at.x, touch.at.z, &lo, &t)) {
        return touch;
    }
    // Through locals: a member's pointer breaks the backends.
    var height = 0.0;
    var ground = 0.0;
    waterHeightOf(lo, t, &height, &ground);
    touch.height = height;
    // Waves have no ground.
    if (isShallowWater() && !(touch.at.y > ground)) {
        return touch;
    }
    touch.over = true;
    let r = params.waterUp.w;
    let share = min(max((touch.height - touch.at.y + r) / (2.0 * r), 0.0), 1.0);
    let pull = params.waterNumbers.y * share;
    if (!(pull > 0.0)) {
        return touch;
    }
    // In the surface's frame toward the water's velocity, then into the
    // particles' space.
    let toward = pull * waterVelocityOf(lo, t);
    *a += rotate(q, toward * sigma);
    *drag += pull;
    return touch;
}

// A vector of the particles' space along the surface's Y, in its lengths.
fn alongWater(v: vec3<f32>) -> f32 {
    let up = params.waterUp;
    return (up.x * v.x + up.y * v.y + up.z * v.z) / params.waterOrigin.w;
}

// mvfxWaterLift: the surface's lift on a particle touching it, solved with
// the move (velocity v, the drag's carry and push) it makes: along the
// surface's Y, where the step ends without it, c, and its pieces, full
// below the height less the radius, none above the height plus it,
// falling linearly between; the end y1 = c + push L(y1) is on one.
fn waterLift(touch: Touch, v: vec3<f32>, carry: f32, push: f32, a: ptr<function, vec3<f32>>) {
    if (!touch.over) {
        return;
    }
    let r = params.waterUp.w;
    let full = params.waterNumbers.z * params.waterNumbers.x;
    let c = touch.at.y + alongWater(v) * carry + alongWater(*a) * push;
    let top = touch.height + r;
    var lift = 0.0;
    if (c + push * full <= touch.height - r) {
        lift = full;
    } else if (c < top) {
        let k = full / (2.0 * r);
        let end = (c + push * k * top) / (1.0 + push * k);
        lift = k * (top - end);
    }
    *a += params.waterUp.xyz * lift * params.waterOrigin.w;
}
