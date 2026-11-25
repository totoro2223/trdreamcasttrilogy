#pragma once

#include <trx/game/anims/walk.h>
#include <trx/game/items/types.h>

// Ragdolls for human enemies (the 15-mesh skeleton Lara has): the killing
// shot pushes the body along its path and it falls with physics. Visual only:
// the item, its collision and what it drops are left as they were.

// The killing shot; start and hit may be null.
void DC_Ragdoll_Start(
    const ITEM *item, const XYZ_32 *start, const XYZ_32 *hit);
// Once a tick, at its end (after every item moved).
void DC_Ragdoll_Control(void);
// A new level: every body forgotten.
void DC_Ragdoll_Reset(void);
// The body's pose for this frame and bounds in the item's frame, or false
// when the item has no ragdoll.
bool DC_Ragdoll_GetPose(
    const ITEM *item, ANIM_POSE *pose, const BOUNDS_16 **bounds);
