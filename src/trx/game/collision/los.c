#include <trx/game/collision/los.h>

#if defined(__DREAMCAST__)
    #include <platform/dreamcast/dc_probe.h>
#else
    #define DC_PROBE(i, stmt) stmt
#endif

#include <trx/core/log.h>
#include <trx/core/utils.h>
#include <trx/game/items.h>
#include <trx/game/matrix.h>
#include <trx/game/objects.h>
#include <trx/game/objects/families.h>
#include <trx/game/rooms.h>

#include <math.h>
#include <string.h>

#define M_CLIP_1 8
#define M_CLIP_2 8
#define M_MAX_LOS_ROOMS 200

static int32_t m_LOSRooms[M_MAX_LOS_ROOMS] = {};
static int32_t m_LOSNumRooms = 0;

static inline bool M_TryPushLOSRoom(const int16_t room_num)
{
    if (m_LOSNumRooms < M_MAX_LOS_ROOMS) {
        m_LOSRooms[m_LOSNumRooms++] = room_num;
        return true;
    }
    return false;
}

static inline bool M_ResetLOSRooms(const int16_t room_num)
{
    m_LOSNumRooms = 0;
    return M_TryPushLOSRoom(room_num);
}

static inline bool M_RoomInLOSRooms(const int16_t room_num)
{
    for (int32_t i = 0; i < m_LOSNumRooms; i++) {
        if (m_LOSRooms[i] == room_num) {
            return true;
        }
    }
    return false;
}

static XYZ_32 M_GetMidPos(const ITEM *const item)
{
    const BOUNDS_16 *const bounds = Item_GetBoundsAccurate(item);
    const XYZ_32 mid = {
        .x = (bounds->min.x + bounds->max.x) / 2,
        .y = (bounds->min.y + bounds->max.y) / 2,
        .z = (bounds->min.z + bounds->max.z) / 2,
    };

    Matrix_PushUnit();
    Matrix_Rot16(item->rot);
    Matrix_TranslateRel32(mid);
    const XYZ_32 pos = {
        .x = item->pos.x + (g_MatrixPtr->_03 >> W2V_SHIFT),
        .y = item->pos.y + (g_MatrixPtr->_13 >> W2V_SHIFT),
        .z = item->pos.z + (g_MatrixPtr->_23 >> W2V_SHIFT),
    };
    Matrix_Pop();

    return pos;
}

// Returns how far the line moves along one axis per sector along another.
static int32_t M_GetSlope(const int32_t delta, const int32_t span)
{
    // Same result either way; the SH-4 has no 64-bit divide (a library call).
    if (delta > -(1 << 20) && delta < (1 << 20)) {
        return (delta * WALL_L) / span;
    }
    return ((int64_t)delta * WALL_L) / span;
}

static int32_t M_ApplySlope(const int32_t slope, const int32_t delta)
{
    return ((int64_t)slope * delta) >> WALL_SHIFT;
}

static int32_t M_CheckXC(
    const GAME_VECTOR *const start, GAME_VECTOR *const target)
{
    const int32_t dx = target->x - start->x;
    if (dx == 0) {
        return 1;
    }

    const int32_t dy = M_GetSlope(target->y - start->y, dx);
    const int32_t dz = M_GetSlope(target->z - start->z, dx);

    int16_t room_num = start->room_num;
    int16_t last_room_num = start->room_num;

    M_ResetLOSRooms(room_num);

    if (dx < 0) {
        XYZ_32 cur_pos;
        cur_pos.x = ROUND_TO_SECTOR(start->x);
        cur_pos.y = start->y + M_ApplySlope(dy, cur_pos.x - start->x);
        cur_pos.z = start->z + M_ApplySlope(dz, cur_pos.x - start->x);

        while (cur_pos.x > target->x) {
            XYZ_32 sample_pos = cur_pos;

            {
                const SECTOR *const sector =
                    Room_GetSector(sample_pos, &room_num);
                const int32_t height = Room_GetHeight(sector, sample_pos);
                const int32_t ceiling = Room_GetCeiling(sector, sample_pos);
                if (cur_pos.y > height || cur_pos.y < ceiling) {
                    target->pos = cur_pos;
                    target->room_num = room_num;
                    return -1;
                }
            }

            if (room_num != last_room_num) {
                last_room_num = room_num;
                M_TryPushLOSRoom(room_num);
            }

            sample_pos.x -= 1;

            {
                const SECTOR *const sector =
                    Room_GetSector(sample_pos, &room_num);
                const int32_t height = Room_GetHeight(sector, sample_pos);
                const int32_t ceiling = Room_GetCeiling(sector, sample_pos);
                if (cur_pos.y > height || cur_pos.y < ceiling) {
                    target->pos = cur_pos;
                    target->room_num = last_room_num;
                    return 0;
                }
            }

            cur_pos.x -= WALL_L;
            cur_pos.y -= dy;
            cur_pos.z -= dz;
        }
    } else {
        XYZ_32 cur_pos;
        cur_pos.x = ROUND_TO_SECTOR_END(start->x);
        cur_pos.y = start->y + M_ApplySlope(dy, cur_pos.x - start->x);
        cur_pos.z = start->z + M_ApplySlope(dz, cur_pos.x - start->x);

        while (cur_pos.x < target->x) {
            XYZ_32 sample_pos = cur_pos;

            {
                const SECTOR *const sector =
                    Room_GetSector(sample_pos, &room_num);
                const int32_t height = Room_GetHeight(sector, sample_pos);
                const int32_t ceiling = Room_GetCeiling(sector, sample_pos);
                if (cur_pos.y > height || cur_pos.y < ceiling) {
                    target->pos = cur_pos;
                    target->room_num = room_num;
                    return -1;
                }
            }

            if (room_num != last_room_num) {
                last_room_num = room_num;
                M_TryPushLOSRoom(room_num);
            }

            sample_pos.x += 1;

            {
                const SECTOR *const sector =
                    Room_GetSector(sample_pos, &room_num);
                const int32_t height = Room_GetHeight(sector, sample_pos);
                const int32_t ceiling = Room_GetCeiling(sector, sample_pos);
                if (cur_pos.y > height || cur_pos.y < ceiling) {
                    target->pos = cur_pos;
                    target->room_num = last_room_num;
                    return 0;
                }
            }

            cur_pos.x += WALL_L;
            cur_pos.y += dy;
            cur_pos.z += dz;
        }
    }

    target->room_num = room_num;
    return 1;
}

static int32_t M_CheckZC(
    const GAME_VECTOR *const start, GAME_VECTOR *const target)
{
    const int32_t dz = target->z - start->z;
    if (dz == 0) {
        return 1;
    }

    const int32_t dx = M_GetSlope(target->x - start->x, dz);
    const int32_t dy = M_GetSlope(target->y - start->y, dz);

    int16_t room_num = start->room_num;
    int16_t last_room_num = start->room_num;

    M_ResetLOSRooms(room_num);

    if (dz < 0) {
        XYZ_32 cur_pos;
        cur_pos.z = ROUND_TO_SECTOR(start->z);
        cur_pos.x = start->x + M_ApplySlope(dx, cur_pos.z - start->z);
        cur_pos.y = start->y + M_ApplySlope(dy, cur_pos.z - start->z);

        while (cur_pos.z > target->z) {
            XYZ_32 sample_pos = cur_pos;

            {
                const SECTOR *const sector =
                    Room_GetSector(sample_pos, &room_num);
                const int32_t height = Room_GetHeight(sector, sample_pos);
                const int32_t ceiling = Room_GetCeiling(sector, sample_pos);
                if (cur_pos.y > height || cur_pos.y < ceiling) {
                    target->pos = cur_pos;
                    target->room_num = room_num;
                    return -1;
                }
            }

            if (room_num != last_room_num) {
                last_room_num = room_num;
                M_TryPushLOSRoom(room_num);
            }

            sample_pos.z -= 1;

            {
                const SECTOR *const sector =
                    Room_GetSector(sample_pos, &room_num);
                const int32_t height = Room_GetHeight(sector, sample_pos);
                const int32_t ceiling = Room_GetCeiling(sector, sample_pos);
                if (cur_pos.y > height || cur_pos.y < ceiling) {
                    target->pos = cur_pos;
                    target->room_num = last_room_num;
                    return 0;
                }
            }

            cur_pos.z -= WALL_L;
            cur_pos.x -= dx;
            cur_pos.y -= dy;
        }
    } else {
        XYZ_32 cur_pos;
        cur_pos.z = ROUND_TO_SECTOR_END(start->z);
        cur_pos.x = start->x + M_ApplySlope(dx, cur_pos.z - start->z);
        cur_pos.y = start->y + M_ApplySlope(dy, cur_pos.z - start->z);

        while (cur_pos.z < target->z) {
            XYZ_32 sample_pos = cur_pos;

            {
                const SECTOR *const sector =
                    Room_GetSector(sample_pos, &room_num);
                const int32_t height = Room_GetHeight(sector, sample_pos);
                const int32_t ceiling = Room_GetCeiling(sector, sample_pos);
                if (cur_pos.y > height || cur_pos.y < ceiling) {
                    target->pos = cur_pos;
                    target->room_num = room_num;
                    return -1;
                }
            }

            if (room_num != last_room_num) {
                last_room_num = room_num;
                M_TryPushLOSRoom(room_num);
            }

            sample_pos.z += 1;

            {
                const SECTOR *const sector =
                    Room_GetSector(sample_pos, &room_num);
                const int32_t height = Room_GetHeight(sector, sample_pos);
                const int32_t ceiling = Room_GetCeiling(sector, sample_pos);
                if (cur_pos.y > height || cur_pos.y < ceiling) {
                    target->pos = cur_pos;
                    target->room_num = last_room_num;
                    return 0;
                }
            }

            cur_pos.z += WALL_L;
            cur_pos.x += dx;
            cur_pos.y += dy;
        }
    }

    target->room_num = room_num;
    return 1;
}

#if defined(__DREAMCAST__)
// In assembly (creature_sh4.S, dc_los_axis): the walk along one axis, with
// these two for the floor data and the rooms passed. TRX_DC_ASMOFF3 bit
// 524288 turns it off; the C above is its reference.
typedef struct {
    const GAME_VECTOR *start;
    GAME_VECTOR *target;
    int32_t along;
    int32_t across;
    int32_t (*blocked)(int32_t x, int32_t y, int32_t z, int16_t *room);
    void (*push)(int32_t room);
} M_LOS_AXIS;
extern int32_t dc_los_axis(const M_LOS_AXIS *b);
extern uint32_t g_DC_AsmOff3;

static int32_t M_Blocked(
    const int32_t x, const int32_t y, const int32_t z, int16_t *const room)
{
    const XYZ_32 pos = { .x = x, .y = y, .z = z };
    const SECTOR *const sector = Room_GetSector(pos, room);
    const int32_t height = Room_GetHeight(sector, pos);
    const int32_t ceiling = Room_GetCeiling(sector, pos);
    return y > height || y < ceiling;
}

static void M_Push(const int32_t room)
{
    M_TryPushLOSRoom((int16_t)room);
}

static int32_t M_CheckAxis(
    const GAME_VECTOR *const start, GAME_VECTOR *const target, const bool z,
    int32_t (*const ref)(const GAME_VECTOR *, GAME_VECTOR *))
{
    if ((g_DC_AsmOff3 & 524288) != 0) {
        return ref(start, target);
    }
    #if defined(DC_EMIT_VERIFY)
    GAME_VECTOR want = *target;
    const GAME_VECTOR target_in = *target;
    const int32_t want_r = ref(start, &want);
    int32_t want_rooms[M_MAX_LOS_ROOMS];
    memcpy(want_rooms, m_LOSRooms, sizeof(want_rooms));
    const int32_t want_n = m_LOSNumRooms;
    *target = target_in;
    #endif
    // No run along the axis: clear, and the rooms passed left as they were.
    int32_t r = 1;
    if ((z ? target->z - start->z : target->x - start->x) != 0) {
        M_ResetLOSRooms(start->room_num);
        const M_LOS_AXIS b = {
            .start = start,
            .target = target,
            .along = z ? 8 : 0,
            .across = z ? 0 : 8,
            .blocked = M_Blocked,
            .push = M_Push,
        };
        r = dc_los_axis(&b);
        if (r == 2) {
            r = ref(start, target);
        }
    }
    #if defined(DC_EMIT_VERIFY)
    static uint32_t runs;
    static uint32_t bad;
    if ((r != want_r || target->x != want.x || target->y != want.y
         || target->z != want.z || target->room_num != want.room_num
         || m_LOSNumRooms != want_n
         || memcmp(m_LOSRooms, want_rooms, sizeof(int32_t) * want_n) != 0)
        && bad++ < 6) {
        LOG_WARNING(
            "[los-verify] MISMATCH axis %d r %d/%d pos %d %d %d / %d %d %d "
            "room %d/%d rooms %d/%d",
            (int)z, r, want_r, target->x, target->y, target->z, want.x,
            want.y, want.z, target->room_num, want.room_num, m_LOSNumRooms,
            want_n);
    }
    if (++runs % 2000 == 0) {
        LOG_WARNING("[los-verify] %u runs, %u bad", (unsigned)runs, (unsigned)bad);
    }
    #endif
    return r;
}

static int32_t M_CheckX(const GAME_VECTOR *const start, GAME_VECTOR *const target)
{
    return M_CheckAxis(start, target, false, M_CheckXC);
}

static int32_t M_CheckZ(const GAME_VECTOR *const start, GAME_VECTOR *const target)
{
    return M_CheckAxis(start, target, true, M_CheckZC);
}
#else
    #define M_CheckX M_CheckXC
    #define M_CheckZ M_CheckZC
#endif

static int32_t M_ClipTargetSimple(
    const GAME_VECTOR *const start, GAME_VECTOR *const target)
{
    const SECTOR *sector = Room_GetSector(target->pos, &target->room_num);

    // This function exists because of issue #4070.
    int32_t dx = target->x - start->x;
    int32_t dy = target->y - start->y;
    int32_t dz = target->z - start->z;

    const int32_t height = Room_GetHeight(sector, target->pos);
    if (target->y > height && start->y < height) {
        target->y = height;
        target->x = start->x + dx * (height - start->y) / dy;
        target->z = start->z + dz * (height - start->y) / dy;
        return false;
    }

    const int32_t ceiling = Room_GetCeiling(sector, target->pos);
    if (target->y < ceiling && start->y > ceiling) {
        target->y = ceiling;
        target->x = start->x + dx * (ceiling - start->y) / dy;
        target->z = start->z + dz * (ceiling - start->y) / dy;
        return false;
    }

    return true;
}

static int32_t M_ClipTargetWithSlopes(
    const GAME_VECTOR *const start, GAME_VECTOR *const target)
{
    int16_t room_num = target->room_num;
    const SECTOR *sector = Room_GetSector(target->pos, &room_num);

    if (target->y > Room_GetHeight(sector, target->pos)) {
        const XYZ_32 origin = {
            start->x + ((M_CLIP_1 - 1) * (target->x - start->x) / M_CLIP_1),
            start->y + ((M_CLIP_1 - 1) * (target->y - start->y) / M_CLIP_1),
            start->z + ((M_CLIP_1 - 1) * (target->z - start->z) / M_CLIP_1),
        };
        XYZ_32 delta;
        for (int32_t i = M_CLIP_2 - 1; i > 0; i--) {
            delta.x = origin.x + (i * (target->x - origin.x) / M_CLIP_2);
            delta.y = origin.y + (i * (target->y - origin.y) / M_CLIP_2);
            delta.z = origin.z + (i * (target->z - origin.z) / M_CLIP_2);
            sector = Room_GetSector(delta, &room_num);
            if (delta.y < Room_GetHeight(sector, delta)) {
                break;
            }
        }

        target->pos = delta;
        target->room_num = room_num;
        return 0;
    }

    if (target->y < Room_GetCeiling(sector, target->pos)) {
        const XYZ_32 origin = {
            start->x + ((M_CLIP_1 - 1) * (target->x - start->x) / M_CLIP_1),
            start->y + ((M_CLIP_1 - 1) * (target->y - start->y) / M_CLIP_1),
            start->z + ((M_CLIP_1 - 1) * (target->z - start->z) / M_CLIP_1),
        };
        XYZ_32 delta;
        for (int32_t i = M_CLIP_2 - 1; i > 0; i--) {
            delta.x = origin.x + (i * (target->x - origin.x) / M_CLIP_2);
            delta.y = origin.y + (i * (target->y - origin.y) / M_CLIP_2);
            delta.z = origin.z + (i * (target->z - origin.z) / M_CLIP_2);

            sector = Room_GetSector(delta, &room_num);
            if (delta.y > Room_GetCeiling(sector, delta)) {
                break;
            }
        }

        target->pos = delta;
        target->room_num = room_num;
        return 0;
    }

    return 1;
}

// This routine transforms the world-space LOS segment [start,target] into the
// object's local coordinates (undoing its translation and Y-rotation), then
// performs a slab intersection test against that local AABB. The first
// smashable item hit is returned, or NO_ITEM if none.
//
// (AABB = Axis-Aligned Bounding Box. It's the rectangular box defined by
// bounds->min/max along X,Y,Z in the object's local space (no rotation).)
//
// @param start  World-space ray origin
// @param target World-space ray end
// @param item   Item to check
// @return       Whether the item collides with the segment
bool LOS_CheckItemIntersectSegment(
    const GAME_VECTOR *const start, GAME_VECTOR *const target,
    const ITEM *const item)
{
    const double dx = target->x - start->x;
    const double dy = target->y - start->y;
    const double dz = target->z - start->z;

    // Translate into object-local space
    const double ox = start->x - item->pos.x;
    const double oy = start->y - item->pos.y;
    const double oz = start->z - item->pos.z;

    // Unrotate by -rot.y around Y axis
    const float c = cosf(-item->rot.y * M_PI / DEG_180);
    const float s = sinf(-item->rot.y * M_PI / DEG_180);
    const double lx = ox * c + oz * s;
    const double ly = oy;
    const double lz = oz * c - ox * s;
    const double ldx = dx * c + dz * s;
    const double ldy = dy;
    const double ldz = dz * c - dx * s;

    // Local AABB extents from item's bounds
    const BOUNDS_16 *const orig_bounds = Item_GetBoundsAccurate(item);
    BOUNDS_16 patched_bounds = *orig_bounds;

    const BOUNDS_16 *const bounds = &patched_bounds;

    // Parametric interval [t0..t1] in Q14 fixed-point
    double t0 = 0.0;
    double t1 = 1.0;

    // X slab
    if (ldx != 0) {
        double t_near = (double)(bounds->min.x - lx) / ldx;
        double t_far = (double)(bounds->max.x - lx) / ldx;
        if (t_near > t_far) {
            SWAP(t_near, t_far);
        }
        if (t_near > t1 || t_far < t0) {
            return false;
        }
        CLAMPL(t0, t_near);
        CLAMPG(t1, t_far);
    } else if (lx < bounds->min.x || lx > bounds->max.x) {
        return false;
    }

    // Y slab
    if (ldy != 0) {
        double t_near = (double)(bounds->min.y - ly) / ldy;
        double t_far = (double)(bounds->max.y - ly) / ldy;
        if (t_near > t_far) {
            SWAP(t_near, t_far);
        }
        if (t_near > t1 || t_far < t0) {
            return false;
        }
        CLAMPL(t0, t_near);
        CLAMPG(t1, t_far);
    } else if (ly < bounds->min.y || ly > bounds->max.y) {
        return false;
    }

    // Z slab
    if (ldz != 0) {
        double t_near = (double)(bounds->min.z - lz) / ldz;
        double t_far = (double)(bounds->max.z - lz) / ldz;
        if (t_near > t_far) {
            SWAP(t_near, t_far);
        }
        if (t_near > t1 || t_far < t0) {
            return false;
        }
        CLAMPL(t0, t_near);
        CLAMPG(t1, t_far);
    } else if (lz < bounds->min.z || lz > bounds->max.z) {
        return false;
    }

    // world-space hit position = start + t0 * (target-start)
    target->x = start->x + t0 * dx;
    target->y = start->y + t0 * dy;
    target->z = start->z + t0 * dz;

    return true;
}

// Smashes all objects along the ray path.
//
// @param start  World-space ray origin
// @param target World-space ray end
// @return       First smashable item's index, or NO_ITEM if none hit
// Whether an object is smashable or shattering: asked of every item at every
// shot, so the answer is kept by object, until a family changes.
static bool M_IsSmashable(const OBJECT_ID object_id)
{
    #define M_SMASH_CACHE_SIZE 1024
    static uint8_t cache[M_SMASH_CACHE_SIZE]; // 0 not known, 1 no, 2 yes
    static int32_t revision = -1;
    const bool cached = object_id >= 0 && object_id < M_SMASH_CACHE_SIZE;
    if (cached) {
        if (revision != ObjectFamily_GetRevision()) {
            memset(cache, 0, sizeof(cache));
            revision = ObjectFamily_GetRevision();
        }
        if (cache[object_id] != 0) {
            return cache[object_id] == 2;
        }
    }
    const bool smashable = ObjectFamily_Has(object_id, OBJ_FAMILY_SMASHABLE)
        || ObjectFamily_Has(object_id, OBJ_FAMILY_SHATTERABLE)
        || ObjectFamily_Has(object_id, OBJ_FAMILY_HEAVY_SHATTERABLE);
    if (cached) {
        cache[object_id] = smashable ? 2 : 1;
    }
    return smashable;
}

int32_t LOS_CheckSmashable(
    const GAME_VECTOR start, const GAME_VECTOR target,
    XYZ_32 *const out_hit_pos)
{
    int32_t best_dist = INT32_MAX;
    int16_t best_item_num = NO_ITEM;

    for (int16_t item_num = 0; item_num < Item_GetTotalCount(); item_num++) {
        const ITEM *const item = Item_Get(item_num);
        if (item->is_finished) {
            continue;
        }
        if (!M_IsSmashable(item->object_id)) {
            continue;
        }

        GAME_VECTOR hit_pos = target;
        if (!LOS_CheckItemIntersectSegment(&start, &hit_pos, item)) {
            continue;
        }

        // Confirm item is reachable via visible rooms
        {
            GAME_VECTOR start_tmp = start;
            GAME_VECTOR target_tmp = {
                .pos = M_GetMidPos(item),
            };
            if (!LOS_Check(&start_tmp, &target_tmp, false)) {
                continue;
            }
        }

        // Ray segment intersects the object's local AABB
        const int32_t dist = Item_GetDistance(item, start.pos);
        if (dist < best_dist) {
            best_dist = dist;
            best_item_num = item_num;
            if (out_hit_pos != nullptr) {
                *out_hit_pos = hit_pos.pos;
            }
        }
    }

    return best_item_num;
}

static bool M_LOSCheck(
    const GAME_VECTOR *start, GAME_VECTOR *target, bool use_slope_clipping);

bool LOS_Check(
    const GAME_VECTOR *const start, GAME_VECTOR *const target,
    const bool use_slope_clipping)
{
    bool result;
    DC_PROBE(
        DC_PROBE_C_LOS, result = M_LOSCheck(start, target, use_slope_clipping));
#if defined(__DREAMCAST__)
    g_DC_ProbeUs[DC_PROBE_C_LOSN]++; // TEMP
#endif
    return result;
}

static bool M_LOSCheck(
    const GAME_VECTOR *const start, GAME_VECTOR *const target,
    const bool use_slope_clipping)
{
    const int32_t dx = ABS(target->x - start->x);
    const int32_t dz = ABS(target->z - start->z);

    int32_t los1;
    int32_t los2;
    if (dz > dx) {
        los1 = M_CheckX(start, target);
        los2 = M_CheckZ(start, target);
    } else {
        los1 = M_CheckZ(start, target);
        los2 = M_CheckX(start, target);
    }

    if (!los2) {
        return false;
    }

    if (dx == 0 && dz == 0) {
        target->room_num = start->room_num;
    }

    const bool clip_result =
        (use_slope_clipping ? M_ClipTargetWithSlopes(start, target)
                            : M_ClipTargetSimple(start, target));
    return clip_result && los1 == 1 && los2 == 1;
}
