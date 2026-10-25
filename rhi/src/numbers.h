// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The numbers of an emitter's step on the GPU (sim.wgsl's Step), from its
// view and its plan.

#ifndef MAUL_VFX_RHI_SRC_NUMBERS_H
#define MAUL_VFX_RHI_SRC_NUMBERS_H

#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/plan.h"

#include <string.h>

// e^-x for x >= 0, the part's own in binary64: x halved until small, a
// series, squared back.
double mvfxExpMinus(double x);

// A collider's words as the kernels read them
// (shared/collider_numbers.wgsl): its kind and inside; center and radius;
// normal; half extents; rotation; its grid (first word, size), the grid's
// format; its scale and grow; the words between them 0.
#define MVFX_COLLIDER_WORDS 32
void mvfxColliderWords(const mvfxPlannedCollider* collider, const uint32_t grid[5],
                       uint32_t words[MVFX_COLLIDER_WORDS]);

// The forces' words as the kernels read them (shared/forces.wgsl): a head
// (their count, whether any kills, the drag every point has), 24 words
// each, then each one's field of 108 at 4 + 24 MVFX_MAX_FORCES; each
// force's grid as mvfxForceGridWords gives it.
#define MVFX_FORCE_WORDS (4 + 132 * MVFX_MAX_FORCES)
void mvfxForceWords(const mvfxPlannedForce* forces, int32_t count, float sharedDrag,
                    const uint32_t (*gridWords)[5], uint32_t* words);

// Writes four floats' bits at words.
static inline void mvfxFloatWords(uint32_t* words, float a, float b, float c, float d)
{
    const float f[4] = {a, b, c, d};
    memcpy(words, f, sizeof f);
}

// A value's first word, or all ones for one not stored.
static inline uint32_t mvfxWordOf(int16_t array)
{
    return array >= 0 ? (uint32_t)array : 0xFFFFFFFFu;
}

// The sources: the births' size, colour, angle, spin, orientation,
// angular velocity, drag scale, custom values and lifetime.
#define MVFX_GPU_SOURCES        16
#define MVFX_SOURCE_SIZE        0
#define MVFX_SOURCE_COLOR       1
#define MVFX_SOURCE_ANGLE       2
#define MVFX_SOURCE_SPIN        3
#define MVFX_SOURCE_ORIENTATION 4
#define MVFX_SOURCE_ANGULAR     5
#define MVFX_SOURCE_DRAG        6
#define MVFX_SOURCE_CUSTOM      7
#define MVFX_SOURCE_LIFE        15

// The values over life the kernels read: the rotation speed, then the
// velocity modules' speed limit, motion scale, added speed, radial speed
// and orbit rate.
#define MVFX_GPU_LIVES 6

// The step's numbers (sim.wgsl's Step), in words: 84 of the emitter, its
// plan and its forces' head, 24 a force and 108 its field for the most
// forces an emitter has; 8 of its collision's head and response, 48 of
// its depth image, 32 a collider for the most it has; 32 of where a
// particle's values are among its words, 64 of the instance's
// parameters, 4 of the angular drag's factors over the plan, 16 of the
// velocity modules and 24 of the beam; 4 of the kill rules' and events' head, 20 of the
// spawning from events' and 20 a rule for the most an emitter has; then,
// unchanged from step to step and uploaded once, 16 a source, 8 of the
// frame's and mesh index's draws, 4 of the bounds' growth by size, 4 of
// how particles turn and move, 12 of the values over life, 20 of how it
// spawns from events, 16 of the shape, 4 of how births join ribbons, 36
// of the rigid particles, 40 of the units, but for their last 4 (their
// frames', which the host's frames change from step to step, uploaded each
// step for units held to them), and 4 of a particle liquid; then 24 of
// its water surface, uploaded each step for an emitter coupled to one.
// The layout plan budgets them at 16 KiB:
// they grow only for what the sim kernel's own entries read, a solver
// with kernels of its own carrying its own numbers.
#define MVFX_VALUES_FIRST     (84 + 132 * MVFX_MAX_FORCES + 56 + 64 * MVFX_MAX_COLLIDERS)
#define MVFX_VALUES_WORDS     140
#define MVFX_BEAM_WORDS       24
#define MVFX_KILLS_FIRST      (MVFX_VALUES_FIRST + MVFX_VALUES_WORDS)
#define MVFX_SPAWN_FIRST      (MVFX_KILLS_FIRST + 4)
#define MVFX_KILL_RULES_FIRST (MVFX_KILLS_FIRST + 24)
#define MVFX_SOURCES_FIRST    (MVFX_KILL_RULES_FIRST + 20 * MVFX_MAX_KILL_RULES)
#define MVFX_SPAWN_RULE_FIRST (MVFX_SOURCES_FIRST + 16 * MVFX_GPU_SOURCES + 28)
#define MVFX_SHAPE_FIRST      (MVFX_SPAWN_RULE_FIRST + 20)
#define MVFX_RIBBON_FIRST     (MVFX_SHAPE_FIRST + 16)
#define MVFX_RIGID_FIRST      (MVFX_RIBBON_FIRST + 4)
#define MVFX_UNITS_FIRST      (MVFX_RIGID_FIRST + 36)
#define MVFX_UNIT_TEARS       (MVFX_UNITS_FIRST + 32)
#define MVFX_UNIT_FRAMES      (MVFX_UNITS_FIRST + 36)
#define MVFX_LIQUID_HEAD      (MVFX_UNITS_FIRST + 40)
#define MVFX_WATER_HEAD       (MVFX_UNITS_FIRST + 44)
#define MVFX_WATER_COUPLING   24
#define MVFX_STEP_WORDS       (MVFX_WATER_HEAD + MVFX_WATER_COUPLING)
static_assert(MVFX_STEP_WORDS * 4 <= 16384, "the step's numbers within their 16 KiB");
// The words of a host frame for units held to the host's frames: its
// origin, then its rotation.
#define MVFX_FRAME_WORDS 8u
// The points of a unit pinned (sim.wgsl's UNIT_PIN_FIRST and UNIT_PIN_ROW).
#define MVFX_UNIT_PIN_FIRST 1u
#define MVFX_UNIT_PIN_ROW   2u

// The words a particle of an emitter takes on the GPU: its arrays, the
// death tick's two words and the step's word.
#define MVFX_GPU_OWN_WORDS 3

// The workgroups of 64 update takes for a capacity, striding over the
// particles: at most 65,535, the most a dispatch has along one axis on
// every device.
uint32_t mvfxUpdateGroups(int32_t capacity);

// The scan's blocks for a capacity (reduce's and scatter's workgroups):
// its live particles and twice its births, 1,024 elements a block; at most
// 49,152 for the largest capacity, 2^24.
uint32_t mvfxScanBlocks(int32_t capacity);

// The words of the step's numbers a plan's kernels read, in four spans
// from their first words: the head, the forces' slots and the fields of
// the forces it has; the collision's head, its depth image and the
// colliders it has; where a particle's values are, the instance's
// parameters, the angular factors and the velocity modules; the kill
// rules' and events' head, the spawning from events' and the rules it
// has; the beam's words, the values' last, only for a beam. The sources'
// block
// (MVFX_SOURCES_FIRST on) is written once. The rest keep what an earlier
// step left, never read.
typedef struct mvfxStepSpans
{
    uint32_t first[4];
    uint32_t count[4];
} mvfxStepSpans;

mvfxStepSpans mvfxStepSpansOf(const mvfxEmitterPlan* plan, bool beam);

// The values over life records read: alpha, the three sizes', each
// custom attribute's.
#define MVFX_RECORD_LIVES (4 + MVFX_MAX_CUSTOM_ATTRIBUTES)

// Where the baked tables are among the grids buffer's words (all ones for
// none): each source's curve and gradient, each value over life's curve
// the step reads; the colour's gradient over life and each value over
// life's curve records read.
typedef struct mvfxStepTables
{
    uint32_t sources[MVFX_GPU_SOURCES][2];
    uint32_t lives[MVFX_GPU_LIVES];
    uint32_t gradient;
    uint32_t recordLives[MVFX_RECORD_LIVES];
    // A mesh shape's region (rhi/src/mesh_image.h), all ones for none.
    uint32_t mesh;
    // The ribbons' width curve and colour gradient over their length.
    uint32_t ribbonWidth;
    uint32_t ribbonColor;
    // The pair force's strength over the distance.
    uint32_t pairStrength;
    // A rigid hull's vertices at a size of 1, and its faces: their
    // hullFaceCount + 1 starts, their corners' vertices, then their
    // planes at a size of 1 (four words a face).
    uint32_t hull;
    uint32_t hullFaces;
} mvfxStepTables;

// The words of a grid pass's numbers (grid.wgsl's Grid): 28 of the grid,
// its query and the anchor's shift, then 160 of steering and the pair
// force; what an emitter
// does with a neighbor grid: builds one (its particles are queried), finds
// its neighbors' attributes from its source's, the source being its own,
// steers or has a pair force (its query gathers more, and it may steer
// with no grid); the first of the clock's two words, then the shift's
// and steering's, the only ones a step changes.
#define MVFX_GRID_WORDS  188
#define MVFX_GRID_BUILDS 1u
#define MVFX_GRID_FINDS  2u
#define MVFX_GRID_OWN    4u
#define MVFX_GRID_STEERS 8u
#define MVFX_GRID_CLOCK  20u
#define MVFX_GRID_SHIFT  24u
#define MVFX_GRID_STEER  28u

// Whether steering flocks (grid/steer.wgsl's STEER_FLOCK).
#define MVFX_STEER_FLOCK 2u

// The mask of a grid's keys for a capacity, as the core's tables (the
// least power of two at least twice it, at least 2, less one).
uint32_t mvfxTableMask(int32_t capacity);

// Writes an emitter's contact numbers (contacts.wgsl's Contacts): its
// capacity, the most contacts a pass takes, the workgroups of the block
// passes and the others; the grid's mask; the radius scale, restitution
// and friction.
void mvfxContactNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                        uint32_t words[12]);

// Writes an emitter's plexus numbers (plexus.wgsl's Plexus): its
// capacity, the most links a particle owns and the emitter keeps, the
// workgroups of the block passes; the grid's mask, the places' first
// word, the workgroups of the particle passes; one over the distance and
// the distance squared, rounded as the core's.
void mvfxPlexusNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                       uint32_t words[12]);

// The words of an emitter's self-collision numbers (self.wgsl's
// SelfNumbers).
#define MVFX_SELF_WORDS 24

// Writes an emitter's self-collision numbers for a plan's step: its
// capacity, the most candidates, the workgroups of the block passes and
// the others; a unit's points and points across, the grid's mask, the
// attach flags' first word; the places', velocities' and alive words,
// the pins (none without anchors); the thickness, the step's seconds,
// gravity's travel over them and the friction; the directions across and
// down, the spacing and the thickness over the def's, rounded as the
// core's.
void mvfxSelfNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                     uint32_t words[MVFX_SELF_WORDS]);

// What an emitter does with a neighbor grid (MVFX_GRID_*, 0 for nothing),
// own when its source is itself.
uint32_t mvfxGridFlags(const mvfxEmitterView* view, bool own);

// Writes an emitter's grid numbers for a step (its tables' pair strength):
// its capacity and the
// source's (0 for a source of none), the workgroups of the block passes
// and the others; its mask, what it does, the most a query counts and the
// source's mask; the inverse of its cells and the source's, the radius
// squared; where a particle's place, velocity, death and neighbor
// attributes are; the clock at the plan's end; steering's (grid.wgsl's
// Steering). The source is its own for an emitter that finds none.
void mvfxGridNumbers(const mvfxEmitterView* view, const mvfxEmitterView* source,
                     const mvfxEmitterPlan* plan, const mvfxStepTables* tables, uint32_t mask,
                     uint32_t sourceMask, int32_t sourceCapacity, bool own,
                     uint32_t words[MVFX_GRID_WORDS]);

// An emitter's values over life records read, in the kernel's order.
void mvfxRecordLivesOf(const mvfxEmitterView* view, mvfxLifeView lives[MVFX_RECORD_LIVES]);

// An emitter's events buffer (sim.wgsl's events): EVENT_FACTS words a
// particle of its capacity, MVFX_SCAN_GROUPS a block of its scan (its
// events' four groups and its crossings of a water surface), then its
// events,
// MVFX_EVENT_WORDS each; the events' first word, and the words in all
// (before an emitter's spawn region, its units' tears' facts,
// MVFX_TEAR_FACTS a point: 11 an edge across, then down, and its
// crossings' facts).
#define MVFX_EVENT_FACTS 12u
#define MVFX_EVENT_WORDS 28u
#define MVFX_TEAR_FACTS  22u
// A particle's crossing of a water surface (sim/water.wgsl's
// noteCrossing), after the tears' facts; then the crossings' list, a head
// and two words a particle of the capacity.
#define MVFX_SCAN_GROUPS    5u
#define MVFX_CROSSING_FACTS 6u
#define MVFX_CROSSING_HEAD  8u
static inline uint64_t mvfxCrossingListWords(int32_t capacity)
{
    return MVFX_CROSSING_HEAD + 2u * (uint64_t)capacity;
}
uint32_t mvfxEventFirst(int32_t capacity);
uint64_t mvfxEventBufferWords(int32_t capacity, int32_t eventCapacity);

// The words of a record pass's numbers (records.wgsl's Records).
#define MVFX_RECORD_WORDS 488

// The orders a record pass writes in: the pool's, its reverse, or sorted
// by place.
#define MVFX_ORDER_POOL     0u
#define MVFX_ORDER_REVERSED 1u
#define MVFX_ORDER_SORTED   2u

// An emitter's order for its records.
uint32_t mvfxRecordOrder(const mvfxEmitterView* view);

// The first word of a record pass's order words: the order, the sort's
// measure and flip, the sort's workgroups.
#define MVFX_RECORD_ORDER 476

// The words of an emitter's events' numbers a plan changes, in two spans
// of MVFX_RECORD_SPAN words: the head (with the clock at the plan's end)
// and the map into the view's space. The rest are the emitter's and the
// part's, its order words among them (its capacity, no view), the same at
// every step.
#define MVFX_RECORD_SPAN 12
#define MVFX_RECORD_MAP  32

// The elements of a sort's block; the blocks of a capacity. An emitter
// that sorts keeps keys and indices twice over its blocks (4 words an
// element) and two sets of digit counts (256 words a block each); one
// drawn as ribbons keeps the chains' 10 words an element after them
// (ribbon_records.wgsl's scratch).
#define MVFX_SORT_BLOCK        1024u
#define MVFX_RIBBON_SORT_WORDS 14u
uint32_t mvfxSortBlocks(int32_t capacity);

// Whether a layout is one the record pass writes: its stride a multiple of
// 4 bytes, no ribbon or neighbor field.
bool mvfxIsRecordLayout(const mvfxLayoutView* layout);

// Writes a record pass's numbers: the particle buffer's capacity, the
// records' place, count and layout, the clock at the plan's end, where a
// particle's values are, the map into the view's space, the values over
// life and their tables, the sRGB steps, the order and what a sort
// measures from the eye.
void mvfxRecordNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                       const mvfxLayoutView* layout, const mvfxGpuRecordsDef* def,
                       const mvfxStepTables* tables, const float srgb[MVFX_SRGB_STEPS],
                       uint32_t words[MVFX_RECORD_WORDS]);

// The first word of a lights pass's own numbers, in the fields' place (it
// writes none): the light def's every, whether alpha scales, the
// workgroups of its block passes, unused; its intensity, radius and
// radius by size (binary32 bits), unused.
#define MVFX_RECORD_LIGHT 348

// Writes a lights pass's numbers: a record pass's without fields (its
// order unread: lights come in the pool's order), the light def's beside
// them.
void mvfxLightNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                      const mvfxGpuLightsDef* def, const mvfxStepTables* tables,
                      const float srgb[MVFX_SRGB_STEPS], uint32_t words[MVFX_RECORD_WORDS]);

// The words of a ribbon pass's own numbers (ribbon_records.wgsl's
// Ribbons), beside its record numbers.
#define MVFX_RIBBON_WORDS 44

// Whether a layout is one the ribbon pass writes: its stride a multiple
// of 4 bytes, no neighbor field.
bool mvfxIsRibbonLayout(const mvfxLayoutView* layout);

// Writes a ribbon pass's own numbers: where a particle's ribbon id and
// distance are, the texture coordinate along and the facing, the tile
// length, the scroll's offset at the plan's end, the width, the normal,
// the width's and colour's tables, the list's capacity and the counts'
// first word; the most pieces a segment is cut into, whether the bend
// decides them, whether the current ribbon ends at a pinned head and the
// plan's ribbon counter, the head's place in the particles' space, what
// is written, and the bend's cosines.
#define MVFX_SINK_POINTS 0u
#define MVFX_SINK_STRIPS 1u
#define MVFX_SINK_TUBES  2u
// Quads a particle (ribbon_records/sprites.wgsl), the sprite's numbers in
// the ribbons' place.
#define MVFX_SINK_SPRITES 3u
// Sheets' meshes (ribbon_records/sheets.wgsl), their numbers in the
// ribbons' place.
#define MVFX_SINK_SHEETS 4u

// What a ribbon pass writes: points with the list, strips or tubes (of
// sides, capped or not) with indices; the list's or the indices'
// capacity, the counts' first word.
typedef struct mvfxRibbonSink
{
    uint32_t kind;
    uint32_t sides;
    bool caps;
    int64_t capacity;
    uint32_t countWord;
} mvfxRibbonSink;

// Writes a sprite pass's numbers in a ribbon pass's place: its billboard,
// the view's up and the stretch, the indices' capacity and the counts'
// first word (the sink's).
void mvfxSpriteNumbers(const mvfxEmitterView* view, const mvfxView* frame,
                       const mvfxRibbonSink* sink, uint32_t words[MVFX_RIBBON_WORDS]);

// Writes a sheet pass's numbers in a ribbon pass's place: the torn edges'
// first word, the indices' capacity and the counts' first word (the
// sink's), a sheet's points across and in all.
void mvfxSheetNumbers(const mvfxEmitterView* view, const mvfxRibbonSink* sink,
                      uint32_t words[MVFX_RIBBON_WORDS]);

// The span of an emitter's plexus link elements, two a link of its most,
// in whole sort blocks: the links buffer holds the links, then the
// ribbon kernels' scratch arrays of this span (ribbon_records.wgsl's
// SCRATCH_*), fourteen spans in all.
uint32_t mvfxLinkSpan(int32_t maxLinks);

// Writes a plexus pass's ribbon numbers in link mode: no ribbon ids or
// distances, stretched, facing the eye, of the plexus width without a
// curve or gradient, one piece, no head; the fade's distance (0 for
// none); the sink's kind, capacity and counts' word, the links' span.
void mvfxLinkNumbers(const mvfxEmitterView* view, const mvfxRibbonSink* sink,
                     uint32_t words[MVFX_RIBBON_WORDS]);

void mvfxRibbonNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                       const mvfxStepTables* tables, const mvfxRibbonSink* sink,
                       uint32_t words[MVFX_RIBBON_WORDS]);

// Writes emitter's step numbers: the plan's counts and ticks, the
// emitter's shape, velocity and motion, the instance's places from the
// plan's anchor after, gravity in the particles' space, the drag factors
// over the plan for the drag every particle has (the emitter's and its
// winds' and velocity fields' without a falloff), its forces and its
// colliders, each one's grid as forceGrids and colliderGrids give it, and
// its depth image, its normals read from a texture when normals is set,
// the instance's parameters, the angular drag's factors over the plan,
// its velocity modules and kill rules, its sources, its bounds' growth by
// size, how its particles turn and move, and its values over life, each
// table where tables gives it.
void mvfxStepNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, int32_t emitter,
                     const uint32_t (*forceGrids)[5], const uint32_t (*colliderGrids)[5],
                     bool normals, const mvfxStepTables* tables, uint32_t words[MVFX_STEP_WORDS]);

// The words a host's particle takes after a plan's births (sim.wgsl's
// HostBirth).
#define MVFX_HOST_BIRTH_WORDS 16

// An emitter's spawn region in its events buffer, after its own events
// (sim.wgsl's spawn region): 16 words of its counts, then the source's
// events copied and seven arrays of the source's event capacity. Its
// first word, and its words.
uint32_t mvfxSpawnFirst(int32_t capacity, int32_t eventCapacity);
uint64_t mvfxSpawnWords(int32_t sourceEventCapacity);

// The spawn region's words of the source's events written and the first
// of its step's (after a split frame's steps before), and of its copied
// events.
#define MVFX_SPAWN_SOURCE 8u
#define MVFX_SPAWN_FROM   9u
#define MVFX_SPAWN_EVENTS 16u

// Writes an emitter's numbers for spawning from events, beside its step's
// (nothing for one that does not): the ticks whose events spawn, its
// plan's host births, its spawn region, the serials its plan's births
// took, the first vector of its staged plan after hostRecords host
// records, its births' room and the source's anchor less its own; how it
// spawns, the source's event capacity, its start delay and loop.
void mvfxSpawnNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                      const mvfxEmitterView* source, const mvfxEmitterPlan* sourcePlan,
                      int32_t hostRecords, uint32_t words[MVFX_STEP_WORDS]);

// Writes a plan's host births in the kernels' form: each one's place in
// the particles' space (a world-space emitter's an offset from the
// plan's anchor after) and what it gives; its velocity; its colour; its
// size.
void mvfxHostBirthWords(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, uint32_t* words);

// An emitter's sources (MVFX_SOURCE_SIZE and so on), a null one's kind
// none.
void mvfxSourcesOf(const mvfxEmitterView* view, mvfxSourceView sources[MVFX_GPU_SOURCES]);

// An emitter's values over life in the kernels' order.
void mvfxLivesOf(const mvfxEmitterView* view, mvfxLifeView lives[MVFX_GPU_LIVES]);

#endif
