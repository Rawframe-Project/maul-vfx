// The ribbon records kernel's sheets (spliced into ribbon_records.wgsl):
// an emitter's sheets expanded into meshes as the CPU path's
// mvfxInstance_ExpandSheets expands them, oldest first: a vertex a point
// through the ribbon vertex writer (its tangent across and side down from
// its neighbours either way, one-sided at the edges, its face normal
// their cross product, each in binary32; its corner its place across and
// row over the last of each; its ribbon the sheet's index) and the six
// indices of each quad between rows none of whose edges is torn; the
// first whole sheets that fit both buffers, the counts all of them.
// sheetFit, in one workgroup, places each quad among its sheet's whole
// ones, each sheet's first index and the sheets that fit, in the sort
// buffer; sheetPoints writes them. The sheet's numbers stand in the
// ribbons': words.x the torn edges' first word (NONE for none), sink.y a
// sheet's points across, sink.z its points, tables.z the indices the list
// holds, tables.w the counts' first word.

const SINK_SHEETS: u32 = 4u;

// Whether a sheet's quad from point i (w across) is whole, none of its
// four edges torn, as the core's mvfxIsQuadWhole.
fn sheetQuadWhole(i: u32, w: u32) -> bool {
    let torn = ribbon.words.x;
    if (torn == NONE) {
        return true;
    }
    let t = wordAt(torn, i);
    return ((t | wordAt(torn, i + w)) & 1u) == 0u && ((t | wordAt(torn, i + 1u)) & 2u) == 0u;
}

// Where sheetFit leaves its work in the sort buffer: from word 0 each
// point's quad's place among its sheet's whole quads; from the capacity
// each sheet's whole quads' indices, then its first index; after them the
// sheets that fit.
fn sheetFirst(u: u32) -> u32 {
    return params.head.x + u;
}

fn sheetsFitting() -> u32 {
    return 2u * params.head.x;
}

// Sheet u's whole quads placed in order, their indices' count.
fn placeQuads(u: u32, w: u32, points: u32) -> u32 {
    let rows = points / w;
    var whole = 0u;
    for (var r = 0u; r + 1u < rows; r++) {
        for (var c = 0u; c + 1u < w; c++) {
            let q = u * points + r * w + c;
            if (sheetQuadWhole(q, w)) {
                sortWords[q] = whole;
                whole += 1u;
            }
        }
    }
    return 6u * whole;
}

// Sheet fit, in one workgroup: each invocation places a run of the
// sheets' quads; a scan of the runs' indices gives each sheet's first
// index, in order; the sheets that fit are those whose vertices and
// indices both buffers hold after those before (a prefix, as the CPU
// path stops at the first that does not); the counts by the last
// invocation, as the strips': the vertices and indices there are, those
// written.
@compute @workgroup_size(256, 1, 1)
fn sheetFit(@builtin(local_invocation_index) local: u32) {
    if (local == 0u) {
        liveCount = statistics[0];
    }
    let live = workgroupUniformLoad(&liveCount);
    let w = ribbon.sink.y;
    let points = ribbon.sink.z;
    let sheets = live / points;
    let per = (sheets + CHUNK - 1u) / CHUNK;
    let lo = min(local * per, sheets);
    let hi = min(lo + per, sheets);
    var run = 0u;
    for (var u = lo; u < hi; u++) {
        let own = placeQuads(u, w, points);
        sortWords[sheetFirst(u)] = own;
        run += own;
    }
    var first = exclusiveSum(local, run);
    var fitting = 0u;
    for (var u = lo; u < hi; u++) {
        let own = sortWords[sheetFirst(u)];
        sortWords[sheetFirst(u)] = first;
        let fits = (u + 1u) * points <= params.head.y && first + own <= ribbon.tables.z;
        fitting += select(0u, 1u, fits);
        first += own;
    }
    let n = exclusiveSum(local, fitting) + fitting;
    storageBarrier();
    workgroupBarrier();
    if (local == CHUNK - 1u) {
        sortWords[sheetsFitting()] = n;
        count[ribbon.tables.w + COUNT_POINTS] = sheets * points;
        count[ribbon.tables.w + COUNT_RIBBONS] = first;
        count[ribbon.tables.w + COUNT_POINTS_WRITTEN] = n * points;
        count[ribbon.tables.w + COUNT_RIBBONS_WRITTEN] = select(first, sortWords[sheetFirst(n)],
                                                                n < sheets);
    }
}

// Point i's frame in its sheet from its first point (w across, rows
// down), at row r and place c, as the core's SheetFrame.
fn sheetFrame(first: u32, w: u32, rows: u32, r: u32, c: u32, point: ptr<function, Point>) {
    let right = placeOf(first + r * w + min(c + 1u, w - 1u));
    let left = placeOf(first + r * w + select(c, c - 1u, c > 0u));
    let below = placeOf(first + min(r + 1u, rows - 1u) * w + c);
    let above = placeOf(first + select(r, r - 1u, r > 0u) * w + c);
    let across = right - left;
    let down = below - above;
    (*point).tangent = unitOf(across);
    (*point).side = unitOf(down);
    (*point).face = unitOf(cross(across, down));
}

// Sheet points: each point of the sheets that fit, its vertex and, for a
// whole quad from it, the quad's six indices at its place among its
// sheet's (two triangles, counterclockwise seen from the face normal's
// side), the workgroups striding over the points.
@compute @workgroup_size(64, 1, 1)
fn sheetPoints(@builtin(global_invocation_id) id: vec3<u32>) {
    let w = ribbon.sink.y;
    let points = ribbon.sink.z;
    let rows = points / w;
    let n = sortWords[sheetsFitting()];
    for (var i = id.x; i < n * points; i += params.shape.y * 64u) {
        let u = i / points;
        let first = u * points;
        let r = (i - first) / w;
        let c = (i - first) % w;
        var point = Point();
        point.width = 1.0;
        point.color = vec4<f32>(1.0);
        point.ribbon = u;
        point.corner = vec2<f32>(f32(c) / f32(w - 1u), f32(r) / f32(rows - 1u));
        sheetFrame(first, w, rows, r, c, &point);
        writePoint(i, i, i, 0.0, placeOf(i), point);
        if (r + 1u < rows && c + 1u < w && sheetQuadWhole(i, w)) {
            let at = params.shape.w + sortWords[sheetFirst(u)] + 6u * sortWords[i];
            ribbonList[at] = i;
            ribbonList[at + 1u] = i + 1u;
            ribbonList[at + 2u] = i + w;
            ribbonList[at + 3u] = i + w;
            ribbonList[at + 4u] = i + 1u;
            ribbonList[at + 5u] = i + w + 1u;
        }
    }
}
