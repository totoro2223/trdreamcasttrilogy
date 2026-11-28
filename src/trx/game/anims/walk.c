#include <trx/game/anims/walk.h>

#include <trx/core/utils.h>
#include <trx/debug.h>
#include <trx/game/matrix.h>
#include <trx/game/objects.h>
#include <trx/game/objects/draw.h>

static void M_Push(const bool interpolated)
{
    if (interpolated) {
        Matrix_Push_I();
    } else {
        Matrix_Push();
    }
}

static void M_Pop(const bool interpolated)
{
    if (interpolated) {
        Matrix_Pop_I();
    } else {
        Matrix_Pop();
    }
}

static void M_TranslateRel32(const XYZ_32 offset, const bool interpolated)
{
    if (interpolated) {
        Matrix_TranslateRel32_I(offset);
    } else {
        Matrix_TranslateRel32(offset);
    }
}

static void M_Rot16(
    const XYZ_16 rot_a, const XYZ_16 rot_b, const bool interpolated)
{
    if (interpolated) {
        Matrix_Rot16_ID(rot_a, rot_b);
    } else {
        Matrix_Rot16(rot_a);
    }
}

static void M_EnterRoot(ANIM_WALK *const walk)
{
    const ANIM_POSE *const pose = &walk->desc.pose;
    if (walk->interpolated) {
        Matrix_InitInterpolate(pose->frac, pose->rate);
        if (!walk->desc.skips_frame_offset) {
            Matrix_TranslateRel16_ID(pose->offset_a, pose->offset_b);
        }
    } else if (!walk->desc.skips_frame_offset) {
        Matrix_TranslateRel16(pose->offset_a);
    }
    if (pose->rots_a != nullptr) {
        M_Rot16(pose->rots_a[0], pose->rots_b[0], walk->interpolated);
    }
    if (walk->desc.applies_base_rot) {
        Object_ApplyExtraRotation(
            &walk->extra_rotation, walk->desc.obj->base_rot,
            walk->interpolated);
    }
}

static void M_EnterJoint(ANIM_WALK *const walk)
{
    const ANIM_POSE *const pose = &walk->desc.pose;
    const ANIM_BONE *const bone = &walk->bones[walk->joint - 1];
#if defined(__DREAMCAST__)
    if (Matrix_DC_WalkJoint(
            bone->matrix_pop, bone->matrix_push, bone->pos,
            pose->rots_a != nullptr ? &pose->rots_a[walk->joint] : nullptr,
            pose->rots_a != nullptr ? &pose->rots_b[walk->joint] : nullptr,
            walk->interpolated)) {
        walk->depth += (bone->matrix_push ? 1 : 0) - (bone->matrix_pop ? 1 : 0);
        Object_ApplyExtraRotation(
            &walk->extra_rotation, bone->rot, walk->interpolated);
        return;
    }
#endif
    if (bone->matrix_pop) {
        walk->depth--;
        M_Pop(walk->interpolated);
    }
    if (bone->matrix_push) {
        walk->depth++;
        M_Push(walk->interpolated);
    }

    M_TranslateRel32(bone->pos, walk->interpolated);
    if (pose->rots_a != nullptr) {
        M_Rot16(
            pose->rots_a[walk->joint], pose->rots_b[walk->joint],
            walk->interpolated);
    }
    Object_ApplyExtraRotation(
        &walk->extra_rotation, bone->rot, walk->interpolated);
}

ANIM_POSE Anim_Pose_FromFrame(const ANIM_FRAME *const frame)
{
    if (frame == nullptr) {
        return (ANIM_POSE) {};
    }
    return (ANIM_POSE) {
        .offset_a = frame->offset,
        .rots_a = Anim_GetFrameRots(frame),
    };
}

ANIM_POSE Anim_Pose_FromFrames(
    const ANIM_FRAME *const frame_a, const ANIM_FRAME *const frame_b,
    const int32_t frac, const int32_t rate)
{
    if (frame_a == nullptr || frame_b == nullptr) {
        return Anim_Pose_FromFrame(frame_a);
    }
    return (ANIM_POSE) {
        .offset_a = frame_a->offset,
        .offset_b = frame_b->offset,
        .rots_a = Anim_GetFrameRots(frame_a),
        .rots_b = Anim_GetFrameRots(frame_b),
        .frac = frac,
        .rate = rate,
    };
}

ANIM_POSE Anim_Pose_FromRots(const XYZ_16 *const rots, const XYZ_16 offset)
{
    return (ANIM_POSE) { .offset_a = offset, .rots_a = rots };
}

void Anim_Walk_BeginToJoint(
    ANIM_WALK *const walk, const ANIM_WALK_DESC *const desc,
    const int32_t last_joint)
{
    *walk = (ANIM_WALK) {
        .desc = *desc,
        .bones = Object_TryGetBone(desc->obj, 0),
        .extra_rotation = desc->extra_rotations,
        .joint = -1,
        .interpolated = desc->pose.rots_b != nullptr && desc->pose.frac != 0
            && desc->pose.rate != 0,
    };
    walk->last_joint = last_joint;
    if (walk->desc.pose.rots_b == nullptr) {
        walk->desc.pose.rots_b = walk->desc.pose.rots_a;
    }
}

void Anim_Walk_Begin(ANIM_WALK *const walk, const ANIM_WALK_DESC *const desc)
{
    Anim_Walk_BeginToJoint(walk, desc, desc->obj->mesh_count - 1);
}

bool Anim_Walk_Next(ANIM_WALK *const walk)
{
    walk->joint++;
    if (walk->joint > walk->last_joint) {
        return false;
    }

    if (walk->joint == 0) {
        M_EnterRoot(walk);
    } else {
        M_EnterJoint(walk);
    }
    return true;
}

#if defined(__DREAMCAST__)
    #include <trx/core/log.h>

    #include <string.h>

// In assembly (pose_sh4.S, dc_walk_bones); M_EnterJoint is its reference.
// TRX_DC_ASMOFF3 bit 67108864 turns it off.
typedef struct {
    uint32_t *joint;
    const ANIM_BONE *bones;
    const XYZ_16 *rots_a;
    const XYZ_16 *rots_b;
    const int16_t **extra;
    int32_t first;
    int32_t last;
    int32_t interpolated;
    void (*rot_y)(int16_t angle);
    void (*rot_x)(int16_t angle);
    void (*rot_z)(int16_t angle);
    void (*draw)(void *ctx, int32_t joint);
    void *ctx;
    int32_t *depth;
} M_WALK_BONES;
_Static_assert(sizeof(ANIM_BONE) == 17, "dc_walk_bones reads 17-byte bones");
_Static_assert(sizeof(M_WALK_BONES) == 56, "dc_walk_bones's block");
extern void dc_walk_bones(const M_WALK_BONES *b);
extern uint32_t g_DC_AsmOff3;

    #if defined(DC_EMIT_VERIFY)
        #define M_VERIFY_JOINTS 64
static MATRIX m_WantTops[M_VERIFY_JOINTS][4];
static bool m_WalkBad;

typedef struct {
    void (*draw)(void *ctx, int32_t joint);
    void *ctx;
} M_VERIFY_CTX;

static void M_VerifyDraw(void *const ctx, const int32_t joint)
{
    MATRIX tops[4];
    Matrix_DC_Tops(tops);
    if (memcmp(tops, m_WantTops[joint], sizeof(tops)) != 0) {
        m_WalkBad = true;
    }
    const M_VERIFY_CTX *const v = ctx;
    v->draw(v->ctx, joint);
}
    #endif

bool Anim_Walk_DC_Rest(
    ANIM_WALK *const walk, void (*const draw)(void *ctx, int32_t joint),
    void *const ctx)
{
    if ((g_DC_AsmOff3 & 67108864) != 0 || walk->joint != 0
        || walk->bones == nullptr || walk->last_joint < 1) {
        return false;
    }
    uint32_t joint_block[13];
    if (!Matrix_DC_WalkJointReady(joint_block)) {
        return false;
    }
    const ANIM_POSE *const pose = &walk->desc.pose;
    M_WALK_BONES b = {
        .joint = joint_block,
        .bones = walk->bones,
        .rots_a = pose->rots_a,
        .rots_b = pose->rots_b,
        .extra = &walk->extra_rotation,
        .first = 1,
        .last = walk->last_joint,
        .interpolated = walk->interpolated,
        .rot_y = walk->interpolated ? Matrix_RotY_I : Matrix_RotY,
        .rot_x = walk->interpolated ? Matrix_RotX_I : Matrix_RotX,
        .rot_z = walk->interpolated ? Matrix_RotZ_I : Matrix_RotZ,
        .draw = draw,
        .ctx = ctx,
        .depth = &walk->depth,
    };
    #if defined(DC_EMIT_VERIFY)
    if (walk->last_joint < M_VERIFY_JOINTS) {
        const int16_t *const extra_in = walk->extra_rotation;
        const int32_t depth_in = walk->depth;
        Matrix_DC_SaveState();
        for (int32_t j = 1; j <= walk->last_joint; j++) {
            walk->joint = j;
            M_EnterJoint(walk);
            Matrix_DC_Tops(m_WantTops[j]);
        }
        const int16_t *const extra_want = walk->extra_rotation;
        const int32_t depth_want = walk->depth;
        Matrix_DC_LoadState();
        walk->extra_rotation = extra_in;
        walk->depth = depth_in;
        walk->joint = 0;
        M_VERIFY_CTX v = { .draw = draw, .ctx = ctx };
        b.draw = M_VerifyDraw;
        b.ctx = &v;
        m_WalkBad = false;
        dc_walk_bones(&b);
        static uint32_t runs;
        static uint32_t bad;
        if ((m_WalkBad || extra_want != walk->extra_rotation
             || depth_want != walk->depth)
            && bad++ < 6) {
            LOG_WARNING(
                "[bones-verify] MISMATCH joints %d depth %d/%d",
                walk->last_joint, walk->depth, depth_want);
        }
        if (++runs % 1000 == 0) {
            LOG_WARNING(
                "[bones-verify] %u runs, %u bad", (unsigned)runs,
                (unsigned)bad);
        }
    } else {
        dc_walk_bones(&b);
    }
    #else
    dc_walk_bones(&b);
    #endif
    walk->joint = walk->last_joint;
    return true;
}
#endif

XYZ_32 Anim_Walk_GetPos(ANIM_WALK *const walk, const XYZ_32 local)
{
    ASSERT(!walk->ended);
    M_Push(walk->interpolated);
    M_TranslateRel32(local, walk->interpolated);
    if (walk->interpolated) {
        Matrix_Interpolate();
    }
    const XYZ_32 result = {
        .x = g_MatrixPtr->_03 >> W2V_SHIFT,
        .y = g_MatrixPtr->_13 >> W2V_SHIFT,
        .z = g_MatrixPtr->_23 >> W2V_SHIFT,
    };
    M_Pop(walk->interpolated);
    return result;
}

void Anim_Walk_End(ANIM_WALK *const walk)
{
    ASSERT(!walk->ended);
    walk->ended = true;
    while (walk->depth > 0) {
        walk->depth--;
        M_Pop(walk->interpolated);
    }
}
