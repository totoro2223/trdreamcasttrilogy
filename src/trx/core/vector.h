#pragma once

#include <stddef.h>
#include <stdint.h>

// Visible here so that Vector_Get is worked out where it is called (an
// index into the array) rather than through a call for each element.
struct VECTOR_PRIV {
    char *items;
};

typedef struct {
    int32_t count;
    int32_t capacity;
    size_t item_size;

    struct VECTOR_PRIV *priv;
} VECTOR;

VECTOR *Vector_Create(size_t item_size);
VECTOR *Vector_CreateAtCapacity(size_t item_size, int32_t capacity);
void Vector_EnsureCapacity(VECTOR *vector, int32_t capacity);
// Gives back the room the vector grew into but does not use.
void Vector_ShrinkToFit(VECTOR *vector);
void Vector_Free(VECTOR *vector);

int32_t Vector_IndexOf(const VECTOR *vector, const void *item);
int32_t Vector_LastIndexOf(const VECTOR *vector, const void *item);
bool Vector_Contains(const VECTOR *vector, const void *item);

// An index outside the vector: logged, then a trap (as ASSERT).
[[noreturn]] void Vector_FailIndex(const VECTOR *vector, int32_t index);
static inline void *Vector_Get(const VECTOR *const vector, const int32_t index)
{
    if ((uint32_t)index >= (uint32_t)vector->count) {
        Vector_FailIndex(vector, index);
    }
    return vector->priv->items + index * vector->item_size;
}
void *Vector_GetData(const VECTOR *vector);
void *Vector_Expand(VECTOR *vector, int32_t count);
void Vector_Add(VECTOR *vector, const void *item);
void Vector_Insert(VECTOR *vector, int32_t index, const void *item);
void Vector_Swap(VECTOR *vector, int32_t index1, int32_t index2);

bool Vector_Remove(VECTOR *vector, const void *item);
void Vector_RemoveAt(VECTOR *vector, int32_t index);

void Vector_Reverse(VECTOR *vector);
void Vector_Clear(VECTOR *vector);
void Vector_ClearRealloc(VECTOR *vector);
