#pragma once

#include <trx/game/items/types.h>

bool Lara_Draw(const ITEM *item);
#if defined(__DREAMCAST__)
int32_t Lara_Draw_GetIKDrop(void);
#endif
