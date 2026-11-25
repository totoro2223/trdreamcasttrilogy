#include "dc_ragdoll.h"
#include "dc_mode.h"

#include <trx/core/math/trig.h>
#include <trx/game/anims/common.h>
#include <trx/game/const.h>
#include <trx/game/interpolation.h>
#include <trx/game/items/anim.h>
#include <trx/game/items/manager.h>
#include <trx/game/lara/common.h>
#include <trx/game/objects/common.h>
#include <trx/game/rooms.h>

#include <math.h>
#include <string.h>

// The body as points (Verlet): each mesh's origin, and a tip for a mesh
// with no child further out. A mesh holds its origin, its children's origins
// and its far end rigid; each joint keeps its part within the cone the
// model's own animations sweep it through. Each mesh's turn is then read back
// out of its points. Which skeletons are human is told from their shape
// (M_IsHumanoid): every game and enemy has its own.
#define M_MAX_MESHES 32
#define M_MAX_POINTS 48
#define M_MAX_LINKS 160
#define M_MAX_TYPES 8 // human enemy kinds in a level
#define M_MAX_SIMS 2 // bodies moving at once; a third death plays its anim
#define M_MAX_BODIES 16 // bodies at rest; the oldest goes back to its anim
#define M_ITERATIONS 3
#define M_DAMP 0.98f
#define M_MAX_SPEED 64.0f // per tick, a point; a fall in the game tops out at 128
#define M_RADIUS 16.0f
#define M_STEP_FACE 64.0f // a point this far below a new floor came in sideways
#define M_PUSH_ALL 3.0f // per tick, the whole body, along the shot
#define M_PUSH_HIT 16.0f // per tick, more near where the shot struck
#define M_PUSH_REACH 320.0f
#define M_STILL 3.0f // mean squared movement a tick under which a body rests
#define M_STILL_TICKS 15
#define M_MAX_TICKS 150
#define M_RATE 256
#define M_JOINT_SLACK 0.35f // 20 degrees past what the animations reach
#define M_JOINT_MIN 0.6f // 35 degrees
#define M_JOINT_MAX 2.6f
#define M_JOINT_LOOSE 1.2f // 70 degrees, with no animations to go by
#define M_CONE_GIVE 0.5f // the share of a cone's correction taken at once
#define M_SHARED_ORIGIN 16 // a child this close to its parent's origin shares it
#define M_TALL 250 // how far up the chest and down the legs of a human go

typedef float M_VEC[3];
typedef float M_MAT[9]; // row major; a column is an axis of the mesh

typedef struct {
    uint8_t a;
    uint8_t b;
    float len;
} M_LINK;

// What a kind of enemy's skeleton makes of points, worked out at level load.
typedef struct {
    OBJECT_ID object_id;
    int32_t mesh_count;
    int32_t point_count;
    int8_t parent[M_MAX_MESHES];
    int8_t org[M_MAX_MESHES]; // the point at its origin
    int8_t end[M_MAX_MESHES]; // the point at its far end
    int8_t hint[M_MAX_MESHES]; // a third point that fixes its turn, or -1
    int8_t tip_of[M_MAX_POINTS]; // a tip's mesh, or -1 for an origin
    M_VEC tip[M_MAX_MESHES]; // a tip in its mesh's own frame
    M_VEC cone_axis[M_MAX_MESHES]; // the joint's middle, in the parent's frame
    M_VEC twist[M_MAX_MESHES]; // square to it: what a limb's turn follows
    float cone_cos[M_MAX_MESHES];
    float cone_sin[M_MAX_MESHES];
} M_TYPE;

typedef struct {
    int16_t item_num;
    OBJECT_ID object_id;
    bool shown; // its pose filled in
    uint32_t age;
    XYZ_16 rots[2][M_MAX_MESHES]; // the last tick's and this tick's
    XYZ_16 offset[2];
    BOUNDS_16 bounds;
} M_BODY;

typedef struct {
    M_BODY *body; // null: a free slot
    const M_TYPE *type;
    M_VEC p[M_MAX_POINTS];
    M_VEC old[M_MAX_POINTS];
    int16_t room[M_MAX_POINTS];
    M_MAT frame[M_MAX_MESHES]; // each mesh's frame from its points
    M_MAT rest[M_MAX_MESHES]; // each mesh's turn from that frame
    M_LINK links[M_MAX_LINKS];
    int32_t link_count;
    float cone_cos[M_MAX_MESHES]; // the kind's, widened to take the pose it
    float cone_sin[M_MAX_MESHES]; // died in
    int32_t ticks;
    int32_t still;
} M_SIM;

static M_TYPE m_Types[M_MAX_TYPES];
static int32_t m_TypeCount;
static M_BODY m_Bodies[M_MAX_BODIES];
static M_SIM m_Sims[M_MAX_SIMS];
static uint32_t m_Age;
static bool m_Ready;

static float M_Dot(const M_VEC a, const M_VEC b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void M_Sub(M_VEC r, const M_VEC a, const M_VEC b)
{
    r[0] = a[0] - b[0];
    r[1] = a[1] - b[1];
    r[2] = a[2] - b[2];
}

static void M_Cross(M_VEC r, const M_VEC a, const M_VEC b)
{
    const float x = a[1] * b[2] - a[2] * b[1];
    const float y = a[2] * b[0] - a[0] * b[2];
    const float z = a[0] * b[1] - a[1] * b[0];
    r[0] = x;
    r[1] = y;
    r[2] = z;
}

static float M_Normalize(M_VEC v)
{
    const float len = sqrtf(M_Dot(v, v));
    if (len > 1e-4f) {
        const float inv = 1.0f / len;
        v[0] *= inv;
        v[1] *= inv;
        v[2] *= inv;
    }
    return len;
}

static void M_MulVec(M_VEC r, const M_MAT m, const M_VEC v)
{
    const float x = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
    const float y = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
    const float z = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
    r[0] = x;
    r[1] = y;
    r[2] = z;
}

static void M_MulVecT(M_VEC r, const M_MAT m, const M_VEC v)
{
    const float x = m[0] * v[0] + m[3] * v[1] + m[6] * v[2];
    const float y = m[1] * v[0] + m[4] * v[1] + m[7] * v[2];
    const float z = m[2] * v[0] + m[5] * v[1] + m[8] * v[2];
    r[0] = x;
    r[1] = y;
    r[2] = z;
}

// r = a * b, or a^T * b
static void M_Mul(M_MAT r, const M_MAT a, const M_MAT b, const bool a_t)
{
    M_MAT t;
    for (int32_t i = 0; i < 3; i++) {
        for (int32_t j = 0; j < 3; j++) {
            float s = 0.0f;
            for (int32_t k = 0; k < 3; k++) {
                s += (a_t ? a[k * 3 + i] : a[i * 3 + k]) * b[k * 3 + j];
            }
            t[i * 3 + j] = s;
        }
    }
    memcpy(r, t, sizeof(t));
}

// As M_RotYXZ (matrix.c): Y, then X, then Z.
static void M_FromRot(M_MAT m, const XYZ_16 rot)
{
    const float k = 6.28318531f / 65536.0f;
    const float sy = sinf(rot.y * k), cy = cosf(rot.y * k);
    const float sx = sinf(rot.x * k), cx = cosf(rot.x * k);
    const float sz = sinf(rot.z * k), cz = cosf(rot.z * k);
    m[0] = cy * cz + sy * sx * sz;
    m[1] = -cy * sz + sy * sx * cz;
    m[2] = sy * cx;
    m[3] = cx * sz;
    m[4] = cx * cz;
    m[5] = -sx;
    m[6] = -sy * cz + cy * sx * sz;
    m[7] = sy * sz + cy * sx * cz;
    m[8] = cy * cx;
}

// As M_ToRot16 (matrix.c), with the engine's own arctangent
// (Math_Atan(x, y) is atan2(y, x)): newlib's is several times slower.
static XYZ_16 M_ToRot(const M_MAT m)
{
    const float k = 16384.0f;
    float sx = -m[5];
    sx = sx > 1.0f ? 1.0f : (sx < -1.0f ? -1.0f : sx);
    const float cx = sqrtf(m[2] * m[2] + m[8] * m[8]);
    if (cx > 1e-4f) {
        return (XYZ_16) {
            .x = (int16_t)Math_Atan((int32_t)(cx * k), (int32_t)(sx * k)),
            .y = (int16_t)Math_Atan((int32_t)(m[8] * k), (int32_t)(m[2] * k)),
            .z = (int16_t)Math_Atan((int32_t)(m[4] * k), (int32_t)(m[3] * k)),
        };
    }
    return (XYZ_16) {
        .x = (int16_t)(sx < 0.0f ? -16384 : 16384),
        .y = (int16_t)Math_Atan((int32_t)(m[0] * k), (int32_t)(-m[6] * k)),
        .z = 0,
    };
}

static void M_SetColumns(M_MAT f, const M_VEC x, const M_VEC y, const M_VEC z)
{
    for (int32_t r = 0; r < 3; r++) {
        f[r * 3 + 0] = x[r];
        f[r * 3 + 1] = y[r];
        f[r * 3 + 2] = z[r];
    }
}

static void M_FrameFrom(M_MAT f, const M_VEC along, const M_VEC hint)
{
    M_VEC x;
    M_VEC y;
    M_VEC z;
    memcpy(x, along, sizeof(x));
    M_Normalize(x);
    M_Cross(z, x, hint);
    M_Normalize(z);
    M_Cross(y, z, x);
    M_SetColumns(f, x, y, z);
}

static void M_World(M_MAT w, const M_SIM *s, int32_t mesh);

// The least turn that takes the last frame's x onto x (Rodrigues).
static void M_Transport(M_MAT f, const float *const o, const M_VEC x)
{
    const M_VEC ox = { o[0], o[3], o[6] };
    M_VEC v;
    M_Cross(v, ox, x);
    const float c = M_Dot(ox, x);
    if (c > 0.99999f || c < -0.9f) {
        memcpy(f, o, sizeof(M_MAT));
        return;
    }
    const float k = 1.0f / (1.0f + c);
    const M_VEC a = { o[1], o[4], o[7] };
    M_VEC va;
    M_VEC vva;
    M_Cross(va, v, a);
    M_Cross(vva, v, va);
    const M_VEC y = { a[0] + va[0] + vva[0] * k, a[1] + va[1] + vva[1] * k,
                      a[2] + va[2] + vva[2] * k };
    M_VEC z;
    M_Cross(z, x, y);
    M_Normalize(z);
    M_VEC yy;
    M_Cross(yy, z, x);
    M_SetColumns(f, x, yy, z);
}

// A frame with x along the mesh. A mesh with a third point of its own takes
// z square to it. A limb takes its turn about its length from its parent
// (else its twist would drift and carry its joint's cone round with it);
// only while it lies along that direction does it turn its last frame the
// least way instead.
static void M_Frame(M_MAT f, const M_SIM *const s, const int32_t mesh)
{
    const M_TYPE *const t = s->type;
    M_VEC x;
    M_Sub(x, s->p[t->end[mesh]], s->p[t->org[mesh]]);
    if (t->hint[mesh] >= 0) {
        M_VEC h;
        M_Sub(h, s->p[t->hint[mesh]], s->p[t->org[mesh]]);
        M_FrameFrom(f, x, h);
        return;
    }
    M_Normalize(x);
    if (mesh > 0) {
        M_MAT wp;
        M_World(wp, s, t->parent[mesh]);
        M_VEC h;
        M_MulVec(h, wp, t->twist[mesh]);
        if (fabsf(M_Dot(x, h)) < 0.95f) {
            M_FrameFrom(f, x, h);
            return;
        }
    }
    M_Transport(f, s->frame[mesh], x);
}

static void M_UpdateFrames(M_SIM *const s)
{
    for (int32_t i = 0; i < s->type->mesh_count; i++) {
        M_MAT f;
        M_Frame(f, s, i);
        memcpy(s->frame[i], f, sizeof(f));
    }
}

static void M_World(M_MAT w, const M_SIM *const s, const int32_t mesh)
{
    M_Mul(w, s->frame[mesh], s->rest[mesh], false);
}

// --- the skeleton ---------------------------------------------------------

static void M_BonePos(M_VEC r, const OBJECT *const obj, const int32_t mesh)
{
    const ANIM_BONE *const bone = Object_GetBone(obj, mesh - 1);
    r[0] = (float)bone->pos.x;
    r[1] = (float)bone->pos.y;
    r[2] = (float)bone->pos.z;
}

static bool M_Parents(const OBJECT *const obj, int8_t *const parent)
{
    int8_t stack[M_MAX_MESHES];
    int32_t depth = 0;
    int32_t cur = 0;
    parent[0] = -1;
    for (int32_t i = 1; i < obj->mesh_count; i++) {
        const ANIM_BONE *const bone = Object_GetBone(obj, i - 1);
        if (bone->matrix_pop) {
            if (depth == 0) {
                return false;
            }
            cur = stack[--depth];
        }
        if (bone->matrix_push) {
            if (depth == M_MAX_MESHES) {
                return false;
            }
            stack[depth++] = (int8_t)cur;
        }
        parent[i] = (int8_t)cur;
        cur = i;
    }
    return true;
}

// Standing at rest (no turns): the highest (least y) and lowest points of a
// mesh's whole branch, from the mesh's own origin.
static void M_Reach(
    const OBJECT *const obj, const int8_t *const parent, const int32_t top,
    float *const up, float *const down)
{
    M_VEC pos[M_MAX_MESHES];
    pos[top][0] = pos[top][1] = pos[top][2] = 0.0f;
    *up = 0.0f;
    *down = 0.0f;
    for (int32_t i = top + 1; i < obj->mesh_count; i++) {
        int32_t a = i;
        while (a > top && parent[a] != top) {
            a = parent[a];
        }
        if (a <= top || parent[a] != top) {
            continue; // not in the branch
        }
        M_VEC b;
        M_BonePos(b, obj, i);
        for (int32_t k = 0; k < 3; k++) {
            pos[i][k] = pos[parent[i]][k] + b[k];
        }
        *up = -pos[i][1] > *up ? -pos[i][1] : *up;
        *down = pos[i][1] > *down ? pos[i][1] : *down;
    }
}

// Two legs and a chest off the hips, the chest with arms and a head (three or
// more branches) reaching up, the legs reaching down.
static bool M_IsHumanoid(const OBJECT *const obj, const int8_t *const parent)
{
    int32_t kids[M_MAX_MESHES] = {};
    for (int32_t i = 1; i < obj->mesh_count; i++) {
        kids[parent[i]]++;
    }
    if (kids[0] != 3) {
        return false;
    }
    int32_t chests = 0;
    int32_t legs = 0;
    for (int32_t i = 1; i < obj->mesh_count; i++) {
        if (parent[i] != 0) {
            continue;
        }
        M_VEC b;
        M_BonePos(b, obj, i);
        float up;
        float down;
        M_Reach(obj, parent, i, &up, &down);
        if (kids[i] >= 3 && up - b[1] > M_TALL) {
            chests++;
        } else if (kids[i] <= 1 && down + b[1] > M_TALL) {
            legs++;
        }
    }
    return chests == 1 && legs == 2;
}

// Where an animation frame puts a part's far end, seen from its parent.
static void M_EndDir(
    M_VEC r, const M_TYPE *const t, const OBJECT *const obj,
    const int32_t mesh, const XYZ_16 *const rots)
{
    M_VEC local;
    const int32_t end = t->end[mesh];
    if (t->tip_of[end] == mesh) {
        memcpy(local, t->tip[mesh], sizeof(local));
    } else {
        for (int32_t c = mesh + 1; c < t->mesh_count; c++) {
            if (t->parent[c] == mesh && t->org[c] == end) {
                M_BonePos(local, obj, c);
                break;
            }
        }
    }
    M_MAT m;
    M_FromRot(m, rots[mesh]);
    M_MulVec(r, m, local);
    M_Normalize(r);
}

static void M_BuildType(M_TYPE *const t, const OBJECT_ID object_id)
{
    const OBJECT *const obj = Object_Get(object_id);
    t->object_id = object_id;
    t->mesh_count = obj->mesh_count;
    M_Parents(obj, t->parent);
    memset(t->tip_of, -1, sizeof(t->tip_of));

    int32_t n = 0;
    for (int32_t i = 0; i < t->mesh_count; i++) {
        M_VEC b = { 0.0f, 0.0f, 0.0f };
        if (i > 0) {
            M_BonePos(b, obj, i);
        }
        t->org[i] = i > 0 && sqrtf(M_Dot(b, b)) < M_SHARED_ORIGIN
            ? t->org[t->parent[i]]
            : (int8_t)n++;
    }
    for (int32_t i = 0; i < t->mesh_count; i++) {
        t->end[i] = -1;
        for (int32_t c = i + 1; c < t->mesh_count && t->end[i] < 0; c++) {
            if (t->parent[c] == i && t->org[c] != t->org[i]) {
                t->end[i] = t->org[c];
            }
        }
        if (t->end[i] < 0) {
            const OBJECT_MESH *const m = Object_GetMesh(obj->mesh_idx + i);
            M_VEC c = { m->center.x, m->center.y, m->center.z };
            float len = sqrtf(M_Dot(c, c));
            if (len < 1.0f) {
                c[0] = 0.0f;
                c[1] = i == 0 ? -1.0f : 1.0f;
                c[2] = 0.0f;
                len = 1.0f;
            }
            const float scale = (len < 48.0f ? 48.0f : len) / len;
            for (int32_t k = 0; k < 3; k++) {
                t->tip[i][k] = c[k] * scale;
            }
            t->tip_of[n] = (int8_t)i;
            t->end[i] = (int8_t)n++;
        }
    }
    t->point_count = n;

    // A third point: the child origin most square to the mesh's length.
    for (int32_t i = 0; i < t->mesh_count; i++) {
        t->hint[i] = -1;
        M_VEC x;
        M_EndDir(x, t, obj, i, (XYZ_16[M_MAX_MESHES]) {});
        float best = 0.3f;
        for (int32_t c = i + 1; c < t->mesh_count; c++) {
            if (t->parent[c] != i || t->org[c] == t->org[i]
                || t->org[c] == t->end[i]) {
                continue;
            }
            M_VEC b;
            M_BonePos(b, obj, c);
            M_Normalize(b);
            M_VEC s;
            M_Cross(s, x, b);
            const float sin = sqrtf(M_Dot(s, s));
            if (sin > best) {
                best = sin;
                t->hint[i] = t->org[c];
            }
        }
    }

    // Each joint's cone: the middle and the widest of where the animations
    // turn the part, seen from its parent.
    M_VEC sum[M_MAX_MESHES] = {};
    int32_t frames_seen = 0;
    for (int32_t pass = 0; pass < 2; pass++) {
        float widest[M_MAX_MESHES] = {};
        for (int32_t a = 0; a < obj->anim_count; a++) {
            const ANIM *const anim = Object_GetAnim(obj, a);
            if (anim->frame_ptr == nullptr || anim->interpolation == 0) {
                continue;
            }
            const int32_t keys =
                (anim->frame_end - anim->frame_base) / anim->interpolation + 1;
            for (int32_t f = 0; f < keys; f++) {
                const XYZ_16 *const rots =
                    Anim_GetFrameRots(&anim->frame_ptr[f]);
                frames_seen += pass == 0 ? 1 : 0;
                for (int32_t i = 1; i < t->mesh_count; i++) {
                    M_VEC d;
                    M_EndDir(d, t, obj, i, rots);
                    if (pass == 0) {
                        for (int32_t k = 0; k < 3; k++) {
                            sum[i][k] += d[k];
                        }
                    } else {
                        float c = M_Dot(d, t->cone_axis[i]);
                        c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
                        const float angle = acosf(c);
                        widest[i] = angle > widest[i] ? angle : widest[i];
                    }
                }
            }
        }
        float widest_all = 0.0f;
        for (int32_t i = 1; i < t->mesh_count; i++) {
            widest_all = widest[i] > widest_all ? widest[i] : widest_all;
        }
        // Frames that never move anything (an object that borrows another's
        // animations has a stand-in of its own): nothing to measure.
        if (pass == 1 && widest_all < 0.05f) {
            frames_seen = 0;
        }
        for (int32_t i = 1; i < t->mesh_count; i++) {
            if (pass == 0) {
                memcpy(t->cone_axis[i], sum[i], sizeof(M_VEC));
                if (M_Normalize(t->cone_axis[i]) < 1e-3f) {
                    M_EndDir(
                        t->cone_axis[i], t, obj, i,
                        (XYZ_16[M_MAX_MESHES]) {});
                }
            } else {
                // An object that borrows another's animations has none of its
                // own to measure: a wide cone round its standing pose.
                float angle = frames_seen > 0 ? widest[i] + M_JOINT_SLACK
                                              : M_JOINT_LOOSE;
                angle = angle < M_JOINT_MIN ? M_JOINT_MIN
                    : angle > M_JOINT_MAX   ? M_JOINT_MAX
                                            : angle;
                t->cone_cos[i] = cosf(angle);
                t->cone_sin[i] = sinf(angle);
                // The parent's axis least along the cone's, made square to it.
                const float *const c = t->cone_axis[i];
                const int32_t k = fabsf(c[0]) < fabsf(c[1])
                    ? (fabsf(c[0]) < fabsf(c[2]) ? 0 : 2)
                    : (fabsf(c[1]) < fabsf(c[2]) ? 1 : 2);
                M_VEC e = { 0.0f, 0.0f, 0.0f };
                e[k] = 1.0f;
                const float along = M_Dot(e, c);
                for (int32_t j = 0; j < 3; j++) {
                    t->twist[i][j] = e[j] - c[j] * along;
                }
                M_Normalize(t->twist[i]);
            }
        }
    }
}

static const M_TYPE *M_GetType(const OBJECT_ID object_id)
{
    for (int32_t i = 0; i < m_TypeCount; i++) {
        if (m_Types[i].object_id == object_id) {
            return &m_Types[i];
        }
    }
    return nullptr;
}

// --- the physics ----------------------------------------------------------

static void M_AddLink(M_SIM *const s, const int32_t a, const int32_t b)
{
    if (a == b || s->link_count >= M_MAX_LINKS) {
        return;
    }
    for (int32_t i = 0; i < s->link_count; i++) {
        const M_LINK *const l = &s->links[i];
        if ((l->a == a && l->b == b) || (l->a == b && l->b == a)) {
            return;
        }
    }
    M_VEC d;
    M_Sub(d, s->p[b], s->p[a]);
    const float len = sqrtf(M_Dot(d, d));
    if (len < 4.0f) {
        return;
    }
    s->links[s->link_count++] =
        (M_LINK) { .a = (uint8_t)a, .b = (uint8_t)b, .len = len };
}

static void M_SolveLink(M_SIM *const s, const M_LINK *const l)
{
    float *const a = s->p[l->a];
    float *const b = s->p[l->b];
    M_VEC d;
    M_Sub(d, b, a);
    const float len = sqrtf(M_Dot(d, d));
    if (len < 1e-3f) {
        return;
    }
    const float k = 0.5f * (len - l->len) / len;
    for (int32_t i = 0; i < 3; i++) {
        a[i] += d[i] * k;
        b[i] -= d[i] * k;
    }
}

// The child point b kept within the cone round the axis seen from a.
static void M_Cone(
    M_SIM *const s, const int32_t a, const int32_t b, const M_VEC axis,
    const float cos_max, const float sin_max)
{
    M_VEC d;
    M_Sub(d, s->p[b], s->p[a]);
    const float len = M_Normalize(d);
    const float c = M_Dot(d, axis);
    if (len < 1e-3f || c >= cos_max) {
        return;
    }
    M_VEC perp = { d[0] - axis[0] * c, d[1] - axis[1] * c, d[2] - axis[2] * c };
    if (M_Normalize(perp) < 1e-4f) {
        return;
    }
    for (int32_t i = 0; i < 3; i++) {
        const float want =
            s->p[a][i] + (axis[i] * cos_max + perp[i] * sin_max) * len;
        s->p[b][i] += (want - s->p[b][i]) * M_CONE_GIVE;
    }
}

static bool M_Collide(M_SIM *const s, const int32_t i)
{
    float *const p = s->p[i];
    float *const old = s->old[i];
    int16_t room = s->room[i];
    const XYZ_32 pos = { (int32_t)p[0], (int32_t)p[1], (int32_t)p[2] };
    const SECTOR *const sector = Room_GetSector(pos, &room);
    const int32_t height =
        sector != nullptr ? Room_GetHeight(sector, pos) : NO_HEIGHT;
    if (height == NO_HEIGHT || old[1] > (float)height + M_STEP_FACE) {
        // A wall, or the face of a step: back where it was across.
        p[0] = old[0];
        p[2] = old[2];
        return false;
    }
    s->room[i] = room;
    if (p[1] > (float)height - M_RADIUS) {
        p[1] = (float)height - M_RADIUS;
        return true;
    }
    return false;
}

static void M_ItemFrame(M_MAT r, M_VEC at, const ITEM *const item)
{
    M_FromRot(r, item->rot);
    at[0] = (float)item->pos.x;
    at[1] = (float)item->pos.y;
    at[2] = (float)item->pos.z;
}

static int16_t M_Clamp16(const float v)
{
    return (int16_t)(v > 32000.0f ? 32000 : (v < -32000.0f ? -32000 : (int32_t)v));
}

// This tick's pose, from the points, into slot 1 (the last tick's to 0).
static void M_Pose(M_SIM *const s, const ITEM *const item, const bool first)
{
    const M_TYPE *const t = s->type;
    M_BODY *const body = s->body;
    M_MAT r_item;
    M_VEC at;
    M_ItemFrame(r_item, at, item);

    M_MAT w[M_MAX_MESHES];
    for (int32_t i = 0; i < t->mesh_count; i++) {
        M_World(w[i], s, i);
    }
    XYZ_16 rots[M_MAX_MESHES];
    M_MAT local;
    M_Mul(local, r_item, w[0], true);
    rots[0] = M_ToRot(local);
    for (int32_t i = 1; i < t->mesh_count; i++) {
        M_Mul(local, w[t->parent[i]], w[i], true);
        rots[i] = M_ToRot(local);
    }

    M_VEC lo = { 1e9f, 1e9f, 1e9f };
    M_VEC hi = { -1e9f, -1e9f, -1e9f };
    for (int32_t i = 0; i < t->point_count; i++) {
        M_VEC d;
        M_Sub(d, s->p[i], at);
        M_VEC l;
        M_MulVecT(l, r_item, d);
        for (int32_t k = 0; k < 3; k++) {
            lo[k] = l[k] < lo[k] ? l[k] : lo[k];
            hi[k] = l[k] > hi[k] ? l[k] : hi[k];
        }
    }
    M_VEC d;
    M_Sub(d, s->p[0], at);
    M_VEC root;
    M_MulVecT(root, r_item, d);
    const XYZ_16 offset = { M_Clamp16(root[0]), M_Clamp16(root[1]),
                            M_Clamp16(root[2]) };
    body->bounds = (BOUNDS_16) {
        .min = { M_Clamp16(lo[0] - 64.0f), M_Clamp16(lo[1] - 64.0f),
                 M_Clamp16(lo[2] - 64.0f) },
        .max = { M_Clamp16(hi[0] + 64.0f), M_Clamp16(hi[1] + 64.0f),
                 M_Clamp16(hi[2] + 64.0f) },
    };

    const size_t size = sizeof(XYZ_16) * t->mesh_count;
    memcpy(body->rots[0], first ? rots : body->rots[1], size);
    memcpy(body->rots[1], rots, size);
    body->offset[0] = first ? offset : body->offset[1];
    body->offset[1] = offset;
    body->shown = true;
}

static M_BODY *M_FindBody(const ITEM *const item)
{
    const int16_t num = Item_GetIndex(item);
    for (int32_t i = 0; i < M_MAX_BODIES; i++) {
        M_BODY *const body = &m_Bodies[i];
        if (body->item_num == num && body->object_id == item->object_id) {
            return body;
        }
    }
    return nullptr;
}

static bool M_IsMoving(const M_BODY *const body)
{
    for (int32_t j = 0; j < M_MAX_SIMS; j++) {
        if (m_Sims[j].body == body) {
            return true;
        }
    }
    return false;
}

static M_BODY *M_TakeBody(void)
{
    M_BODY *oldest = nullptr;
    for (int32_t i = 0; i < M_MAX_BODIES; i++) {
        M_BODY *const body = &m_Bodies[i];
        if (body->item_num == NO_ITEM) {
            return body;
        }
        if (!M_IsMoving(body) && (oldest == nullptr || body->age < oldest->age)) {
            oldest = body;
        }
    }
    return oldest;
}

void DC_Ragdoll_Reset(void)
{
    for (int32_t i = 0; i < M_MAX_BODIES; i++) {
        m_Bodies[i].item_num = NO_ITEM;
        m_Bodies[i].shown = false;
    }
    for (int32_t i = 0; i < M_MAX_SIMS; i++) {
        m_Sims[i].body = nullptr;
    }
    // The human kinds of enemy this level has.
    m_TypeCount = 0;
    for (int32_t id = 0; id < Object_GetCount() && m_TypeCount < M_MAX_TYPES;
         id++) {
        const OBJECT *const obj = Object_TryGet(id);
        int8_t parent[M_MAX_MESHES];
        if (obj == nullptr || !obj->loaded || !obj->intelligent || obj->mesh_count < 3
            || obj->mesh_count > M_MAX_MESHES || !M_Parents(obj, parent)
            || !M_IsHumanoid(obj, parent)) {
            continue;
        }
        M_BuildType(&m_Types[m_TypeCount], (OBJECT_ID)id);
        if (m_Types[m_TypeCount].point_count > M_MAX_POINTS) {
            continue;
        }
        m_TypeCount++;
    }
    m_Ready = true;
}

void DC_Ragdoll_Start(
    const ITEM *const item, const XYZ_32 *const start, const XYZ_32 *const hit)
{
    // Off unless a test disc turns it on (TRX_DC_ASMOFF3 bit 2).
    if ((g_DC_AsmOff3 & 2) == 0) {
        return;
    }
    const M_TYPE *const t = m_Ready ? M_GetType(item->object_id) : nullptr;
    if (t == nullptr || Room_Get(item->room_num)->flags.underwater) {
        return;
    }
    M_BODY *body = M_FindBody(item); // a body left by an item of this slot
    if (body != nullptr && M_IsMoving(body)) {
        return;
    }
    M_SIM *s = nullptr;
    for (int32_t i = 0; i < M_MAX_SIMS && s == nullptr; i++) {
        s = m_Sims[i].body == nullptr ? &m_Sims[i] : nullptr;
    }
    const ANIM_FRAME *const frame = Item_GetBestFrame(item);
    if (s == nullptr || frame == nullptr) {
        return;
    }
    body = body != nullptr ? body : M_TakeBody();
    if (body == nullptr) {
        return;
    }
    const OBJECT *const obj = Object_Get(item->object_id);
    s->type = t;

    // The pose it died in, mesh by mesh, as the walk draws it.
    const XYZ_16 *const rots = Anim_GetFrameRots(frame);
    M_MAT w[M_MAX_MESHES];
    M_VEC org[M_MAX_MESHES];
    M_MAT r;
    M_VEC at;
    M_ItemFrame(w[0], at, item);
    const M_VEC off = { frame->offset.x, frame->offset.y, frame->offset.z };
    M_MulVec(org[0], w[0], off);
    for (int32_t k = 0; k < 3; k++) {
        org[0][k] += at[k];
    }
    M_FromRot(r, rots[0]);
    M_Mul(w[0], w[0], r, false);
    for (int32_t i = 1; i < t->mesh_count; i++) {
        const int32_t parent = t->parent[i];
        M_VEC b;
        M_BonePos(b, obj, i);
        M_MulVec(org[i], w[parent], b);
        for (int32_t k = 0; k < 3; k++) {
            org[i][k] += org[parent][k];
        }
        M_FromRot(r, rots[i]);
        M_Mul(w[i], w[parent], r, false);
    }
    for (int32_t i = 0; i < t->mesh_count; i++) {
        if (t->org[i] == (i > 0 ? t->org[t->parent[i]] : -1)) {
            continue; // shares its parent's
        }
        memcpy(s->p[t->org[i]], org[i], sizeof(M_VEC));
    }
    for (int32_t i = 0; i < t->point_count; i++) {
        const int32_t mesh = t->tip_of[i];
        if (mesh < 0) {
            continue;
        }
        M_MulVec(s->p[i], w[mesh], t->tip[mesh]);
        for (int32_t k = 0; k < 3; k++) {
            s->p[i][k] += org[mesh][k];
        }
    }

    // First frames: a limb's x along it, the rest from its mesh's own turn.
    for (int32_t i = 0; i < t->mesh_count; i++) {
        M_VEC x;
        M_Sub(x, s->p[t->end[i]], s->p[t->org[i]]);
        M_Normalize(x);
        M_VEC h = { w[i][1], w[i][4], w[i][7] };
        if (fabsf(M_Dot(h, x)) > 0.9f) {
            h[0] = w[i][2];
            h[1] = w[i][5];
            h[2] = w[i][8];
        }
        M_FrameFrom(s->frame[i], x, h);
    }
    // In order, so that a limb finds its parent's turn already settled.
    for (int32_t i = 0; i < t->mesh_count; i++) {
        M_MAT f;
        M_Frame(f, s, i);
        memcpy(s->frame[i], f, sizeof(f));
        M_Mul(s->rest[i], s->frame[i], w[i], true);
    }

    // Each joint's cone, wide enough for the pose it died in.
    for (int32_t i = 1; i < t->mesh_count; i++) {
        M_VEC d;
        M_EndDir(d, t, obj, i, rots);
        float c = M_Dot(d, t->cone_axis[i]);
        c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
        float angle = acosf(c) + 0.1f;
        const float kind = acosf(t->cone_cos[i]);
        angle = angle < kind ? kind : (angle > M_JOINT_MAX ? M_JOINT_MAX : angle);
        s->cone_cos[i] = cosf(angle);
        s->cone_sin[i] = sinf(angle);
    }

    // Each mesh rigid: its origin, its far end, its children's origins.
    s->link_count = 0;
    for (int32_t i = 0; i < t->mesh_count; i++) {
        // The origin, the far end and the third point first, no repeats.
        int8_t set[M_MAX_MESHES + 3];
        int32_t n = 0;
        int8_t want[M_MAX_MESHES + 3];
        int32_t m = 0;
        want[m++] = t->org[i];
        want[m++] = t->end[i];
        want[m++] = t->hint[i];
        for (int32_t c = i + 1; c < t->mesh_count; c++) {
            if (t->parent[c] == i) {
                want[m++] = t->org[c];
            }
        }
        for (int32_t a = 0; a < m; a++) {
            bool seen = want[a] < 0;
            for (int32_t b = 0; b < n && !seen; b++) {
                seen = set[b] == want[a];
            }
            if (!seen) {
                set[n++] = want[a];
            }
        }
        // Tied to the first three, a set is as rigid as with every pair.
        for (int32_t a = 0; a < n && a < 3; a++) {
            for (int32_t b = a + 1; b < n; b++) {
                M_AddLink(s, set[a], set[b]);
            }
        }
    }

    // The speed it had, and the shot's push.
    M_VEC v = { 0.0f, item->gravity ? (float)item->fall_speed : 0.0f, 0.0f };
    const float yaw = item->rot.y * (6.28318531f / 65536.0f);
    v[0] = sinf(yaw) * item->speed;
    v[2] = cosf(yaw) * item->speed;
    M_VEC dir;
    M_VEC struck;
    const ITEM *const lara = Lara_GetItem();
    if (start != nullptr && hit != nullptr) {
        dir[0] = (float)(hit->x - start->x);
        dir[1] = (float)(hit->y - start->y);
        dir[2] = (float)(hit->z - start->z);
        struck[0] = (float)hit->x;
        struck[1] = (float)hit->y;
        struck[2] = (float)hit->z;
    } else {
        memcpy(struck, s->p[0], sizeof(struck));
        dir[0] = struck[0] - (lara != nullptr ? (float)lara->pos.x : struck[0]);
        dir[1] = 0.0f;
        dir[2] = struck[2] - (lara != nullptr ? (float)lara->pos.z : struck[2]);
    }
    M_Normalize(dir);
    for (int32_t i = 0; i < t->point_count; i++) {
        M_VEC d;
        M_Sub(d, s->p[i], struck);
        float near = 1.0f - sqrtf(M_Dot(d, d)) / M_PUSH_REACH;
        near = near < 0.0f ? 0.0f : near;
        const float push = M_PUSH_ALL + M_PUSH_HIT * near;
        for (int32_t k = 0; k < 3; k++) {
            s->old[i][k] = s->p[i][k] - v[k] - dir[k] * push;
        }
        s->room[i] = item->room_num;
    }

    *body = (M_BODY) {
        .item_num = Item_GetIndex(item),
        .object_id = item->object_id,
        .age = ++m_Age,
    };
    s->body = body;
    s->ticks = 0;
    s->still = 0;
    M_Pose(s, item, true);
}

static void M_Step(M_SIM *const s)
{
    const M_TYPE *const t = s->type;
    M_MAT w[M_MAX_MESHES];
    for (int32_t i = 0; i < t->mesh_count; i++) {
        M_World(w[i], s, i);
    }
    M_VEC axis[M_MAX_MESHES];
    for (int32_t i = 1; i < t->mesh_count; i++) {
        M_MulVec(axis[i], w[t->parent[i]], t->cone_axis[i]);
    }

    for (int32_t i = 0; i < t->point_count; i++) {
        M_VEC v;
        M_Sub(v, s->p[i], s->old[i]);
        const float speed = sqrtf(M_Dot(v, v)) * M_DAMP;
        const float scale = speed > M_MAX_SPEED
            ? M_DAMP * M_MAX_SPEED / speed
            : M_DAMP;
        for (int32_t k = 0; k < 3; k++) {
            s->old[i][k] = s->p[i][k];
            s->p[i][k] += v[k] * scale;
        }
        s->p[i][1] += (float)GRAVITY;
    }

    for (int32_t it = 0; it < M_ITERATIONS; it++) {
        for (int32_t l = 0; l < s->link_count; l++) {
            M_SolveLink(s, &s->links[l]);
        }
        for (int32_t i = 1; i < t->mesh_count; i++) {
            M_Cone(
                s, t->org[i], t->end[i], axis[i], s->cone_cos[i],
                s->cone_sin[i]);
        }
    }

    float moved = 0.0f;
    for (int32_t i = 0; i < t->point_count; i++) {
        if (M_Collide(s, i)) {
            // On the floor it slides to a stop.
            s->old[i][0] = s->p[i][0] - (s->p[i][0] - s->old[i][0]) * 0.5f;
            s->old[i][2] = s->p[i][2] - (s->p[i][2] - s->old[i][2]) * 0.5f;
        }
        M_VEC d;
        M_Sub(d, s->p[i], s->old[i]);
        moved += M_Dot(d, d);
    }
    moved /= (float)t->point_count;
    s->still = moved < M_STILL ? s->still + 1 : 0;
    s->ticks++;
    M_UpdateFrames(s);
}

void DC_Ragdoll_Control(void)
{
    for (int32_t i = 0; i < M_MAX_SIMS; i++) {
        M_SIM *const s = &m_Sims[i];
        if (s->body == nullptr) {
            continue;
        }
        const ITEM *const item = Item_Get(s->body->item_num);
        if (item->object_id != s->body->object_id || item->hit_points > 0) {
            s->body->item_num = NO_ITEM;
            s->body->shown = false;
            s->body = nullptr;
            continue;
        }
        M_Step(s);
        M_Pose(s, item, false);
        if (s->still >= M_STILL_TICKS || s->ticks >= M_MAX_TICKS) {
            // At rest: the pose stays, the points are let go.
            M_BODY *const body = s->body;
            memcpy(body->rots[0], body->rots[1], sizeof(body->rots[0]));
            body->offset[0] = body->offset[1];
            s->body = nullptr;
        }
    }
}

bool DC_Ragdoll_GetPose(
    const ITEM *const item, ANIM_POSE *const pose,
    const BOUNDS_16 **const bounds)
{
    if (!m_Ready || item->hit_points > 0) {
        return false;
    }
    const M_BODY *const body = M_FindBody(item);
    if (body == nullptr || !body->shown) {
        return false;
    }
    const float alpha = (float)Interpolation_GetWorldRate();
    const int32_t frac = (int32_t)(alpha * M_RATE);
    *pose = (ANIM_POSE) {
        .offset_a = body->offset[0],
        .offset_b = body->offset[1],
        .rots_a = body->rots[0],
        .rots_b = body->rots[1],
        .frac = frac < 0 ? 0 : (frac > M_RATE ? M_RATE : frac),
        .rate = M_RATE,
    };
    *bounds = &body->bounds;
    return true;
}
