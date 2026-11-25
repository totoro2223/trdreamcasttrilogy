// The relief's and the shine's values changed in play (see bump_tune.h).

#include "bump_tune.h"

#include <trx/core/log.h>
#include "../nolua.h"

#include <dc/maple/controller.h>
#include <kos/timer.h>

#include <stddef.h>
#include <stdio.h>

// The values the game ships with: what batcher.c's constants were.
#define M_DEFAULTS                                                             \
    {                                                                          \
        .depth = 0.765f, .shade_floor = 0.45f, .dim = 0.0f, .range = 4.0f,           \
        .fade = 3.0f, .glow = 0.8f, .shine_flat = 0.2f, .detail_near = 1.0f,   \
        .detail_far = 2.0f, .glow_range = 2.4f, .glow_fade = 1.8f,             \
        .tilt = 0.2f,                                                          \
    }

DC_BUMP_TUNE g_DC_Bump = M_DEFAULTS;
static const DC_BUMP_TUNE m_First = M_DEFAULTS;

typedef struct {
    const char *name;
    size_t offset;
    float step;
    float min;
    float max;
} M_PARAM;

#define M_P(n, f, s, lo, hi) { n, offsetof(DC_BUMP_TUNE, f), s, lo, hi }
static const M_PARAM m_Params[] = {
    M_P("BUMP DEPTH", depth, 0.05f, 0.0f, 1.5f),
    M_P("BUMP FLOOR", shade_floor, 0.05f, 0.0f, 0.95f),
    M_P("BUMP FLAT DIM", dim, 0.05f, 0.0f, 0.6f),
    M_P("BUMP RANGE BLOCKS", range, 0.25f, 0.5f, 10.0f),
    M_P("BUMP FADE BLOCKS", fade, 0.25f, 0.25f, 10.0f),
    M_P("SHINE STRENGTH", glow, 0.05f, 0.0f, 2.0f),
    M_P("SHINE FLAT TEXEL", shine_flat, 0.05f, 0.0f, 1.0f),
    M_P("SHINE DETAIL NEAR", detail_near, 0.25f, 0.0f, 8.0f),
    M_P("SHINE DETAIL FAR", detail_far, 0.25f, 0.25f, 10.0f),
    M_P("SHINE RANGE BLOCKS", glow_range, 0.2f, 0.5f, 8.0f),
    M_P("SHINE FADE BLOCKS", glow_fade, 0.2f, 0.25f, 8.0f),
    M_P("PASSING LIGHT TILT", tilt, 0.05f, 0.0f, 1.0f),
};
#define M_PARAM_COUNT ((int32_t)(sizeof(m_Params) / sizeof(m_Params[0])))

static int32_t m_Selected = 0;
// What was held at the last poll, and when a held direction repeats next.
static uint32_t m_Held = 0;
static uint64_t m_RepeatMs = 0;
#define M_REPEAT_FIRST_MS 450
#define M_REPEAT_MS 110

static float *M_Value(const int32_t i)
{
    return (float *)((char *)&g_DC_Bump + m_Params[i].offset);
}

static float M_First(const int32_t i)
{
    return *(const float *)((const char *)&m_First + m_Params[i].offset);
}

static void M_Show(void)
{
    DC_Msg("%s %.2f", m_Params[m_Selected].name, (double)*M_Value(m_Selected));
}

static void M_Print(void)
{
    LOG_INFO(
        "[bump-tune] depth %.3f floor %.3f dim %.3f range %.2f fade %.2f glow "
        "%.3f shine_flat %.3f detail %.2f-%.2f glow_range %.2f glow_fade %.2f "
        "tilt %.3f",
        (double)g_DC_Bump.depth, (double)g_DC_Bump.shade_floor, (double)g_DC_Bump.dim,
        (double)g_DC_Bump.range, (double)g_DC_Bump.fade, (double)g_DC_Bump.glow,
        (double)g_DC_Bump.shine_flat, (double)g_DC_Bump.detail_near,
        (double)g_DC_Bump.detail_far, (double)g_DC_Bump.glow_range,
        (double)g_DC_Bump.glow_fade, (double)g_DC_Bump.tilt);
}

static void M_Change(const int32_t dir)
{
    const M_PARAM *const p = &m_Params[m_Selected];
    float *const v = M_Value(m_Selected);
    float n = *v + (float)dir * p->step;
    // Rounded to the step, so that a long run of changes does not drift.
    n = (float)(int32_t)(n / p->step + (n >= 0.0f ? 0.5f : -0.5f)) * p->step;
    n = n < p->min ? p->min : n > p->max ? p->max : n;
    *v = n;
    g_DC_Bump.version++;
    M_Show();
    M_Print();
}

void DC_BumpTune_Toggle(void)
{
    g_DC_Bump.active = !g_DC_Bump.active;
    m_Held = 0xFFFFFFFFu; // what is held as it turns on is not a press
    if (g_DC_Bump.active) {
        DC_Msg("RELIEF TUNING ON");
        DC_Msg("UP DOWN VALUE  LEFT RIGHT CHANGE");
        M_Show();
    } else {
        DC_Msg("RELIEF TUNING OFF");
        M_Print();
    }
}

int32_t DC_BumpTune_Text(char out[][40]);
bool DC_BumpTune_Poll(const uint32_t b)
{
    if (!g_DC_Bump.active) {
        return false;
    }
    const uint32_t keys = CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT
        | CONT_DPAD_RIGHT | CONT_A | CONT_B;
    const uint32_t now_held = b & keys;
    const uint32_t pressed = now_held & ~m_Held;
    const uint64_t ms = timer_ms_gettime64();
    if (pressed & CONT_DPAD_UP) {
        m_Selected = (m_Selected + M_PARAM_COUNT - 1) % M_PARAM_COUNT;
        M_Show();
    }
    if (pressed & CONT_DPAD_DOWN) {
        m_Selected = (m_Selected + 1) % M_PARAM_COUNT;
        M_Show();
    }
    if (pressed & CONT_DPAD_LEFT) {
        M_Change(-1);
        m_RepeatMs = ms + M_REPEAT_FIRST_MS;
    }
    if (pressed & CONT_DPAD_RIGHT) {
        M_Change(1);
        m_RepeatMs = ms + M_REPEAT_FIRST_MS;
    }
    // Held: it goes on changing.
    if (!pressed && ms >= m_RepeatMs) {
        if (now_held & CONT_DPAD_LEFT) {
            M_Change(-1);
            m_RepeatMs = ms + M_REPEAT_MS;
        } else if (now_held & CONT_DPAD_RIGHT) {
            M_Change(1);
            m_RepeatMs = ms + M_REPEAT_MS;
        }
    }
    if (pressed & CONT_A) {
        *M_Value(m_Selected) = M_First(m_Selected);
        g_DC_Bump.version++;
        M_Show();
        M_Print();
    }
    if (pressed & CONT_B) {
        const uint32_t version = g_DC_Bump.version + 1;
        const bool active = g_DC_Bump.active;
        g_DC_Bump = m_First;
        g_DC_Bump.version = version;
        g_DC_Bump.active = active;
        DC_Msg("RELIEF VALUES BACK TO FIRST");
        M_Print();
    }
    m_Held = now_held;
    return true;
}

// The lines the overlay keeps on screen while the tuning is on: the value
// being changed and its neighbours, the one chosen marked.
int32_t DC_BumpTune_Text(char out[][40])
{
    if (!g_DC_Bump.active) {
        return 0;
    }
    int32_t n = 0;
    for (int32_t k = -1; k <= 1; k++) {
        const int32_t i = (m_Selected + k + M_PARAM_COUNT) % M_PARAM_COUNT;
        snprintf(
            out[n++], 40, "%c %s %.2f", k == 0 ? '>' : ' ', m_Params[i].name,
            (double)*M_Value(i));
    }
    return n;
}
