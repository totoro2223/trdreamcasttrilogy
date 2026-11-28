#include <trx/core/log.h>
#include <stddef.h>
#include <string.h>
#include <trx/config.h>
#include <trx/debug.h>
#include <trx/game/camera.h>
#include <trx/game/collision/los.h>
#include <trx/game/lara.h>
#include <trx/game/pathing.h>
#include <trx/game/random.h>
#include <trx/game/rooms.h>

// clang-format off
#define M_MAX_ELEVATION   (85 * DEG_1) // = 15470
#define M_COMBAT_DISTANCE (WALL_L * 5 / 2) // = 2560
#define M_LOOK_DISTANCE   (WALL_L * 3 / 2) // = 1536
#define M_LOOK_CLAMP      (STEP_L + 50) // = 296
// clang-format on

#define M_SHIFT_ARGS                                                           \
    int32_t *x, int32_t *y, int32_t *h, int32_t target_x, int32_t target_y,    \
        int32_t target_h, int32_t left, int32_t top, int32_t right,            \
        int32_t bottom

typedef struct {
    int16_t chase_speed;
    int32_t min_square;
    int32_t shift_scale;
    bool test_shift_pair;
    bool clip_shift_height;
    bool test_early_lb_shift;
    bool use_fixed_los_check;
    bool override_chase_speed;
    CAMERA_LOOK_SETTINGS look_settings;
    CAMERA_LOOK_SETTINGS look_settings_surf;
} M_SETTINGS;

static const M_SETTINGS m_CameraSettings[CAMERA_MODE_NUMBER_OF] = {
    [CAMERA_MODE_TR1] = {
        .chase_speed = 12,
        .min_square = SQUARE(WALL_L / 4),
        .shift_scale = 1,
        .test_shift_pair = false,
        .clip_shift_height = false,
        .test_early_lb_shift = true,
        .use_fixed_los_check = false,
        .override_chase_speed = false,
        .look_settings = {
            // clang-format off
            .max_head_rotation = +50 * DEG_1,
            .min_head_rotation = -50 * DEG_1,
            .head_turn         = +2 * DEG_1,
            .max_head_tilt     = +22 * DEG_1,
            .min_head_tilt     = -42 * DEG_1,
            .torso_head_rot_y  = 1.0f,
            .torso_head_rot_x  = 1.0f,
            // clang-format on
        },
        .look_settings_surf = {
            // clang-format off
            .head_turn         = +3 * DEG_1,
            .max_head_rotation = +50 * DEG_1,
            .min_head_rotation = -50 * DEG_1,
            .max_head_tilt     = +40 * DEG_1,
            .min_head_tilt     = -40 * DEG_1,
            .torso_head_rot_y  = 0.5f,
            .torso_head_rot_x  = 0.0f,
            // clang-format on
        },
    },

    [CAMERA_MODE_TR2] = {
        .chase_speed = 10,
        .min_square = SQUARE(WALL_L / 3),
        .shift_scale = 2,
        .test_shift_pair = true,
        .clip_shift_height = true,
        .test_early_lb_shift = false,
        .use_fixed_los_check = true,
        .override_chase_speed = true,
        .look_settings = {
            // clang-format off
            .head_turn         = +2 * DEG_1,
            .max_head_rotation = +44 * DEG_1,
            .min_head_rotation = -44 * DEG_1,
            .max_head_tilt     = +22 * DEG_1,
            .min_head_tilt     = -42 * DEG_1,
            .torso_head_rot_y  = 1.0f,
            .torso_head_rot_x  = 1.0f,
            // clang-format on
        },
        .look_settings_surf = {
            // clang-format off
            .head_turn         = +3 * DEG_1,
            .max_head_rotation = +50 * DEG_1,
            .min_head_rotation = -50 * DEG_1,
            .max_head_tilt     = +40 * DEG_1,
            .min_head_tilt     = -40 * DEG_1,
            .torso_head_rot_y  = 0.5f,
            .torso_head_rot_x  = 0.0f,
            // clang-format on
        },
    },
};

static BOX_INFO m_FixedBox = {};

static const M_SETTINGS *M_GetSettings(void)
{
    return &m_CameraSettings[g_Config.visuals.camera_mode];
}

static int16_t M_GetChaseSpeed(void)
{
    return M_GetSettings()->chase_speed;
}

static const CAMERA_LOOK_SETTINGS *M_GetLookSettingsFunc(const bool on_surface)
{
    return on_surface ? &M_GetSettings()->look_settings_surf
                      : &M_GetSettings()->look_settings;
}

static const BOX_INFO *M_GetBox(
    const SECTOR *const sector, const int32_t x, const int32_t z,
    const bool generate_box)
{
    const BOX_INFO *const box = Box_GetBox(sector->box);
    if (box != nullptr || !generate_box) {
        return box;
    }

    // A level may have blocked specific sector or room pathfinding, so create a
    // dummy one-sector box to prevent erratic camera positioning.
    m_FixedBox.left = ROUND_TO_SECTOR(z);
    m_FixedBox.top = ROUND_TO_SECTOR(x);
    m_FixedBox.right = ROUND_TO_SECTOR_END(z);
    m_FixedBox.bottom = ROUND_TO_SECTOR_END(x);
    return &m_FixedBox;
}

static const SECTOR *M_GetSector(
    const int32_t x, const int32_t y, const int32_t z, int16_t room_num)
{
    const XYZ_32 pos = { x, y, z };
    const SECTOR *const sector = Room_GetSector(pos, &room_num);
    const int32_t height = Room_GetHeight(sector, pos);
    const int32_t ceiling = Room_GetCeiling(sector, pos);
    if (y > height || y < ceiling) {
        return nullptr;
    }
    return sector;
}

static bool M_IsGoodPosition(
    const int32_t x, const int32_t y, const int32_t z, int16_t room_num)
{
    return M_GetSector(x, y, z, room_num) != nullptr;
}

static int32_t M_ShiftClampC(GAME_VECTOR *const pos, const int32_t clamp)
{
    const SECTOR *const sector = Room_GetSector(pos->pos, &pos->room_num);
    const BOX_INFO *const box = M_GetBox(sector, pos->x, pos->z, true);

    const XYZ_32 old_pos = pos->pos;

    const int32_t left = box->left + clamp;
    const int32_t right = box->right - clamp;
    if (pos->z < left
        && !M_IsGoodPosition(pos->x, pos->y, pos->z - clamp, pos->room_num)) {
        pos->z = left;
    } else if (
        pos->z > right
        && !M_IsGoodPosition(pos->x, pos->y, pos->z + clamp, pos->room_num)) {
        pos->z = right;
    }

    const int32_t top = box->top + clamp;
    const int32_t bottom = box->bottom - clamp;
    if (pos->x < top
        && !M_IsGoodPosition(pos->x - clamp, pos->y, pos->z, pos->room_num)) {
        pos->x = top;
    } else if (
        pos->x > bottom
        && !M_IsGoodPosition(pos->x + clamp, pos->y, pos->z, pos->room_num)) {
        pos->x = bottom;
    }

    int32_t height = Room_GetHeight(sector, old_pos) - clamp;
    int32_t ceiling = Room_GetCeiling(sector, old_pos) + clamp;

    if (height < ceiling) {
        ceiling = (height + ceiling) >> 1;
        height = ceiling;
    }

    if (old_pos.y > height) {
        return height - old_pos.y;
    } else if (old_pos.y < ceiling) {
        return ceiling - old_pos.y;
    }

    return 0;
}

#if defined(__DREAMCAST__)
// In assembly (creature_sh4.S, dc_cam_shift_clamp); the C above is its
// reference. TRX_DC_ASMOFF3 bit 8388608 turns it off.
typedef struct {
    GAME_VECTOR *pos;
    int32_t clamp;
    const SECTOR *(*sec)(GAME_VECTOR *pos);
    const BOX_INFO *(*box)(const SECTOR *sector, int32_t x, int32_t z);
    int32_t (*good)(int32_t x, int32_t y, int32_t z, int32_t room);
    int32_t (*height)(const SECTOR *sector, const int32_t *old);
    int32_t (*ceiling)(const SECTOR *sector, const int32_t *old);
} M_CAM_CLAMP;
extern int32_t dc_cam_shift_clamp(const M_CAM_CLAMP *b);
extern uint32_t g_DC_AsmOff3;

static const SECTOR *M_ClampSec(GAME_VECTOR *const pos)
{
    return Room_GetSector(pos->pos, &pos->room_num);
}

static const BOX_INFO *M_ClampBox(
    const SECTOR *const sector, const int32_t x, const int32_t z)
{
    return M_GetBox(sector, x, z, true);
}

static int32_t M_ClampGood(
    const int32_t x, const int32_t y, const int32_t z, const int32_t room)
{
    return M_IsGoodPosition(x, y, z, (int16_t)room);
}

static int32_t M_ClampHeight(const SECTOR *const sector, const int32_t *const o)
{
    return Room_GetHeight(sector, (XYZ_32) { .x = o[0], .y = o[1], .z = o[2] });
}

static int32_t M_ClampCeiling(const SECTOR *const sector, const int32_t *const o)
{
    return Room_GetCeiling(sector, (XYZ_32) { .x = o[0], .y = o[1], .z = o[2] });
}

static int32_t M_ShiftClamp(GAME_VECTOR *const pos, const int32_t clamp)
{
    if ((g_DC_AsmOff3 & 8388608) != 0) {
        return M_ShiftClampC(pos, clamp);
    }
    const M_CAM_CLAMP b = {
        .pos = pos,
        .clamp = clamp,
        .sec = M_ClampSec,
        .box = M_ClampBox,
        .good = M_ClampGood,
        .height = M_ClampHeight,
        .ceiling = M_ClampCeiling,
    };
    #if defined(DC_EMIT_VERIFY)
    GAME_VECTOR want = *pos;
    const GAME_VECTOR in = *pos;
    const int32_t want_r = M_ShiftClampC(&want, clamp);
    *pos = in;
    const int32_t r = dc_cam_shift_clamp(&b);
    static uint32_t runs;
    static uint32_t bad;
    if ((r != want_r || memcmp(&want, pos, sizeof(want)) != 0) && bad++ < 6) {
        LOG_WARNING(
            "[camclamp-verify] MISMATCH r %d/%d pos %d %d %d / %d %d %d", r,
            want_r, pos->x, pos->y, pos->z, want.x, want.y, want.z);
    }
    if (++runs % 100 == 1) {
        LOG_WARNING(
            "[camclamp-verify] %u runs, %u bad", (unsigned)runs, (unsigned)bad);
    }
    return r;
    #else
    return dc_cam_shift_clamp(&b);
    #endif
}
#else
    #define M_ShiftClamp M_ShiftClampC
#endif

static void M_SmartShiftC(
    GAME_VECTOR *const target, void (*shift)(M_SHIFT_ARGS))
{
    LOS_Check(&g_Camera.target, target, false);

    const ROOM *room = Room_Get(g_Camera.target.room_num);
    const SECTOR *sector =
        Room_GetWorldSector(room, g_Camera.target.x, g_Camera.target.z);
    const BOX_INFO *box =
        M_GetBox(sector, g_Camera.target.x, g_Camera.target.z, true);

    room = Room_Get(target->room_num);
    sector = Room_GetWorldSector(room, target->x, target->z);
    if (room->flags.swamp) {
        target->y = room->max_ceiling - STEP_L;
        sector = Room_GetSector(target->pos, &target->room_num);
    }

    if (target->z < box->left || target->z > box->right || target->x < box->top
        || target->x > box->bottom) {
        box = M_GetBox(sector, target->x, target->z, true);
    }

    int32_t left = box->left;
    int32_t right = box->right;
    int32_t top = box->top;
    int32_t bottom = box->bottom;

    const M_SETTINGS *const settings = M_GetSettings();

    int32_t test = ROUND_TO_SECTOR_END(target->z - WALL_L);
    const SECTOR *const good_left =
        M_GetSector(target->x, target->y, test, target->room_num);
    if (good_left != nullptr) {
        box = M_GetBox(good_left, target->x, test, false);
        if (box != nullptr && box->left < left) {
            left = box->left;
        }
    } else if (settings->test_shift_pair) {
        left = test;
    }

    test = ROUND_TO_SECTOR(target->z + WALL_L);
    const SECTOR *const good_right =
        M_GetSector(target->x, target->y, test, target->room_num);
    if (good_right != nullptr) {
        box = M_GetBox(good_right, target->x, test, false);
        if (box != nullptr && box->right > right) {
            right = box->right;
        }
    } else if (settings->test_shift_pair) {
        right = test;
    }

    test = ROUND_TO_SECTOR_END(target->x - WALL_L);
    const SECTOR *const good_top =
        M_GetSector(test, target->y, target->z, target->room_num);
    if (good_top != nullptr) {
        box = M_GetBox(good_top, test, target->z, false);
        if (box != nullptr && box->top < top) {
            top = box->top;
        }
    } else if (settings->test_shift_pair) {
        top = test;
    }

    test = ROUND_TO_SECTOR(target->x + WALL_L);
    const SECTOR *const good_bottom =
        M_GetSector(test, target->y, target->z, target->room_num);
    if (good_bottom != nullptr) {
        box = M_GetBox(good_bottom, test, target->z, false);
        if (box != nullptr && box->bottom > bottom) {
            bottom = box->bottom;
        }
    } else if (settings->test_shift_pair) {
        bottom = test;
    }

    left += STEP_L;
    right -= STEP_L;
    top += STEP_L;
    bottom -= STEP_L;

    GAME_VECTOR target_a = {
        .x = target->x,
        .y = target->y,
        .z = target->z,
        .room_num = target->room_num,
    };

    GAME_VECTOR target_b = {
        .x = target->x,
        .y = target->y,
        .z = target->z,
        .room_num = target->room_num,
    };

    bool clip = false;
    bool prefer_a = !settings->test_shift_pair;

#define L_SHIFT(axis1, axis2, l1, l2, r1, r2)                                  \
    shift(                                                                     \
        &target_a.axis1, &target_a.axis2, &target_a.y, g_Camera.target.axis1,  \
        g_Camera.target.axis2, g_Camera.target.y, l1, l2, r1, r2);             \
    shift(                                                                     \
        &target_b.axis1, &target_b.axis2, &target_b.y, g_Camera.target.axis1,  \
        g_Camera.target.axis2, g_Camera.target.y, l1, r2, r1, l2)

    if (!settings->test_shift_pair) {
        if (target->z < left && good_left == nullptr) {
            clip = true;
            if (target->x < g_Camera.target.x) {
                L_SHIFT(z, x, left, top, right, bottom);
            } else {
                L_SHIFT(z, x, left, bottom, right, top);
            }
        } else if (target->z > right && good_right == nullptr) {
            clip = true;
            if (target->x < g_Camera.target.x) {
                L_SHIFT(z, x, right, top, left, bottom);
            } else {
                L_SHIFT(z, x, right, bottom, left, top);
            }
        } else if (target->x < top && good_top == nullptr) {
            clip = true;
            if (target->z < g_Camera.target.z) {
                L_SHIFT(x, z, top, left, bottom, right);
            } else {
                L_SHIFT(x, z, top, right, bottom, left);
            }
        } else if (target->x > bottom && good_bottom == nullptr) {
            clip = true;
            if (target->z < g_Camera.target.z) {
                L_SHIFT(x, z, bottom, left, top, right);
            } else {
                L_SHIFT(x, z, bottom, right, top, left);
            }
        }
    } else {
        if (ABS(target->z - g_Camera.target.z)
            > ABS(target->x - g_Camera.target.x)) {
            if (target->z < left && good_left == nullptr) {
                clip = true;
                prefer_a = g_Camera.pos.x < g_Camera.target.x;
                L_SHIFT(z, x, left, top, right, bottom);
            } else if (target->z > right && good_right == nullptr) {
                clip = true;
                prefer_a = g_Camera.pos.x < g_Camera.target.x;
                L_SHIFT(z, x, right, top, left, bottom);
            } else if (target->x < top && good_top == nullptr) {
                clip = true;
                prefer_a = target->z < g_Camera.target.z;
                L_SHIFT(x, z, top, left, bottom, right);
            } else if (target->x > bottom && good_bottom == nullptr) {
                clip = true;
                prefer_a = target->z < g_Camera.target.z;
                L_SHIFT(x, z, bottom, left, top, right);
            }
        } else {
            if (target->x < top && good_top == nullptr) {
                clip = true;
                prefer_a = g_Camera.pos.z < g_Camera.target.z;
                L_SHIFT(x, z, top, left, bottom, right);
            } else if (target->x > bottom && good_bottom == nullptr) {
                clip = true;
                prefer_a = g_Camera.pos.z < g_Camera.target.z;
                L_SHIFT(x, z, bottom, left, top, right);
            } else if (target->z < left && good_left == nullptr) {
                clip = true;
                prefer_a = target->x < g_Camera.target.x;
                L_SHIFT(z, x, left, top, right, bottom);
            } else if (target->z > right && good_right == nullptr) {
                clip = true;
                prefer_a = target->x < g_Camera.target.x;
                L_SHIFT(z, x, right, top, left, bottom);
            }
        }
    }

#undef L_SHIFT

    if (!clip) {
        return;
    }

    if (settings->test_shift_pair) {
        if (prefer_a) {
            prefer_a = LOS_Check(&g_Camera.target, &target_a, false);
        } else {
            prefer_a = !LOS_Check(&g_Camera.target, &target_b, false);
        }
    }

    if (prefer_a) {
        target->pos = target_a.pos;
    } else {
        target->pos = target_b.pos;
    }

    Room_GetSector(target->pos, &target->room_num);
}

static void M_ClipC(M_SHIFT_ARGS)
{
    const int32_t x_diff = *x - target_x;
    const int32_t y_diff = *y - target_y;
    const int32_t h_diff = *h - target_h;
    int32_t height = *h;

    if ((right > left) != (target_x < left)) {
        if (x_diff != 0) {
            *y = target_y + (left - target_x) * y_diff / x_diff;
            height = target_h + (left - target_x) * h_diff / x_diff;
        }
        *x = left;
    }

    if ((bottom > top && target_y > top && (*y) < top)
        || (bottom < top && target_y < top && (*y) > top)) {
        if (y_diff != 0) {
            *x = target_x + (top - target_y) * x_diff / y_diff;
            height = target_h + (top - target_y) * h_diff / y_diff;
        }
        *y = top;
    }

    const M_SETTINGS *const settings = M_GetSettings();
    if (settings->clip_shift_height) {
        *h = height;
    }
}

static void M_ShiftC(M_SHIFT_ARGS)
{
    const int32_t l_square = SQUARE(target_x - left);
    const int32_t r_square = SQUARE(target_x - right);
    const int32_t t_square = SQUARE(target_y - top);
    const int32_t b_square = SQUARE(target_y - bottom);

    const int32_t tl_square = t_square + l_square;
    const int32_t tr_square = t_square + r_square;
    const int32_t bl_square = b_square + l_square;

    const M_SETTINGS *const settings = M_GetSettings();
    const int32_t scaled_target =
        g_Camera.target_square * settings->shift_scale;

    int32_t shift;
    if (g_Camera.target_square < tl_square) {
        *x = left;
        shift = g_Camera.target_square - l_square;
        if (shift >= 0) {
            shift = Math_Sqrt(shift);
            *y = target_y + (top >= bottom ? shift : -shift);
        }
    } else if (tl_square > settings->min_square) {
        *x = left;
        *y = top;
    } else if (g_Camera.target_square < bl_square) {
        *x = left;
        shift = g_Camera.target_square - l_square;
        if (shift >= 0) {
            shift = Math_Sqrt(shift);
            *y = target_y + (top < bottom ? shift : -shift);
        }
    } else if (
        settings->test_early_lb_shift && bl_square > settings->min_square) {
        *x = left;
        *y = bottom;
    } else if (scaled_target < tr_square) {
        shift = scaled_target - t_square;
        if (shift >= 0) {
            shift = Math_Sqrt(shift);
            *x = target_x + (left < right ? shift : -shift);
            *y = top;
        }
    } else if (settings->test_early_lb_shift || bl_square <= tr_square) {
        *x = right;
        *y = top;
    } else {
        *x = left;
        *y = bottom;
    }
}

#if defined(__DREAMCAST__)
// In assembly (creature_sh4.S: dc_cam_shift, dc_cam_clip, dc_cam_orbit,
// dc_cam_smart_shift); the C above is their reference. TRX_DC_ASMOFF3 bit
// 16777216 turns them off.
typedef struct {
    int32_t *x;
    int32_t *y;
    int32_t *h;
    int32_t tx;
    int32_t ty;
    int32_t th;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    int32_t target_square;
    int32_t min_square;
    int32_t shift_scale;
    int32_t flag;
    int32_t axes[2];
} M_CAM_SH;
_Static_assert(sizeof(M_CAM_SH) == 64, "M_CAM_SH");
extern void dc_cam_shift(const M_CAM_SH *b);
extern void dc_cam_clip(const M_CAM_SH *b);

typedef struct {
    GAME_VECTOR *out;
    const int32_t *centre;
    int32_t dist;
    int32_t elev;
    int32_t angle;
    int32_t room;
    int32_t (*sin)(int32_t angle);
} M_CAM_ORBIT;
extern int32_t dc_cam_orbit(const M_CAM_ORBIT *b);

typedef struct {
    GAME_VECTOR *target;
    GAME_VECTOR *cam_target;
    const GAME_VECTOR *cam_pos;
    int32_t pair;
    void (*shift)(const M_CAM_SH *b);
    int32_t min_square;
    int32_t shift_scale;
    int32_t flag;
    int32_t target_square;
    int32_t (*los)(const GAME_VECTOR *a, GAME_VECTOR *b);
    const BOX_INFO *(*cbox)(const GAME_VECTOR *t);
    const SECTOR *(*tsec)(GAME_VECTOR *t);
    const BOX_INFO *(*box)(
        const SECTOR *sector, int32_t x, int32_t z, int32_t gen);
    const SECTOR *(*good)(int32_t x, int32_t y, int32_t z, int32_t room);
    const SECTOR *(*sec)(const int32_t *pos, int16_t *room);
} M_CAM_SMART;
_Static_assert(offsetof(M_CAM_SMART, sec) == 56, "M_CAM_SMART");
extern void dc_cam_smart_shift(const M_CAM_SMART *b);

static void M_ShiftBlock(M_CAM_SH *const b, const bool clip, M_SHIFT_ARGS)
{
    const M_SETTINGS *const settings = M_GetSettings();
    *b = (M_CAM_SH) {
        .x = x,
        .y = y,
        .h = h,
        .tx = target_x,
        .ty = target_y,
        .th = target_h,
        .left = left,
        .top = top,
        .right = right,
        .bottom = bottom,
        .target_square = g_Camera.target_square,
        .min_square = settings->min_square,
        .shift_scale = settings->shift_scale,
        .flag = clip ? settings->clip_shift_height
                     : settings->test_early_lb_shift,
    };
}

static void M_Shift(M_SHIFT_ARGS)
{
    if ((g_DC_AsmOff3 & 16777216) != 0) {
        M_ShiftC(
            x, y, h, target_x, target_y, target_h, left, top, right, bottom);
        return;
    }
    M_CAM_SH b;
    M_ShiftBlock(
        &b, false, x, y, h, target_x, target_y, target_h, left, top, right,
        bottom);
    dc_cam_shift(&b);
}

static void M_Clip(M_SHIFT_ARGS)
{
    if ((g_DC_AsmOff3 & 16777216) != 0) {
        M_ClipC(
            x, y, h, target_x, target_y, target_h, left, top, right, bottom);
        return;
    }
    M_CAM_SH b;
    M_ShiftBlock(
        &b, true, x, y, h, target_x, target_y, target_h, left, top, right,
        bottom);
    dc_cam_clip(&b);
}

static int32_t M_SSLos(const GAME_VECTOR *const a, GAME_VECTOR *const b)
{
    return LOS_Check(a, b, false) ? 1 : 0;
}

static const BOX_INFO *M_SSCamBox(const GAME_VECTOR *const t)
{
    const ROOM *const room = Room_Get(t->room_num);
    const SECTOR *const sector = Room_GetWorldSector(room, t->x, t->z);
    return M_GetBox(sector, t->x, t->z, true);
}

static const SECTOR *M_SSTargetSec(GAME_VECTOR *const t)
{
    const ROOM *const room = Room_Get(t->room_num);
    const SECTOR *sector = Room_GetWorldSector(room, t->x, t->z);
    if (room->flags.swamp) {
        t->y = room->max_ceiling - STEP_L;
        sector = Room_GetSector(t->pos, &t->room_num);
    }
    return sector;
}

static const BOX_INFO *M_SSBox(
    const SECTOR *const sector, const int32_t x, const int32_t z,
    const int32_t gen)
{
    return M_GetBox(sector, x, z, gen != 0);
}

static const SECTOR *M_SSGood(
    const int32_t x, const int32_t y, const int32_t z, const int32_t room)
{
    return M_GetSector(x, y, z, (int16_t)room);
}

static const SECTOR *M_SSSec(const int32_t *const p, int16_t *const room)
{
    return Room_GetSector((XYZ_32) { .x = p[0], .y = p[1], .z = p[2] }, room);
}

    #if defined(DC_EMIT_VERIFY)
// Random cases for the two shifts against their C, once.
static void M_ShiftSelfTest(void)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    uint32_t seed = 12345;
        #define L_RND(n)                                                       \
            ((int32_t)((seed = seed * 1103515245u + 12345u) >> 8) % (n))
    uint32_t bad = 0;
    const int32_t keep_square = g_Camera.target_square;
    for (int32_t i = 0; i < 200000; i++) {
        const int32_t base_x = L_RND(60000);
        const int32_t base_y = L_RND(60000);
        const int32_t tx = base_x + L_RND(8000);
        const int32_t ty = base_y + L_RND(8000);
        const int32_t th = L_RND(20000) - 10000;
        int32_t args[4];
        for (int32_t j = 0; j < 4; j++) {
            args[j] = ((j & 1) ? base_y : base_x) + L_RND(8000);
        }
        g_Camera.target_square = L_RND(1 << 25);
        const int32_t x0 = base_x + L_RND(8000);
        const int32_t y0 = base_y + L_RND(8000);
        const int32_t h0 = L_RND(20000) - 10000;
        for (int32_t k = 0; k < 2; k++) {
            int32_t cx = x0, cy = y0, ch = h0;
            int32_t ax = x0, ay = y0, ah = h0;
            if (k == 0) {
                M_ShiftC(
                    &cx, &cy, &ch, tx, ty, th, args[0], args[1], args[2],
                    args[3]);
            } else {
                M_ClipC(
                    &cx, &cy, &ch, tx, ty, th, args[0], args[1], args[2],
                    args[3]);
            }
            M_CAM_SH b;
            M_ShiftBlock(
                &b, k == 1, &ax, &ay, &ah, tx, ty, th, args[0], args[1],
                args[2], args[3]);
            if (k == 0) {
                dc_cam_shift(&b);
            } else {
                dc_cam_clip(&b);
            }
            if ((cx != ax || cy != ay || ch != ah) && bad++ < 6) {
                LOG_WARNING(
                    "[camshift-verify] MISMATCH %s %d %d %d / %d %d %d",
                    k ? "clip" : "shift", ax, ay, ah, cx, cy, ch);
            }
        }
    }
        #undef L_RND
    g_Camera.target_square = keep_square;
    LOG_WARNING("[camshift-verify] 400000 runs, %u bad", (unsigned)bad);
}
    #endif

static void M_SmartShift(
    GAME_VECTOR *const target, void (*shift)(M_SHIFT_ARGS))
{
    const bool clip = shift == M_Clip;
    if ((g_DC_AsmOff3 & 16777216) != 0) {
        M_SmartShiftC(target, clip ? M_ClipC : M_ShiftC);
        return;
    }
    const M_SETTINGS *const settings = M_GetSettings();
    const M_CAM_SMART b = {
        .target = target,
        .cam_target = &g_Camera.target,
        .cam_pos = &g_Camera.pos,
        .pair = settings->test_shift_pair,
        .shift = clip ? dc_cam_clip : dc_cam_shift,
        .min_square = settings->min_square,
        .shift_scale = settings->shift_scale,
        .flag = clip ? settings->clip_shift_height
                     : settings->test_early_lb_shift,
        .target_square = g_Camera.target_square,
        .los = M_SSLos,
        .cbox = M_SSCamBox,
        .tsec = M_SSTargetSec,
        .box = M_SSBox,
        .good = M_SSGood,
        .sec = M_SSSec,
    };
    #if defined(DC_EMIT_VERIFY)
    M_ShiftSelfTest();
    const GAME_VECTOR in = *target;
    const GAME_VECTOR cam_target = g_Camera.target;
    M_SmartShiftC(target, clip ? M_ClipC : M_ShiftC);
    const GAME_VECTOR want = *target;
    const GAME_VECTOR want_cam = g_Camera.target;
    *target = in;
    g_Camera.target = cam_target;
    dc_cam_smart_shift(&b);
    static uint32_t runs;
    static uint32_t bad;
    if ((want.x != target->x || want.y != target->y || want.z != target->z
         || want.room_num != target->room_num
         || memcmp(&want_cam, &g_Camera.target, sizeof(want_cam)) != 0)
        && bad++ < 6) {
        LOG_WARNING(
            "[smartshift-verify] MISMATCH %d %d %d r%d / %d %d %d r%d",
            target->x, target->y, target->z, target->room_num, want.x, want.y,
            want.z, want.room_num);
    }
    if (++runs % 1000 == 0) {
        LOG_WARNING(
            "[smartshift-verify] %u runs, %u bad", (unsigned)runs,
            (unsigned)bad);
    }
    #else
    dc_cam_smart_shift(&b);
    #endif
}

// The camera's place round its target: distance = dist * cos(elev), turned
// by angle behind the target, raised by dist * sin(elev).
static int32_t M_OrbitC(
    GAME_VECTOR *const out, const int32_t dist, const int16_t elev,
    const int16_t angle)
{
    const int32_t distance = (dist * Math_Cos(elev)) >> W2V_SHIFT;
    XYZ_32 offset = XYZ_32_RotateYaw((XYZ_32) { .z = -distance }, angle);
    offset.y = (dist * Math_Sin(elev)) >> W2V_SHIFT;
    *out = (GAME_VECTOR) {
        .x = g_Camera.target.x + offset.x,
        .y = g_Camera.target.y + offset.y,
        .z = g_Camera.target.z + offset.z,
        .room_num = g_Camera.pos.room_num,
    };
    return distance;
}

static int32_t M_Orbit(
    GAME_VECTOR *const out, const int32_t dist, const int16_t elev,
    const int16_t angle)
{
    if ((g_DC_AsmOff3 & 16777216) != 0) {
        return M_OrbitC(out, dist, elev, angle);
    }
    const int32_t centre[3] = {
        g_Camera.target.x,
        g_Camera.target.y,
        g_Camera.target.z,
    };
    const M_CAM_ORBIT b = {
        .out = out,
        .centre = centre,
        .dist = dist,
        .elev = elev,
        .angle = angle,
        .room = g_Camera.pos.room_num,
        .sin = Math_Sin,
    };
    #if defined(DC_EMIT_VERIFY)
    GAME_VECTOR want;
    const int32_t want_d = M_OrbitC(&want, dist, elev, angle);
    const int32_t d = dc_cam_orbit(&b);
    static uint32_t runs;
    static uint32_t bad;
    if ((d != want_d || want.x != out->x || want.y != out->y
         || want.z != out->z || want.room_num != out->room_num)
        && bad++ < 6) {
        LOG_WARNING(
            "[orbit-verify] MISMATCH d %d/%d %d %d %d / %d %d %d", d, want_d,
            out->x, out->y, out->z, want.x, want.y, want.z);
    }
    if (++runs % 1000 == 0) {
        LOG_WARNING(
            "[orbit-verify] %u runs, %u bad", (unsigned)runs, (unsigned)bad);
    }
    return d;
    #else
    return dc_cam_orbit(&b);
    #endif
}
#else
    #define M_Shift M_ShiftC
    #define M_Clip M_ClipC
    #define M_SmartShift M_SmartShiftC
static int32_t M_Orbit(
    GAME_VECTOR *const out, const int32_t dist, const int16_t elev,
    const int16_t angle)
{
    const int32_t distance = (dist * Math_Cos(elev)) >> W2V_SHIFT;
    XYZ_32 offset = XYZ_32_RotateYaw((XYZ_32) { .z = -distance }, angle);
    offset.y = (dist * Math_Sin(elev)) >> W2V_SHIFT;
    *out = (GAME_VECTOR) {
        .x = g_Camera.target.x + offset.x,
        .y = g_Camera.target.y + offset.y,
        .z = g_Camera.target.z + offset.z,
        .room_num = g_Camera.pos.room_num,
    };
    return distance;
}
#endif

static void M_MoveC(const GAME_VECTOR *const target, const int32_t speed)
{
    const GAME_VECTOR old_pos = g_Camera.pos;
    GAME_VECTOR pos = g_Camera.pos;
    pos.x += (target->x - pos.x) / speed;
    pos.z += (target->z - pos.z) / speed;
    pos.y += (target->y - pos.y) / speed;
    pos.room_num = target->room_num;

    Camera_SetChunky(false);

    const SECTOR *sector = Room_GetSector(pos.pos, &pos.room_num);
    int32_t height = Room_GetHeight(sector, pos.pos);
    if (height == NO_HEIGHT) {
        // Attempt to clamp within the previous sector's height bounds. Only if
        // that fails continue to revert fully to the last good Y position.
        pos.room_num = old_pos.room_num;
        sector = Room_GetSector(old_pos.pos, &pos.room_num);
        height = Room_GetHeight(sector, old_pos.pos);
        const int32_t old_ceiling = Room_GetCeiling(sector, old_pos.pos);
        CLAMP(pos.y, old_ceiling + STEP_L, height - STEP_L);
        sector = Room_GetSector(pos.pos, &pos.room_num);
        height = Room_GetHeight(sector, pos.pos);
        if (height == NO_HEIGHT) {
            pos.y = old_pos.y;
            pos.room_num = old_pos.room_num;
            sector = Room_GetSector(pos.pos, &pos.room_num);
            height = Room_GetHeight(sector, pos.pos);
        }
    }

    if (height != NO_HEIGHT) {
        height -= STEP_L;
    }
    if (pos.y >= height && target->y >= height) {
        LOS_Check(&g_Camera.target, &pos, false);
        sector = Room_GetSector(pos.pos, &pos.room_num);
        height = Room_GetHeight(sector, pos.pos);
        if (height != NO_HEIGHT) {
            height -= STEP_L;
        }
    }

    int32_t ceiling = Room_GetCeiling(sector, pos.pos);
    if (ceiling != NO_HEIGHT) {
        ceiling += STEP_L;
    }
    if (height != NO_HEIGHT && ceiling != NO_HEIGHT && height < ceiling) {
        ceiling = (height + ceiling) >> 1;
        height = ceiling;
    }

    g_Camera.pos = pos;
    Camera_ApplyBounce();

    if (height != NO_HEIGHT && g_Camera.pos.y > height) {
        g_Camera.shift = height - g_Camera.pos.y;
    } else if (ceiling != NO_HEIGHT && g_Camera.pos.y < ceiling) {
        g_Camera.shift = ceiling - g_Camera.pos.y;
    } else {
        g_Camera.shift = 0;
    }

    Camera_UpdateMicPosition();
}

#if defined(__DREAMCAST__)
// In assembly (creature_sh4.S, dc_cam_move); the C above is its reference.
// TRX_DC_ASMOFF3 bit 8388608 turns it off.
typedef struct {
    GAME_VECTOR *cam;
    const GAME_VECTOR *target;
    int32_t speed;
    int32_t *shift;
    const SECTOR *(*sec)(const int32_t *pos, int16_t *room);
    int32_t (*height)(const SECTOR *sector, const int32_t *pos);
    int32_t (*ceiling)(const SECTOR *sector, const int32_t *pos);
    void (*los)(GAME_VECTOR *pos);
    void (*chunky)(void);
    void (*bounce)(void);
    void (*mic)(void);
} M_CAM_MOVE;
extern void dc_cam_move(const M_CAM_MOVE *b);

static const SECTOR *M_MoveSec(const int32_t *const p, int16_t *const room)
{
    return Room_GetSector((XYZ_32) { .x = p[0], .y = p[1], .z = p[2] }, room);
}

static void M_MoveLOS(GAME_VECTOR *const pos)
{
    LOS_Check(&g_Camera.target, pos, false);
}

static void M_MoveChunky(void)
{
    Camera_SetChunky(false);
}

static void M_Move(const GAME_VECTOR *const target, const int32_t speed)
{
    if ((g_DC_AsmOff3 & 8388608) != 0) {
        M_MoveC(target, speed);
        return;
    }
    const M_CAM_MOVE b = {
        .cam = &g_Camera.pos,
        .target = target,
        .speed = speed,
        .shift = &g_Camera.shift,
        .sec = M_MoveSec,
        .height = M_ClampHeight,
        .ceiling = M_ClampCeiling,
        .los = M_MoveLOS,
        .chunky = M_MoveChunky,
        .bounce = Camera_ApplyBounce,
        .mic = Camera_UpdateMicPosition,
    };
    #if defined(DC_EMIT_VERIFY)
    // The target may be g_Camera's own: kept apart for both runs.
    const GAME_VECTOR tgt = *target;
    const CAMERA_INFO cam_in = g_Camera;
    const uint32_t seed = *Random_GetControlState();
    M_MoveC(&tgt, speed);
    const CAMERA_INFO want = g_Camera;
    const uint32_t want_seed = *Random_GetControlState();
    g_Camera = cam_in;
    *Random_GetControlState() = seed;
    M_CAM_MOVE bb = b;
    bb.target = &tgt;
    dc_cam_move(&bb);
    static uint32_t runs;
    static uint32_t bad;
    if ((memcmp(&want, &g_Camera, sizeof(want)) != 0
         || want_seed != *Random_GetControlState())
        && bad++ < 6) {
        LOG_WARNING(
            "[cammove-verify] MISMATCH pos %d %d %d / %d %d %d shift %d/%d",
            g_Camera.pos.x, g_Camera.pos.y, g_Camera.pos.z, want.pos.x,
            want.pos.y, want.pos.z, g_Camera.shift, want.shift);
    }
    if (++runs % 1000 == 0) {
        LOG_WARNING(
            "[cammove-verify] %u runs, %u bad", (unsigned)runs, (unsigned)bad);
    }
    {
        // The box clamp runs only when looking or on a fixed camera: tried
        // here on copies of the target (its verify compares the two), with
        // the clamps the game uses.
        GAME_VECTOR probe = g_Camera.target;
        M_ShiftClamp(&probe, STEP_L);
        probe = g_Camera.target;
        M_ShiftClamp(&probe, M_LOOK_CLAMP);
    }
    #else
    dc_cam_move(&b);
    #endif
}
#else
    #define M_Move M_MoveC
#endif

static void M_Chase(const ITEM *const item)
{
    g_Camera.target_elevation += item->rot.x;
    g_Camera.target_elevation = MIN(g_Camera.target_elevation, M_MAX_ELEVATION);
    g_Camera.target_elevation =
        MAX(g_Camera.target_elevation, -M_MAX_ELEVATION);

    const int16_t angle = g_Camera.target_angle + item->rot.y;

    // The camera trails the target, so it steps against the angle.
    GAME_VECTOR target;
    const int32_t distance = M_Orbit(
        &target, g_Camera.target_distance, g_Camera.target_elevation, angle);
    g_Camera.target_square = SQUARE(distance);

    const M_SETTINGS *const settings = M_GetSettings();
    const int16_t speed =
        settings->override_chase_speed || g_Camera.fixed_camera
        ? g_Camera.speed
        : settings->chase_speed;
    M_SmartShift(&target, M_Shift);
    M_Move(&target, speed);
}

static void M_Combat(const ITEM *const item)
{
    g_Camera.target.z = item->pos.z;
    g_Camera.target.x = item->pos.x;
    g_Camera.target_distance = M_COMBAT_DISTANCE;

    const LARA_INFO *const lara_info = Lara_GetLaraInfo();
    if (lara_info->target != nullptr) {
        g_Camera.target_angle = lara_info->target_angles[0] + item->rot.y;
        g_Camera.target_elevation = lara_info->target_angles[1] + item->rot.x;
    } else {
        g_Camera.target_angle =
            lara_info->torso_rot.y + lara_info->head_rot.y + item->rot.y;
        g_Camera.target_elevation =
            lara_info->torso_rot.x + lara_info->head_rot.x + item->rot.x;
    }

    GAME_VECTOR target;
    M_Orbit(
        &target, M_COMBAT_DISTANCE, g_Camera.target_elevation,
        g_Camera.target_angle);

    if (lara_info->water_status == LWS_UNDERWATER) {
        const ITEM *const lara_item = Lara_GetItem();
        const int32_t water_height =
            lara_info->water_surface_dist + lara_item->pos.y;
        if (g_Camera.target.y > water_height && water_height > target.y) {
            target.y = lara_info->water_surface_dist + lara_item->pos.y;
            target.z = g_Camera.target.z
                + (water_height - g_Camera.target.y)
                    * (target.z - g_Camera.target.z)
                    / (target.y - g_Camera.target.y);
            target.x = g_Camera.target.x
                + (water_height - g_Camera.target.y)
                    * (target.x - g_Camera.target.x)
                    / (target.y - g_Camera.target.y);
        }
    }

    M_SmartShift(&target, M_Shift);
    M_Move(&target, g_Camera.speed);
}

static void M_Fixed(void)
{
    const OBJECT_VECTOR *const fixed = Camera_GetFixedObject(g_Camera.num);
    GAME_VECTOR target = {
        .x = fixed->x,
        .y = fixed->y,
        .z = fixed->z,
        .room_num = fixed->data,
    };

    const M_SETTINGS *const settings = M_GetSettings();
    if (settings->use_fixed_los_check
        && !LOS_Check(&g_Camera.target, &target, false)) {
        M_ShiftClamp(&target, STEP_L);
    }

    g_Camera.fixed_camera = true;
    M_Move(&target, g_Camera.speed);

    if (g_Camera.timer != 0) {
        g_Camera.timer--;
        if (g_Camera.timer == 0) {
            g_Camera.timer = -1;
        }
    }
}

static void M_Look(const ITEM *const item)
{
    const XYZ_32 old = {
        .x = g_Camera.target.x,
        .y = g_Camera.target.y,
        .z = g_Camera.target.z,
    };

    g_Camera.target.z = item->pos.z;
    g_Camera.target.x = item->pos.x;
    g_Camera.target_distance = M_LOOK_DISTANCE;

    const LARA_INFO *const lara_info = Lara_GetLaraInfo();
    g_Camera.target_angle =
        item->rot.y + lara_info->torso_rot.y + lara_info->head_rot.y;
    g_Camera.target_elevation =
        item->rot.x + lara_info->torso_rot.x + lara_info->head_rot.x;

    g_Camera.shift =
        (-STEP_L * 2 * Math_Sin(g_Camera.target_elevation)) >> W2V_SHIFT;
    g_Camera.target.pos =
        XYZ_32_OffsetYaw(g_Camera.target.pos, item->rot.y, g_Camera.shift);

    if (!M_IsGoodPosition(
            g_Camera.target.x, g_Camera.target.y, g_Camera.target.z,
            g_Camera.target.room_num)) {
        g_Camera.target.x = item->pos.x;
        g_Camera.target.z = item->pos.z;
    }

    g_Camera.target.y += M_ShiftClamp(&g_Camera.target, M_LOOK_CLAMP);

    GAME_VECTOR target;
    M_Orbit(
        &target, M_LOOK_DISTANCE, g_Camera.target_elevation,
        g_Camera.target_angle);

    M_SmartShift(&target, M_Clip);
    g_Camera.target.z = old.z + (g_Camera.target.z - old.z) / g_Camera.speed;
    g_Camera.target.x = old.x + (g_Camera.target.x - old.x) / g_Camera.speed;
    M_Move(&target, g_Camera.speed);
    g_Camera.debuff = 5;
}

static void M_ClampResult(void)
{
    XYZ_32 *const pos = &g_Camera.interp.result.pos;
    const int32_t shift = g_Camera.interp.result.shift;
    const ROOM *const room = Room_Get(g_Camera.interp.room_num);
    const SECTOR *sector = Room_GetWorldSector(room, pos->x, pos->z);
    if (sector->box != NO_BOX) {
        goto finish;
    }

    sector = Room_GetWorldSector(room, g_Camera.pos.x, g_Camera.pos.z);
    if (sector->box == NO_BOX) {
        goto finish;
    }

    const BOX_INFO *const box = Box_GetBox(sector->box);
    CLAMP(pos->x, box->top, box->bottom);
    CLAMP(pos->z, box->left, box->right);

finish:
    const int32_t floor = Room_GetHeightEx(
        sector, *pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    const int32_t ceiling = Room_GetCeilingEx(
        sector, *pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    if (floor != NO_HEIGHT && ceiling != NO_HEIGHT) {
        CLAMP(pos->y, ceiling - shift, floor - shift);
    }
    Room_GetSector(
        (XYZ_32) { pos->x, pos->y + shift, pos->z }, &g_Camera.interp.room_num);
}

static void M_Reset(void)
{
    const ITEM *const lara_item = Lara_GetItem();
    ASSERT(lara_item != nullptr);
    g_Camera.shift = lara_item->pos.y - WALL_L;

    g_Camera.target.x = lara_item->pos.x;
    g_Camera.target.y = g_Camera.shift;
    g_Camera.target.z = lara_item->pos.z;
    g_Camera.target.room_num = lara_item->room_num;

    g_Camera.pos.x = g_Camera.target.x;
    g_Camera.pos.y = g_Camera.target.y;
    g_Camera.pos.z = g_Camera.target.z - 100;
    g_Camera.pos.room_num = g_Camera.target.room_num;
}

static void M_Update(
    const ITEM *const item, const bool fixed_camera, int32_t target_y)
{
    const BOUNDS_16 *bounds = Item_GetBoundsAccurate(item);

    if (g_Camera.item != nullptr && !fixed_camera) {
        bounds = Item_GetBoundsAccurate(g_Camera.item);

        const int32_t dx = g_Camera.item->pos.x - item->pos.x;
        const int32_t dz = g_Camera.item->pos.z - item->pos.z;
        const int32_t shift = Math_Sqrt(SQUARE(dx) + SQUARE(dz));
        int16_t angle = Math_Atan(dz, dx) - item->rot.y;

        int16_t tilt = Math_Atan(
            shift,
            target_y - (bounds->min.y + bounds->max.y) / 2
                - g_Camera.item->pos.y);
        angle >>= 1;
        tilt >>= 1;

        if (angle > CAMERA_MIN_HEAD_ROTATION && angle < CAMERA_MAX_HEAD_ROTATION
            && tilt > CAMERA_MIN_HEAD_TILT && tilt < CAMERA_MAX_HEAD_TILT) {
            LARA_INFO *const lara_info = Lara_GetLaraInfo();
            int16_t change = angle - lara_info->head_rot.y;
            if (change > CAMERA_HEAD_TURN) {
                lara_info->head_rot.y += CAMERA_HEAD_TURN;
            } else if (change < -CAMERA_HEAD_TURN) {
                lara_info->head_rot.y -= CAMERA_HEAD_TURN;
            } else {
                lara_info->head_rot.y = angle;
            }

            change = tilt - lara_info->head_rot.x;
            if (change > CAMERA_HEAD_TURN) {
                lara_info->head_rot.x += CAMERA_HEAD_TURN;
            } else if (change < -CAMERA_HEAD_TURN) {
                lara_info->head_rot.x -= CAMERA_HEAD_TURN;
            } else {
                lara_info->head_rot.x += change;
            }
            lara_info->torso_rot.x = lara_info->head_rot.x;
            lara_info->torso_rot.y = lara_info->head_rot.y;
            g_Camera.type = CAM_LOOK;
            g_Camera.item->looked_at = true;
        }
    }

    if (g_Camera.type == CAM_LOOK || g_Camera.type == CAM_COMBAT) {
        target_y -= STEP_L;
        g_Camera.target.room_num = item->room_num;
        if (g_Camera.fixed_camera) {
            g_Camera.target.y = target_y;
            g_Camera.speed = 1;
        } else {
            g_Camera.target.y += (target_y - g_Camera.target.y) >> 2;
            g_Camera.speed = g_Camera.type == CAM_LOOK ? CAMERA_LOOK_SPEED
                                                       : CAMERA_COMBAT_SPEED;
        }
        g_Camera.fixed_camera = false;
        if (g_Camera.type == CAM_LOOK) {
            M_Look(item);
        } else {
            M_Combat(item);
        }
    } else {
        if (fixed_camera) {
            g_Camera.debuff = 0;
        }
        if (g_Camera.debuff > 0) {
            const XYZ_32 old = g_Camera.target.pos;
            g_Camera.target.x = (item->pos.x + old.x) / 2;
            g_Camera.target.z = (item->pos.z + old.z) / 2;
            g_Camera.debuff--;
        } else {
            g_Camera.target.x = item->pos.x;
            g_Camera.target.z = item->pos.z;
        }

        if (g_Camera.flags == CF_FOLLOW_CENTRE) {
            const int32_t shift = (bounds->min.z + bounds->max.z) / 2;
            g_Camera.target.pos =
                XYZ_32_OffsetYaw(g_Camera.target.pos, item->rot.y, shift);
        }

        g_Camera.target.room_num = item->room_num;
        if (g_Camera.fixed_camera != fixed_camera) {
            g_Camera.target.y = target_y;
            g_Camera.fixed_camera = true;
            g_Camera.speed = 1;
        } else {
            g_Camera.fixed_camera = false;
            g_Camera.target.y += (target_y - g_Camera.target.y) / 4;
        }

        const SECTOR *const sector = Room_GetSector(
            (XYZ_32) { g_Camera.target.x, target_y, g_Camera.target.z },
            &g_Camera.target.room_num);
        const int32_t height = Room_GetHeight(sector, g_Camera.target.pos);
        if (g_Camera.target.y > height) {
            Camera_SetChunky(false);
        }

        if (g_Camera.type == CAM_CHASE || g_Camera.flags == CF_CHASE_OBJECT) {
            M_Chase(item);
        } else {
            M_Fixed();
        }
    }
}

static const CAMERA_STRATEGY m_Strategy = {
    .get_chase_speed_func = M_GetChaseSpeed,
    .get_look_settings_func = M_GetLookSettingsFunc,
    .clamp_result_func = M_ClampResult,
    .reset_func = M_Reset,
    .update_func = M_Update,
};

REGISTER_CAMERA(CAMERA_MODE_TR1, m_Strategy)
REGISTER_CAMERA(CAMERA_MODE_TR2, m_Strategy)
