#include <trx/game/lara/draw.h>


#include <trx/config.h>
#include <trx/core/math/geom.h>
#include <trx/game/camera.h>
#include <trx/game/gun.h>
#include <trx/game/gun/common.h>
#include <trx/game/gun/registry.h>
#include <trx/game/items/utils.h>
#include <trx/game/lara.h>
#include <trx/game/lara/electric.h>
#include <trx/game/lara/pose.h>
#include <trx/game/matrix.h>
#include <trx/game/output.h>
#include <trx/game/output/sources/shadows.h>
#include <trx/game/output/state.h>
#include <trx/game/output/vars.h>
#include <trx/game/output/water.h>
#include <trx/game/random.h>
#include <trx/game/rooms.h>
#include <trx/version.h>

// TEMP: see platform/dreamcast/dc_probe.h
#if defined(__DREAMCAST__)
    #include <platform/dreamcast/dc_probe.h>
#else
    #define DC_PROBE(i, stmt) stmt
    #define DC_PROBE_START(t)
    #define DC_PROBE_STOP(i, t)
#endif

#if defined(__DREAMCAST__)
#include <platform/dreamcast/render/shadow.h>
#endif

typedef enum {
    M_ARMS_EMPTY,
    M_ARMS_PISTOLS,
    M_ARMS_RIFLE,
    M_ARMS_OTHER,
} M_ARMS;

static bool m_CacheMatrices = false;
static bool m_IsLara = true;

#if defined(__DREAMCAST__)
    #include <math.h>

// Leg IK (option "Leg IK"): while Lara is on her feet, each foot goes onto the floor
// under it and the hips come down as far as the lower foot needs. Drawing
// only: her position, collision and animations are untouched. Each leg is a
// two-bone chain solved in closed form; the thigh, calf and foot get an extra
// pitch, the hips an extra drop, eased in and out.
    #define M_IK_REACH 128 // a foot further from her floor than this is left
    #define M_IK_EASE 0.25f
    // The foot also pitches with the slope under it, up to 30 degrees, read
    // over this far ahead and behind. A rise bigger than a walkable slope
    // gives over that span is a step, and the foot stays as animated.
    #define M_IK_SLOPE_SPAN 64
    #define M_IK_SLOPE_STEP 72
    #define M_IK_MAX_PITCH 0.5235988f
    // A drop or rise of her y in one drawn frame between these is a step
    // (slopes move her less), and the body is eased onto it at this rate.
    #define M_IK_STEP_MIN 24
    #define M_IK_STEP_EASE 0.15f
static float m_IKRot[LM_NUMBER_OF];
static float m_IKDrop;
// The body is drawn this far below her y, after a step, easing to 0.
static float m_IKStep;
static int32_t m_IKLastY;
// The extra pitch of a mesh, 0 for any but Lara's legs.
static int16_t m_IKAngle[LM_NUMBER_OF];
static int32_t m_IKDropUnits;

static XYZ_32 M_IKPos(void)
{
    return (XYZ_32) {
        g_WMatrixPtr->_03 >> W2V_SHIFT,
        g_WMatrixPtr->_13 >> W2V_SHIFT,
        g_WMatrixPtr->_23 >> W2V_SHIFT,
    };
}

static float M_IKLength(const XYZ_32 v)
{
    return sqrtf((float)v.x * v.x + (float)v.y * v.y + (float)v.z * v.z);
}

// The angle facing side c of a triangle of sides a, b and c.
static float M_IKAngle(const float a, const float b, const float c)
{
    float cos = (a * a + b * b - c * c) / (2.0f * a * b);
    cos = cos < -1.0f ? -1.0f : (cos > 1.0f ? 1.0f : cos);
    return acosf(cos);
}

// The states where her feet are on the floor: standing, walking, running,
// turning and stepping. The offset each foot gets is the floor under it
// against hers, so a lifted foot stays lifted by as much as the animation has.
static bool M_IKOnFeet(const int16_t state)
{
    return state == LS(LS_STOP) || state == LS(LS_WALK) || state == LS(LS_RUN)
        || state == LS(LS_WALK_BACK) || state == LS(LS_FAST_BACK)
        || state == LS(LS_TURN_RIGHT) || state == LS(LS_TURN_LEFT)
        || state == LS(LS_FAST_TURN) || state == LS(LS_STEP_RIGHT)
        || state == LS(LS_STEP_LEFT);
}

// The floor at x, z against hers, read from just above her floor so that the
// room below is not taken.
static int32_t M_IKFloor(const ITEM *const item, const int32_t x, const int32_t z)
{
    XYZ_32 at = { x, item->pos.y - STEP_L / 2, z };
    int16_t room_num = item->room_num;
    const SECTOR *const sector = Room_GetSector(at, &room_num);
    at.y = item->pos.y;
    return Room_GetHeight(sector, at) - item->pos.y;
}

static void M_IKUpdate(const ITEM *const item, const ANIM_BONE *const bone)
{
    float target[LM_NUMBER_OF] = {};
    float drop = 0.0f;
    const LARA_INFO *const lara = Lara_GetLaraInfo();
    ANIM_FRAME *frames[2] = {};
    int32_t rate;
    Item_GetFrames(item, frames, &rate);
    const bool on_feet = g_Config.rendering.dc_leg_ik && frames[0] != nullptr
        && M_IKOnFeet(item->current_anim_state) && !item->gravity
        && lara->water_status == LWS_ABOVE_WATER && !Lara_Vehicle_IsMounted()
        && Lara_Pose_Get() == nullptr;
    // A step she was snapped onto: the body starts where it was.
    const int32_t y = item->interp.result.pos.y;
    const int32_t dy = y - m_IKLastY;
    m_IKLastY = y;
    if (on_feet && ABS(dy) >= M_IK_STEP_MIN && ABS(dy) <= STEPUP_HEIGHT) {
        m_IKStep -= (float)dy;
    }
    if (on_feet) {
        const ANIM_FRAME *const frame = frames[0];
        const XYZ_16 *const rots = Anim_GetFrameRots(frame);
        XYZ_32 hip[2];
        XYZ_32 ankle[2];
        int32_t floor[2];
        float slope[2] = {};
        const float yaw = item->rot.y * (3.14159265f / 32768.0f);
        const int32_t dx = (int32_t)(sinf(yaw) * M_IK_SLOPE_SPAN);
        const int32_t dz = (int32_t)(cosf(yaw) * M_IK_SLOPE_SPAN);
        for (int32_t leg = 0; leg < 2; leg++) {
            const LARA_MESH thigh = leg == 0 ? LM_THIGH_L : LM_THIGH_R;
            Matrix_PushUnit();
            Matrix_Rot16(item->rot);
            Matrix_TranslateRel16(frame->offset);
            Matrix_Rot16(rots[LM_HIPS]);
            Matrix_TranslateRel32(bone[thigh - 1].pos);
            hip[leg] = M_IKPos();
            Matrix_Rot16(rots[thigh]);
            Matrix_TranslateRel32(bone[thigh].pos);
            Matrix_Rot16(rots[thigh + 1]);
            Matrix_TranslateRel32(bone[thigh + 1].pos);
            ankle[leg] = M_IKPos();
            Matrix_Pop();

            const int32_t x = item->pos.x + ankle[leg].x;
            const int32_t z = item->pos.z + ankle[leg].z;
            // Against where the body is drawn, during a step.
            floor[leg] = M_IKFloor(item, x, z) - (int32_t)m_IKStep;
            if (floor[leg] < -M_IK_REACH && floor[leg] >= -STEPUP_HEIGHT) {
                floor[leg] = -M_IK_REACH; // the step up: as high as she bends
            }
            if (floor[leg] > M_IK_REACH || floor[leg] < -M_IK_REACH) {
                floor[leg] = 0; // a ledge or a wall: as the animation has it
            } else {
                // The slope along her facing, under this foot: positive when
                // the floor rises ahead (a positive pitch lifts the toes).
                const int32_t ahead =
                    M_IKFloor(item, x + dx, z + dz) - M_IKFloor(item, x - dx, z - dz);
                if (ahead <= M_IK_SLOPE_STEP && ahead >= -M_IK_SLOPE_STEP) {
                    float pitch = atan2f((float)-ahead, 2.0f * M_IK_SLOPE_SPAN);
                    pitch = pitch > M_IK_MAX_PITCH ? M_IK_MAX_PITCH : pitch;
                    pitch = pitch < -M_IK_MAX_PITCH ? -M_IK_MAX_PITCH : pitch;
                    slope[leg] = pitch;
                }
            }
            if (floor[leg] > drop) {
                drop = (float)floor[leg];
            }
        }
        for (int32_t leg = 0; leg < 2; leg++) {
            const LARA_MESH thigh = leg == 0 ? LM_THIGH_L : LM_THIGH_R;
            const float l1 = M_IKLength(bone[thigh].pos);
            const float l2 = M_IKLength(bone[thigh + 1].pos);
            XYZ_32 rel = {
                ankle[leg].x - hip[leg].x,
                ankle[leg].y - hip[leg].y,
                ankle[leg].z - hip[leg].z,
            };
            const float lo = fabsf(l1 - l2) + 1.0f;
            const float hi = l1 + l2 - 1.0f;
            float d = M_IKLength(rel);
            d = d < lo ? lo : (d > hi ? hi : d);
            // The leg is shorter by what the hips come down past this foot.
            rel.y -= (int32_t)drop - floor[leg];
            float d2 = M_IKLength(rel);
            d2 = d2 < lo ? lo : (d2 > hi ? hi : d2);
            // The thigh turns the knee forward, the calf bends back by the
            // knee's change and the foot keeps its pitch.
            const float t = M_IKAngle(l1, d2, l2) - M_IKAngle(l1, d, l2);
            const float c = M_IKAngle(l1, l2, d2) - M_IKAngle(l1, l2, d);
            target[thigh] = t;
            target[thigh + 1] = c;
            target[thigh + 2] = -(t + c) + slope[leg];
        }
    }

    m_IKDrop += (drop - m_IKDrop) * M_IK_EASE;
    m_IKStep -= m_IKStep * M_IK_STEP_EASE;
    m_IKDropUnits = (int32_t)(m_IKDrop + m_IKStep);
    for (int32_t i = 0; i < LM_NUMBER_OF; i++) {
        m_IKRot[i] += (target[i] - m_IKRot[i]) * M_IK_EASE;
        m_IKAngle[i] = (int16_t)(m_IKRot[i] * (32768.0f / 3.14159265f));
    }
}

// The extra pitch IK gives a mesh, after its own rotation.
static void M_IKApply(const LARA_MESH mesh, const bool interpolated)
{
    if (!m_IsLara) {
        return;
    }
    if (interpolated) {
        Matrix_RotX_I(m_IKAngle[mesh]);
    } else {
        Matrix_RotX(m_IKAngle[mesh]);
    }
}
#endif

static M_ARMS M_GetArms(const LARA_GUN_TYPE gun_type)
{
    if (!Gun_Registry_Get(gun_type)->is_declared) {
        return M_ARMS_EMPTY;
    }
    switch (Gun_Registry_Get(gun_type)->type) {
    case WEAPON_TYPE_DUAL_PISTOLS:
    case WEAPON_TYPE_SINGLE_PISTOL:
        return M_ARMS_PISTOLS;
    case WEAPON_TYPE_RIFLE:
        return M_ARMS_RIFLE;
    case WEAPON_TYPE_FLARE:
        return M_ARMS_EMPTY;
    default:
        return M_ARMS_OTHER;
    }
}

static void M_CalculateLight(
    const ITEM *const item, const ANIM_FRAME *const frame)
{
    if (g_TRVersion >= 3) {
        GAME_VECTOR sample_pos = {
            .pos = { 0, 0, 0 },
            .room_num = item->room_num,
        };
        if (!Lara_GetMeshPos(LM_TORSO, &sample_pos.pos)) {
            Lara_GetJointAbsPosition(&sample_pos.pos, LM_TORSO);
        }
        Room_GetSector(sample_pos.pos, &sample_pos.room_num);
        Output_CalculateObjectLightingAt(item, sample_pos);
    } else {
        Output_CalculateObjectLighting(item, &frame->bounds);
    }
}

static void M_CacheMatrix(const LARA_MESH mesh)
{
    if (!m_CacheMatrices) {
        return;
    }

    LARA_INFO *const lara = Lara_GetLaraInfo();
    lara->mesh_pos_matrices[mesh] = *g_WMatrixPtr;
}

static void M_DrawEquipmentMesh(
    const LARA_SKIN_EQUIPMENT *const equipment, const CLIP clip,
    const bool interpolated)
{
    const OBJECT_MESH *const mesh = equipment->mesh;
    const GAME_VECTOR pos = {
        .room_num = Lara_GetItem()->room_num,
        .pos = Matrix_MulVec32_M(
            g_WMatrixPtr,
            (XYZ_32) {
                mesh->center.x,
                mesh->center.y - 24,
                mesh->center.z,
            }),
    };
    Output_Water_PushLaraMesh(WATER_LARA_MESH_OTHER, pos, mesh->radius);
    // A weapon on her back made from its inventory model (see
    // M_SetBackGunFromModel) is turned, scaled and centred first.
    const bool turned = equipment->rot.y != 0 || equipment->rot.z != 0;
    if (interpolated) {
        Matrix_Push_I();
        Matrix_TranslateRel16_I(equipment->offset);
        if (turned) {
            Matrix_RotY_I(equipment->rot.y);
            Matrix_RotZ_I(equipment->rot.z);
            // The interpolation rebuilds the matrix from its angles and drops
            // a scale: the scale and the pivot come after it.
            Matrix_Push();
            Matrix_Interpolate();
            if (equipment->scale != 0) {
                Matrix_Scale(equipment->scale);
            }
            Matrix_TranslateRel16(equipment->pivot);
            Output_DrawObjectMesh(mesh, clip);
            Matrix_Pop();
        } else {
            Output_DrawObjectMesh_I(mesh, clip);
        }
        Matrix_Pop_I();
    } else {
        Matrix_Push();
        Matrix_TranslateRel16(equipment->offset);
        if (turned) {
            Matrix_RotY(equipment->rot.y);
            Matrix_RotZ(equipment->rot.z);
            if (equipment->scale != 0) {
                Matrix_Scale(equipment->scale);
            }
            Matrix_TranslateRel16(equipment->pivot);
        }
        Output_DrawObjectMesh(mesh, clip);
        Matrix_Pop();
    }
    Output_Water_PopLaraMesh();
}

static void M_DrawLaraMesh(
    const ITEM *const item, const LARA_MESH mesh_num, const CLIP clip,
    const bool interpolated)
{
    if (m_IsLara && !Item_IsMeshVisible(item, mesh_num)) {
        return;
    }

    const OBJECT_MESH *const mesh = Lara_Mesh_Get(mesh_num);
    XYZ_32 origin = XYZ_32_From16(mesh->center);
    switch (mesh_num) {
    case LM_TORSO:
    case LM_CALF_L:
    case LM_CALF_R:
    case LM_FOOT_L:
    case LM_FOOT_R:
        origin.y -= 24;
        break;
    case LM_HEAD:
        origin.y -= 8;
        break;
    default:
        break;
    }
    // Takes the matrix the mesh is drawn with, which for a blended frame is
    // the blend rather than either end of it.
    MATRIX wmatrix = *g_WMatrixPtr;
    if (interpolated) {
        Matrix_Push();
        DC_PROBE(DC_PROBE_L_BLEND, Matrix_Interpolate());
        wmatrix = *g_WMatrixPtr;
#if !defined(__DREAMCAST__)
        Matrix_Pop();
#endif
    }
#if defined(__DREAMCAST__)
    // The blend stays on the stack for the mesh below, which then needs no
    // blending of its own: each blend is two 64-bit matrices worked out again.
    const bool mesh_interpolated = false;
#else
    const bool mesh_interpolated = interpolated;
#endif

    DC_PROBE_START(probe_water);
    const GAME_VECTOR pos = {
        .room_num = item->room_num,
        .pos = Matrix_MulVec32_M(&wmatrix, origin),
    };
    // Reads the water state from the joint the mesh hangs off, as the
    // original does, and for every mesh drawn rather than for the few the
    // matrix cache covers.
    Output_Water_ObserveLaraMesh(
        mesh_num,
        (GAME_VECTOR) {
            .room_num = item->room_num,
            .pos = Matrix_MulVec32_M(&wmatrix, (XYZ_32) {}),
        });
    Output_Water_PushLaraMesh(mesh_num, pos, mesh->radius);
    DC_PROBE_STOP(DC_PROBE_L_WATER, probe_water);
    DC_PROBE_START(probe_stash);
    if (m_IsLara) {
        Lara_Joints_StashMatrix(mesh_num, mesh_interpolated);
    }
    DC_PROBE_STOP(DC_PROBE_L_JOINTS, probe_stash);
    DC_PROBE_START(probe_mesh);
    if (mesh_interpolated) {
        Output_DrawObjectMesh_I(mesh, clip);
    } else {
        Output_DrawObjectMesh(mesh, clip);
    }
    DC_PROBE_STOP(DC_PROBE_L_MESH, probe_mesh);
#if defined(__DREAMCAST__)
    if (interpolated) {
        Matrix_Pop();
    }
#endif

    DC_PROBE_START(probe_joints);
    if (m_IsLara) {
        Lara_Joints_Draw(mesh_num, clip, interpolated);
    }
    DC_PROBE_STOP(DC_PROBE_L_JOINTS, probe_joints);
    Output_Water_PopLaraMesh();
}

static void M_DrawBodyPart(
    const LARA_MESH mesh, const ANIM_BONE *const bone,
    const XYZ_16 *mesh_rots_1, const XYZ_16 *mesh_rots_2, const CLIP clip)
{
    const LARA_INFO *const lara = Lara_GetLaraInfo();
    if (mesh_rots_2 != nullptr) {
        Matrix_TranslateRel32_I(bone[mesh - 1].pos);
        Matrix_Rot16_ID(mesh_rots_1[mesh], mesh_rots_2[mesh]);
#if defined(__DREAMCAST__)
        M_IKApply(mesh, true);
#endif
        M_CacheMatrix(mesh);
        M_DrawLaraMesh(Lara_GetItem(), mesh, clip, true);
    } else {
        Matrix_TranslateRel32(bone[mesh - 1].pos);
        Matrix_Rot16(mesh_rots_1[mesh]);
#if defined(__DREAMCAST__)
        M_IKApply(mesh, false);
#endif
        M_CacheMatrix(mesh);
        M_DrawLaraMesh(Lara_GetItem(), mesh, clip, false);
    }
}

static inline void M_DrawEquipment(
    const LARA_MESH mesh, const CLIP clip, const bool interpolated)
{
    if (!m_IsLara) {
        return;
    }

    if (!Item_IsMeshVisible(Lara_GetItem(), mesh)) {
        return;
    }

    const LARA_SKIN_EQUIPMENT *const equipment = Lara_Skin_GetEquipment(mesh);
    if (!equipment->visible || equipment->mesh == nullptr) {
        return;
    }

    DC_PROBE_START(probe_equip);
    M_DrawEquipmentMesh(equipment, clip, interpolated);
    DC_PROBE_STOP(DC_PROBE_L_EQUIP, probe_equip);
}

static bool M_Draw_I(
    const ITEM *const item, const ANIM_FRAME *const frame1,
    const ANIM_FRAME *const frame2, const int32_t frac, const int32_t rate)
{
    const OBJECT *const obj = Object_Get(item->object_id);
    LARA_INFO *const lara = Lara_GetLaraInfo();
    const BOUNDS_16 *const shadow_bounds = Item_GetBoundsAccurate(item);

    if (!Lara_Vehicle_IsMounted()) {
        DC_PROBE_START(probe_shadow);
        OutputSource_Shadows_Draw(obj->shadow_size, shadow_bounds, item);
        DC_PROBE_STOP(DC_PROBE_L_SHADOW, probe_shadow);
    }

    MATRIX saved_matrix = *g_MatrixPtr;
    MATRIX wsaved_matrix = *g_WMatrixPtr;

    Matrix_Push();
    Matrix_TranslateAbs32(item->interp.result.pos);
    Matrix_Rot16(item->interp.result.rot);

    const CLIP clip = Output_CheckBoundsClip(&frame1->bounds);
    if (clip == CLIP_NOT_VISIBLE && !m_IsLara) {
        m_CacheMatrices = false;
        Matrix_Pop();
        return false;
    }

    if (g_Config.debug.enable_debug_bounding_boxes) {
        const BOUNDS_32 bounds = BOUNDS_32_From16(&frame1->bounds);
        Output_DrawCuboid(&bounds);
    }

    m_CacheMatrices = m_IsLara;
    Matrix_Push();
    DC_PROBE(DC_PROBE_L_LIGHT, M_CalculateLight(item, frame1));
    if (m_CacheMatrices) {
        lara->mesh_pos_matrices_valid = false;
    }

    const ANIM_BONE *const bone =
        m_IsLara ? Lara_Skin_GetBoneBase() : Object_GetBone(obj, 0);
    const XYZ_16 *mesh_rots_1 = Anim_GetFrameRots(frame1);
    const XYZ_16 *mesh_rots_2 = Anim_GetFrameRots(frame2);

    Matrix_InitInterpolate(frac, rate);
    Matrix_TranslateRel16_ID(frame1->offset, frame2->offset);
#if defined(__DREAMCAST__)
    if (m_IsLara && m_IKDropUnits != 0) {
        Matrix_TranslateRel32_I((XYZ_32) { 0, m_IKDropUnits, 0 });
    }
#endif
    Matrix_Rot16_ID(mesh_rots_1[LM_HIPS], mesh_rots_2[LM_HIPS]);
    M_CacheMatrix(LM_HIPS);
    M_DrawLaraMesh(item, LM_HIPS, clip, true);
    M_DrawEquipment(LM_HIPS, clip, true);

    Matrix_Push_I();
    M_DrawBodyPart(LM_THIGH_L, bone, mesh_rots_1, mesh_rots_2, clip);
    M_DrawEquipment(LM_THIGH_L, clip, true);
    M_DrawBodyPart(LM_CALF_L, bone, mesh_rots_1, mesh_rots_2, clip);
    M_DrawBodyPart(LM_FOOT_L, bone, mesh_rots_1, mesh_rots_2, clip);
    Matrix_Pop_I();

    Matrix_Push_I();
    M_DrawBodyPart(LM_THIGH_R, bone, mesh_rots_1, mesh_rots_2, clip);
    M_DrawEquipment(LM_THIGH_R, clip, true);
    M_DrawBodyPart(LM_CALF_R, bone, mesh_rots_1, mesh_rots_2, clip);
    M_DrawBodyPart(LM_FOOT_R, bone, mesh_rots_1, mesh_rots_2, clip);
    Matrix_Pop_I();

    Matrix_TranslateRel32_I(bone[LM_TORSO - 1].pos);
    if (Lara_IsMachineGunActive()) {
        mesh_rots_2 =
            Anim_GetFrameRots(&lara->right_arm.frame_base[lara->right_arm.frame_num]);
        mesh_rots_1 = mesh_rots_2;
    }

    Matrix_Rot16_ID(mesh_rots_1[LM_TORSO], mesh_rots_2[LM_TORSO]);
    Matrix_Rot16_I(lara->interp.result.torso_rot);
    M_CacheMatrix(LM_TORSO);
    M_DrawLaraMesh(item, LM_TORSO, clip, true);
    M_DrawEquipment(LM_TORSO, clip, true);

    Matrix_Push_I();
    Matrix_TranslateRel32_I(bone[LM_HEAD - 1].pos);
    Matrix_Rot16_ID(mesh_rots_1[LM_HEAD], mesh_rots_2[LM_HEAD]);
    Matrix_Rot16_I(lara->interp.result.head_rot);
    M_CacheMatrix(LM_HEAD);
    M_DrawLaraMesh(item, LM_HEAD, clip, true);
    M_DrawEquipment(LM_HEAD, clip, true);

    *g_MatrixPtr = saved_matrix;
    *g_WMatrixPtr = wsaved_matrix;
    if (m_IsLara) {
        DC_PROBE(DC_PROBE_L_HAIR, Lara_Hair_Draw());
    }
    Matrix_Pop_I();

    LARA_GUN_TYPE gun_type = LGT_UNARMED;
    if (lara->gun_status == LGS_READY || lara->gun_status == LGS_SPECIAL
        || lara->gun_status == LGS_DRAW || lara->gun_status == LGS_UNDRAW) {
        gun_type = lara->gun_type;
    }

    switch (M_GetArms(gun_type)) {
    case M_ARMS_EMPTY:
        Matrix_Push_I();
        M_DrawBodyPart(LM_UARM_R, bone, mesh_rots_1, mesh_rots_2, clip);
        M_DrawBodyPart(LM_LARM_R, bone, mesh_rots_1, mesh_rots_2, clip);
        M_DrawBodyPart(LM_HAND_R, bone, mesh_rots_1, mesh_rots_2, clip);
        M_DrawEquipment(LM_HAND_R, clip, true);
        Matrix_Pop_I();

        Matrix_Push_I();
        Matrix_TranslateRel32_I(bone[LM_UARM_L - 1].pos);
        if (lara->flare.control) {
            const ANIM *const anim = Anim_GetAnim(lara->left_arm.anim_num);
            mesh_rots_1 =
                Anim_GetFrameRots(&lara->left_arm
                    .frame_base[lara->left_arm.frame_num - anim->frame_base]);
            mesh_rots_2 = mesh_rots_1;
        }

        Matrix_Rot16_ID(mesh_rots_1[LM_UARM_L], mesh_rots_2[LM_UARM_L]);
        M_CacheMatrix(LM_UARM_L);
        M_DrawLaraMesh(item, LM_UARM_L, clip, true);

        M_DrawBodyPart(LM_LARM_L, bone, mesh_rots_1, mesh_rots_2, clip);
        M_DrawBodyPart(LM_HAND_L, bone, mesh_rots_1, mesh_rots_2, clip);
        M_DrawEquipment(LM_HAND_L, clip, true);

        if (g_TRVersion < 3 && Gun_IsFlareType(lara->gun_type)
            && lara->left_arm.flash_gun) {
            Gun_DrawFlash(lara->gun_type, clip, true);
        }
        Matrix_Pop();
        break;

    case M_ARMS_PISTOLS: {
        Matrix_Push_I();
        Matrix_TranslateRel32_I(bone[LM_UARM_R - 1].pos);
        Matrix_InterpolateArm();
        if (Gun_IsSinglePistolType(gun_type)) {
            Matrix_Rot16(lara->interp.result.torso_rot);
        } else {
            Matrix_Rot16(lara->right_arm.interp.result.rot);
        }

        const ANIM *anim = Anim_GetAnim(lara->right_arm.anim_num);
        mesh_rots_1 =
            Anim_GetFrameRots(&lara->right_arm
                .frame_base[lara->right_arm.frame_num - anim->frame_base]);
        Matrix_Rot16(mesh_rots_1[LM_UARM_R]);
        M_CacheMatrix(LM_UARM_R);
        M_DrawLaraMesh(item, LM_UARM_R, clip, false);

        M_DrawBodyPart(LM_LARM_R, bone, mesh_rots_1, nullptr, clip);
        M_DrawBodyPart(LM_HAND_R, bone, mesh_rots_1, nullptr, clip);
        M_DrawEquipment(LM_HAND_R, clip, false);
        if (lara->right_arm.flash_gun) {
            saved_matrix = *g_MatrixPtr;
            wsaved_matrix = *g_WMatrixPtr;
        }
        Matrix_Pop_I();

        Matrix_Push_I();
        Matrix_TranslateRel32_I(bone[LM_UARM_L - 1].pos);
        Matrix_InterpolateArm();
        if (Gun_IsSinglePistolType(gun_type)) {
            Matrix_Rot16(lara->interp.result.torso_rot);
        } else {
            Matrix_Rot16(lara->left_arm.interp.result.rot);
        }

        anim = Anim_GetAnim(lara->left_arm.anim_num);
        mesh_rots_1 =
            Anim_GetFrameRots(&lara->left_arm
                .frame_base[lara->left_arm.frame_num - anim->frame_base]);
        Matrix_Rot16(mesh_rots_1[LM_UARM_L]);
        M_CacheMatrix(LM_UARM_L);
        M_DrawLaraMesh(item, LM_UARM_L, clip, false);

        M_DrawBodyPart(LM_LARM_L, bone, mesh_rots_1, nullptr, clip);
        M_DrawBodyPart(LM_HAND_L, bone, mesh_rots_1, nullptr, clip);
        M_DrawEquipment(LM_HAND_L, clip, false);

        if (lara->left_arm.flash_gun) {
            Gun_DrawFlash(gun_type, clip, false);
        }
        if (lara->right_arm.flash_gun) {
            *g_MatrixPtr = saved_matrix;
            *g_WMatrixPtr = wsaved_matrix;
            Gun_DrawFlash(gun_type, clip, false);
        }
        Matrix_Pop();
        break;
    }

    case M_ARMS_RIFLE: {
        Matrix_Push_I();
        Matrix_TranslateRel32_I(bone[LM_UARM_R - 1].pos);
        mesh_rots_1 =
            Anim_GetFrameRots(&lara->right_arm.frame_base[lara->right_arm.frame_num]);
        mesh_rots_2 = mesh_rots_1;
        Matrix_Rot16_ID(mesh_rots_1[LM_UARM_R], mesh_rots_2[LM_UARM_R]);
        M_CacheMatrix(LM_UARM_R);
        M_DrawLaraMesh(item, LM_UARM_R, clip, true);

// NOTE: gcc wrongly complains about mesh_rots_1 possibly being nullptr.
// While this is not the case, it's curious how the pistols subtract the
// frame_base from lara->*_arm.frame_num to access the mesh_rots, and the
// rifles do not.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

        M_DrawBodyPart(LM_LARM_R, bone, mesh_rots_1, mesh_rots_2, clip);
        M_DrawBodyPart(LM_HAND_R, bone, mesh_rots_1, mesh_rots_2, clip);
        M_DrawEquipment(LM_HAND_R, clip, true);

        if (lara->right_arm.flash_gun) {
            saved_matrix = *g_MatrixPtr;
            wsaved_matrix = *g_WMatrixPtr;
        }
        Matrix_Pop_I();

        Matrix_Push_I();
        M_DrawBodyPart(LM_UARM_L, bone, mesh_rots_1, mesh_rots_2, clip);
        M_DrawBodyPart(LM_LARM_L, bone, mesh_rots_1, mesh_rots_2, clip);
        M_DrawBodyPart(LM_HAND_L, bone, mesh_rots_1, mesh_rots_2, clip);

#pragma GCC diagnostic pop

        if (lara->right_arm.flash_gun) {
            *g_MatrixPtr = saved_matrix;
            *g_WMatrixPtr = wsaved_matrix;
            Gun_DrawFlash(gun_type, clip, false);
        }
        Matrix_Pop();
        break;
    }

    default:
        break;
    }

    if (m_CacheMatrices) {
        lara->mesh_pos_matrices_valid = true;
    }
    m_CacheMatrices = false;

    Matrix_Pop();
    Matrix_Pop();
    return true;
}

static bool M_Draw(const ITEM *const item);

bool Lara_Draw(const ITEM *const item)
{
#if defined(__DREAMCAST__)
    // Lara's body casts a shadow volume or soft shadows in place of the
    // round shadow, when one of those is chosen.
    const SHADOW_TYPE type = g_Config.visuals.shadow_type;
    const bool is_lara = item == Lara_GetItem();
    DC_Shadow_SetLara(is_lara);
    DC_Shadow_SetCasting(
        is_lara
        && (type == SHADOW_TYPE_VOLUME || type == SHADOW_TYPE_SOFT
            || type == SHADOW_TYPE_SOFT_HIGH));
    const bool drawn = M_Draw(item);
    DC_Shadow_SetCasting(false);
    DC_Shadow_SetLara(false);
    return drawn;
#else
    return M_Draw(item);
#endif
}

static bool M_Draw(const ITEM *const item)
{
    const ITEM *const lara_item = Lara_GetItem();
    m_IsLara = item == lara_item;
    if (m_IsLara) {
        Output_Water_ObserveLaraFrame();
#if defined(__DREAMCAST__)
        M_IKUpdate(item, Lara_Skin_GetBoneBase());
#endif
    }
    if (m_IsLara
        && (!item->is_visible || item->trigger.spent || item->mesh_bits == 0)) {
        return false;
    }

    const int32_t top = g_PhdTop;
    const int32_t left = g_PhdLeft;
    const int32_t right = g_PhdRight;
    const int32_t bottom = g_PhdBottom;

    g_PhdLeft = Viewport_GetMinX(VIEWPORT_GAME);
    g_PhdRight = Viewport_GetMaxX(VIEWPORT_GAME);
    g_PhdTop = Viewport_GetMinY(VIEWPORT_GAME);
    g_PhdBottom = Viewport_GetMaxY(VIEWPORT_GAME);

    LARA_INFO *const lara = Lara_GetLaraInfo();
    ANIM_FRAME *frames[2];
    if (lara->hit_direction < 0) {
        int32_t rate;
        const int32_t frac = Item_GetFrames(lara_item, frames, &rate);
        if (frac != 0 && Lara_Pose_Get() == nullptr) {
            M_Draw_I(item, frames[0], frames[1], frac, rate);
            goto finish;
        }
    }

    const ANIM_FRAME *const hit_frame = Lara_GetHitFrame(item);
    const ANIM_FRAME *const frame =
        hit_frame == nullptr ? frames[0] : hit_frame;

    // A cutscene poses her with no animation behind it, so there is no frame
    // to take her bounds from until she is animating again.
    if (frame == nullptr) {
        goto finish;
    }

    const OBJECT *const obj = Object_Get(item->object_id);
    const BOUNDS_16 *const shadow_bounds = Item_GetBoundsAccurate(item);
    if (!Lara_Vehicle_IsMounted()) {
        DC_PROBE_START(probe_shadow);
        OutputSource_Shadows_Draw(obj->shadow_size, shadow_bounds, item);
        DC_PROBE_STOP(DC_PROBE_L_SHADOW, probe_shadow);
    }

    MATRIX saved_matrix = *g_MatrixPtr;
    MATRIX wsaved_matrix = *g_WMatrixPtr;

    Matrix_Push();
    Matrix_TranslateAbs32(item->interp.result.pos);
    Matrix_Rot16(item->interp.result.rot);
    const MATRIX item_matrix = *g_MatrixPtr;
    const MATRIX item_wmatrix = *g_WMatrixPtr;
    const CLIP clip = Output_CheckBoundsClip(&frame->bounds);
    if (clip == CLIP_NOT_VISIBLE && !m_IsLara) {
        m_CacheMatrices = false;
        Matrix_Pop();
        return false;
    }

    if (g_Config.debug.enable_debug_bounding_boxes) {
        const BOUNDS_32 bounds = BOUNDS_32_From16(&frame->bounds);
        Output_DrawCuboid(&bounds);
    }

    m_CacheMatrices = m_IsLara;
    Matrix_Push();
    DC_PROBE(DC_PROBE_L_LIGHT, M_CalculateLight(item, frame));
    if (m_CacheMatrices) {
        lara->mesh_pos_matrices_valid = false;
    }

    const ANIM_BONE *const bone =
        m_IsLara ? Lara_Skin_GetBoneBase() : Object_GetBone(obj, 0);
    const LARA_POSE *const pose = Lara_Pose_Get();
    const XYZ_16 *mesh_rots = pose != nullptr ? pose->rots : Anim_GetFrameRots(frame);

    Matrix_TranslateRel16(pose != nullptr ? pose->offset : frame->offset);
#if defined(__DREAMCAST__)
    if (m_IsLara && m_IKDropUnits != 0) {
        Matrix_TranslateRel32((XYZ_32) { 0, m_IKDropUnits, 0 });
    }
#endif
    Matrix_Rot16(mesh_rots[LM_HIPS]);
    M_CacheMatrix(LM_HIPS);
    M_DrawLaraMesh(item, LM_HIPS, clip, false);
    M_DrawEquipment(LM_HIPS, clip, false);

    Matrix_Push();
    M_DrawBodyPart(LM_THIGH_L, bone, mesh_rots, nullptr, clip);
    M_DrawEquipment(LM_THIGH_L, clip, false);
    M_DrawBodyPart(LM_CALF_L, bone, mesh_rots, nullptr, clip);
    M_DrawBodyPart(LM_FOOT_L, bone, mesh_rots, nullptr, clip);
    Matrix_Pop();

    Matrix_Push();
    M_DrawBodyPart(LM_THIGH_R, bone, mesh_rots, nullptr, clip);
    M_DrawEquipment(LM_THIGH_R, clip, false);
    M_DrawBodyPart(LM_CALF_R, bone, mesh_rots, nullptr, clip);
    M_DrawBodyPart(LM_FOOT_R, bone, mesh_rots, nullptr, clip);
    Matrix_Pop();

    Matrix_TranslateRel32(bone[LM_TORSO - 1].pos);
    if (Lara_IsMachineGunActive() && pose == nullptr) {
        mesh_rots =
            Anim_GetFrameRots(&lara->right_arm.frame_base[lara->right_arm.frame_num]);
    }

    Matrix_Rot16(mesh_rots[LM_TORSO]);
    Matrix_Rot16(lara->interp.result.torso_rot);
    M_CacheMatrix(LM_TORSO);
    M_DrawLaraMesh(item, LM_TORSO, clip, false);
    M_DrawEquipment(LM_TORSO, clip, false);

    Matrix_Push();
    Matrix_TranslateRel32(bone[LM_HEAD - 1].pos);
    Matrix_Rot16(mesh_rots[LM_HEAD]);
    Matrix_Rot16(lara->interp.result.head_rot);
    M_CacheMatrix(LM_HEAD);
    M_DrawLaraMesh(item, LM_HEAD, clip, false);
    M_DrawEquipment(LM_HEAD, clip, false);

    *g_MatrixPtr = saved_matrix;
    *g_WMatrixPtr = wsaved_matrix;
    if (m_IsLara) {
        DC_PROBE(DC_PROBE_L_HAIR, Lara_Hair_Draw());
    }

    Matrix_Pop();

    LARA_GUN_TYPE gun_type = LGT_UNARMED;
    if (pose == nullptr
        && (lara->gun_status == LGS_READY || lara->gun_status == LGS_SPECIAL
            || lara->gun_status == LGS_DRAW
            || lara->gun_status == LGS_UNDRAW)) {
        gun_type = lara->gun_type;
    }

    switch (M_GetArms(gun_type)) {
    case M_ARMS_EMPTY:
        Matrix_Push();
        M_DrawBodyPart(LM_UARM_R, bone, mesh_rots, nullptr, clip);
        M_DrawBodyPart(LM_LARM_R, bone, mesh_rots, nullptr, clip);
        M_DrawBodyPart(LM_HAND_R, bone, mesh_rots, nullptr, clip);
        M_DrawEquipment(LM_HAND_R, clip, false);
        Matrix_Pop();

        Matrix_Push();
        Matrix_TranslateRel32(bone[LM_UARM_L - 1].pos);
        if (lara->flare.control && pose == nullptr) {
            const ANIM *const anim = Anim_GetAnim(lara->left_arm.anim_num);
            mesh_rots =
                Anim_GetFrameRots(&lara->left_arm
                    .frame_base[lara->left_arm.frame_num - anim->frame_base]);
        }

        Matrix_Rot16(mesh_rots[LM_UARM_L]);
        M_DrawLaraMesh(item, LM_UARM_L, clip, false);

        M_DrawBodyPart(LM_LARM_L, bone, mesh_rots, nullptr, clip);
        M_DrawBodyPart(LM_HAND_L, bone, mesh_rots, nullptr, clip);
        M_DrawEquipment(LM_HAND_L, clip, false);

        if (g_TRVersion < 3 && Gun_IsFlareType(lara->gun_type)
            && lara->left_arm.flash_gun) {
            Gun_DrawFlash(lara->gun_type, clip, false);
        }

        Matrix_Pop();
        break;

    case M_ARMS_PISTOLS: {
        Matrix_Push();
        Matrix_TranslateRel32(bone[LM_UARM_R - 1].pos);
        g_MatrixPtr->_00 = item_matrix._00;
        g_MatrixPtr->_01 = item_matrix._01;
        g_MatrixPtr->_02 = item_matrix._02;
        g_MatrixPtr->_10 = item_matrix._10;
        g_MatrixPtr->_11 = item_matrix._11;
        g_MatrixPtr->_12 = item_matrix._12;
        g_MatrixPtr->_20 = item_matrix._20;
        g_MatrixPtr->_21 = item_matrix._21;
        g_MatrixPtr->_22 = item_matrix._22;
        g_WMatrixPtr->_00 = item_wmatrix._00;
        g_WMatrixPtr->_01 = item_wmatrix._01;
        g_WMatrixPtr->_02 = item_wmatrix._02;
        g_WMatrixPtr->_10 = item_wmatrix._10;
        g_WMatrixPtr->_11 = item_wmatrix._11;
        g_WMatrixPtr->_12 = item_wmatrix._12;
        g_WMatrixPtr->_20 = item_wmatrix._20;
        g_WMatrixPtr->_21 = item_wmatrix._21;
        g_WMatrixPtr->_22 = item_wmatrix._22;

        if (Gun_IsSinglePistolType(gun_type)) {
            Matrix_Rot16(lara->interp.result.torso_rot);
        } else {
            Matrix_Rot16(lara->right_arm.interp.result.rot);
        }
        if (pose == nullptr) {
            const ANIM *const anim = Anim_GetAnim(lara->right_arm.anim_num);
            mesh_rots =
                Anim_GetFrameRots(&lara->right_arm
                    .frame_base[lara->right_arm.frame_num - anim->frame_base]);
        }
        Matrix_Rot16(mesh_rots[LM_UARM_R]);
        M_DrawLaraMesh(item, LM_UARM_R, clip, false);

        M_DrawBodyPart(LM_LARM_R, bone, mesh_rots, nullptr, clip);
        M_DrawBodyPart(LM_HAND_R, bone, mesh_rots, nullptr, clip);
        M_DrawEquipment(LM_HAND_R, clip, false);

        if (lara->right_arm.flash_gun) {
            saved_matrix = *g_MatrixPtr;
            wsaved_matrix = *g_WMatrixPtr;
        }
        Matrix_Pop();

        Matrix_Push();
        Matrix_TranslateRel32(bone[LM_UARM_L - 1].pos);
        g_MatrixPtr->_00 = item_matrix._00;
        g_MatrixPtr->_01 = item_matrix._01;
        g_MatrixPtr->_02 = item_matrix._02;
        g_MatrixPtr->_10 = item_matrix._10;
        g_MatrixPtr->_11 = item_matrix._11;
        g_MatrixPtr->_12 = item_matrix._12;
        g_MatrixPtr->_20 = item_matrix._20;
        g_MatrixPtr->_21 = item_matrix._21;
        g_MatrixPtr->_22 = item_matrix._22;
        g_WMatrixPtr->_00 = item_wmatrix._00;
        g_WMatrixPtr->_01 = item_wmatrix._01;
        g_WMatrixPtr->_02 = item_wmatrix._02;
        g_WMatrixPtr->_10 = item_wmatrix._10;
        g_WMatrixPtr->_11 = item_wmatrix._11;
        g_WMatrixPtr->_12 = item_wmatrix._12;
        g_WMatrixPtr->_20 = item_wmatrix._20;
        g_WMatrixPtr->_21 = item_wmatrix._21;
        g_WMatrixPtr->_22 = item_wmatrix._22;

        if (Gun_IsSinglePistolType(gun_type)) {
            Matrix_Rot16(lara->interp.result.torso_rot);
        } else {
            Matrix_Rot16(lara->left_arm.interp.result.rot);
        }
        if (pose == nullptr) {
            const ANIM *const anim = Anim_GetAnim(lara->left_arm.anim_num);
            mesh_rots =
                Anim_GetFrameRots(&lara->left_arm
                    .frame_base[lara->left_arm.frame_num - anim->frame_base]);
        }
        Matrix_Rot16(mesh_rots[LM_UARM_L]);
        M_CacheMatrix(LM_UARM_L);
        M_DrawLaraMesh(item, LM_UARM_L, clip, false);

        M_DrawBodyPart(LM_LARM_L, bone, mesh_rots, nullptr, clip);
        M_DrawBodyPart(LM_HAND_L, bone, mesh_rots, nullptr, clip);
        M_DrawEquipment(LM_HAND_L, clip, false);

        if (lara->left_arm.flash_gun) {
            Gun_DrawFlash(gun_type, clip, false);
        }
        if (lara->right_arm.flash_gun) {
            *g_MatrixPtr = saved_matrix;
            *g_WMatrixPtr = wsaved_matrix;
            Gun_DrawFlash(gun_type, clip, false);
        }

        Matrix_Pop();
        break;
    }

    case M_ARMS_RIFLE: {
        Matrix_Push();
        Matrix_TranslateRel32(bone[LM_UARM_R - 1].pos);
        if (pose == nullptr) {
            mesh_rots =
                Anim_GetFrameRots(&lara->right_arm.frame_base[lara->right_arm.frame_num]);
        }
        Matrix_Rot16(mesh_rots[LM_UARM_R]);
        M_CacheMatrix(LM_UARM_R);
        M_DrawLaraMesh(item, LM_UARM_R, clip, false);

        M_DrawBodyPart(LM_LARM_R, bone, mesh_rots, nullptr, clip);
        M_DrawBodyPart(LM_HAND_R, bone, mesh_rots, nullptr, clip);
        M_DrawEquipment(LM_HAND_R, clip, false);

        if (lara->right_arm.flash_gun) {
            saved_matrix = *g_MatrixPtr;
            wsaved_matrix = *g_WMatrixPtr;
        }
        Matrix_Pop();

        Matrix_Push();
        M_DrawBodyPart(LM_UARM_L, bone, mesh_rots, nullptr, clip);
        M_DrawBodyPart(LM_LARM_L, bone, mesh_rots, nullptr, clip);
        M_DrawBodyPart(LM_HAND_L, bone, mesh_rots, nullptr, clip);

        if (lara->right_arm.flash_gun) {
            *g_MatrixPtr = saved_matrix;
            *g_WMatrixPtr = wsaved_matrix;
            Gun_DrawFlash(gun_type, clip, false);
        }

        Matrix_Pop();
        break;
    }

    default:
        break;
    }

    if (m_CacheMatrices) {
        lara->mesh_pos_matrices_valid = true;
    }
    m_CacheMatrices = false;

    Matrix_Pop();
    Matrix_Pop();

finish:
    if (m_IsLara && lara->electric != 0) {
        Lara_Electricity_Draw(0, lara_item);
        Lara_Electricity_Draw(1, lara_item);
    }

    g_PhdLeft = left;
    g_PhdRight = right;
    g_PhdTop = top;
    g_PhdBottom = bottom;
    return true;
}
