! Hand-written SH-4 for the creatures' logic (creature/common.c), TR1 and TR2.
! The C stays as the reference: a DC_EMIT_VERIFY build runs both and compares.
!
!   void dc_creature_mood(const DC_MOOD_BLOCK *b);
!
! Creature_Mood: Creature_UpdateMood then Creature_ApplyMood, branch by
! branch for TR1 and TR2, with the same calls in the same order (the control
! generator's draws included): Box_ValidBox, Box_TargetBox, Box_StalkBox,
! Box_EscapeBox, Random_GetControl, Box_CalculateTarget, and the attack's
! target through DC_Creature_MoodAttack.
! b: 0 the item  4 the AI_INFO  8 the CREATURE  12 Lara's item  16 violent
!    20 the object's smartness  24 1: a far creature may skip its next box
!    this tick (not in a demo, on its odd phase)  28 the game (1 to 3)
! CREATURE: 12 alerted
! CREATURE: 16 monkey_ahead  17 jump_ahead  28 mood  32 the LOT_INFO
!    80 target  92 enemy.  LOT_INFO: 20 node  28 search_num  30 zone_count
!    32 target_box  34 required_box.  BOX_NODE (8): 2 search_num  6 box_num.
! ITEM: 28 frame_num  42 hit_points  46 box_num  126 hit_status.
! AI_INFO: 0 zone_num  2 enemy_zone_num  4 distance.
! Registers: r8 item  r9 info  r10 creature  r11 lot  r12 enemy  r13 b
! r14 the mood  @r15 a box drawn at random.

    .text
    .align 2

    .macro CALL fn
    mov.l   \fn,r1
    jsr     @r1
    nop
    .endm

! T: zone_num == enemy_zone_num.
    .macro ZEQ
    mov.w   @r9,r1
    mov.w   @(2,r9),r0
    cmp/eq  r0,r1
    .endm

! Conditional jumps past a bt/bf's reach.
    .macro JT target
    bf      98f
    bra     \target
    nop
98:
    .endm
    .macro JF target
    bt      97f
    bra     \target
    nop
97:
    .endm

    .macro SETMOOD m
    mov     #\m,r0
    mov.l   r0,@(28,r10)
    .endm

! lot->required_box = NO_BOX.
    .macro NOREQ
    mov     #34,r0
    mov     #-1,r1
    mov.w   r1,@(r0,r11)
    .endm

! A box of the zone drawn at random (lot->node[zone_count * Random / 0x7FFF]),
! into r0 and @r15.
    .macro RANDBOX sfx
    CALL    .Lcm_rand\sfx
    mov     r0,r1
    mov     #30,r0
    mov.w   @(r0,r11),r2
    mul.l   r1,r2
    sts     macl,r1                 ! p, not below 0
    mov     r1,r2
    shlr8   r2
    shlr2   r2
    shlr2   r2
    shlr2   r2
    shlr    r2                      ! q = p >> 15
    mov     r2,r3
    shll8   r3
    shll2   r3
    shll2   r3
    shll2   r3
    shll    r3
    mov     r1,r5
    sub     r3,r5
    add     r2,r5                   ! r = p - 32767 q
    mov.l   .Lcm_7fff\sfx,r6
51: cmp/hs  r6,r5
    bf      52f
    add     #1,r2
    bra     51b
    sub     r6,r5
52: mov.l   @(20,r11),r1
    shll2   r2
    shll    r2
    add     r2,r1
    mov.w   @(6,r1),r0
    mov.l   r0,@r15
    .endm

! Box_ValidBox(item, zone_num, box in r0): T clear when not.
    .macro VALID sfx
    mov     r8,r4
    mov.w   @r9,r5
    mov     r0,r6
    CALL    .Lcm_valid\sfx
    tst     r0,r0
    .endm

! Box_StalkBox / Box_EscapeBox(item, enemy, box in r6): T clear when true.
    .macro STALK fn
    mov     r8,r4
    mov     r12,r5
    CALL    \fn
    tst     r0,r0
    .endm

! Box_TargetBox(lot, box in r5).
    .macro TARGET sfx
    mov     r11,r4
    CALL    .Lcm_target\sfx
    .endm

    .global _dc_creature_mood
    .align 2
_dc_creature_mood:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    mov.l   r13,@-r15
    mov.l   r14,@-r15
    sts.l   pr,@-r15
    add     #-4,r15
    mov     r4,r13
    mov.l   @r13,r8
    mov.l   @(4,r13),r9
    mov.l   @(8,r13),r10
    mov     r10,r11
    add     #32,r11                 ! the LOT_INFO
    mov     #92,r0
    mov.l   @(r0,r10),r12           ! the enemy

! --- Creature_UpdateMood
    mov     #46,r0
    mov.w   @(r0,r8),r1
    shll2   r1
    shll    r1
    mov.l   @(20,r11),r2
    add     r1,r2
    mov.w   @(2,r2),r0
    extu.w  r0,r1                   ! the node's search_num
    mov     #28,r0
    mov.w   @(r0,r11),r0
    extu.w  r0,r0
    mov.l   .Lcm_8000,r2
    or      r2,r0
    cmp/eq  r0,r1
    bf      1f
    NOREQ
1:  mov.l   @(28,r10),r0
    cmp/eq  #1,r0                   ! MOOD_ATTACK
    bt      2f
    mov     #34,r0
    mov.w   @(r0,r11),r0
    cmp/eq  #-1,r0
    bt      2f
    mov     r8,r4
    mov.w   @r9,r5
    mov     #32,r0
    mov.w   @(r0,r11),r6            ! target_box
    CALL    .Lcm_valid
    tst     r0,r0
    bf      2f
    ZEQ
    bf      3f
    SETMOOD 0
3:  NOREQ
2:  mov.l   @(28,r10),r14           ! the mood before
    tst     r12,r12
    bf      4f
    SETMOOD 0                       ! no enemy: bored
    bra     um_done
    nop
4:  mov     #42,r0
    mov.w   @(r0,r12),r1
    cmp/pl  r1
    bt      5f
    mov.l   @(28,r13),r0            ! TR3: only Lara dead bores it
    cmp/eq  #3,r0
    bf      60f
    mov.l   @(12,r13),r1
    cmp/eq  r1,r12
    bf      5f
60: SETMOOD 0                       ! the enemy is dead
    bra     um_done
    nop
5:  mov.l   @(16,r13),r0
    tst     r0,r0
    bt      um_calm
    mov     r14,r0                  ! violent
    cmp/eq  #1,r0
    bt      v_attack
    cmp/eq  #2,r0
    bt      v_escape
    cmp/eq  #0,r0
    bt      v_bs
    cmp/eq  #3,r0
    JF      um_done
v_bs:
    ZEQ
    bf      6f
    SETMOOD 1
    bra     um_done
    nop
6:  mov     #126,r0
    mov.b   @(r0,r8),r0
    tst     r0,r0
    JT      um_done
    SETMOOD 2
    bra     um_done
    nop
v_attack:
    ZEQ
    JT      um_done
    SETMOOD 0
    bra     um_done
    nop
v_escape:
    ZEQ
    JF      um_done
    SETMOOD 1
    bra     um_done
    nop
um_calm:
    mov     r14,r0
    cmp/eq  #1,r0
    bt      c_attack
    cmp/eq  #2,r0
    bt      c_escape
    cmp/eq  #0,r0
    bt      c_bs
    cmp/eq  #3,r0
    JF      um_done
c_bs:
    mov.l   @(28,r13),r0
    cmp/eq  #3,r0
    bf      61f
    mov.b   @(12,r10),r0            ! TR3: alerted, the enemy in another zone
    tst     r0,r0
    bt      7f
    ZEQ
    bt      7f
    mov.l   @(4,r9),r1
    mov.l   .Lcm_3w,r2
    cmp/gt  r2,r1
    bf      62f
    SETMOOD 3
    bra     um_done
    nop
62: SETMOOD 0
    bra     um_done
    nop
61: mov     #126,r0
    mov.b   @(r0,r8),r0
    tst     r0,r0
    bt      7f                      ! not hit
    CALL    .Lcm_rand
    mov.l   .Lcm_2048,r1
    cmp/ge  r1,r0
    bf      c_bs_esc                ! the draw below 2048
    ZEQ
    bt      7f
c_bs_esc:
    SETMOOD 2
    bra     um_done
    nop
7:  ZEQ
    JF      um_done
    mov.l   @(4,r9),r1              ! distance
    mov.l   .Lcm_attack,r2
    cmp/ge  r2,r1
    bf      8f                      ! nearer than the attack range
    mov.l   @(28,r10),r0
    cmp/eq  #3,r0
    bf      9f
    mov     #34,r0
    mov.w   @(r0,r11),r0
    cmp/eq  #-1,r0
    bf      9f
8:  SETMOOD 1
    bra     um_done
    nop
9:  SETMOOD 3
    bra     um_done
    nop
c_attack:
    mov     #126,r0
    mov.b   @(r0,r8),r0
    tst     r0,r0
    bt      10f
    CALL    .Lcm_rand
    mov.l   .Lcm_2048,r1
    cmp/ge  r1,r0
    bf      c_at_esc
    ZEQ
    bt      10f
c_at_esc:
    mov.l   @(28,r13),r0
    cmp/eq  #3,r0
    bt      63f
    SETMOOD 2
    bra     um_done
    nop
63: SETMOOD 3                       ! TR3: stalks instead
    bra     um_done
    nop
10: ZEQ
    JT      um_done
    mov.l   @(28,r13),r0
    cmp/eq  #3,r0
    bf      64f
    mov.l   @(4,r9),r1              ! TR3: bored only past 6 blocks
    mov.l   .Lcm_6w,r2
    cmp/gt  r2,r1
    JF      um_done
64: SETMOOD 0
    bra     um_done
    nop
c_escape:
    ZEQ
    JF      um_done
    CALL    .Lcm_rand
    mov.l   .Lcm_256,r1
    cmp/ge  r1,r0
    JT      um_done
    SETMOOD 3
um_done:
    mov.l   @(28,r10),r0
    cmp/eq  r14,r0
    bt      am_start
    mov     r14,r0
    cmp/eq  #1,r0
    bf      11f
    mov     #32,r0
    mov.w   @(r0,r11),r5
    TARGET                          ! left the attack: its box again
11: NOREQ
    bra     am_start
    nop

    .align 2
.Lcm_8000:
    .long   0x8000
.Lcm_7fff:
    .long   0x7FFF
.Lcm_2048:
    .long   2048
.Lcm_256:
    .long   256
.Lcm_attack:
    .long   9437184
.Lcm_3w:
    .long   3072
.Lcm_6w:
    .long   6144
.Lcm_think:
    .long   37748736
.Lcm_rand:
    .long   _Random_GetControl
.Lcm_valid:
    .long   _Box_ValidBox
.Lcm_target:
    .long   _Box_TargetBox
.Lcm_stalk:
    .long   _Box_StalkBox
.Lcm_escape:
    .long   _Box_EscapeBox
.Lcm_calc:
    .long   _Box_CalculateTarget
.Lcm_attackfn:
    .long   _DC_Creature_MoodAttack

! --- Creature_ApplyMood
am_start:
    mov     #92,r0
    mov.l   @(r0,r10),r12           ! creature->enemy
    mov.l   @(28,r10),r14
    mov.l   @(24,r13),r0
    tst     r0,r0
    bt      1f
    mov     #32,r0
    mov.w   @(r0,r11),r0
    cmp/eq  #-1,r0
    bt      1f
    mov     #34,r0
    mov.w   @(r0,r11),r0
    cmp/eq  #-1,r0
    bt      1f
    mov.l   @(4,r9),r1
    mov.l   .Lcm_think2,r2
    cmp/gt  r2,r1
    bf      1f
    mov     #-1,r14                 ! no new box this tick
1:  mov     r14,r0
    cmp/eq  #0,r0
    bt      a_bored
    cmp/eq  #1,r0
    bt      a_attack
    cmp/eq  #2,r0
    bt      a_escape
    cmp/eq  #3,r0
    bt      a_stalk
    bra     a_end
    nop

a_bored:
    RANDBOX 2
    VALID 2
    JT      a_end
    mov.l   @r15,r6
    STALK   .Lcm_stalk2
    bt      2f
    tst     r12,r12
    bt      2f
    mov     #42,r0
    mov.w   @(r0,r12),r1
    cmp/pl  r1
    bf      2f
    mov.l   @r15,r5
    TARGET 2
    mov.l   @(28,r13),r0            ! TR1 and TR2 stalk it
    cmp/eq  #3,r0
    bt      65f
    SETMOOD 3
65: bra     a_end
    nop
2:  mov     #34,r0
    mov.w   @(r0,r11),r0
    cmp/eq  #-1,r0
    JF      a_end
    mov.l   @r15,r5
    TARGET 2
    bra     a_end
    nop

a_attack:
    mov.l   @(20,r13),r1            ! smartness
    cmp/pz  r1
    bf      3f
    CALL    .Lcm_rand2
    mov.l   @(20,r13),r1
    cmp/gt  r0,r1                   ! the draw below the smartness
    JF      a_end
3:  mov     r8,r4
    mov     r11,r5
    mov     r12,r6
    CALL    .Lcm_attackfn2
    bra     a_end
    nop

a_escape:
    RANDBOX 2
    VALID 2
    JT      a_end
    mov     #34,r0
    mov.w   @(r0,r11),r0
    cmp/eq  #-1,r0
    JF      a_end
    mov.l   @r15,r6
    STALK   .Lcm_escape2
    bt      4f
    mov.l   @r15,r5
    TARGET 2
    bra     a_end
    nop
4:  ZEQ
    JF      a_end
    mov.l   @r15,r6
    STALK   .Lcm_stalk2
    JT      a_end
    mov.l   @(28,r13),r0            ! TR3: not when violent
    cmp/eq  #3,r0
    bf      66f
    mov.l   @(16,r13),r0
    tst     r0,r0
    JF      a_end
66: mov.l   @r15,r5
    TARGET 2
    SETMOOD 3
    bra     a_end
    nop

a_stalk:
    mov     #34,r0
    mov.w   @(r0,r11),r0
    cmp/eq  #-1,r0
    bt      5f
    mov     r0,r6
    STALK   .Lcm_stalk2
    JF      a_end                   ! still stalking its box
5:  RANDBOX 2
    VALID 2
    JT      a_end
    mov.l   @r15,r6
    STALK   .Lcm_stalk2
    bt      6f
    mov.l   @r15,r5
    TARGET 2
    bra     a_end
    nop
6:  mov     #34,r0
    mov.w   @(r0,r11),r0
    cmp/eq  #-1,r0
    JF      a_end
    mov.l   @r15,r5
    TARGET 2
    ZEQ
    JT      a_end
    SETMOOD 0

a_end:
    mov     #32,r0
    mov.w   @(r0,r11),r0
    cmp/eq  #-1,r0
    bf      7f
    mov     #46,r0
    mov.w   @(r0,r8),r5
    TARGET 2
7:  mov     r10,r4
    add     #80,r4                  ! creature->target
    mov     r8,r5
    mov     r11,r6
    CALL    .Lcm_calc2
    mov     r10,r1
    add     #16,r1
    mov     #0,r0
    mov.b   r0,@r1                  ! no monkey bars nor jumps before TR4
    mov.b   r0,@(1,r1)
    add     #4,r15
    lds.l   @r15+,pr
    mov.l   @r15+,r14
    mov.l   @r15+,r13
    mov.l   @r15+,r12
    mov.l   @r15+,r11
    mov.l   @r15+,r10
    mov.l   @r15+,r9
    rts
    mov.l   @r15+,r8

    .align 2
.Lcm_think2:
    .long   37748736
.Lcm_7fff2:
    .long   0x7FFF
.Lcm_valid2:
    .long   _Box_ValidBox
.Lcm_target2:
    .long   _Box_TargetBox
.Lcm_rand2:
    .long   _Random_GetControl
.Lcm_stalk2:
    .long   _Box_StalkBox
.Lcm_escape2:
    .long   _Box_EscapeBox
.Lcm_calc2:
    .long   _Box_CalculateTarget
.Lcm_attackfn2:
    .long   _DC_Creature_MoodAttack

! int32_t dc_creature_animate(const DC_ANIM_BLOCK *b);   Creature_Animate
!
! Creature_Animate for TR1 and TR2, step by step as the C: the room first
! (TR2, not a water creature), Item_Animate (a finished item dies), the box
! under the bounds' top and the heights of it and of the next box on the
! route, the step back to the sector it came from where the box does not do,
! the push off the sector's edges where Box_BadFloor says the floor is bad
! (with the turn and tilt when pushed), the overlap with another creature
! (back to where it was), then flying, jumping or walking onto the floor,
! drowning, and the room. Returns 0 when the creature died, else 1.
! The calls that take a structure by value or are static in the C go through
! DC_Cr_* (creature/common.c), with plain numbers.
! b: 0 the item  4 item_num  8 angle  12 tilt  16 the CREATURE  20 the zone
!    24 the boxes  28 the game (1, 2)  32 the object's radius  36 1: a water
!    creature  40 the top of a flyer against the ceiling: 0 its bounds',
!    1 none (0), 2 128  44 1: no overlap (TR3's T-rex)  48 1: a walker's top
!    against the ceiling is 768 (TR3's T-rex, Shiva, claw mutant)
! ITEM: 0 floor  16 object_id  28 frame_num  32 room_num  38 speed
!    46 box_num  100 pos  112 rot (x y z)  123 is_finished.
! CREATURE: 32 the LOT_INFO  84 target.y.  LOT_INFO: 0 step  2 drop  4 fly
!    16 is_jumping  17 is_monkeying  20 node.  BOX_NODE (8): 0 exit_box.
! BOX_INFO (24): 16 height.  SECTOR: 34 box.  BOUNDS_16: 2 min.y.
! Frame at r15: 0 old x  4 old y  8 old z  12 y (the bounds' top)  16 room
! (int16)  20 height  24 next height  28 the bounds  32..48 Box_BadFloor's
! numbers (y, height, next height, room, LOT)  52 shift x  56 shift z
! 60 dy  64 min y.
! Registers: r8 item  r9 b  r10 creature  r11 lot  r12 sector  r13 radius
! r14 scratch kept across calls.

    .macro LDI off, reg
    mov     #\off,r0
    mov.l   @(r0,r8),\reg
    .endm
    .macro STI reg, off
    mov     #\off,r0
    mov.l   \reg,@(r0,r8)
    .endm
    .macro ACALL fn
    mov.l   \fn,r1
    jsr     @r1
    nop
    .endm
    .macro AJT target
    bf      96f
    bra     \target
    nop
96:
    .endm
    .macro AJF target
    bt      95f
    bra     \target
    nop
95:
    .endm

! sector = DC_Cr_Sector(x, y, z, &room) with x and z the item's, y the
! bounds' top.
    .macro SECT_XYZ fn
    LDI     100,r4
    mov.l   @(12,r15),r5
    LDI     108,r6
    mov     r15,r7
    add     #16,r7
    ACALL   \fn
    mov     r0,r12
    .endm

! The box heights at the sector (r12): height, next height.
    .macro BOXH sfx
    mov     #34,r0
    mov.w   @(r0,r12),r0            ! its box
    cmp/eq  #-1,r0
    bf      81f
    mov     r12,r4
    LDI     100,r5
    mov.l   @(12,r15),r6
    LDI     108,r7
    ACALL   .Lan_height\sfx
    mov.l   r0,@(20,r15)
    bra     83f
    mov.l   r0,@(24,r15)            ! no next box
81: mov     r0,r1                   ! the box
    shll2   r1
    shll    r1
    mov     r1,r2
    add     r1,r1
    add     r2,r1                   ! 24 box
    mov.l   @(24,r9),r2
    add     r2,r1
    add     #16,r1
    mov.w   @r1,r2
    mov.l   r2,@(20,r15)            ! height
    shll2   r0
    shll    r0                      ! 8 box
    mov.l   @(20,r11),r1
    add     r0,r1
    mov.w   @r1,r0                  ! exit_box
    cmp/eq  #-1,r0
    bf      82f
    bra     83f
    mov.l   r2,@(24,r15)
82: mov     r0,r1
    shll2   r1
    shll    r1
    mov     r1,r2
    add     r1,r1
    add     r2,r1
    mov.l   @(24,r9),r2
    add     r2,r1
    add     #16,r1
    mov.w   @r1,r2
    mov.l   r2,@(24,r15)            ! next height
83:
    .endm

! Box_BadFloor(x + dx, y, z + dz, ...): T clear when the floor is bad.
    .macro BAD dxs, dzs
    mov     r15,r4
    add     #32,r4
    LDI     100,r5
    .ifc \dxs,+
    add     r13,r5
    .endif
    .ifc \dxs,-
    sub     r13,r5
    .endif
    LDI     108,r6
    .ifc \dzs,+
    add     r13,r6
    .endif
    .ifc \dzs,-
    sub     r13,r6
    .endif
    ACALL   .Lan_bad2
    tst     r0,r0
    .endm

    .global _dc_creature_animate
    .align 2
_dc_creature_animate:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    mov.l   r13,@-r15
    mov.l   r14,@-r15
    sts.l   pr,@-r15
    add     #-68,r15
    mov     r4,r9
    mov.l   @r9,r8                  ! the item
    mov.l   @(16,r9),r10            ! the creature
    mov     r10,r11
    add     #32,r11                 ! the LOT_INFO
    mov.l   @(32,r9),r13            ! the radius
    LDI     100,r1
    mov.l   r1,@r15                 ! old
    LDI     104,r1
    mov.l   r1,@(4,r15)
    LDI     108,r1
    mov.l   r1,@(8,r15)

    mov.l   @(28,r9),r0             ! TR2, TR3, not in the water: room first
    mov     #2,r1
    cmp/ge  r1,r0
    bf      1f
    mov.l   @(36,r9),r0
    tst     r0,r0
    bf      1f
    mov     #32,r0
    mov.w   @(r0,r8),r0
    mov.w   r0,@(16,r15)
    LDI     100,r4
    LDI     104,r5
    LDI     108,r6
    mov     r15,r7
    add     #16,r7
    ACALL   .Lan_sector
    mov.l   @(4,r9),r4
    mov.w   @(16,r15),r0
    mov     r0,r5
    ACALL   .Lan_update
1:  mov     r8,r4
    ACALL   .Lan_animate
    mov     #123,r0
    mov.b   @(r0,r8),r0
    tst     r0,r0
    bt      2f
    mov.l   @(4,r9),r4
    ACALL   .Lan_die
    bra     an_ret
    mov     #0,r0
2:  mov     r8,r4
    ACALL   .Lan_bounds
    mov.l   r0,@(28,r15)
    mov.w   @(2,r0),r0              ! min.y
    mov     r0,r1
    LDI     104,r2
    add     r2,r1
    mov.l   r1,@(12,r15)            ! y
    mov     #32,r0
    mov.w   @(r0,r8),r0
    mov.w   r0,@(16,r15)            ! the room
    mov.l   @r15,r4                 ! old x, y, old z
    mov.l   @(12,r15),r5
    mov.l   @(8,r15),r6
    mov     r15,r7
    add     #16,r7
    ACALL   .Lan_sector
    SECT_XYZ .Lan_sector
    BOXH

    ! Back to its sector where the box does not do.
    mov     #34,r0
    mov.w   @(r0,r12),r0
    cmp/eq  #-1,r0
    AJT     an_back
    mov     #16,r0
    mov.b   @(r0,r11),r0            ! is_jumping
    tst     r0,r0
    AJF     an_walls
    mov     #46,r0
    mov.w   @(r0,r8),r1             ! the item's box
    mov     #34,r0
    mov.w   @(r0,r12),r2            ! the sector's
    mov.l   @(28,r9),r0             ! TR3's flyers: no zones to keep to
    cmp/eq  #3,r0
    bf      70f
    mov.w   @(4,r11),r0
    tst     r0,r0
    bf      71f
70: mov.l   @(20,r9),r3             ! the zone
    mov     r1,r0
    add     r0,r0
    mov.w   @(r0,r3),r4
    mov     r2,r0
    add     r0,r0
    mov.w   @(r0,r3),r5
    cmp/eq  r4,r5
    AJF     an_back
71: mov     r1,r0                   ! box_height
    cmp/eq  #-1,r0
    bf      3f
    mov.l   @(20,r15),r4
    bra     4f
    nop
3:  shll2   r0
    shll    r0
    mov     r0,r2
    add     r0,r0
    add     r2,r0
    mov.l   @(24,r9),r2
    add     r2,r0
    add     #16,r0
    mov.w   @r0,r4
4:  mov.l   @(20,r15),r1
    sub     r1,r4                   ! box_height - height
    mov.w   @r11,r1                 ! step
    cmp/gt  r1,r4
    AJT     an_back
    mov.w   @(2,r11),r0             ! drop
    cmp/gt  r4,r0
    AJF     an_walls
an_back:
    LDI     100,r1
    mov     #-10,r0
    shad    r0,r1                   ! pos_x (TR1 and TR2 test z by it too)
    mov.l   @r15,r2
    shad    r0,r2                   ! old x >> 10
    mov.l   @r15,r3
    cmp/gt  r1,r2
    bf      5f
    mov.l   .Lan_mask,r4
    and     r4,r3
    STI     r3,100
    bra     6f
    nop
5:  cmp/gt  r2,r1
    bf      6f
    mov.l   .Lan_1023,r4
    or      r4,r3
    STI     r3,100
6:  mov.l   @(28,r9),r0             ! TR3 tests z by z
    cmp/eq  #3,r0
    bf      72f
    LDI     108,r1
    mov     #-10,r0
    shad    r0,r1
72: mov.l   @(8,r15),r2
    mov     #-10,r0
    shad    r0,r2                   ! old z >> 10
    mov.l   @(8,r15),r3
    cmp/gt  r1,r2
    bf      7f
    mov.l   .Lan_mask,r4
    and     r4,r3
    STI     r3,108
    bra     8f
    nop
7:  cmp/gt  r2,r1
    bf      8f
    mov.l   .Lan_1023,r4
    or      r4,r3
    STI     r3,108
8:  SECT_XYZ .Lan_sector
    BOXH
    bra     an_walls
    nop

    .align 2
.Lan_sector:
    .long   _DC_Cr_Sector
.Lan_height:
    .long   _DC_Cr_Height
.Lan_update:
    .long   _Item_UpdateRoom
.Lan_animate:
    .long   _Item_Animate
.Lan_die:
    .long   _DC_Cr_Die
.Lan_bounds:
    .long   _Item_GetBoundsAccurate
.Lan_mask:
    .long   0xFFFFFC00
.Lan_1023:
    .long   1023
.Lan_bad:
    .long   _DC_Cr_BadFloor

! The push off the sector's edges.
an_walls:
    mov.l   @(12,r15),r1
    mov.l   r1,@(32,r15)
    mov.l   @(20,r15),r1
    mov.l   r1,@(36,r15)
    mov.l   @(24,r15),r1
    mov.l   r1,@(40,r15)
    mov.w   @(16,r15),r0
    mov.l   r0,@(44,r15)
    mov.l   r11,@(48,r15)
    mov     #0,r0
    mov.l   r0,@(52,r15)            ! shift x
    mov.l   r0,@(56,r15)            ! shift z
    LDI     100,r1
    mov.l   .Lan_1023b,r2
    and     r2,r1
    mov     r1,r14                  ! pos_x within the sector
    LDI     108,r1
    and     r2,r1                   ! pos_z
    mov.l   .Lan_1024,r3
    sub     r13,r3                  ! WALL_L - radius
    cmp/ge  r13,r1
    AJF     w_zlow
    cmp/gt  r3,r1
    AJT     w_zhigh
    cmp/ge  r13,r14                 ! z in the middle
    bt      1f
    BAD     -,0
    AJT     w_done
    mov     r13,r1
    sub     r14,r1
    bra     w_done
    mov.l   r1,@(52,r15)
1:  mov.l   .Lan_1024b,r3
    sub     r13,r3
    cmp/gt  r3,r14
    AJF     w_done
    BAD     +,0
    AJT     w_done
    mov.l   .Lan_1024b,r1
    sub     r13,r1
    sub     r14,r1
    bra     w_done
    mov.l   r1,@(52,r15)

w_zlow:
    BAD     0,-
    bt      1f
    LDI     108,r1
    mov.l   .Lan_1023b,r2
    and     r2,r1
    mov     r13,r2
    sub     r1,r2
    mov.l   r2,@(56,r15)            ! radius - pos_z
1:  cmp/ge  r13,r14
    AJT     w_zlow_xhigh
    BAD     -,0
    bt      2f
    mov     r13,r1
    sub     r14,r1
    bra     w_done
    mov.l   r1,@(52,r15)
2:  mov.l   @(56,r15),r0
    tst     r0,r0
    AJF     w_done
    BAD     -,-
    AJT     w_done
    mov     #114,r0
    mov.w   @(r0,r8),r1
    mov.l   .Lan_m135,r2
    cmp/gt  r2,r1
    bf      3f
    mov.l   .Lan_45,r2
    cmp/ge  r2,r1
    bt      3f
    LDI     108,r1                  ! shift z
    mov.l   .Lan_1023b,r2
    and     r2,r1
    mov     r13,r2
    sub     r1,r2
    bra     w_done
    mov.l   r2,@(56,r15)
3:  mov     r13,r1
    sub     r14,r1
    bra     w_done
    mov.l   r1,@(52,r15)
w_zlow_xhigh:
    mov.l   .Lan_1024b,r3
    sub     r13,r3
    cmp/gt  r3,r14
    AJF     w_done
    BAD     +,0
    bt      4f
    mov.l   .Lan_1024b,r1
    sub     r13,r1
    sub     r14,r1
    bra     w_done
    mov.l   r1,@(52,r15)
4:  mov.l   @(56,r15),r0
    tst     r0,r0
    AJF     w_done
    BAD     +,-
    AJT     w_done
    mov     #114,r0
    mov.w   @(r0,r8),r1
    mov.l   .Lan_m45,r2
    cmp/gt  r2,r1
    bf      5f
    mov.l   .Lan_135,r2
    cmp/ge  r2,r1
    bt      5f
    LDI     108,r1
    mov.l   .Lan_1023b,r2
    and     r2,r1
    mov     r13,r2
    sub     r1,r2
    bra     w_done
    mov.l   r2,@(56,r15)
5:  mov.l   .Lan_1024b,r1
    sub     r13,r1
    sub     r14,r1
    bra     w_done
    mov.l   r1,@(52,r15)

w_zhigh:
    BAD     0,+
    bt      1f
    LDI     108,r1
    mov.l   .Lan_1023b,r2
    and     r2,r1
    mov.l   .Lan_1024b,r2
    sub     r13,r2
    sub     r1,r2
    mov.l   r2,@(56,r15)            ! WALL_L - radius - pos_z
1:  cmp/ge  r13,r14
    AJT     w_zhigh_xhigh
    BAD     -,0
    bt      2f
    mov     r13,r1
    sub     r14,r1
    bra     w_done
    mov.l   r1,@(52,r15)
2:  mov.l   @(56,r15),r0
    tst     r0,r0
    AJF     w_done
    BAD     -,+
    AJT     w_done
    mov     #114,r0
    mov.w   @(r0,r8),r1
    mov.l   .Lan_m45,r2
    cmp/gt  r2,r1
    bf      3f
    mov.l   .Lan_135,r2
    cmp/ge  r2,r1
    bt      3f
    mov     r13,r1                  ! shift x
    sub     r14,r1
    bra     w_done
    mov.l   r1,@(52,r15)
3:  LDI     108,r1
    mov.l   .Lan_1023b,r2
    and     r2,r1
    mov.l   .Lan_1024b,r2
    sub     r13,r2
    sub     r1,r2
    bra     w_done
    mov.l   r2,@(56,r15)
w_zhigh_xhigh:
    mov.l   .Lan_1024b,r3
    sub     r13,r3
    cmp/gt  r3,r14
    AJF     w_done
    BAD     +,0
    bt      4f
    mov.l   .Lan_1024b,r1
    sub     r13,r1
    sub     r14,r1
    bra     w_done
    mov.l   r1,@(52,r15)
4:  mov.l   @(56,r15),r0
    tst     r0,r0
    AJF     w_done
    BAD     +,+
    AJT     w_done
    mov     #114,r0
    mov.w   @(r0,r8),r1
    mov.l   .Lan_m135,r2
    cmp/gt  r2,r1
    bf      5f
    mov.l   .Lan_45,r2
    cmp/ge  r2,r1
    bt      5f
    mov.l   .Lan_1024b,r1           ! shift x
    sub     r13,r1
    sub     r14,r1
    bra     w_done
    mov.l   r1,@(52,r15)
5:  LDI     108,r1
    mov.l   .Lan_1023b,r2
    and     r2,r1
    mov.l   .Lan_1024b,r2
    sub     r13,r2
    sub     r1,r2
    mov.l   r2,@(56,r15)

w_done:
    mov.l   @(52,r15),r1
    mov.l   @(56,r15),r2
    LDI     100,r3
    add     r1,r3
    STI     r3,100
    LDI     108,r3
    add     r2,r3
    STI     r3,108
    or      r1,r2
    tst     r2,r2
    bt      an_overlap
    SECT_XYZ .Lan_sector2
    mov     #114,r0
    mov.w   @(r0,r8),r1
    mov.l   @(8,r9),r2
    add     r2,r1
    mov.w   r1,@(r0,r8)             ! rot.y += angle
    mov     r8,r4
    mov.l   @(12,r9),r5
    add     r5,r5
    exts.w  r5,r5
    ACALL   .Lan_tilt
    bra     an_overlap
    nop

    .align 2
.Lan_1023b:
    .long   1023
.Lan_bad2:
    .long   _DC_Cr_BadFloor
.Lan_1024b:
    .long   1024
.Lan_1024:
    .long   1024
.Lan_m135:
    .long   -24576
.Lan_45:
    .long   8192
.Lan_m45:
    .long   -8192
.Lan_135:
    .long   24576
.Lan_sector2:
    .long   _DC_Cr_Sector
.Lan_tilt:
    .long   _Creature_Tilt

! Another creature in the way: back to where it was.
an_overlap:
    mov.l   @(44,r9),r0             ! TR3's T-rex: no overlap
    tst     r0,r0
    bf      1f
    mov.l   @(4,r9),r4
    ACALL   .Lan_overlap
    tst     r0,r0
    bt      1f
    mov     r0,r14                  ! the other creature
    mov.l   @(28,r9),r0
    cmp/eq  #3,r0
    bf      73f
    mov     #108,r0                 ! TR3: turned away from it
    mov.l   @(r0,r14),r4
    LDI     108,r1
    sub     r1,r4                   ! dz
    mov     #100,r0
    mov.l   @(r0,r14),r5
    LDI     100,r1
    sub     r1,r5                   ! dx
    ACALL   .Lan_atan0
    mov     r0,r2
    mov     #114,r0
    mov.w   @(r0,r8),r1             ! rot.y
    sub     r1,r2
    exts.w  r2,r2                   ! the angle
    tst     r2,r2
    bt      76f
    mov     r2,r3
    cmp/pz  r3
    bt      74f
    neg     r3,r3
74: mov.l   .Lan_2048,r4
    cmp/ge  r4,r3
    bt      75f
    sub     r2,r1                   ! within 2048: all the way
    bra     77f
    nop
75: cmp/pl  r2
    bf      78f
    sub     r4,r1
    bra     77f
    nop
78: add     r4,r1
77: mov     #114,r0
    mov.w   r1,@(r0,r8)
76: bra     an_ret
    mov     #1,r0
73: mov.l   @r15,r1
    STI     r1,100
    mov.l   @(4,r15),r1
    STI     r1,104
    mov.l   @(8,r15),r1
    STI     r1,108
    bra     an_ret
    mov     #1,r0
1:  mov.w   @(4,r11),r0             ! fly
    tst     r0,r0
    AJT     an_ground

    ! A flyer: up or down towards its target, between floor and ceiling.
    mov     r0,r13                  ! fly (the radius is no longer needed)
    mov     #84,r0
    mov.l   @(r0,r10),r1            ! target.y
    LDI     104,r2
    sub     r2,r1                   ! dy
    cmp/gt  r13,r1
    bf      2f
    mov     r13,r1
2:  neg     r13,r2
    cmp/gt  r1,r2
    bf      3f
    mov     r2,r1
3:  mov.l   r1,@(60,r15)
    mov     r12,r4
    LDI     100,r5
    mov.l   @(12,r15),r6
    LDI     108,r7
    ACALL   .Lan_height3
    mov     r0,r14                  ! height
    LDI     104,r1
    mov.l   @(60,r15),r2
    add     r1,r2
    cmp/gt  r14,r2                  ! y + dy > height
    bf      fl_ceiling
    cmp/gt  r14,r1
    bt      4f
    mov     #0,r2
    mov.l   r2,@(60,r15)
    STI     r14,104
    bra     fl_move
    nop
4:  neg     r13,r2
    mov.l   r2,@(60,r15)
    mov.l   @r15,r1
    STI     r1,100
    mov.l   @(8,r15),r1
    STI     r1,108
    bra     fl_move
    nop
fl_ceiling:
    mov.l   @(28,r9),r0             ! TR3's flyers keep out of the water
    cmp/eq  #3,r0
    bf      79f
    mov.l   @(36,r9),r0
    tst     r0,r0
    bf      79f
    mov     r8,r4
    mov.l   @(12,r15),r5
    ACALL   .Lan_wbelow
    tst     r0,r0
    bt      79f
    neg     r13,r1
    mov.l   r1,@(60,r15)            ! dy = -fly
79: mov     r12,r4
    LDI     100,r5
    mov.l   @(12,r15),r6
    LDI     108,r7
    ACALL   .Lan_ceiling
    mov     r0,r14                  ! ceiling
    mov.l   @(40,r9),r0
    cmp/eq  #1,r0
    bf      5f
    mov     #0,r1
    bra     6f
    nop
5:  cmp/eq  #2,r0
    bf      7f
    mov.l   .Lan_128,r1
    bra     6f
    nop
7:  mov.l   @(28,r15),r1
    add     #2,r1
    mov.w   @r1,r1                  ! min.y
6:  LDI     104,r2
    add     r1,r2                   ! y + min_y
    mov.l   @(60,r15),r3
    add     r2,r3
    cmp/ge  r14,r3
    bt      fl_move
    cmp/ge  r14,r2
    bt      8f
    mov.l   @r15,r1
    STI     r1,100
    mov.l   @(8,r15),r1
    STI     r1,108
    bra     fl_move
    mov.l   r13,@(60,r15)
8:  mov     #0,r1
    mov.l   r1,@(60,r15)
fl_move:
    LDI     104,r1
    mov.l   @(60,r15),r2
    add     r2,r1
    STI     r1,104
    SECT_XYZ .Lan_sector3
    mov     r12,r4
    LDI     100,r5
    mov.l   @(12,r15),r6
    LDI     108,r7
    ACALL   .Lan_height3
    mov.l   r0,@r8                  ! floor
    mov     #38,r0
    mov.w   @(r0,r8),r4             ! speed
    tst     r4,r4
    bt      9f
    mov.l   @(60,r15),r5
    neg     r5,r5
    ACALL   .Lan_atan
    exts.w  r0,r0
    bra     10f
    nop
9:  mov     #0,r0
10: mov.l   @(28,r9),r1             ! TR2: held to 20 degrees
    mov     r1,r2
    mov     #2,r1
    cmp/ge  r1,r2
    bf      11f
    mov.l   .Lan_xrot,r1
    cmp/gt  r1,r0
    bf      12f
    mov     r1,r0
12: neg     r1,r1
    cmp/gt  r0,r1
    bf      11f
    mov     r1,r0
11: mov     r0,r1                   ! item_angle
    mov     #112,r0
    mov.w   @(r0,r8),r2             ! rot.x
    mov.l   .Lan_deg1,r3
    mov     r2,r4
    sub     r3,r4
    cmp/gt  r1,r4                   ! item_angle < rot.x - DEG_1
    bf      13f
    mov.w   r4,@(r0,r8)
    bra     an_drown
    nop
13: mov     r2,r4
    add     r3,r4
    cmp/gt  r4,r1                   ! item_angle > rot.x + DEG_1
    bf      14f
    mov.w   r4,@(r0,r8)
    bra     an_drown
    nop
14: mov.w   r1,@(r0,r8)
    bra     an_drown
    nop

    .align 2
.Lan_overlap:
    .long   _DC_Cr_Overlap
.Lan_wbelow:
    .long   _DC_Cr_WaterBelow
.Lan_atan0:
    .long   _Math_Atan
.Lan_2048:
    .long   2048
.Lan_height3:
    .long   _DC_Cr_Height
.Lan_ceiling:
    .long   _DC_Cr_Ceiling
.Lan_128:
    .long   128
.Lan_sector3:
    .long   _DC_Cr_Sector
.Lan_atan:
    .long   _Math_Atan
.Lan_xrot:
    .long   3640
.Lan_deg1:
    .long   182

! Jumping or walking: onto the floor under its own position.
an_ground:
    LDI     100,r4
    LDI     104,r5
    LDI     108,r6
    mov     r15,r7
    add     #16,r7
    ACALL   .Lan_sector4
    mov     r0,r12
    mov     #16,r0
    mov.b   @(r0,r11),r0            ! TR3, walking: the ceiling first
    tst     r0,r0
    bf      80f
    mov.l   @(28,r9),r0
    cmp/eq  #3,r0
    bf      80f
    mov     r12,r4
    LDI     100,r5
    LDI     104,r6
    LDI     108,r7
    ACALL   .Lan_ceiling4
    mov     r0,r14
    mov.l   @(48,r9),r0
    tst     r0,r0
    bt      81f
    mov.l   .Lan_768,r1             ! the big ones: three steps
    bra     82f
    nop
81: mov.l   @(28,r15),r1
    add     #2,r1
    mov.w   @r1,r1                  ! min.y
82: LDI     104,r2
    add     r1,r2
    cmp/ge  r14,r2
    bt      80f
    mov.l   @r15,r1                 ! under the ceiling: back where it was
    STI     r1,100
    mov.l   @(4,r15),r1
    STI     r1,104
    mov.l   @(8,r15),r1
    STI     r1,108
    LDI     100,r4
    LDI     104,r5
    LDI     108,r6
    mov     r15,r7
    add     #16,r7
    ACALL   .Lan_sector4
    mov     r0,r12
80: mov     r12,r4
    LDI     100,r5
    LDI     104,r6
    LDI     108,r7
    ACALL   .Lan_height4
    mov     r0,r14                  ! the floor
    mov.l   r0,@r8
    mov     #16,r0
    mov.b   @(r0,r11),r0            ! is_jumping
    tst     r0,r0
    bt      g_walk
    mov     #17,r0
    mov.b   @(r0,r11),r0            ! is_monkeying
    tst     r0,r0
    bt      1f
    mov     r12,r4
    LDI     100,r5
    mov.l   @(12,r15),r6
    LDI     108,r7
    ACALL   .Lan_ceiling4
    mov.l   @(28,r15),r1
    add     #2,r1
    mov.w   @r1,r1
    sub     r1,r0
    STI     r0,104
    bra     an_drown
    nop
1:  LDI     104,r1
    cmp/gt  r14,r1
    bf      an_drown
    mov.l   .Lan_256,r2
    add     r14,r2
    cmp/gt  r2,r1
    bf      2f
    mov.l   @r15,r1
    STI     r1,100
    mov.l   @(4,r15),r1
    STI     r1,104
    mov.l   @(8,r15),r1
    STI     r1,108
    bra     an_drown
    nop
2:  STI     r14,104
    bra     an_drown
    nop
g_walk:
    LDI     104,r1
    cmp/gt  r14,r1
    bf      3f
    STI     r14,104                 ! below the floor: onto it
    bra     4f
    nop
3:  mov     r14,r2
    sub     r1,r2
    mov     #64,r3
    cmp/gt  r3,r2
    bf      5f
    add     #64,r1                  ! a quarter step down at a time
    STI     r1,104
    bra     4f
    nop
5:  cmp/gt  r1,r14
    bf      4f
    STI     r14,104
4:  mov     #0,r1
    mov     #112,r0
    mov.w   r1,@(r0,r8)             ! rot.x
an_drown:
    mov.l   @(36,r9),r0
    tst     r0,r0
    bf      1f
    LDI     100,r4
    LDI     104,r5
    mov.l   .Lan_512,r1
    sub     r1,r5
    LDI     108,r6
    mov     r15,r7
    add     #16,r7
    ACALL   .Lan_sector4
    mov     r8,r4
    mov.l   @(28,r15),r5
    mov.w   @(16,r15),r0
    mov     r0,r6
    ACALL   .Lan_drowned
    tst     r0,r0
    bt      1f
    mov     r8,r4
    mov     #0,r5
    ACALL   .Lan_fatal
1:  mov.l   @(4,r9),r4
    mov.w   @(16,r15),r0
    mov     r0,r5
    ACALL   .Lan_update4
    mov     #1,r0
an_ret:
    add     #68,r15
    lds.l   @r15+,pr
    mov.l   @r15+,r14
    mov.l   @r15+,r13
    mov.l   @r15+,r12
    mov.l   @r15+,r11
    mov.l   @r15+,r10
    mov.l   @r15+,r9
    rts
    mov.l   @r15+,r8

    .align 2
.Lan_sector4:
    .long   _DC_Cr_Sector
.Lan_768:
    .long   768
.Lan_height4:
    .long   _DC_Cr_Height
.Lan_ceiling4:
    .long   _DC_Cr_Ceiling
.Lan_256:
    .long   256
.Lan_512:
    .long   512
.Lan_drowned:
    .long   _DC_Cr_Drowned
.Lan_fatal:
    .long   _Item_TakeFatalDamage
.Lan_update4:
    .long   _Item_UpdateRoom

! void dc_target_box(const DC_TBOX *b);   pathing/box.c Box_TargetBox
!
! A target in the box (its number's low 15 bits): half a block in from the
! near corner and a random way across, as the games draw it (TR3: the box's
! size in blocks less one, >> 5, times the draw; TR1, TR2: the draw times the
! box's size less a block, >> 15, TR2 a unit wider), z drawn before x; the
! box is the LOT's required one, and the target's y its floor (a flyer's a
! step and a half up). Nothing for a box past the level's.
! b: 0 the LOT_INFO  4 the box  8 the boxes  12 how many  16 the game
! BOX_INFO (24): 0 left  4 right  8 top  12 bottom  16 height.
! LOT_INFO: 4 fly  34 required_box  36 target (x y z).
    .global _dc_target_box
    .align 2
_dc_target_box:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    sts.l   pr,@-r15
    mov.l   @r4,r8                  ! the LOT
    mov.l   @(4,r4),r9
    mov.l   .Ltb_7fff,r0
    and     r0,r9                   ! the box's number
    mov.l   @(8,r4),r10             ! the boxes
    mov.l   @(16,r4),r11            ! the game
    tst     r10,r10
    bt      tb_done
    mov.l   @(12,r4),r0
    cmp/hs  r0,r9
    bt      tb_done
    mov     r9,r0
    shll2   r0
    shll    r0
    mov     r0,r1
    add     r0,r0
    add     r1,r0                   ! 24 box
    add     r0,r10                  ! the box
    mov     r11,r0
    cmp/eq  #3,r0
    bf      tb_old
    ! TR3
    mov.l   @(4,r10),r1
    add     #1,r1
    mov.l   @r10,r0
    sub     r0,r1
    mov     #-10,r0
    shad    r0,r1
    add     #-1,r1
    mov     #-5,r0
    shad    r0,r1                   ! width >> 5
    mov.l   .Ltb_rand,r0
    jsr     @r0
    mov     r1,r11
    mul.l   r0,r11
    sts     macl,r1
    mov.l   @r10,r0
    add     r0,r1
    mov.l   .Ltb_512,r0
    add     r0,r1
    mov     r8,r2
    add     #44,r2
    mov.l   r1,@r2                  ! z
    mov.l   @(12,r10),r1
    add     #1,r1
    mov.l   @(8,r10),r0
    sub     r0,r1
    mov     #-10,r0
    shad    r0,r1
    add     #-1,r1
    mov     #-5,r0
    shad    r0,r1                   ! depth >> 5
    mov.l   .Ltb_rand,r0
    jsr     @r0
    mov     r1,r11
    mul.l   r0,r11
    sts     macl,r1
    mov.l   @(8,r10),r0
    add     r0,r1
    mov.l   .Ltb_512,r0
    add     r0,r1
    mov     r8,r2
    add     #36,r2
    mov.l   r1,@r2                  ! x
    bra     tb_req
    nop
tb_old:
    mov     #0,r1                   ! TR2 a unit wider
    mov     r11,r0
    cmp/eq  #1,r0
    bt      1f
    mov     #1,r1
1:  mov     r1,r11
    mov.l   .Ltb_rand,r0
    jsr     @r0
    nop
    mov.l   @(4,r10),r1
    add     r11,r1
    mov.l   @r10,r2
    sub     r2,r1
    mov.l   .Ltb_1024,r2
    sub     r2,r1                   ! right + shift - left - 1024
    mul.l   r0,r1
    sts     macl,r1
    mov     #-15,r0
    shad    r0,r1
    mov.l   @r10,r0
    add     r0,r1
    mov.l   .Ltb_512,r0
    add     r0,r1
    mov     r8,r2
    add     #44,r2
    mov.l   r1,@r2                  ! z
    mov.l   .Ltb_rand,r0
    jsr     @r0
    nop
    mov.l   @(12,r10),r1
    add     r11,r1
    mov.l   @(8,r10),r2
    sub     r2,r1
    mov.l   .Ltb_1024,r2
    sub     r2,r1
    mul.l   r0,r1
    sts     macl,r1
    mov     #-15,r0
    shad    r0,r1
    mov.l   @(8,r10),r0
    add     r0,r1
    mov.l   .Ltb_512,r0
    add     r0,r1
    mov     r8,r2
    add     #36,r2
    mov.l   r1,@r2                  ! x
tb_req:
    mov     #34,r0
    mov.w   r9,@(r0,r8)             ! required_box
    mov.w   @(16,r10),r0
    mov     r0,r1                   ! height
    mov.w   @(4,r8),r0              ! fly
    tst     r0,r0
    bt      2f
    mov.l   .Ltb_384,r0
    sub     r0,r1
2:  mov     r8,r2
    add     #40,r2
    mov.l   r1,@r2                  ! y
tb_done:
    lds.l   @r15+,pr
    mov.l   @r15+,r11
    mov.l   @r15+,r10
    mov.l   @r15+,r9
    rts
    mov.l   @r15+,r8

    .align 2
.Ltb_7fff:
    .long   0x7FFF
.Ltb_512:
    .long   512
.Ltb_1024:
    .long   1024
.Ltb_384:
    .long   384
.Ltb_rand:
    .long   _Random_GetControl
