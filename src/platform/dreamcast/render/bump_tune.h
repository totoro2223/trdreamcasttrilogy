#pragma once

// A test aid: the relief's (bump mapping) and the shine's values, changed in
// play with the d-pad and shown on the game's console. L + R + X turns it on
// and off; while it is on the d-pad, A and B go to it and not to the game:
//   Up / Down      the value to change (the console shows it)
//   Left / Right   lower / raise it (held, it repeats)
//   A              that value back to its first one
//   B              every value back to its first
// Distances are in blocks (1024 units). batcher.c reads g_DC_Bump.

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float depth; // how deep the relief's shade runs, of the whole colour
    float shade_floor; // the least colour a slope turned from the light keeps
    float dim; // how much colour a flat texel of the relief loses
    float range; // blocks: how near a face takes its relief
    float fade; // blocks: the stretch it fades out over
    float glow; // the specular shine's brightness
    float shine_flat; // of the shine's spot, what a flat texel takes
    float detail_near; // blocks: the shine shows the relief whole nearer
    float detail_far; // blocks: and none from this far
    float glow_range; // blocks: how near a face takes its shine
    float glow_fade; // blocks: the stretch the shine fades out over
    float tilt; // how far a passing light turns the relief's light
    // Counts up at each change: what batcher.c keeps from the values (the
    // kept relief, its table of constants) is made again.
    uint32_t version;
    bool active;
} DC_BUMP_TUNE;

extern DC_BUMP_TUNE g_DC_Bump;

void DC_BumpTune_Toggle(void);
// Called with the pad's buttons each poll; true while the tuning is on (the
// d-pad, A and B are then its, not the game's).
bool DC_BumpTune_Poll(uint32_t buttons);
// Up to three lines of text for the overlay while it is on: how many.
int32_t DC_BumpTune_Text(char out[][40]);
