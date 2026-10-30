// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "depth.h"

#include "direction.h"
#include "refusal.h"

#include <math.h>
#include <string.h>

static int32_t PixelBytes(mvfxDepthFormat format)
{
    return format == mvfx_depthUnorm16 ? 2 : 4;
}

// Whether rows of a size fit a buffer: each row at least a row of pixels,
// the last ending within the bytes.
static bool RowsFit(int32_t width, int32_t height, int32_t stride, int32_t pixelBytes,
                    int64_t byteCount)
{
    int64_t row = (int64_t)width * pixelBytes;
    return stride >= row && byteCount >= (int64_t)(height - 1) * stride + row;
}

// Swaps the row with the largest value in column c of a Gauss-Jordan
// table, from row c down, into row c; false when that value is 0.
static bool Pivot(double a[4][8], int32_t c)
{
    int32_t pivot = c;
    for (int32_t r = c + 1; r < 4; ++r)
    {
        pivot = fabs(a[r][c]) > fabs(a[pivot][c]) ? r : pivot;
    }
    if (!(fabs(a[pivot][c]) > 1e-300))
    {
        return false;
    }
    for (int32_t k = 0; k < 8; ++k)
    {
        double swap = a[c][k];
        a[c][k] = a[pivot][k];
        a[pivot][k] = swap;
    }
    return true;
}

// Makes column c of a Gauss-Jordan table 1 at row c and 0 elsewhere.
static void Eliminate(double a[4][8], int32_t c)
{
    double scale = 1.0 / a[c][c];
    for (int32_t k = 0; k < 8; ++k)
    {
        a[c][k] *= scale;
    }
    for (int32_t r = 0; r < 4; ++r)
    {
        double f = r == c ? 0.0 : a[r][c];
        for (int32_t k = 0; k < 8; ++k)
        {
            a[r][k] -= f * a[c][k];
        }
    }
}

// Inverts a 4x4 column-major matrix by Gauss-Jordan elimination with
// partial pivoting; false when it is singular or the inverse not finite.
static bool Invert(const double m[16], double out[16])
{
    double a[4][8];
    for (int32_t r = 0; r < 4; ++r)
    {
        for (int32_t c = 0; c < 4; ++c)
        {
            a[r][c] = m[c * 4 + r];
            a[r][c + 4] = r == c ? 1.0 : 0.0;
        }
    }
    for (int32_t c = 0; c < 4; ++c)
    {
        if (!Pivot(a, c))
        {
            return false;
        }
        Eliminate(a, c);
    }
    bool finite = true;
    for (int32_t r = 0; r < 4; ++r)
    {
        for (int32_t c = 0; c < 4; ++c)
        {
            out[c * 4 + r] = a[r][c + 4];
            finite = finite && isfinite(out[c * 4 + r]);
        }
    }
    return finite;
}

static bool IsPoseValid(const mvfxDepthImage* image)
{
    double square = 0.0;
    for (int32_t k = 0; k < 4; ++k)
    {
        if (!(fabsf(image->rotation[k]) <= 1.001f))
        {
            return false;
        }
        square += (double)image->rotation[k] * (double)image->rotation[k];
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        if (!(fabs(image->origin[k]) <= 0x1p52))
        {
            return false;
        }
    }
    return fabs(sqrt(square) - 1.0) <= 0.001;
}

mvfxResult mvfxPrepareDepthView(const mvfxDepthImage* image, bool planned, mvfxDepthView* out)
{
    bool bare = planned && image->pixels == nullptr;
    if ((image->pixels == nullptr && !bare) || image->format > mvfx_depthUnorm24 ||
        image->width < 1 || image->height < 1)
    {
        return mvfx_errorInvalid;
    }
    // The sizes bound the rows measured below.
    if (image->width > MVFX_MAX_DEPTH_SIZE || image->height > MVFX_MAX_DEPTH_SIZE)
    {
        return mvfx_errorLimit;
    }
    bool sized = bare || RowsFit(image->width, image->height, image->stride,
                                 PixelBytes(image->format), image->byteCount);
    bool normals =
        image->normals == nullptr ||
        RowsFit(image->width, image->height, image->normalStride, 12, image->normalByteCount);
    if (!sized || !normals || !IsPoseValid(image))
    {
        return mvfx_errorInvalid;
    }
    for (int32_t k = 0; k < 16; ++k)
    {
        if (!isfinite(image->projection[k]))
        {
            return mvfx_errorInvalid;
        }
        out->projection[k] = (double)image->projection[k];
    }
    out->image = *image;
    return mvfxRequire(Invert(out->projection, out->inverse));
}

// The column-major matrix m times (v, 1), in binary64.
static void Transform(const double m[16], const double v[3], double out[4])
{
    for (int32_t r = 0; r < 4; ++r)
    {
        out[r] = m[r] * v[0] + m[4 + r] * v[1] + m[8 + r] * v[2] + m[12 + r];
    }
}

// A place in the particles' space in the view's space.
static void ToView(const mvfxResolvedDepth* depth, const float place[3], double out[3])
{
    const mvfxDepthImage* image = &depth->view->image;
    double world[3];
    mvfxUnmapPoint(&depth->map, place, world);
    double d[3] = {world[0] - image->origin[0], world[1] - image->origin[1],
                   world[2] - image->origin[2]};
    const float* q = image->rotation;
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    mvfxRotateDouble(inverse, d, out);
}

// A place in the view's space projected: its device x, y and depth and
// its pixel; false outside the image or behind the camera.
typedef struct Projected
{
    double ndc[3];
    int32_t column;
    int32_t row;
} Projected;

static bool Project(const mvfxDepthView* view, const double v[3], Projected* out)
{
    double clip[4];
    Transform(view->projection, v, clip);
    if (!(clip[3] > 0.0))
    {
        return false;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out->ndc[k] = clip[k] / clip[3];
    }
    if (!(fabs(out->ndc[0]) <= 1.0 && fabs(out->ndc[1]) <= 1.0 && out->ndc[2] >= 0.0 &&
          out->ndc[2] <= 1.0))
    {
        return false;
    }
    int32_t w = view->image.width;
    int32_t h = view->image.height;
    out->column = (int32_t)((out->ndc[0] * 0.5 + 0.5) * (double)w);
    out->row = (int32_t)((0.5 - out->ndc[1] * 0.5) * (double)h);
    out->column = out->column < w ? out->column : w - 1;
    out->row = out->row < h ? out->row : h - 1;
    return true;
}

// A pixel's device depth; false where it holds no surface.
static bool DepthAt(const mvfxDepthView* view, int32_t column, int32_t row, double* out)
{
    const mvfxDepthImage* image = &view->image;
    if (column < 0 || row < 0 || column >= image->width || row >= image->height)
    {
        return false;
    }
    const unsigned char* at = (const unsigned char*)image->pixels + (int64_t)row * image->stride +
                              (int64_t)column * PixelBytes(image->format);
    double d = 0.0;
    if (image->format == mvfx_depthFloat32)
    {
        float f = 0.0f;
        memcpy(&f, at, sizeof f);
        d = (double)f;
    }
    else if (image->format == mvfx_depthUnorm16)
    {
        uint16_t u = 0;
        memcpy(&u, at, sizeof u);
        d = (double)u / 65535.0;
    }
    else
    {
        uint32_t u = 0;
        memcpy(&u, at, sizeof u);
        d = (double)(u & 0xFFFFFFu) / 16777215.0;
    }
    *out = d;
    double far = image->reversed ? 0.0 : 1.0;
    return d >= 0.0 && d <= 1.0 && d != far;
}

// The view's point at device x, y and depth; false at infinity.
static bool Unproject(const mvfxDepthView* view, double x, double y, double d, double out[3])
{
    const double ndc[3] = {x, y, d};
    double h[4];
    Transform(view->inverse, ndc, h);
    if (!(fabs(h[3]) > 1e-300))
    {
        return false;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = h[k] / h[3];
    }
    return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}

// A pixel's surface point, at its center; false where it has none.
static bool PixelPoint(const mvfxDepthView* view, int32_t column, int32_t row, double out[3])
{
    double d = 0.0;
    if (!DepthAt(view, column, row, &d))
    {
        return false;
    }
    double x = ((double)column + 0.5) / (double)view->image.width * 2.0 - 1.0;
    double y = 1.0 - ((double)row + 0.5) / (double)view->image.height * 2.0;
    return Unproject(view, x, y, d, out);
}

static double Length(const double v[3])
{
    return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// The shorter of the differences to a pixel's two neighbors along an
// axis, toward increasing columns or rows; false with neither.
static bool Across(const mvfxDepthView* view, const double center[3], int32_t column, int32_t row,
                   int32_t dc, int32_t dr, double out[3])
{
    double after[3] = {0.0, 0.0, 0.0};
    double before[3] = {0.0, 0.0, 0.0};
    bool hasAfter = PixelPoint(view, column + dc, row + dr, after);
    bool hasBefore = PixelPoint(view, column - dc, row - dr, before);
    double a[3];
    double b[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        a[k] = after[k] - center[k];
        b[k] = center[k] - before[k];
    }
    if (!hasAfter && !hasBefore)
    {
        return false;
    }
    bool useAfter = hasAfter && (!hasBefore || Length(a) <= Length(b));
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = useAfter ? a[k] : b[k];
    }
    return true;
}

// The view's direction toward the camera along the ray at device x, y.
static void TowardCamera(const mvfxDepthView* view, double x, double y, double out[3])
{
    double nearer[3] = {0.0, 0.0, 1.0};
    double farther[3] = {0.0, 0.0, 0.0};
    double dn = view->image.reversed ? 0.75 : 0.25;
    double df = view->image.reversed ? 0.25 : 0.75;
    if (Unproject(view, x, y, dn, nearer) && Unproject(view, x, y, df, farther))
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out[k] = nearer[k] - farther[k];
        }
        return;
    }
    out[0] = 0.0;
    out[1] = 0.0;
    out[2] = 1.0;
}

// The surface's normal at a pixel in the view's space, toward the camera:
// the normal image's, or the cross of the shorter neighbor differences.
static void NormalAt(const mvfxDepthView* view, const Projected* p, double out[3])
{
    double toward[3] = {0.0, 0.0, 1.0};
    TowardCamera(view, p->ndc[0], p->ndc[1], toward);
    const mvfxDepthImage* image = &view->image;
    double n[3] = {toward[0], toward[1], toward[2]};
    double center[3] = {0.0, 0.0, 0.0};
    double dx[3] = {0.0, 0.0, 0.0};
    double dy[3] = {0.0, 0.0, 0.0};
    if (image->normals != nullptr)
    {
        const unsigned char* at = (const unsigned char*)image->normals +
                                  (int64_t)p->row * image->normalStride + (int64_t)p->column * 12;
        float given[3] = {0.0f, 0.0f, 0.0f};
        memcpy(given, at, sizeof given);
        for (int32_t k = 0; k < 3; ++k)
        {
            n[k] = isfinite(given[k]) ? (double)given[k] : 0.0;
        }
    }
    else if (PixelPoint(view, p->column, p->row, center) &&
             Across(view, center, p->column, p->row, 1, 0, dx) &&
             Across(view, center, p->column, p->row, 0, 1, dy))
    {
        n[0] = dx[1] * dy[2] - dx[2] * dy[1];
        n[1] = dx[2] * dy[0] - dx[0] * dy[2];
        n[2] = dx[0] * dy[1] - dx[1] * dy[0];
    }
    double length = Length(n);
    if (!(length > 0.0) || !isfinite(length))
    {
        n[0] = toward[0];
        n[1] = toward[1];
        n[2] = toward[2];
        length = Length(n);
    }
    double sign = n[0] * toward[0] + n[1] * toward[1] + n[2] * toward[2] < 0.0 ? -1.0 : 1.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = sign * n[k] / length;
    }
}

bool mvfxDepthSees(const mvfxResolvedDepth* depth, const float place[3])
{
    double v[3];
    ToView(depth, place, v);
    Projected p = {0};
    return Project(depth->view, v, &p);
}

// Whether a place meets the image: along its view ray, within the
// particle's radius in front of the surface or less than the thickness
// behind it; the surface's point on that ray in the view's space.
static bool Meets(const mvfxResolvedDepth* depth, const float place[3], Projected* p,
                  double surface[3])
{
    const mvfxDepthView* view = depth->view;
    double v[3];
    ToView(depth, place, v);
    double d = 0.0;
    if (!Project(view, v, p) || !DepthAt(view, p->column, p->row, &d) ||
        !Unproject(view, p->ndc[0], p->ndc[1], d, surface))
    {
        return false;
    }
    double toward[3];
    TowardCamera(view, p->ndc[0], p->ndc[1], toward);
    double length = Length(toward);
    // How far behind the surface it is, along the ray away from the camera.
    double behind = -((v[0] - surface[0]) * toward[0] + (v[1] - surface[1]) * toward[1] +
                      (v[2] - surface[2]) * toward[2]) /
                    length;
    double radius = (double)depth->radius / (double)depth->map.sigma;
    return behind > -radius && behind < (double)depth->thickness;
}

// The samples a segment takes: one a pixel it crosses in the image and
// one each half thickness of its length, so a sample falls within the
// thickness of any surface it passes (at least one, at most the
// emitter's).
static int32_t SamplesFor(const mvfxResolvedDepth* depth, const float s[3], const float e[3])
{
    double vs[3];
    double ve[3];
    ToView(depth, s, vs);
    ToView(depth, e, ve);
    double d[3] = {ve[0] - vs[0], ve[1] - vs[1], ve[2] - vs[2]};
    double wanted = Length(d) / (0.5 * (double)depth->thickness);
    Projected ps = {0};
    Projected pe = {0};
    if (!Project(depth->view, vs, &ps) || !Project(depth->view, ve, &pe))
    {
        wanted = (double)depth->samples;
    }
    else
    {
        double pixels =
            fmax(fabs((double)(pe.column - ps.column)), fabs((double)(pe.row - ps.row)));
        wanted = fmax(wanted, pixels);
    }
    return (int32_t)fmin(fmax(ceil(wanted), 1.0), (double)depth->samples);
}

// The halvings that find a crossing between two samples.
#define DEPTH_HALVINGS 10

// Whether the place at a share of the segment s to e meets the image.
static bool MeetsAt(const mvfxResolvedDepth* depth, const float s[3], const float e[3], float at,
                    Projected* p, double surface[3])
{
    float place[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        place[k] = s[k] + at * (e[k] - s[k]);
    }
    return Meets(depth, place, p, surface);
}

// The first share of the segment that meets the image, sampled and then
// halved toward from the sample before; -1 when none does.
static float FirstMeeting(const mvfxResolvedDepth* depth, const float s[3], const float e[3],
                          Projected* p, double surface[3])
{
    int32_t n = SamplesFor(depth, s, e);
    for (int32_t i = 0; i <= n; ++i)
    {
        float hi = (float)i / (float)n;
        if (!MeetsAt(depth, s, e, hi, p, surface))
        {
            continue;
        }
        float lo = i > 0 ? (float)(i - 1) / (float)n : hi;
        for (int32_t halving = 0; halving < DEPTH_HALVINGS && i > 0; ++halving)
        {
            float middle = 0.5f * (lo + hi);
            Projected q = {0};
            double at[3];
            if (MeetsAt(depth, s, e, middle, &q, at))
            {
                hi = middle;
                *p = q;
                surface[0] = at[0];
                surface[1] = at[1];
                surface[2] = at[2];
            }
            else
            {
                lo = middle;
            }
        }
        return hi;
    }
    return -1.0f;
}

bool mvfxDepthHit(const mvfxResolvedDepth* depth, const float s[3], const float e[3], float* t,
                  float point[3], float normal[3])
{
    Projected p = {0};
    double surface[3] = {0.0, 0.0, 0.0};
    float at = FirstMeeting(depth, s, e, &p, surface);
    if (at < 0.0f)
    {
        return false;
    }
    const mvfxDepthImage* image = &depth->view->image;
    double viewNormal[3];
    NormalAt(depth->view, &p, viewNormal);
    float n[3] = {(float)viewNormal[0], (float)viewNormal[1], (float)viewNormal[2]};
    float worldNormal[3];
    mvfxRotate(image->rotation, n, worldNormal);
    double turned[3];
    mvfxRotateDouble(image->rotation, surface, turned);
    double world[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        world[k] = turned[k] + image->origin[k];
    }
    mvfxMapPointDouble(&depth->map, world, point);
    mvfxMapVector(&depth->map, worldNormal, 1.0f, normal);
    (void)mvfxNormalize(normal);
    for (int32_t k = 0; k < 3; ++k)
    {
        point[k] += depth->radius * normal[k];
    }
    *t = at;
    return true;
}

// The column-major product a b of 4x4 matrices, in binary64.
static void Multiply(const double a[16], const double b[16], double out[16])
{
    for (int32_t c = 0; c < 4; ++c)
    {
        for (int32_t r = 0; r < 4; ++r)
        {
            out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] +
                             a[12 + r] * b[c * 4 + 3];
        }
    }
}

// The Hamilton product a b of quaternions (b's turn, then a's), in
// binary64.
static void Compose(const float a[4], const float b[4], double out[4])
{
    double ax = (double)a[0];
    double ay = (double)a[1];
    double az = (double)a[2];
    double aw = (double)a[3];
    double bx = (double)b[0];
    double by = (double)b[1];
    double bz = (double)b[2];
    double bw = (double)b[3];
    out[0] = aw * bx + ax * bw + ay * bz - az * by;
    out[1] = aw * by - ax * bz + ay * bw + az * bx;
    out[2] = aw * bz + ax * by - ay * bx + az * bw;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
}

void mvfxPlanDepth(const mvfxResolvedDepth* depth, const mvfxCollision* collision,
                   mvfxPlannedDepth* out)
{
    const mvfxDepthView* view = depth->view;
    const mvfxDepthImage* image = &view->image;
    const mvfxSpaceMap* map = &depth->map;
    // The particles' space to the view's (ToView), as a matrix: each axis
    // turned back by the map and the view and over sigma; the origin's
    // place.
    const float* q = map->rotation;
    const float back[4] = {-q[0], -q[1], -q[2], q[3]};
    const float* r = image->rotation;
    const float viewBack[4] = {-r[0], -r[1], -r[2], r[3]};
    double toView[16] = {0.0};
    for (int32_t c = 0; c < 3; ++c)
    {
        double axis[3] = {c == 0 ? 1.0 : 0.0, c == 1 ? 1.0 : 0.0, c == 2 ? 1.0 : 0.0};
        double world[3];
        double turned[3];
        mvfxRotateDouble(back, axis, world);
        mvfxRotateDouble(viewBack, world, turned);
        for (int32_t k = 0; k < 3; ++k)
        {
            toView[c * 4 + k] = turned[k] / (double)map->sigma;
        }
    }
    double shift[3] = {-map->after[0], -map->after[1], -map->after[2]};
    double world[3];
    mvfxRotateDouble(back, shift, world);
    for (int32_t k = 0; k < 3; ++k)
    {
        world[k] = world[k] / (double)map->sigma + map->before[k] - image->origin[k];
    }
    double origin[3];
    mvfxRotateDouble(viewBack, world, origin);
    for (int32_t k = 0; k < 3; ++k)
    {
        toView[12 + k] = origin[k];
    }
    toView[15] = 1.0;
    double toClip[16];
    double fromClip[16] = {0.0};
    Multiply(view->projection, toView, toClip);
    // The projection is invertible and the map is a turn and a scale: the
    // product inverts.
    (void)Invert(toClip, fromClip);
    double turn[4];
    Compose(map->rotation, image->rotation, turn);
    *out = (mvfxPlannedDepth){
        .enable = true,
        .width = image->width,
        .height = image->height,
        .reversed = image->reversed,
        .radius = depth->radius,
        .thickness = depth->thickness * map->sigma,
        .samples = depth->samples,
        .killOutsideView = collision->killOutsideView,
    };
    for (int32_t k = 0; k < 16; ++k)
    {
        out->toClip[k] = (float)toClip[k];
        out->fromClip[k] = (float)fromClip[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        out->normalTurn[k] = (float)turn[k];
    }
}
