#pragma once

// The relief (bump map) of a texture page as the PowerVR takes it, shared by
// the game (pvr.c, for the pages the disc has none of) and the tool that makes
// them for the disc (tools/dreamcast/texvq.c): plain C, nothing of KallistiOS.
//
// The relief is at half the page's size, each texel the way its surface faces.
// A texture's brightness stands for its height, which is about right for rock,
// brick and earth; see-through texels lie flat.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// 128 for the game's own making of a page's relief; the tool that makes them
// for the disc includes this with 256 (a relief texel for each of the page's).
#ifndef DC_BUMP_SIZE
#define DC_BUMP_SIZE 128
#endif
#define DC_BUMP_RATIO (DC_BUMP_SIZE / 128)
#if DC_BUMP_SIZE == 256
#define DC_BUMP_SHIFT 8
#else
#define DC_BUMP_SHIFT 9
#endif
// How steep a step of the whole brightness range makes the surface.
#define DC_BUMP_SLOPE (12.0f * DC_BUMP_RATIO)
// The PowerVR's shade goes with the sine of the slope's lean, so the gentle
// slopes a texture's brightness gives all but vanish: they are made steeper
// (the lean, as the length of the normal's slope, times this for the small
// ones) and the steep ones are held under a cap, so that they do not run to
// the edge of the range. len' = cap * tanh(gain * len / cap).
#define DC_BUMP_LEAN_GAIN 1.8f
#define DC_BUMP_LEAN_CAP 3.0f
// A texel that stands straight up: see-through, or none of the page's.
#define DC_BUMP_FLAT 0xFF00
#define DC_BUMP_PI 3.14159265358979323846

// One texel of the half page from the 2x2 it covers: the colour of the solid
// ones, and as alpha the most solid. Strokes a texel wide keep all their
// length so (an average would cut the thin ones and the diagonals).
static inline uint16_t DC_Bump_HalfTexel(
    const uint16_t *const t, const bool argb4444)
{
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    uint32_t weight = 0;
    if (argb4444) {
        uint32_t a = 0;
        for (int32_t k = 0; k < 4; k++) {
            const uint32_t ta = t[k] >> 12;
            a = ta > a ? ta : a;
            weight += ta;
            r += ((t[k] >> 8) & 15) * ta;
            g += ((t[k] >> 4) & 15) * ta;
            b += (t[k] & 15) * ta;
        }
        if (weight == 0) {
            return 0;
        }
        return (uint16_t)((a << 12) | ((r / weight) << 8)
                          | ((g / weight) << 4) | (b / weight));
    }
    for (int32_t k = 0; k < 4; k++) {
        if ((t[k] & 0x8000) != 0) {
            weight++;
            r += (t[k] >> 10) & 31;
            g += (t[k] >> 5) & 31;
            b += t[k] & 31;
        }
    }
    if (weight == 0) {
        return 0;
    }
    return (uint16_t)(0x8000 | ((r / weight) << 10) | ((g / weight) << 5)
                      | (b / weight));
}

// A half-page texel's height: its brightness, 0 for see-through (no texel is
// wholly black and solid). Sets *luma to the brightness of a solid one.
static inline uint8_t DC_Bump_Height(
    const uint16_t t, const bool argb4444, int32_t *const luma)
{
    int32_t r, g, b;
    bool solid;
    if (argb4444) {
        solid = (t >> 12) >= 8;
        r = ((t >> 8) & 15) * 17;
        g = ((t >> 4) & 15) * 17;
        b = (t & 15) * 17;
    } else {
        solid = (t & 0x8000) != 0;
        r = ((t >> 10) & 31) * 255 / 31;
        g = ((t >> 5) & 31) * 255 / 31;
        b = (t & 31) * 255 / 31;
    }
    *luma = (r * 77 + g * 151 + b * 28) >> 8;
    return solid ? (uint8_t)(*luma > 1 ? *luma : 1) : 0;
}

// The heights stretched to the whole range, lo and hi being the darkest and
// brightest solid texels: TR2's rock is dark and low in contrast, and would
// come out all but flat.
static inline void DC_Bump_Stretch(
    uint8_t *const height, const int32_t lo, const int32_t hi)
{
    if (hi <= lo) {
        return;
    }
    for (int32_t i = 0; i < DC_BUMP_SIZE * DC_BUMP_SIZE; i++) {
        if (height[i] != 0) {
            const int32_t h = height[i] > lo ? height[i] : lo;
            height[i] = (uint8_t)(1 + (h - lo) * 254 / (hi - lo));
        }
    }
}

// The heights as the relief's detail alone. A texture's brightness holds more
// than its shape: a shade painted across a block, the light baked into a
// wall, read as a ramp the relief would lean a whole face on. What stays is
// the fine detail (joints, cracks, pits: a smoothing of one texel off a
// smoothing of four) and, half as strong, the middle sizes (four off eight);
// anything broader goes. The band is stretched so that the page's strongest
// detail is as steep as it was, whatever its contrast. Done over each
// texture's own texels (owner), so that one texture's edge is not the next's.
#define DC_BUMP_DETAIL_FINE (1 * DC_BUMP_RATIO)
#define DC_BUMP_DETAIL_MID (4 * DC_BUMP_RATIO)
#define DC_BUMP_DETAIL_COARSE (12 * DC_BUMP_RATIO)
#define DC_BUMP_DETAIL_MID_WEIGHT 1.0f
// The band's p99 magnitude, in height steps, after the stretch.
#define DC_BUMP_DETAIL_REACH 112.0f
#define DC_BUMP_DETAIL_GAIN_MIN 0.5f
#define DC_BUMP_DETAIL_GAIN_MAX 6.0f
// The scratch the detail works in: four pages of heights.
#define DC_BUMP_DETAIL_SCRATCH (6 * DC_BUMP_SIZE * DC_BUMP_SIZE)

// A box smoothing of radius r over the texels of one texture only (and
// of the solid ones), as two passes along the rows and the columns. Where the
// window runs past the texture's edge, the missing texels are the ones the
// texture's own slope would give (a point reflection about the edge texel):
// with only the texels inside, the average leaned inwards and the band
// that follows read as a ridge along every edge, a dark rim when lit (TR1's
// snow).
static inline void DC_Bump_Blur(
    const uint8_t *const src, const uint16_t *const owner, uint8_t *const dst,
    uint8_t *const tmp, const int32_t r)
{
    const int32_t n = DC_BUMP_SIZE;
    for (int32_t pass = 0; pass < 2; pass++) {
        const uint8_t *const in = pass == 0 ? src : tmp;
        uint8_t *const out = pass == 0 ? tmp : dst;
        const int32_t step = pass == 0 ? 1 : n;
        const int32_t across = pass == 0 ? n : 1;
        for (int32_t line = 0; line < n; line++) {
            const int32_t base = line * across;
            // The line: the solid texels, the runs of one texture, and the
            // running sums, so that a window wholly inside a run is two reads.
            int32_t sum[DC_BUMP_SIZE + 1];
            int32_t lo[DC_BUMP_SIZE];
            int32_t hi[DC_BUMP_SIZE];
            sum[0] = 0;
            for (int32_t p = 0; p < n; p++) {
                const int32_t at = base + p * step;
                const bool solid = src[at] != 0;
                sum[p + 1] = sum[p] + (solid ? in[at] : 0);
                lo[p] = p;
                if (solid && p > 0 && src[at - step] != 0
                    && owner[at - step] == owner[at]) {
                    lo[p] = lo[p - 1];
                }
            }
            for (int32_t p = n - 1; p >= 0; p--) {
                const int32_t at = base + p * step;
                hi[p] = p;
                if (src[at] != 0 && p + 1 < n && src[at + step] != 0
                    && owner[at + step] == owner[at]) {
                    hi[p] = hi[p + 1];
                }
            }
            for (int32_t p = 0; p < n; p++) {
                const int32_t at = base + p * step;
                if (src[at] == 0) {
                    out[at] = 0;
                    continue;
                }
                const int32_t left = lo[p];
                const int32_t right = hi[p];
                if (p - r >= left && p + r <= right) {
                    out[at] =
                        (uint8_t)((sum[p + r + 1] - sum[p - r] + r) / (2 * r + 1));
                    continue;
                }
                // At an edge of the texture: the window's missing texels are
                // the point reflection about the edge texel.
                int32_t total = 0;
                for (int32_t k = -r; k <= r; k++) {
                    const int32_t q = p + k;
                    int32_t v;
                    if (q >= left && q <= right) {
                        v = in[base + q * step];
                    } else if (q < left) {
                        const int32_t m = 2 * left - q;
                        v = m <= right ? 2 * in[base + left * step]
                                - in[base + m * step]
                                       : in[base + left * step];
                    } else {
                        const int32_t m = 2 * right - q;
                        v = m >= left ? 2 * in[base + right * step]
                                - in[base + m * step]
                                      : in[base + right * step];
                    }
                    total += v < 0 ? 0 : v > 255 ? 255 : v;
                }
                out[at] = (uint8_t)((total + r) / (2 * r + 1));
            }
        }
    }
}

// A grey-scale dilation (max) or erosion (min) of radius r over the texels of one
// texture, as two passes (a square window): the building block of the valleys.
static inline void DC_Bump_Morph(
    const uint8_t *const src, const uint16_t *const owner, uint8_t *const dst,
    uint8_t *const tmp, const int32_t r, const bool dilate)
{
    const int32_t n = DC_BUMP_SIZE;
    for (int32_t pass = 0; pass < 2; pass++) {
        const uint8_t *const in = pass == 0 ? src : tmp;
        uint8_t *const out = pass == 0 ? tmp : dst;
        const int32_t step = pass == 0 ? 1 : n;
        const int32_t across = pass == 0 ? n : 1;
        for (int32_t line = 0; line < n; line++) {
            const int32_t base = line * across;
            for (int32_t p = 0; p < n; p++) {
                const int32_t at = base + p * step;
                if (src[at] == 0) {
                    out[at] = 0;
                    continue;
                }
                int32_t best = in[at];
                for (int32_t k = -r; k <= r; k++) {
                    const int32_t q = p + k;
                    if (q < 0 || q >= n) {
                        continue;
                    }
                    const int32_t j = base + q * step;
                    if (src[j] == 0 || owner[j] != owner[at]) {
                        continue;
                    }
                    const int32_t v = in[j];
                    best = dilate ? (v > best ? v : best) : (v < best ? v : best);
                }
                out[at] = (uint8_t)best;
            }
        }
    }
}

// The valleys, the soft undulation and the fine detail of a texture, in one
// height (see the constants): what a relief of it is made from. A texture's
// brightness is no height as it stands: a weathered rock's mottling is stain,
// not pits, and its contours (the worms) are all the relief the brightness
// gives it. So:
// - the VALLEYS, the dark strokes narrower than DC_BUMP_VALLEY_R (the joints
//   of bricks and flagstones, cracks, the grooves of a carving) lie sunken, by
//   a grey-scale closing less the brightness; what they enclose stands;
// - the SOFT undulation, the brightness smoothed to a few texels less the
//   broad shading, gives every texture some relief, even one that is all
//   mottling, with no contours to wind about;
// - the FINE detail, the brightness itself less the broad shading, at a lower
//   weight (the carved edges and the bright strokes are in it; so is the
//   speckle, which the coherence of the slopes then thins out).
#define DC_BUMP_VALLEY_R (2 * DC_BUMP_RATIO)
#define DC_BUMP_SOFT_R (3 * DC_BUMP_RATIO)
#define DC_BUMP_W_VALLEY 1.3f
#define DC_BUMP_W_SOFT 1.0f
#define DC_BUMP_W_FINE 0.30f
// A 3x3 median over the texels of one texture: the specks of a dithered
// texture, a texel wide, go; a joint two texels wide or a carved edge stays.
static inline void DC_Bump_Median(
    const uint8_t *const src, const uint16_t *const owner, uint8_t *const dst)
{
    const int32_t n = DC_BUMP_SIZE;
    for (int32_t y = 0; y < n; y++) {
        for (int32_t x = 0; x < n; x++) {
            const int32_t at = y * n + x;
            if (src[at] == 0) {
                dst[at] = 0;
                continue;
            }
            uint8_t v[9];
            int32_t m = 0;
            for (int32_t dy = -1; dy <= 1; dy++) {
                for (int32_t dx = -1; dx <= 1; dx++) {
                    const int32_t xx = x + dx;
                    const int32_t yy = y + dy;
                    if (xx < 0 || xx >= n || yy < 0 || yy >= n) {
                        continue;
                    }
                    const int32_t j = yy * n + xx;
                    if (src[j] != 0 && owner[j] == owner[at]) {
                        v[m++] = src[j];
                    }
                }
            }
            // The middle of m values, by an insertion sort.
            for (int32_t i = 1; i < m; i++) {
                const uint8_t key = v[i];
                int32_t j = i - 1;
                while (j >= 0 && v[j] > key) {
                    v[j + 1] = v[j];
                    j--;
                }
                v[j + 1] = key;
            }
            dst[at] = v[m / 2];
        }
    }
}

static inline void DC_Bump_Detail(
    uint8_t *const height, const uint16_t *const owner, uint8_t *const scratch)
{
    const int32_t count = DC_BUMP_SIZE * DC_BUMP_SIZE;
    uint8_t *const coarse = scratch;
    uint8_t *const soft = scratch + count;
    uint8_t *const closed = scratch + 2 * count;
    uint8_t *const tmp = scratch + 3 * count;
    uint8_t *const eroded = scratch + 4 * count;
    // The texture without its specks: all that follows reads this.
    uint8_t *const med = scratch + 5 * count;
    DC_Bump_Median(height, owner, med);
    DC_Bump_Blur(med, owner, coarse, tmp, DC_BUMP_DETAIL_COARSE);
    DC_Bump_Blur(med, owner, soft, tmp, DC_BUMP_SOFT_R);
    // The closing: a dilation, then an erosion of it.
    DC_Bump_Morph(med, owner, closed, tmp, DC_BUMP_VALLEY_R, true);
    DC_Bump_Morph(closed, owner, eroded, tmp, DC_BUMP_VALLEY_R, false);
    int16_t *const d = (int16_t *)malloc(sizeof(int16_t) * (size_t)count);
    if (d == NULL) {
        return;
    }
    int32_t hist[512] = {};
    int32_t solid = 0;
    for (int32_t i = 0; i < count; i++) {
        d[i] = 0;
        if (height[i] == 0) {
            continue;
        }
        const float valley = (float)((int32_t)eroded[i] - (int32_t)med[i]);
        const float soft_t = (float)((int32_t)soft[i] - (int32_t)coarse[i]);
        const float fine = (float)((int32_t)med[i] - (int32_t)coarse[i]);
        const float v = DC_BUMP_W_FINE * fine + DC_BUMP_W_SOFT * soft_t
            - DC_BUMP_W_VALLEY * valley;
        d[i] = (int16_t)(v < -255.0f ? -255 : v > 255.0f ? 255 : v);
        const int32_t m = d[i] < 0 ? -d[i] : d[i];
        hist[m]++;
        solid++;
    }
    int32_t p99 = 1;
    for (int32_t m = 0, seen = 0; m < 512; m++) {
        seen += hist[m];
        if (seen * 100 >= solid * 99) {
            p99 = m > 1 ? m : 1;
            break;
        }
    }
    float gain = DC_BUMP_DETAIL_REACH / (float)p99;
    gain = gain < DC_BUMP_DETAIL_GAIN_MIN ? DC_BUMP_DETAIL_GAIN_MIN : gain;
    gain = gain > DC_BUMP_DETAIL_GAIN_MAX ? DC_BUMP_DETAIL_GAIN_MAX : gain;
    for (int32_t i = 0; i < count; i++) {
        if (height[i] == 0) {
            continue;
        }
        float h = 128.0f + gain * (float)d[i];
        h = h < 1.0f ? 1.0f : h > 255.0f ? 255.0f : h;
        height[i] = (uint8_t)(h + 0.5f);
    }
    free(d);
}

// Marks the texels of the relief a texture covers as its own (own: its number
// plus one), from its corners in 1/256 of a texel of the whole page.
static inline void DC_Bump_Own(
    uint16_t *const owner, const uint16_t own, const uint16_t *const u,
    const uint16_t *const v, const int32_t corners)
{
    int32_t x0 = DC_BUMP_SIZE;
    int32_t y0 = DC_BUMP_SIZE;
    int32_t x1 = -1;
    int32_t y1 = -1;
    for (int32_t j = 0; j < corners; j++) {
        const int32_t x = u[j] >> DC_BUMP_SHIFT;
        const int32_t y = v[j] >> DC_BUMP_SHIFT;
        x0 = x < x0 ? x : x0;
        y0 = y < y0 ? y : y0;
        x1 = x > x1 ? x : x1;
        y1 = y > y1 ? y : y1;
    }
    x1 = x1 < DC_BUMP_SIZE - 1 ? x1 : DC_BUMP_SIZE - 1;
    y1 = y1 < DC_BUMP_SIZE - 1 ? y1 : DC_BUMP_SIZE - 1;
    for (int32_t y = y0; y <= y1; y++) {
        for (int32_t x = x0; x <= x1; x++) {
            owner[y * DC_BUMP_SIZE + x] = own;
        }
    }
}

// A texel's height in a relief being made; one that is see-through, off the
// page or of another texture on it (owner) stands at the height of the texel
// asking, h: the step to the next texture on the page is no slope of this
// one's, and showed as a dark rim round it when a flash lit it (TR1's snow).
static inline int32_t DC_Bump_HeightAt(
    const uint8_t *const height, const uint16_t *const owner, const int32_t x,
    const int32_t y, const int32_t h, const uint16_t own)
{
    if (x < 0 || x >= DC_BUMP_SIZE || y < 0 || y >= DC_BUMP_SIZE
        || owner[y * DC_BUMP_SIZE + x] != own) {
        return h;
    }
    const int32_t at = height[y * DC_BUMP_SIZE + x];
    return at != 0 ? at : h;
}

// The slope of the relief at (x, y), as the surface normal's lean (nx, ny)
// over a unit rise; false for a see-through texel, which lies flat.
static inline bool DC_Bump_Slope(
    const uint8_t *const height, const uint16_t *const owner, const int32_t x,
    const int32_t y, float *const nx, float *const ny)
{
    const int32_t h = height[y * DC_BUMP_SIZE + x];
    if (h == 0) {
        return false;
    }
    const uint16_t own = owner[y * DC_BUMP_SIZE + x];
    const float dx = (DC_Bump_HeightAt(height, owner, x + 1, y, h, own)
                      - DC_Bump_HeightAt(height, owner, x - 1, y, h, own))
        * (0.5f / 255.0f);
    const float dy = (DC_Bump_HeightAt(height, owner, x, y + 1, h, own)
                      - DC_Bump_HeightAt(height, owner, x, y - 1, h, own))
        * (0.5f / 255.0f);
    float sx = -dx * DC_BUMP_SLOPE;
    float sy = -dy * DC_BUMP_SLOPE;
    const float len = sqrtf(sx * sx + sy * sy);
    if (len > 1.0e-6f) {
        const float k = DC_BUMP_LEAN_CAP
            * tanhf(DC_BUMP_LEAN_GAIN * len / DC_BUMP_LEAN_CAP) / len;
        sx *= k;
        sy *= k;
    }
    *nx = sx;
    *ny = sy;
    return true;
}

// The relief's slopes of a whole page (the lean of the normal at each texel,
// 2 floats a texel in lean), where the one-texel slope above is its building
// block. Three things set them apart:
// - The slope looks two texels either side, the farther pair at half weight:
//   it answers to the lumps of a rock and not only to the speckle between
//   neighbours (the worms), which a baseline of one texel was all.
// - A noise floor: a page's median slope is the speckle's, and the part of a
//   slope under DC_BUMP_CORE_FRAC of it is let go, the rest kept.
// - A strength set by the page: what is left is scaled so that its p90 comes
//   to DC_BUMP_TARGET (within DC_BUMP_STRENGTH_MIN and MAX), so that a rock
//   as low in contrast as TR1's shows its relief as a brick wall does.
// Then the steepening of the gentle ones and the cap, as before.
#define DC_BUMP_CORE_FRAC 0.0f
#define DC_BUMP_TARGET 0.8f
#define DC_BUMP_STRENGTH_MIN 0.7f
#define DC_BUMP_STRENGTH_MAX 5.0f
#define DC_BUMP_HIST_MAX 6.0f
// The coherence of the slopes about a texel (the structure tensor over a window
// of this radius, in texels): a joint, a crack or an edge has its slopes along
// one direction (coherence near 1), the speckle of a dithered rock has them
// every way (near 0). Under COH_LO the slope is let go, over COH_HI it is kept
// whole, between it goes in as the square of the way; so the texture's
// strokes keep their relief and its noise (the worms) has none.
#define DC_BUMP_COH_RADIUS (2 * DC_BUMP_RATIO)
// Off for a relief made by hand (texvq -C): its slopes are all its own.
static int DC_BumpCoherenceOn = 1;
#define DC_BUMP_COH_LO 0.10f
#define DC_BUMP_COH_HI 0.50f
// What the noise keeps of its slope (the structure keeps all): a rock whose
// detail is all turbulence had none left with the speckle let go whole.
#define DC_BUMP_COH_KEEP 0.25f

// A box mean of radius r over a page of floats, along the rows and the columns
// (the edges repeat their last texel).
static inline void DC_Bump_BoxMean(float *const a, float *const tmp, const int32_t r)
{
    const int32_t n = DC_BUMP_SIZE;
    const float inv = 1.0f / (float)(2 * r + 1);
    for (int32_t pass = 0; pass < 2; pass++) {
        const float *const in = pass == 0 ? a : tmp;
        float *const out = pass == 0 ? tmp : a;
        const int32_t step = pass == 0 ? 1 : n;
        const int32_t across = pass == 0 ? n : 1;
        for (int32_t line = 0; line < n; line++) {
            const int32_t base = line * across;
            float sum = 0.0f;
            for (int32_t k = -r; k <= r; k++) {
                const int32_t q = k < 0 ? 0 : k >= n ? n - 1 : k;
                sum += in[base + q * step];
            }
            for (int32_t p = 0; p < n; p++) {
                out[base + p * step] = sum * inv;
                const int32_t add = p + r + 1 >= n ? n - 1 : p + r + 1;
                const int32_t drop = p - r < 0 ? 0 : p - r;
                sum += in[base + add * step] - in[base + drop * step];
            }
        }
    }
}
static inline void DC_Bump_Slopes(
    const uint8_t *const height, const uint16_t *const owner,
    float *const lean)
{
    const int32_t n = DC_BUMP_SIZE;
    int32_t hist[256] = {};
    int32_t solid = 0;
    for (int32_t y = 0; y < n; y++) {
        for (int32_t x = 0; x < n; x++) {
            const int32_t i = y * n + x;
            lean[2 * i] = 0.0f;
            lean[2 * i + 1] = 0.0f;
            const int32_t h = height[i];
            if (h == 0) {
                continue;
            }
            const uint16_t own = owner[i];
            // Sobel over one texel each side: the joint of a brick is two
            // texels wide, and a baseline wider than that would blur it away.
#define DC_BUMP_H(dx, dy)                                                          ((int32_t)DC_Bump_HeightAt(height, owner, x + (dx), y + (dy), h, own))
            const int32_t gx = (DC_BUMP_H(1, -1) - DC_BUMP_H(-1, -1))
                + 2 * (DC_BUMP_H(1, 0) - DC_BUMP_H(-1, 0))
                + (DC_BUMP_H(1, 1) - DC_BUMP_H(-1, 1));
            const int32_t gy = (DC_BUMP_H(-1, 1) - DC_BUMP_H(-1, -1))
                + 2 * (DC_BUMP_H(0, 1) - DC_BUMP_H(0, -1))
                + (DC_BUMP_H(1, 1) - DC_BUMP_H(1, -1));
#undef DC_BUMP_H
            const float sx = -(float)gx * (0.125f / 255.0f) * DC_BUMP_SLOPE;
            const float sy = -(float)gy * (0.125f / 255.0f) * DC_BUMP_SLOPE;
            lean[2 * i] = sx;
            lean[2 * i + 1] = sy;
        }
    }
    // The coherence of the slopes about each texel, and the slopes weighted
    // by it.
    {
        const int32_t count = n * n;
        float *const j = DC_BumpCoherenceOn
            ? malloc(sizeof(float) * 4 * (size_t)count)
            : NULL;
        if (j != NULL) {
            float *const jxx = j;
            float *const jyy = j + count;
            float *const jxy = j + 2 * count;
            float *const tmp = j + 3 * count;
            for (int32_t i = 0; i < count; i++) {
                jxx[i] = lean[2 * i] * lean[2 * i];
                jyy[i] = lean[2 * i + 1] * lean[2 * i + 1];
                jxy[i] = lean[2 * i] * lean[2 * i + 1];
            }
            DC_Bump_BoxMean(jxx, tmp, DC_BUMP_COH_RADIUS);
            DC_Bump_BoxMean(jyy, tmp, DC_BUMP_COH_RADIUS);
            DC_Bump_BoxMean(jxy, tmp, DC_BUMP_COH_RADIUS);
            for (int32_t i = 0; i < count; i++) {
                const float sum = jxx[i] + jyy[i];
                if (sum < 1.0e-9f) {
                    lean[2 * i] = 0.0f;
                    lean[2 * i + 1] = 0.0f;
                    continue;
                }
                const float dd = jxx[i] - jyy[i];
                const float coh = sqrtf(dd * dd + 4.0f * jxy[i] * jxy[i]) / sum;
                float w = (coh - DC_BUMP_COH_LO) / (DC_BUMP_COH_HI - DC_BUMP_COH_LO);
                w = w < 0.0f ? 0.0f : w > 1.0f ? 1.0f : w;
                w = DC_BUMP_COH_KEEP + (1.0f - DC_BUMP_COH_KEEP) * w;
                lean[2 * i] *= w;
                lean[2 * i + 1] *= w;
            }
            free(j);
        }
    }
    for (int32_t i = 0; i < n * n; i++) {
        if (height[i] == 0) {
            continue;
        }
        const float len = sqrtf(lean[2 * i] * lean[2 * i] + lean[2 * i + 1] * lean[2 * i + 1]);
        int32_t bin = (int32_t)(len * (255.0f / DC_BUMP_HIST_MAX));
        hist[bin > 255 ? 255 : bin]++;
        solid++;
    }
    // The median and the p90 of the slopes' lengths.
    float median = 0.0f;
    float p90 = 0.0f;
    for (int32_t b = 0, seen = 0; b < 256; b++) {
        const int32_t before = seen;
        seen += hist[b];
        const float at = (float)(b + 1) * (DC_BUMP_HIST_MAX / 255.0f);
        if (before * 2 < solid && seen * 2 >= solid) {
            median = at;
        }
        if (before * 10 < solid * 9 && seen * 10 >= solid * 9) {
            p90 = at;
            break;
        }
    }
    const float floor_len = DC_BUMP_CORE_FRAC * median;
    float strength = DC_BUMP_TARGET / (p90 - floor_len > 0.02f ? p90 - floor_len : 0.02f);
    strength = strength < DC_BUMP_STRENGTH_MIN ? DC_BUMP_STRENGTH_MIN
        : strength > DC_BUMP_STRENGTH_MAX     ? DC_BUMP_STRENGTH_MAX
                                              : strength;
    for (int32_t i = 0; i < n * n; i++) {
        const float sx = lean[2 * i];
        const float sy = lean[2 * i + 1];
        const float len = sqrtf(sx * sx + sy * sy);
        if (len <= floor_len || len < 1.0e-6f) {
            lean[2 * i] = 0.0f;
            lean[2 * i + 1] = 0.0f;
            continue;
        }
        const float c = (len - floor_len) * strength;
        const float k = DC_BUMP_LEAN_CAP
            * tanhf(DC_BUMP_LEAN_GAIN * c / DC_BUMP_LEAN_CAP) / len;
        lean[2 * i] = sx * k;
        lean[2 * i + 1] = sy * k;
    }
}

// atan on 0 to 1, to about 1e-5: the relief of a page (16384 texels) wants an
// angle and an elevation of each, and the library's asinf and atan2f made a
// page cost 100 ms, a hitch each time a room showed a new one.
static inline float DC_Bump_AtanUnit(const float x)
{
    const float x2 = x * x;
    return x
        * (0.9998660f
           + x2 * (-0.3302995f + x2 * (0.1801410f + x2 * (-0.0851330f + x2 * 0.0208351f))));
}

static inline float DC_Bump_AtanFast(const float x)
{
    return x > 1.0f ? (float)(DC_BUMP_PI * 0.5) - DC_Bump_AtanUnit(1.0f / x)
                    : DC_Bump_AtanUnit(x);
}

// atan2 of (y, x) by turns: 0 to 2 pi.
static inline float DC_Bump_Atan2Turn(const float y, const float x)
{
    const float ax = fabsf(x);
    const float ay = fabsf(y);
    float a = 0.0f;
    if (ax != 0.0f || ay != 0.0f) {
        a = ay > ax ? (float)(DC_BUMP_PI * 0.5) - DC_Bump_AtanUnit(ax / ay)
                    : DC_Bump_AtanUnit(ay / ax);
    }
    if (x < 0.0f) {
        a = (float)DC_BUMP_PI - a;
    }
    if (y < 0.0f) {
        a = (float)(2.0 * DC_BUMP_PI) - a;
    }
    return a;
}

// The PowerVR's bump texel for a slope: the surface's elevation off its plane
// in the high byte (0 to 90 degrees), the way it leans about the normal in the
// low one (a whole turn, from u towards v).
static inline uint16_t DC_Bump_Encode(const float nx, const float ny)
{
    // The elevation: asin(1 / len) with len = sqrt(n^2 + 1) is atan(1 / n),
    // a quarter turn less atan(n).
    const float s =
        (float)(DC_BUMP_PI * 0.5) - DC_Bump_AtanFast(sqrtf(nx * nx + ny * ny));
    const float r = DC_Bump_Atan2Turn(ny, nx);
    uint32_t sb = (uint32_t)(s * (255.0f / ((float)DC_BUMP_PI * 0.5f)) + 0.5f);
    const uint32_t rb = (uint32_t)(r * (256.0f / (2.0f * (float)DC_BUMP_PI))) & 0xFF;
    sb = sb < 255u ? sb : 255u;
    return (uint16_t)((sb << 8) | rb);
}
