#pragma once

#include <trx/game/output/mesh_batcher/batcher.h>
#include <trx/game/rooms/types.h>

void OutputSource_Rooms_Init(MESH_BATCHER *batcher);
void OutputSource_Rooms_Shutdown(void);
void OutputSource_Rooms_ObserveLevelLoad(void);
void OutputSource_Rooms_ObserveLevelUnload(void);
void OutputSource_Rooms_ObserveRoomFlip(const ROOM *room);

void OutputSource_Rooms_StageRoom(const ROOM *room);
#if defined(__DREAMCAST__)
// A static merged into the mesh of the room that draws it (rooms.c): not
// drawn as an object.
bool OutputSource_Rooms_IsStaticMerged(int32_t draw_num);
bool OutputSource_Rooms_RayBlocked(
    const ROOM *room, const XYZ_32 *eye, const XYZ_32 *to);
// Paints the room's big opaque faces into the occlusion buffer (m: the
// game's matrix with the room's position), those within its window.
void OutputSource_Rooms_OcclAdd(
    const ROOM *room, const MATRIX *m, int32_t left, int32_t top,
    int32_t right, int32_t bottom);
// TEMP: the same ray, logged.
void OutputSource_Rooms_TraceRay(
    const ROOM *room, const XYZ_32 *eye, const XYZ_32 *to);
#endif
