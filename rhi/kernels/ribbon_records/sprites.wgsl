// The ribbon records kernel's sprites (spliced into ribbon_records.wgsl):
// an emitter's particles expanded into quads as the CPU path's
// mvfxInstance_ExpandSprites expands them, in the records' order (the
// pool's, its reverse, or the sort's by place): four vertices a particle
// through the ribbon vertex writer (left bottom, right bottom, left top,
// right top, each at its corner's place with its corner field) and the
// six indices of its two triangles, the first quads that fit both
// buffers. The axes are made in the view's space in binary32 from the
// core's inputs: the eye and the quad's center less the view's origin,
// the particle's orientation and velocity in the world. The sprite's
// numbers stand in the ribbons': sink.y the billboard, normal the view's
// up and the directional stretch, tables.z the indices the list holds,
// tables.w the counts' first word.

const SINK_SPRITES: u32 = 3u;

// The billboards (mvfxBillboard) the kernel tells apart; the rest face
// the view.
const BILLBOARD_ROTATED: u32 = 1u;
const BILLBOARD_AXIS_Y: u32 = 2u;
const BILLBOARD_FIXED: u32 = 3u;
const BILLBOARD_DIRECTIONAL: u32 = 4u;

// A quad's axes: right across its width, up along its height, each unit
// or 0.
struct Axes {
    right: vec3<f32>,
    up: vec3<f32>,
}

// The particle record r shows of live, in the records' order.
fn spriteAt(r: u32, live: u32) -> u32 {
    switch params.order.x {
        case ORDER_REVERSED: {
            return live - 1u - r;
        }
        case ORDER_SORTED: {
            return sortWords[sortSpan() + r];
        }
        default: {
            return r;
        }
    }
}

// Axes upright on up and turned about it to face the eye from center.
fn facing(up: vec3<f32>, center: vec3<f32>) -> Axes {
    let upright = unitOf(up);
    return Axes(unitOf(cross(upright, params.eye.xyz - center)), upright);
}

// Axes in the view's plane turned by angle about the view axis.
fn inView(angle: f32) -> Axes {
    let forward = params.forward.xyz;
    let right = unitOf(cross(forward, ribbon.normal.xyz));
    let up = unitOf(cross(right, forward));
    let turn = sinCos(angle);
    let sine = turn.x;
    let cosine = turn.y;
    return Axes(cosine * right + sine * up, cosine * up - sine * right);
}

// Particle i's quad about center: its axes by the billboard; its half
// width and height in the world (a directional one's height stretched).
fn quadOf(i: u32, center: vec3<f32>, half: ptr<function, vec2<f32>>) -> Axes {
    let w = params.words;
    let size = sizeOf(i, ageOf(i)) / params.scales.x;
    *half = 0.5 * size.xy;
    switch ribbon.sink.y {
        case BILLBOARD_ROTATED: {
            var angle = 0.0;
            if (w.angle != NONE) {
                angle = floatAt(w.angle, i);
            }
            return inView(angle);
        }
        case BILLBOARD_AXIS_Y: {
            return facing(vec3<f32>(0.0, 1.0, 0.0), center);
        }
        case BILLBOARD_FIXED: {
            var q = vec4<f32>(0.0, 0.0, 0.0, 1.0);
            if (w.orientation != NONE) {
                q = vec4<f32>(vectorAt(w.orientation, i), floatAt(w.orientation + 3u, i));
            }
            let world = multiply(params.turn, q);
            return Axes(rotate(world, vec3<f32>(1.0, 0.0, 0.0)),
                        rotate(world, vec3<f32>(0.0, 1.0, 0.0)));
        }
        case BILLBOARD_DIRECTIONAL: {
            let along = rotate(params.turn, vectorAt(w.velocity, i)) / params.scales.x;
            (*half).y += 0.5 * ribbon.normal.w * sqrt(dot(along, along));
            return facing(along, center);
        }
        default: {
            return inView(0.0);
        }
    }
}

// Sprite quads: each particle's quad of those that fit, the workgroups
// striding over them; the counts by the first invocation, as the strips':
// the vertices and indices there are, those written.
@compute @workgroup_size(64, 1, 1)
fn spriteQuads(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = statistics[0];
    let fits = min(params.head.y / 4u, ribbon.tables.z / 6u);
    let n = min(live, fits);
    if (id.x == 0u) {
        count[ribbon.tables.w + COUNT_POINTS] = 4u * live;
        count[ribbon.tables.w + COUNT_RIBBONS] = 6u * live;
        count[ribbon.tables.w + COUNT_POINTS_WRITTEN] = 4u * n;
        count[ribbon.tables.w + COUNT_RIBBONS_WRITTEN] = 6u * n;
    }
    for (var r = id.x; r < n; r += params.shape.y * 64u) {
        let i = spriteAt(r, live);
        let center = placeOf(i);
        var half = vec2<f32>(0.0);
        let axes = quadOf(i, center, &half);
        var corner = Point();
        corner.width = 1.0;
        corner.color = vec4<f32>(1.0);
        for (var c = 0u; c < 4u; c++) {
            let across = select(-half.x, half.x, (c & 1u) != 0u);
            let high = select(-half.y, half.y, (c & 2u) != 0u);
            corner.corner = vec2<f32>(f32(c & 1u), f32((c >> 1u) & 1u));
            writePoint(4u * r + c, i, i, 0.0, center + axes.right * across + axes.up * high,
                       corner);
        }
        let at = params.shape.w + 6u * r;
        let base = 4u * r;
        ribbonList[at] = base;
        ribbonList[at + 1u] = base + 1u;
        ribbonList[at + 2u] = base + 2u;
        ribbonList[at + 3u] = base + 2u;
        ribbonList[at + 4u] = base + 1u;
        ribbonList[at + 5u] = base + 3u;
    }
}
