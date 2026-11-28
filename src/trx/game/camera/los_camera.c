#include <trx/config.h>
#include <trx/core/log.h>
#include <trx/core/math.h>
#include <trx/debug.h>
#include <trx/game/camera.h>
#include <trx/game/lara.h>
#include <trx/game/rooms.h>

#include <string.h>

#if defined(__DREAMCAST__)
    #include <trx/game/random.h>
#endif

// clang-format off
#define M_LOS_STEPS         8
#define M_MAX_SNAPS         8
#define M_SNAP_DELTA        (STEP_L * 3) // = 768
#define M_DEFAULT_ELEVATION (-10 * DEG_1) // = -1820
#define M_MAX_ELEVATION     (85 * DEG_1) // = 15470
#define M_CHASE_SHIFT       (STEP_L * 3 / 2) // = 384
#define M_LOOK_SHIFT        (STEP_L * 7 / 8) // = 224
#define M_CHASE_SPEED       10
#define M_MAX_LOOK_TILT     (55 * DEG_1) // = 10010
#define M_MIN_LOOK_TILT     (-75 * DEG_1) // = -13650
#define M_MAX_LOOK_ROTATION (80 * DEG_1) // = 14560
#define M_MIN_LOOK_ROTATION -M_MAX_LOOK_ROTATION // = -14560
// clang-format on

typedef struct {
    struct {
        int16_t current_anim_state;
        int16_t goal_anim_state;
        XYZ_32 pos;
        XYZ_16 rot;
        XYZ_16 head_rot;
        XYZ_16 torso_rot;
    } lara;
    CAMERA_TYPE cam_type;
    int16_t target_angle;
    int16_t target_elevation;
} M_STATE;

typedef struct {
    GAME_VECTOR pos;
    GAME_VECTOR target;
} M_IDEAL;

static M_STATE m_LastState = {};
static M_IDEAL m_LastIdeal = {};
static M_IDEAL m_LastLookIdeal = {};
static int32_t m_Snaps = 0;

static const LARA_ANIMATION_ID m_ForceLookAnims[] = {
    // clang-format off
    LA_STAND_TO_CROUCH,
    LA_CROUCH_TO_STAND,
    NO_CATALOG_ID,
    // clang-format on
};

static CAMERA_LOOK_SETTINGS m_LookSettings = {
    // clang-format off
    .head_turn         = +2 * DEG_1,
    .max_head_rotation = +44 * DEG_1,
    .min_head_rotation = -44 * DEG_1,
    .max_head_tilt     = +30 * DEG_1,
    .min_head_tilt     = -35 * DEG_1,
    .torso_head_rot_y  = 1.0f,
    .torso_head_rot_x  = 1.0f,
    // clang-format on
};

static int16_t M_GetChaseSpeed(void)
{
    return M_CHASE_SPEED;
}

static const CAMERA_LOOK_SETTINGS *M_GetLookSettingsFunc(const bool on_surface)
{
    return &m_LookSettings;
}

static bool M_LOSC(
    GAME_VECTOR *const start, GAME_VECTOR *const target, const int32_t shift)
{
    const XYZ_32 delta = {
        .x = (target->x - start->x) >> 3,
        .y = (target->y - start->y) >> 3,
        .z = (target->z - start->z) >> 3,
    };

    XYZ_32 pos = start->pos;
    int16_t room_num = start->room_num;
    bool valid_space = false;
    bool clipped = false;

    int32_t i = 0;
    for (; i < M_LOS_STEPS; i++) {
        int16_t next_room_num = room_num;
        const SECTOR *const sector = Room_GetSector(pos, &next_room_num);

        if (Room_Get(next_room_num)->flags.swamp) {
            clipped = true;
            break;
        }

        const int32_t height = Room_GetHeightEx(
            sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
        const int32_t ceiling = Room_GetCeilingEx(
            sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });

        if (height == NO_HEIGHT || ceiling == NO_HEIGHT || ceiling >= height) {
            if (!valid_space) {
                pos.x += delta.x;
                pos.y += delta.y;
                pos.z += delta.z;
                continue;
            }

            clipped = true;
            break;
        }

        if (pos.y > height) {
            const int32_t height_diff = pos.y - height;
            if (height_diff < shift) {
                pos.y = height;
            } else {
                clipped = true;
                break;
            }
        }

        if (pos.y < ceiling) {
            const int32_t ceiling_diff = ceiling - pos.y;
            if (ceiling_diff < shift) {
                pos.y = ceiling;
            } else {
                clipped = true;
                break;
            }
        }

        valid_space = true;
        room_num = next_room_num;
        pos.x += delta.x;
        pos.y += delta.y;
        pos.z += delta.z;
    }

    if (i != 0) {
        pos.x -= delta.x;
        pos.y -= delta.y;
        pos.z -= delta.z;
    }

    Room_GetSector(pos, &room_num);
    target->pos = pos;
    target->room_num = room_num;

    return !clipped;
}

#if defined(__DREAMCAST__)
// In assembly (creature_sh4.S, dc_cam_los and dc_cam_collide); the C above is
// their reference. TRX_DC_ASMOFF3 bit 8388608 turns them off.
typedef struct {
    GAME_VECTOR *a;
    GAME_VECTOR *b;
    int32_t shift;
    int32_t (*hc)(const int32_t *pos, int16_t *room, int32_t *out);
    void (*sec)(const int32_t *pos, int16_t *room);
} M_CAM_LOS;
typedef struct {
    GAME_VECTOR *ideal;
    int32_t shift;
    int32_t y_first;
    int32_t (*hc)(const int32_t *pos, int16_t *room, int32_t *out);
    void (*sec)(const int32_t *pos, int16_t *room);
} M_CAM_COLLIDE;
extern bool dc_cam_los(const M_CAM_LOS *b);
extern bool dc_cam_collide(const M_CAM_COLLIDE *b);
extern uint32_t g_DC_AsmOff3;

// Room_GetSector, then (but in a swamp) the height and the ceiling.
static int32_t M_CamHC(
    const int32_t *const p, int16_t *const room, int32_t *const out)
{
    const XYZ_32 pos = { .x = p[0], .y = p[1], .z = p[2] };
    const SECTOR *const sector = Room_GetSector(pos, room);
    if (Room_Get(*room)->flags.swamp) {
        return 1;
    }
    out[0] = Room_GetHeightEx(
        sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    out[1] = Room_GetCeilingEx(
        sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    return 0;
}

static void M_CamSec(const int32_t *const p, int16_t *const room)
{
    Room_GetSector((XYZ_32) { .x = p[0], .y = p[1], .z = p[2] }, room);
}

static bool M_LOS(
    GAME_VECTOR *const start, GAME_VECTOR *const target, const int32_t shift)
{
    if ((g_DC_AsmOff3 & 8388608) != 0) {
        return M_LOSC(start, target, shift);
    }
    const M_CAM_LOS b = {
        .a = start, .b = target, .shift = shift, .hc = M_CamHC, .sec = M_CamSec,
    };
    #if defined(DC_EMIT_VERIFY)
    GAME_VECTOR want = *target;
    const GAME_VECTOR in = *target;
    const bool want_r = M_LOSC(start, &want, shift);
    *target = in;
    const bool r = dc_cam_los(&b);
    static uint32_t runs;
    static uint32_t bad;
    if ((r != want_r || memcmp(&want, target, sizeof(want)) != 0) && bad++ < 6) {
        LOG_WARNING(
            "[camlos-verify] MISMATCH r %d/%d pos %d %d %d / %d %d %d", r,
            want_r, target->x, target->y, target->z, want.x, want.y, want.z);
    }
    if (++runs % 2000 == 0) {
        LOG_WARNING("[camlos-verify] %u runs, %u bad", (unsigned)runs, (unsigned)bad);
    }
    return r;
    #else
    return dc_cam_los(&b);
    #endif
}
#else
    #define M_LOS M_LOSC
#endif

static inline void M_ClampY(int16_t room_num, XYZ_32 *const pos)
{
    const SECTOR *const sector = Room_GetSector(*pos, &room_num);
    const int32_t height = Room_GetHeightEx(
        sector, *pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    const int32_t ceiling = Room_GetCeilingEx(
        sector, *pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });

    if (ceiling < height && ceiling != NO_HEIGHT && height != NO_HEIGHT) {
        if (ceiling > pos->y - 255 && height < pos->y + 255) {
            pos->y = (ceiling + height) >> 1;
        } else if (height < pos->y + 255) {
            pos->y = height - 255;
        } else if (ceiling > pos->y - 255) {
            pos->y = ceiling + 255;
        }
    }
}

static bool M_CollideC(
    GAME_VECTOR *const ideal, const int32_t shift, const bool y_first)
{
    XYZ_32 pos = ideal->pos;
    if (y_first) {
        M_ClampY(ideal->room_num, &pos);
    }

#define L_OUT_OF_BOUNDS                                                        \
    (height < pos.y || height == NO_HEIGHT || ceiling == NO_HEIGHT             \
     || ceiling >= height || pos.y < ceiling)

    // -X clamp
    int16_t room_num = ideal->room_num;
    XYZ_32 sample_pos = { .x = pos.x - shift, .y = pos.y, .z = pos.z };
    const SECTOR *sector = Room_GetSector(sample_pos, &room_num);
    int32_t height = Room_GetHeightEx(
        sector, sample_pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    int32_t ceiling = Room_GetCeilingEx(
        sector, sample_pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    if (L_OUT_OF_BOUNDS) {
        pos.x = ROUND_TO_SECTOR(pos.x) + shift;
    }

    // -Z clamp
    room_num = ideal->room_num;
    sample_pos = (XYZ_32) { .x = pos.x, .y = pos.y, .z = pos.z - shift };
    sector = Room_GetSector(sample_pos, &room_num);
    height = Room_GetHeightEx(
        sector, sample_pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    ceiling = Room_GetCeilingEx(
        sector, sample_pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    if (L_OUT_OF_BOUNDS) {
        pos.z = ROUND_TO_SECTOR(pos.z) + shift;
    }

    // +X clamp
    room_num = ideal->room_num;
    sample_pos = (XYZ_32) { .x = pos.x + shift, .y = pos.y, .z = pos.z };
    sector = Room_GetSector(sample_pos, &room_num);
    height = Room_GetHeightEx(
        sector, sample_pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    ceiling = Room_GetCeilingEx(
        sector, sample_pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    if (L_OUT_OF_BOUNDS) {
        pos.x = ROUND_TO_SECTOR_END(pos.x) - shift;
    }

    // +Z clamp
    room_num = ideal->room_num;
    sample_pos = (XYZ_32) { .x = pos.x, .y = pos.y, .z = pos.z + shift };
    sector = Room_GetSector(sample_pos, &room_num);
    height = Room_GetHeightEx(
        sector, sample_pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    ceiling = Room_GetCeilingEx(
        sector, sample_pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    if (L_OUT_OF_BOUNDS) {
        pos.z = ROUND_TO_SECTOR_END(pos.z) - shift;
    }

    if (!y_first) {
        M_ClampY(ideal->room_num, &pos);
    }

    room_num = ideal->room_num;
    sector = Room_GetSector(pos, &room_num);
    height =
        Room_GetHeightEx(sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    ceiling = Room_GetCeilingEx(
        sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    if (L_OUT_OF_BOUNDS) {
        return true;
    }

#undef L_OUT_OF_BOUNDS

    Room_GetSector(pos, &ideal->room_num);
    ideal->pos = pos;
    return false;
}

#if defined(__DREAMCAST__)
static bool M_Collide(
    GAME_VECTOR *const ideal, const int32_t shift, const bool y_first)
{
    if ((g_DC_AsmOff3 & 8388608) != 0) {
        return M_CollideC(ideal, shift, y_first);
    }
    const M_CAM_COLLIDE b = {
        .ideal = ideal,
        .shift = shift,
        .y_first = y_first,
        .hc = M_CamHC,
        .sec = M_CamSec,
    };
    #if defined(DC_EMIT_VERIFY)
    GAME_VECTOR want = *ideal;
    const GAME_VECTOR in = *ideal;
    const bool want_r = M_CollideC(&want, shift, y_first);
    *ideal = in;
    const bool r = dc_cam_collide(&b);
    static uint32_t runs;
    static uint32_t bad;
    if ((r != want_r || memcmp(&want, ideal, sizeof(want)) != 0) && bad++ < 6) {
        LOG_WARNING(
            "[camcol-verify] MISMATCH r %d/%d pos %d %d %d / %d %d %d", r,
            want_r, ideal->x, ideal->y, ideal->z, want.x, want.y, want.z);
    }
    if (++runs % 2000 == 0) {
        LOG_WARNING("[camcol-verify] %u runs, %u bad", (unsigned)runs, (unsigned)bad);
    }
    return r;
    #else
    return dc_cam_collide(&b);
    #endif
}
#else
    #define M_Collide M_CollideC
#endif

static bool M_UpdateLaraState(void)
{
    const LARA_INFO *const lara = Lara_GetLaraInfo();
    const ITEM *const lara_item = Lara_GetItem();

    const int16_t current_angle =
        g_Config.visuals.camera_mode == CAMERA_MODE_TR3
        ? g_Camera.additional_angle
        : g_Camera.target_angle;
    const int16_t current_elevation =
        g_Config.visuals.camera_mode == CAMERA_MODE_TR3
        ? g_Camera.additional_elevation
        : g_Camera.target_elevation;

    bool same_lara_state =
        m_LastState.lara.current_anim_state == lara_item->current_anim_state
        && m_LastState.lara.goal_anim_state == lara_item->goal_anim_state
        && XYZ_16_AreEquivalent(m_LastState.lara.head_rot, lara->head_rot)
        && XYZ_32_AreEquivalent(m_LastState.lara.pos, lara_item->pos);
    bool same_camera_state = m_LastState.cam_type == g_Camera.type;
    if (g_Camera.type != CAM_LOOK) {
        same_lara_state &=
            XYZ_16_AreEquivalent(m_LastState.lara.rot, lara_item->rot)
            && XYZ_16_AreEquivalent(
                m_LastState.lara.torso_rot, lara->torso_rot);
        same_camera_state &= m_LastState.target_angle == current_angle
            && m_LastState.target_elevation == current_elevation;
    } else {
        same_lara_state &= !Lara_HasAnimation(m_ForceLookAnims);
    }
    if (same_lara_state && same_camera_state) {
        return false;
    }

    m_LastState.lara.current_anim_state = lara_item->current_anim_state;
    m_LastState.lara.goal_anim_state = lara_item->goal_anim_state;
    m_LastState.lara.head_rot = lara->head_rot;
    m_LastState.lara.pos = lara_item->pos;
    if (g_Camera.type != CAM_LOOK) {
        m_LastState.lara.rot = lara_item->rot;
        m_LastState.lara.torso_rot = lara->torso_rot;
        m_LastState.target_angle = current_angle;
        m_LastState.target_elevation = current_elevation;
    }

    return true;
}

static void M_MoveC(GAME_VECTOR *const ideal, const int32_t speed)
{
    if (M_UpdateLaraState()) {
        m_LastIdeal.pos = *ideal;
    } else {
        *ideal = m_LastIdeal.pos;
    }

    g_Camera.pos.x += (ideal->x - g_Camera.pos.x) / speed;
    g_Camera.pos.y += (ideal->y - g_Camera.pos.y) / speed;
    g_Camera.pos.z += (ideal->z - g_Camera.pos.z) / speed;
    g_Camera.pos.room_num = ideal->room_num;

    Camera_ApplyBounce();

    XYZ_32 pos = g_Camera.pos.pos;
    int16_t room_num = g_Camera.pos.room_num;
    const XYZ_32 sample_pos = { pos.x, pos.y + STEP_L, pos.z };
    const SECTOR *sector = Room_GetSector(sample_pos, &room_num);

    const ROOM *const room = Room_Get(room_num);
    if (room->flags.swamp) {
        pos.y = room->max_ceiling - STEP_L;
        Room_GetSector(pos, &g_Camera.pos.room_num);
    }

    sector = Room_GetSector(pos, &room_num);
    int32_t height =
        Room_GetHeightEx(sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    int32_t ceiling = Room_GetCeilingEx(
        sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });

    if (pos.y < ceiling || pos.y > height) {
        M_LOS(&g_Camera.target, &g_Camera.pos, 0);
        const XYZ_32 delta = {
            .x = ABS(g_Camera.pos.x - ideal->x),
            .y = ABS(g_Camera.pos.y - ideal->y),
            .z = ABS(g_Camera.pos.z - ideal->z),
        };

        if (delta.x < M_SNAP_DELTA && delta.y < M_SNAP_DELTA
            && delta.z < M_SNAP_DELTA) {
            GAME_VECTOR start = *ideal;
            GAME_VECTOR target = g_Camera.pos;

            if (!M_LOS(&start, &target, 0)) {
                m_Snaps++;
                if (m_Snaps >= M_MAX_SNAPS) {
                    g_Camera.pos = *ideal;
                    m_Snaps = 0;
                }
            }
        }
    }

    pos = g_Camera.pos.pos;
    room_num = g_Camera.pos.room_num;
    sector = Room_GetSector(pos, &room_num);
    height =
        Room_GetHeightEx(sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    ceiling = Room_GetCeilingEx(
        sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });

    if (pos.y - 255 < ceiling && pos.y + 255 > height && ceiling < height
        && ceiling != NO_HEIGHT && height != NO_HEIGHT) {
        g_Camera.pos.y = (ceiling + height) >> 1;
    } else if (
        pos.y + 255 > height && ceiling < height && ceiling != NO_HEIGHT
        && height != NO_HEIGHT) {
        g_Camera.pos.y = height - 255;
    } else if (
        pos.y - 255 < ceiling && ceiling < height && ceiling != NO_HEIGHT
        && height != NO_HEIGHT) {
        g_Camera.pos.y = ceiling + 255;
    } else if (
        ceiling >= height || height == NO_HEIGHT || ceiling == NO_HEIGHT) {
        g_Camera.pos = *ideal;
    }

    Room_GetSector(g_Camera.pos.pos, &g_Camera.pos.room_num);
    m_LastState.cam_type = g_Camera.type;

    Camera_UpdateMicPosition();
}

#if defined(__DREAMCAST__)
// In assembly (creature_sh4.S: dc_cam3_move, dc_cam3_ideal, dc_cam3_target,
// dc_cam3_look_walk, dc_cam3_look_close); the C is their reference.
// TRX_DC_ASMOFF3 bit 33554432 turns them off.
typedef struct {
    GAME_VECTOR *cam;
    GAME_VECTOR *ideal;
    int32_t speed;
    GAME_VECTOR *last;
    GAME_VECTOR *target;
    int32_t *snaps;
    int32_t (*state)(void);
    void (*bounce)(void);
    int32_t (*sw)(const int32_t *pos, int16_t *room, int32_t *out);
    int32_t (*hc)(const int32_t *pos, int16_t *room, int32_t *out, int32_t tilts);
    void (*sec)(const int32_t *pos, int16_t *room);
    int32_t (*los)(GAME_VECTOR *a, GAME_VECTOR *b, int32_t shift);
} M_CAM3_MOVE;
typedef struct {
    GAME_VECTOR *out;
    const GAME_VECTOR *target;
    const GAME_VECTOR *pos;
    int32_t distance;
    int32_t elevation;
    int32_t target_distance;
    int32_t angle;
    int32_t (*sin)(int32_t angle);
    int32_t (*los)(GAME_VECTOR *a, GAME_VECTOR *b, int32_t shift);
} M_CAM3_IDEAL;
typedef struct {
    GAME_VECTOR *target;
    int16_t *elevation;
    int32_t margin;
    const GAME_VECTOR *last;
    int32_t (*sw)(const int32_t *pos, int16_t *room, int32_t *out);
    int32_t (*hc)(const int32_t *pos, int16_t *room, int32_t *out, int32_t tilts);
    void (*sec)(const int32_t *pos, int16_t *room);
} M_CAM3_TARGET;
typedef struct {
    int32_t *pos;
    const int32_t *delta;
    int16_t *room;
    int32_t (*sw)(const int32_t *pos, int16_t *room, int32_t *out);
    int32_t (*hc)(const int32_t *pos, int16_t *room, int32_t *out, int32_t tilts);
} M_CAM3_WALK;
typedef struct {
    GAME_VECTOR *cam;
    GAME_VECTOR *target;
    const GAME_VECTOR *ideal;
    const int32_t *pos_3;
    const int32_t *pos_1;
    int32_t fixed;
    int32_t lara_room;
    int32_t same_type;
    void (*bounce)(void);
    int32_t (*hc)(const int32_t *pos, int16_t *room, int32_t *out, int32_t tilts);
    void (*sec)(const int32_t *pos, int16_t *room);
    int32_t (*los)(GAME_VECTOR *a, GAME_VECTOR *b, int32_t shift);
} M_CAM3_CLOSE;
extern void dc_cam3_move(const M_CAM3_MOVE *b);
extern void dc_cam3_ideal(const M_CAM3_IDEAL *b);
extern void dc_cam3_target(const M_CAM3_TARGET *b);
extern void dc_cam3_look_walk(const M_CAM3_WALK *b);
extern void dc_cam3_look_close(const M_CAM3_CLOSE *b);

// Room_GetSector; out[2] the room's ceiling less a step; whether a swamp.
static int32_t M_C3Sw(
    const int32_t *const p, int16_t *const room, int32_t *const out)
{
    Room_GetSector((XYZ_32) { .x = p[0], .y = p[1], .z = p[2] }, room);
    const ROOM *const r = Room_Get(*room);
    out[2] = r->max_ceiling - STEP_L;
    return r->flags.swamp ? 1 : 0;
}

// The same with the height and the ceiling in out[0] and out[1].
static int32_t M_C3HC(
    const int32_t *const p, int16_t *const room, int32_t *const out,
    const int32_t tilts)
{
    const XYZ_32 pos = { .x = p[0], .y = p[1], .z = p[2] };
    const SECTOR *const sector = Room_GetSector(pos, room);
    if (tilts) {
        out[0] = Room_GetHeightEx(
            sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
        out[1] = Room_GetCeilingEx(
            sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    } else {
        out[0] = Room_GetHeight(sector, pos);
        out[1] = Room_GetCeiling(sector, pos);
    }
    const ROOM *const r = Room_Get(*room);
    out[2] = r->max_ceiling - STEP_L;
    return r->flags.swamp ? 1 : 0;
}

static int32_t M_C3State(void)
{
    return M_UpdateLaraState() ? 1 : 0;
}

static int32_t M_C3Los(
    GAME_VECTOR *const a, GAME_VECTOR *const b, const int32_t shift)
{
    return M_LOS(a, b, shift) ? 1 : 0;
}

    #if defined(DC_EMIT_VERIFY)
static void M_C3Report(const char *const name, const bool same)
{
    static uint32_t runs[5];
    static uint32_t bad[5];
    static const char *names[5];
    int32_t k = 0;
    while (k < 4 && names[k] != nullptr && names[k] != name) {
        k++;
    }
    names[k] = name;
    if (!same && bad[k]++ < 6) {
        LOG_WARNING("[%s-verify] MISMATCH", name);
    }
    if (++runs[k] % 1000 == 0) {
        LOG_WARNING(
            "[%s-verify] %u runs, %u bad", name, (unsigned)runs[k],
            (unsigned)bad[k]);
    }
}
    #endif

static void M_Move(GAME_VECTOR *const ideal, const int32_t speed)
{
    if ((g_DC_AsmOff3 & 33554432) != 0) {
        M_MoveC(ideal, speed);
        return;
    }
    const M_CAM3_MOVE b = {
        .cam = &g_Camera.pos,
        .ideal = ideal,
        .speed = speed,
        .last = &m_LastIdeal.pos,
        .target = &g_Camera.target,
        .snaps = &m_Snaps,
        .state = M_C3State,
        .bounce = Camera_ApplyBounce,
        .sw = M_C3Sw,
        .hc = M_C3HC,
        .sec = M_CamSec,
        .los = M_C3Los,
    };
    #if defined(DC_EMIT_VERIFY)
    const CAMERA_INFO cam_in = g_Camera;
    const M_STATE state_in = m_LastState;
    const M_IDEAL last_in = m_LastIdeal;
    const int32_t snaps_in = m_Snaps;
    const GAME_VECTOR ideal_in = *ideal;
    const uint32_t seed_in = *Random_GetControlState();
    M_MoveC(ideal, speed);
    const CAMERA_INFO cam_want = g_Camera;
    const M_STATE state_want = m_LastState;
    const M_IDEAL last_want = m_LastIdeal;
    const int32_t snaps_want = m_Snaps;
    const GAME_VECTOR ideal_want = *ideal;
    const uint32_t seed_want = *Random_GetControlState();
    g_Camera = cam_in;
    m_LastState = state_in;
    m_LastIdeal = last_in;
    m_Snaps = snaps_in;
    *ideal = ideal_in;
    *Random_GetControlState() = seed_in;
    #endif
    dc_cam3_move(&b);
    m_LastState.cam_type = g_Camera.type;
    Camera_UpdateMicPosition();
    #if defined(DC_EMIT_VERIFY)
    M_C3Report(
        "cam3move",
        memcmp(&cam_want, &g_Camera, sizeof(cam_want)) == 0
            && memcmp(&state_want, &m_LastState, sizeof(state_want)) == 0
            && memcmp(&last_want, &m_LastIdeal, sizeof(last_want)) == 0
            && snaps_want == m_Snaps
            && memcmp(&ideal_want, ideal, sizeof(ideal_want)) == 0
            && seed_want == *Random_GetControlState());
    #endif
}
#else
    #define M_Move M_MoveC
#endif

static GAME_VECTOR M_GetIdealC(
    const int32_t distance, const int16_t target_rot_y)
{
    int32_t farthest = 0x7FFFFFFF;
    int32_t farthest_num = 0;
    GAME_VECTOR temp[2] = {};
    GAME_VECTOR ideals[5] = {};

    const int32_t rise =
        (Math_Sin(g_Camera.target_elevation) * g_Camera.target_distance)
        >> W2V_SHIFT;

    for (int32_t i = 0; i < 5; i++) {
        const int16_t angle = i > 0 ? ((i - 1) << W2V_SHIFT)
                                    : (g_Camera.target_angle + target_rot_y);
        ideals[i].pos = XYZ_32_OffsetLocalYaw(
            g_Camera.target.pos, (XYZ_32) { .z = -distance }, angle);
        ideals[i].y = g_Camera.target.y + rise;

        ideals[i].room_num = g_Camera.target.room_num;

        if (M_LOS(&g_Camera.target, &ideals[i], 200)) {
            temp[0] = ideals[i];
            temp[1] = g_Camera.pos;

            if (i == 0 || M_LOS(&temp[0], &temp[1], 0)) {
                if (i == 0) {
                    farthest_num = 0;
                    break;
                }

                const int32_t dx = SQUARE(g_Camera.pos.x - ideals[i].x);
                const int32_t dz = SQUARE(g_Camera.pos.z - ideals[i].z) + dx;
                if (dz < farthest) {
                    farthest = dz;
                    farthest_num = i;
                }
            }
        } else if (i == 0) {
            temp[0] = ideals[i];
            temp[1] = g_Camera.pos;

            const int32_t dx = SQUARE(g_Camera.target.x - ideals[i].x);
            const int32_t dz = SQUARE(g_Camera.target.z - ideals[i].z) + dx;
            if (dz > 0x90000) {
                farthest_num = 0;
                break;
            }
        }
    }

    return ideals[farthest_num];
}

#if defined(__DREAMCAST__)
static GAME_VECTOR M_GetIdeal(const int32_t distance, const int16_t target_rot_y)
{
    if ((g_DC_AsmOff3 & 33554432) != 0) {
        return M_GetIdealC(distance, target_rot_y);
    }
    GAME_VECTOR out;
    const M_CAM3_IDEAL b = {
        .out = &out,
        .target = &g_Camera.target,
        .pos = &g_Camera.pos,
        .distance = distance,
        .elevation = g_Camera.target_elevation,
        .target_distance = g_Camera.target_distance,
        .angle = (int16_t)(g_Camera.target_angle + target_rot_y),
        .sin = Math_Sin,
        .los = M_C3Los,
    };
    dc_cam3_ideal(&b);
    #if defined(DC_EMIT_VERIFY)
    const GAME_VECTOR want = M_GetIdealC(distance, target_rot_y);
    M_C3Report(
        "cam3ideal",
        want.x == out.x && want.y == out.y && want.z == out.z
            && want.room_num == out.room_num);
    #endif
    return out;
}
#else
    #define M_GetIdeal M_GetIdealC
#endif

// The camera's target kept a margin within its floor and its ceiling, or
// back where it last was good.
static void M_FixTargetC(const int32_t m)
{
    int16_t room_num = g_Camera.target.room_num;
    const SECTOR *sector = Room_GetSector(
        (XYZ_32) {
            g_Camera.target.x,
            g_Camera.target.y + STEP_L,
            g_Camera.target.z,
        },
        &room_num);

    const ROOM *const room = Room_Get(room_num);
    if (room->flags.swamp) {
        g_Camera.target.y = room->max_ceiling - STEP_L;
    }

    XYZ_32 pos = g_Camera.target.pos;
    sector = Room_GetSector(pos, &g_Camera.target.room_num);
    int32_t height =
        Room_GetHeightEx(sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    int32_t ceiling = Room_GetCeilingEx(
        sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });

    if (ceiling + m > height - m && height != NO_HEIGHT
        && ceiling != NO_HEIGHT) {
        g_Camera.target.y = (height + ceiling) >> 1;
        g_Camera.target_elevation = 0;
    } else if (pos.y > height - m && height != NO_HEIGHT) {
        g_Camera.target.y = height - m;
        g_Camera.target_elevation = 0;
    } else if (pos.y < ceiling + m && ceiling != NO_HEIGHT) {
        g_Camera.target.y = ceiling + m;
        g_Camera.target_elevation = 0;
    }

    Room_GetSector(g_Camera.target.pos, &g_Camera.target.room_num);
    pos = g_Camera.target.pos;
    room_num = g_Camera.target.room_num;
    sector = Room_GetSector(pos, &room_num);
    height =
        Room_GetHeightEx(sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    ceiling = Room_GetCeilingEx(
        sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });

    if (pos.y < ceiling || pos.y > height || ceiling >= height
        || height == NO_HEIGHT || ceiling == NO_HEIGHT) {
        g_Camera.target = m_LastIdeal.target;
    }
}

#if defined(__DREAMCAST__)
static void M_FixTarget(const int32_t m)
{
    if ((g_DC_AsmOff3 & 33554432) != 0) {
        M_FixTargetC(m);
        return;
    }
    const M_CAM3_TARGET b = {
        .target = &g_Camera.target,
        .elevation = &g_Camera.target_elevation,
        .margin = m,
        .last = &m_LastIdeal.target,
        .sw = M_C3Sw,
        .hc = M_C3HC,
        .sec = M_CamSec,
    };
    #if defined(DC_EMIT_VERIFY)
    const CAMERA_INFO cam_in = g_Camera;
    M_FixTargetC(m);
    const CAMERA_INFO want = g_Camera;
    g_Camera = cam_in;
    dc_cam3_target(&b);
    M_C3Report("cam3target", memcmp(&want, &g_Camera, sizeof(want)) == 0);
    #else
    dc_cam3_target(&b);
    #endif
}
#else
    #define M_FixTarget M_FixTargetC
#endif

#if defined(__DREAMCAST__) && defined(DC_EMIT_VERIFY)
static void M_LookWalk(XYZ_32 *pos, XYZ_32 delta, int16_t *room);
static void M_LookClose(
    const GAME_VECTOR *ideal, XYZ_32 pos_1, XYZ_32 pos_3);
#endif
static void M_Chase(const ITEM *const item)
{
    if (g_Camera.target_elevation == 0) {
        g_Camera.target_elevation = M_DEFAULT_ELEVATION;
    }
    g_Camera.target_elevation += item->rot.x;
    CLAMP(g_Camera.target_elevation, -M_MAX_ELEVATION, M_MAX_ELEVATION);

    const int32_t distance =
        (g_Camera.target_distance * Math_Cos(g_Camera.target_elevation))
        >> W2V_SHIFT;

    M_FixTarget(16);
#if defined(__DREAMCAST__) && defined(DC_EMIT_VERIFY)
    {
        // M_Look's walk and close run only when looking: tried here on
        // copies (their verifies compare the two), the camera put back.
        const CAMERA_INFO cam_in = g_Camera;
        const uint32_t seed_in = *Random_GetControlState();
        XYZ_32 p = g_Camera.target.pos;
        const XYZ_32 d = {
            (g_Camera.pos.x - p.x) >> 3,
            (g_Camera.pos.y - p.y) >> 3,
            (g_Camera.pos.z - p.z) >> 3,
        };
        int16_t r = g_Camera.target.room_num;
        M_LookWalk(&p, d, &r);
        const GAME_VECTOR ideal = { .pos = p, .room_num = r };
        M_LookClose(&ideal, g_Camera.target.pos, g_Camera.target.pos);
        g_Camera = cam_in;
        *Random_GetControlState() = seed_in;
    }
#endif

    GAME_VECTOR ideal = M_GetIdeal(distance, item->rot.y);
    M_Collide(&ideal, M_CHASE_SHIFT, true);

    if (m_LastState.cam_type == CAM_FIXED) {
        g_Camera.speed = 1;
    }

    M_Move(&ideal, g_Camera.speed);
}

static void M_Combat(const ITEM *const item)
{
    g_Camera.target.x = item->pos.x;
    g_Camera.target.z = item->pos.z;

    const LARA_INFO *const lara = Lara_GetLaraInfo();
    if (lara->target != nullptr) {
        g_Camera.target_angle = lara->target_angles[0] + item->rot.y;
        g_Camera.target_elevation = lara->target_angles[1] + item->rot.x;
    } else {
        g_Camera.target_angle =
            lara->torso_rot.y + lara->head_rot.y + item->rot.y;
        g_Camera.target_elevation =
            lara->head_rot.x + item->rot.x + lara->torso_rot.x - 2730;
    }

    M_FixTarget(64);

    g_Camera.target_distance = CAMERA_DEFAULT_DISTANCE;
    const int32_t distance =
        g_Camera.target_distance * Math_Cos(g_Camera.target_elevation)
        >> W2V_SHIFT;

    GAME_VECTOR ideal = M_GetIdeal(distance, 0);
    M_Collide(&ideal, M_CHASE_SHIFT, true);

    if (m_LastState.cam_type == CAM_FIXED) {
        g_Camera.speed = 1;
    }

    M_Move(&ideal, g_Camera.speed);
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

    g_Camera.fixed_camera = true;
    M_Move(&target, g_Camera.speed);

    if (g_Camera.timer != 0) {
        g_Camera.timer--;
        if (g_Camera.timer == 0) {
            g_Camera.timer = -1;
        }
    }
}

static XYZ_32 M_GetHeadPos(const int32_t x, const int32_t y, const int32_t z)
{
    XYZ_32 pos = {
        .x = x,
        .y = y,
        .z = z,
    };
    if (!Lara_GetMeshPos(LM_HEAD, &pos)) {
        // If look is held while loading a level, Lara won't have been drawn yet
        // so ensure a valid fallback position is used.
        Collide_GetJointAbsPosition(Lara_GetItem(), &pos, LM_HEAD);
    }
    return pos;
}

// From Lara's head out along delta, while the place is good.
static void M_LookWalkC(
    XYZ_32 *const pos, const XYZ_32 delta, int16_t *const room)
{
    XYZ_32 ideal_pos = *pos;
    int16_t room_num = *room;
    const SECTOR *sector;
    int32_t height;
    int32_t ceiling;

    int32_t i = 0;
    for (; i < M_LOS_STEPS; i++) {
        int16_t next_room_num = room_num;
        sector = Room_GetSector(
            (XYZ_32) { ideal_pos.x, ideal_pos.y + STEP_L, ideal_pos.z },
            &next_room_num);

        if (Room_Get(next_room_num)->flags.swamp) {
            ideal_pos.y = Room_Get(next_room_num)->max_ceiling - STEP_L;
            break;
        }

        sector = Room_GetSector(ideal_pos, &next_room_num);
        height = Room_GetHeight(sector, ideal_pos);
        ceiling = Room_GetCeiling(sector, ideal_pos);

        if (height == NO_HEIGHT || ceiling == NO_HEIGHT || ceiling >= height
            || ideal_pos.y > height || ideal_pos.y < ceiling) {
            break;
        }

        room_num = next_room_num;
        ideal_pos.x += delta.x;
        ideal_pos.y += delta.y;
        ideal_pos.z += delta.z;
    }

    if (i != 0) {
        ideal_pos.x -= delta.x;
        ideal_pos.y -= delta.y;
        ideal_pos.z -= delta.z;
    }
    *pos = ideal_pos;
    *room = room_num;
}

// The camera to its ideal and kept within the floor and the ceiling.
static void M_LookCloseC(
    const GAME_VECTOR *const ideal_in, const XYZ_32 pos_1, const XYZ_32 pos_3)
{
    const GAME_VECTOR ideal = *ideal_in;
    const int16_t lara_room = Lara_GetItem()->room_num;
    XYZ_32 ideal_pos;
    int16_t room_num;
    const SECTOR *sector;
    int32_t height;
    int32_t ceiling;

    if (m_LastState.cam_type == CAM_FIXED) {
        g_Camera.pos.pos = ideal.pos;
        g_Camera.target.pos = pos_3;
    } else {
        g_Camera.pos.x += (ideal.x - g_Camera.pos.x) >> 2;
        g_Camera.pos.y += (ideal.y - g_Camera.pos.y) >> 2;
        g_Camera.pos.z += (ideal.z - g_Camera.pos.z) >> 2;
        g_Camera.target.x += (pos_3.x - g_Camera.target.x) >> 2;
        g_Camera.target.y += (pos_3.y - g_Camera.target.y) >> 2;
        g_Camera.target.z += (pos_3.z - g_Camera.target.z) >> 2;
    }

    g_Camera.target.room_num = lara_room;

    if (g_Camera.type == m_LastState.cam_type) {
        Camera_ApplyBounce();
    }

    Room_GetSector(g_Camera.pos.pos, &g_Camera.pos.room_num);
    ideal_pos = g_Camera.pos.pos;
    room_num = g_Camera.pos.room_num;
    sector = Room_GetSector(ideal_pos, &room_num);
    height = Room_GetHeight(sector, ideal_pos);
    ceiling = Room_GetCeiling(sector, ideal_pos);

    if (ceiling != NO_HEIGHT && height != NO_HEIGHT) {
        if (ceiling > ideal_pos.y - 255 && height < ideal_pos.y + 255
            && height > ceiling) {
            g_Camera.pos.y = (ceiling + height) >> 1;
        } else if (height < ideal_pos.y + 255 && height > ceiling) {
            g_Camera.pos.y = height - 255;
        } else if (ceiling > ideal_pos.y - 255 && height > ceiling) {
            g_Camera.pos.y = ceiling + 255;
        }
    }

    ideal_pos = g_Camera.pos.pos;
    room_num = g_Camera.pos.room_num;
    sector = Room_GetSector(ideal_pos, &room_num);
    height = Room_GetHeight(sector, ideal_pos);
    ceiling = Room_GetCeiling(sector, ideal_pos);

    if (Room_Get(room_num)->flags.swamp) {
        g_Camera.pos.y = Room_Get(room_num)->max_ceiling - STEP_L;
    } else if (
        ideal_pos.y < ceiling || ideal_pos.y > height || ceiling >= height
        || height == NO_HEIGHT || ceiling == NO_HEIGHT) {
        M_LOS(&g_Camera.target, &g_Camera.pos, 0);
    }

    ideal_pos = g_Camera.pos.pos;
    room_num = g_Camera.pos.room_num;
    sector = Room_GetSector(ideal_pos, &room_num);
    height = Room_GetHeight(sector, ideal_pos);
    ceiling = Room_GetCeiling(sector, ideal_pos);

    if (ideal_pos.y < ceiling || ideal_pos.y > height || ceiling >= height
        || height == NO_HEIGHT || ceiling == NO_HEIGHT
        || Room_Get(room_num)->flags.swamp) {
        g_Camera.pos.pos = pos_1;
        g_Camera.pos.room_num = lara_room;
    }

    Room_GetSector(g_Camera.pos.pos, &g_Camera.pos.room_num);
}

#if defined(__DREAMCAST__)
static void M_LookWalk(
    XYZ_32 *const pos, const XYZ_32 delta, int16_t *const room)
{
    if ((g_DC_AsmOff3 & 33554432) != 0) {
        M_LookWalkC(pos, delta, room);
        return;
    }
    // Through aligned words: XYZ_32 is packed.
    int32_t p[3] = { pos->x, pos->y, pos->z };
    const int32_t d[3] = { delta.x, delta.y, delta.z };
    int16_t r = *room;
    const M_CAM3_WALK b = {
        .pos = p, .delta = d, .room = &r, .sw = M_C3Sw, .hc = M_C3HC,
    };
    dc_cam3_look_walk(&b);
    #if defined(DC_EMIT_VERIFY)
    XYZ_32 want = *pos;
    int16_t want_room = *room;
    M_LookWalkC(&want, delta, &want_room);
    M_C3Report(
        "cam3walk",
        want.x == p[0] && want.y == p[1] && want.z == p[2] && want_room == r);
    #endif
    *pos = (XYZ_32) { .x = p[0], .y = p[1], .z = p[2] };
    *room = r;
}

static void M_LookClose(
    const GAME_VECTOR *const ideal, const XYZ_32 pos_1, const XYZ_32 pos_3)
{
    if ((g_DC_AsmOff3 & 33554432) != 0) {
        M_LookCloseC(ideal, pos_1, pos_3);
        return;
    }
    const int32_t p1[3] = { pos_1.x, pos_1.y, pos_1.z };
    const int32_t p3[3] = { pos_3.x, pos_3.y, pos_3.z };
    const M_CAM3_CLOSE b = {
        .cam = &g_Camera.pos,
        .target = &g_Camera.target,
        .ideal = ideal,
        .pos_3 = p3,
        .pos_1 = p1,
        .fixed = m_LastState.cam_type == CAM_FIXED,
        .lara_room = Lara_GetItem()->room_num,
        .same_type = g_Camera.type == m_LastState.cam_type,
        .bounce = Camera_ApplyBounce,
        .hc = M_C3HC,
        .sec = M_CamSec,
        .los = M_C3Los,
    };
    #if defined(DC_EMIT_VERIFY)
    const CAMERA_INFO cam_in = g_Camera;
    const uint32_t seed_in = *Random_GetControlState();
    M_LookCloseC(ideal, pos_1, pos_3);
    const CAMERA_INFO want = g_Camera;
    const uint32_t seed_want = *Random_GetControlState();
    g_Camera = cam_in;
    *Random_GetControlState() = seed_in;
    dc_cam3_look_close(&b);
    M_C3Report(
        "cam3close",
        memcmp(&want, &g_Camera, sizeof(want)) == 0
            && seed_want == *Random_GetControlState());
    #else
    dc_cam3_look_close(&b);
    #endif
}
#else
    #define M_LookWalk M_LookWalkC
    #define M_LookClose M_LookCloseC
#endif

static void M_Look(const ITEM *const item)
{
    LARA_INFO *const lara = Lara_GetLaraInfo();
    const ITEM *const lara_item = Lara_GetItem();
    XYZ_16 head_rot = lara->head_rot;
    XYZ_16 torso_rot = lara->torso_rot;

    lara->torso_rot.x = 0;
    lara->torso_rot.y = 0;
    lara->head_rot.x <<= 1;
    lara->head_rot.y <<= 1;

    CLAMP(lara->head_rot.x, M_MIN_LOOK_TILT, M_MAX_LOOK_TILT);
    CLAMP(lara->head_rot.y, M_MIN_LOOK_ROTATION, M_MAX_LOOK_ROTATION);

    // Get head-relative points in mesh space (faithful), then project the
    // camera's ray onto the head->forward axis to remove breathing-induced
    // roll/tilt wobble without moving the ray off Lara's head.
    const XYZ_32 head_pos = M_GetHeadPos(0, 0, 0);
    XYZ_32 pos_1 = M_GetHeadPos(0, 16, 64);

    int16_t room_num = lara_item->room_num;
    const XYZ_32 sample_pos = { pos_1.x, pos_1.y + STEP_L, pos_1.z };
    const SECTOR *sector = Room_GetSector(sample_pos, &room_num);

    const ROOM *room = Room_Get(room_num);
    if (room->flags.swamp) {
        pos_1.y = room->max_ceiling - STEP_L;
    }

    sector = Room_GetSector(pos_1, &room_num);
    int32_t height = Room_GetHeight(sector, pos_1);
    int32_t ceiling = Room_GetCeiling(sector, pos_1);

    if (height == NO_HEIGHT || ceiling == NO_HEIGHT || ceiling >= height
        || pos_1.y > height || pos_1.y < ceiling) {
        pos_1 = M_GetHeadPos(0, 16, 0);
        room_num = lara_item->room_num;
        sector = Room_GetSector(
            (XYZ_32) { pos_1.x, pos_1.y + STEP_L, pos_1.z }, &room_num);

        room = Room_Get(room_num);
        if (room->flags.swamp) {
            pos_1.y = room->max_ceiling - STEP_L;
        }

        sector = Room_GetSector(pos_1, &room_num);
        height = Room_GetHeight(sector, pos_1);
        ceiling = Room_GetCeiling(sector, pos_1);

        if (height == NO_HEIGHT || ceiling == NO_HEIGHT || ceiling >= height
            || pos_1.y > height || pos_1.y < ceiling) {
            pos_1 = M_GetHeadPos(0, 16, -64);
        }
    }

    XYZ_32 pos_2 = M_GetHeadPos(0, 0, -1024);
    XYZ_32 pos_3 = M_GetHeadPos(0, 0, 2048);

    // Constrain the camera ray to pass through Lara's head (OG behavior), but
    // remove idle-breathing wobble by projecting onto a stable forward axis
    // derived from yaw + pitch (no roll/tilt from torso animation).
    const int16_t yaw = lara_item->rot.y + lara->head_rot.y;
    const int16_t pitch = lara_item->rot.x + lara->head_rot.x;
    const XYZ_32 axis = XYZ_32_FromYawPitch(yaw, pitch, 1 << W2V_SHIFT);

    const int64_t axis_len2 = XYZ_32_GetLength2_64(axis);
    if (axis_len2 != 0) {
        XYZ_32_ProjectPointOntoAxis(head_pos, axis, axis_len2, &pos_1);
        XYZ_32_ProjectPointOntoAxis(head_pos, axis, axis_len2, &pos_2);
        XYZ_32_ProjectPointOntoAxis(head_pos, axis, axis_len2, &pos_3);
    }

    const XYZ_32 delta = {
        .x = (pos_2.x - pos_1.x) >> 3,
        .y = (pos_2.y - pos_1.y) >> 3,
        .z = (pos_2.z - pos_1.z) >> 3,
    };

    XYZ_32 ideal_pos = pos_1;
    room_num = lara_item->room_num;
    M_LookWalk(&ideal_pos, delta, &room_num);

    GAME_VECTOR ideal = {
        .pos = ideal_pos,
        .room_num = room_num,
    };
    if (M_UpdateLaraState()) {
        m_LastLookIdeal.pos = ideal;
        m_LastLookIdeal.target.pos = pos_3;
    } else {
        ideal = m_LastLookIdeal.pos;
        pos_3 = m_LastLookIdeal.target.pos;
    }

    M_Collide(&ideal, M_LOOK_SHIFT, true);
    M_LookClose(&ideal, pos_1, pos_3);
    m_LastState.cam_type = g_Camera.type;

    Camera_UpdateMicPosition();

    lara->head_rot = head_rot;
    lara->torso_rot = torso_rot;
}

static void M_ClampResult(void)
{
    XYZ_32 pos = g_Camera.interp.result.pos;
    int16_t room_num = g_Camera.interp.room_num;
    const SECTOR *const sector = Room_GetSector(pos, &room_num);
    const int32_t height =
        Room_GetHeightEx(sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    const int32_t ceiling = Room_GetCeilingEx(
        sector, pos, &(ROOM_HEIGHT_CTX) { .fix_tilts = true });
    if (height == NO_HEIGHT || ceiling == NO_HEIGHT || height < g_Camera.pos.y
        || ceiling > g_Camera.pos.y) {
        pos = g_Camera.pos.pos;
        room_num = g_Camera.pos.room_num;
    }
    Room_GetSector(pos, &room_num);
    g_Camera.interp.result.pos = pos;
    g_Camera.interp.room_num = room_num;
}

static void M_Reset(void)
{
    const ITEM *const lara_item = Lara_GetItem();
    ASSERT(lara_item != nullptr);
    g_Camera.shift = 0;
    m_LastIdeal.target.pos = lara_item->pos;
    m_LastIdeal.target.pos.y -= WALL_L;
    m_LastIdeal.target.room_num = lara_item->room_num;

    g_Camera.target = m_LastIdeal.target;
    g_Camera.pos = m_LastIdeal.target;
    g_Camera.pos.z -= 100;
}

static void M_Update(
    const ITEM *const item, const bool fixed_camera, int32_t target_y)
{
    Camera_SetChunky(false);
    if (g_Camera.type != CAM_LOOK) {
        m_LastIdeal.target = g_Camera.target;
    }

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
                lara_info->head_rot.x = tilt;
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
        g_Camera.target.x = item->pos.x;
        g_Camera.target.z = item->pos.z;

        if (g_Camera.flags == CF_FOLLOW_CENTRE) {
            const int32_t shift = (bounds->min.z + bounds->max.z) / 2;
            g_Camera.target.pos =
                XYZ_32_OffsetYaw(g_Camera.target.pos, item->rot.y, shift);
        }

        g_Camera.target.room_num = item->room_num;
        g_Camera.target.y = target_y;

        if (g_Camera.fixed_camera != fixed_camera) {
            g_Camera.fixed_camera = true;
            g_Camera.speed = 1;
        } else {
            g_Camera.fixed_camera = false;
        }

        if (g_Camera.speed != 1 && m_LastState.cam_type != CAM_LOOK) {
            g_Camera.target.x =
                ((g_Camera.target.x - m_LastIdeal.target.x) >> 2)
                + m_LastIdeal.target.x;
            g_Camera.target.y =
                ((g_Camera.target.y - m_LastIdeal.target.y) >> 2)
                + m_LastIdeal.target.y;
            g_Camera.target.z =
                ((g_Camera.target.z - m_LastIdeal.target.z) >> 2)
                + m_LastIdeal.target.z;
        }

        Room_GetSector(g_Camera.target.pos, &g_Camera.target.room_num);

        if (g_Camera.type == CAM_CHASE || g_Camera.flags == CF_CHASE_OBJECT) {
            M_Chase(item);
        } else {
            M_Fixed();
        }
    }
}

bool Camera_LOSCheck(
    GAME_VECTOR *const start, GAME_VECTOR *const target, const int32_t shift)
{
    return M_LOS(start, target, shift);
}

bool Camera_Collide(
    GAME_VECTOR *const ideal, const int32_t shift, const bool y_first)
{
    return M_Collide(ideal, shift, y_first);
}

void Camera_Invalidate(void)
{
    m_LastState.cam_type = CAM_FIXED;
}

static const CAMERA_STRATEGY m_Strategy = {
    .get_chase_speed_func = M_GetChaseSpeed,
    .get_look_settings_func = M_GetLookSettingsFunc,
    .clamp_result_func = M_ClampResult,
    .reset_func = M_Reset,
    .update_func = M_Update,
};

REGISTER_CAMERA(CAMERA_MODE_TR3, m_Strategy)
REGISTER_CAMERA(CAMERA_MODE_TR4, m_Strategy)
