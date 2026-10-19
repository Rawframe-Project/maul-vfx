// The record kernels' bindings and workgroup words (tools/gen_kernels.py
// splices it where a kernel names it, after shared/record_fields.wgsl):
// the numbers, the particles (read), the statistics, the grids, the
// records, the count, the sort and its digit counts.

@group(0) @binding(0) var<uniform> params: Records;
@group(0) @binding(1) var<storage, read> particles: array<u32>;
@group(0) @binding(2) var<storage, read> statistics: array<u32>;
@group(0) @binding(3) var<storage, read> grids: array<u32>;
@group(0) @binding(4) var<storage, read_write> recordWords: array<u32>;
@group(0) @binding(5) var<storage, read_write> count: array<u32>;
// The emitter's sort (an emitter that sorts; a word of the simulation's
// otherwise): keys, then indices, twice over its blocks; and two sets of
// digit counts, 256 a block each, digit-major.
@group(0) @binding(6) var<storage, read_write> sortWords: array<u32>;
@group(0) @binding(7) var<storage, read_write> digits: array<u32>;

var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> liveCount: u32;
