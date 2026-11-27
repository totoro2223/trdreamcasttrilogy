! Hand-written SH-4 for Lara's braid (lara/hair.c, M_Control's segment loop),
! the game's own breeze (TR1, TR2) or none.
!
!   void dc_hair_step(DC_HAIR *b);
!
! Each segment from 1 to 6: moved on by three quarters of its velocity,
! dropped and held between the water and the floor by Lara's water state
! (the breeze pushing it along z above the water), pushed out of the body's
! five spheres, its parent turned towards it (Math_Atan), and put back at the
! bone's length from the parent: the parent's position plus the bone turned
! by Y then X. That turn is the C's Matrix_PushUnit, Matrix_TranslateSet32,
! Matrix_RotY, Matrix_RotX, Matrix_TranslateRel32 worked out on the unit
! matrix, to the same bits: rows (c, s sx >> 14, s cx >> 14), (0, cx, -sx),
! (-s, c sx >> 14, c cx >> 14), each sum >> 14. The velocity is what the
! segment moved. The C copies the packed segments into this aligned block and
! back. A sum s r d / dist is an integer division, done in floats: exact while
! r d stays under 2^24 (a sphere's radius times a point inside it).
! DC_HAIR (int32): 0 pos[7][3]  84 vel[7][3]  168 rot[7] (y, x)  224 bone[7][3]
!   308 sphere[5] (x y z r)  388 water state  392 water height  396 height
!   400 wind z (the game's breeze)  404 1: TR3's breeze  408 its wind x and
!   412 z (zero but above the water in a windy room)
! r8 b  r9 the segment (i)  r10 its pos  r11 the parent's pos  r12 i
! r13 scratch kept  r14 the sphere; frame: 0 old x y z  12 sin y  16 cos y
!   20 sin x  24 cos x

    .text
    .align 2

    .macro HCALL fn
    mov.l   \fn,r1
    jsr     @r1
    nop
    .endm

! The integer quotient a / b (r4, r5, b > 0) into r0, through the FPU.
    .macro IDIV
    lds     r4,fpul
    float   fpul,fr0
    lds     r5,fpul
    float   fpul,fr1
    fdiv    fr1,fr0
    ftrc    fr0,fpul
    sts     fpul,r0
    .endm

    .global _dc_hair_step
    .align 2
_dc_hair_step:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    mov.l   r13,@-r15
    mov.l   r14,@-r15
    sts.l   pr,@-r15
    add     #-28,r15
    mov     r4,r8
    mov     #1,r12
hs_seg:
    mov     r12,r10
    shll2   r10
    mov     r10,r0
    add     r10,r10
    add     r0,r10                  ! 12 i
    add     r8,r10                  ! the segment's pos
    mov     r10,r11
    add     #-12,r11                ! the parent's
    mov.l   @r10,r1
    mov.l   r1,@r15                 ! old
    mov.l   @(4,r10),r1
    mov.l   r1,@(4,r15)
    mov.l   @(8,r10),r1
    mov.l   r1,@(8,r15)
    ! pos += vel * 3 / 4 (toward zero)
    mov     #0,r13
hs_vel:
    mov     r10,r1
    add     r13,r1
    mov     r1,r2
    add     #84,r2                  ! its vel
    mov.l   @r2,r3
    mov     r3,r0
    add     r3,r3
    add     r0,r3                   ! v * 3
    cmp/pz  r3
    bt      1f
    add     #3,r3
1:  shar    r3
    shar    r3
    mov.l   @r1,r0
    add     r3,r0
    mov.l   r0,@r1
    add     #4,r13
    mov     r13,r0
    cmp/eq  #12,r0
    bf      hs_vel

    ! Water and floor (the game's breeze, or none).
    mov     r8,r1
    add     #127,r1
    add     #127,r1
    add     #127,r1
    add     #7,r1                   ! b + 388
    mov.l   @(16,r1),r0             ! TR3's breeze
    tst     r0,r0
    bt      hs_game
    mov.l   @(20,r1),r2             ! the room's wind, when above the water
    mov.l   @r10,r0
    add     r2,r0
    mov.l   r0,@r10
    mov.l   @(24,r1),r2
    mov.l   @(8,r10),r0
    add     r2,r0
    mov.l   r0,@(8,r10)
    mov.l   @(4,r1),r3              ! water height
    mov.l   @(8,r1),r4              ! height
    mov.l   @(4,r10),r5             ! y
    mov.l   .Lhs_noh,r6
    cmp/eq  r6,r3
    bt      8f
    cmp/ge  r3,r5                   ! not under the water's top
    bt      9f
8:  add     #10,r5                  ! drifts down, to the water's top
    cmp/eq  r6,r3
    bt      9f
    cmp/gt  r3,r5
    bf      9f
    mov     r3,r5
9:  cmp/gt  r4,r5                   ! below the floor: back, onto it
    bf      10f
    mov.l   @r15,r0
    mov.l   r0,@r10
    mov.l   @(8,r15),r0
    mov.l   r0,@(8,r10)
    mov     r5,r0
    sub     r4,r0
    mov.l   .Lhs_256,r2
    cmp/gt  r2,r0
    bt      10f
    mov     r4,r5
10: bra     hs_sety
    nop
hs_game:
    mov.l   @r1,r2                  ! water state
    mov.l   @(4,r1),r3              ! water height
    mov.l   @(8,r1),r4              ! height
    mov.l   @(4,r10),r5             ! y
    mov     r2,r0
    cmp/eq  #4,r0                   ! wading
    bf      2f
    mov.l   .Lhs_noh,r6
    cmp/eq  r6,r3
    bt      3f
    cmp/gt  r3,r5
    bt      2f
3:  mov     #0,r2                   ! above the water
2:  mov     r2,r0
    cmp/eq  #0,r0
    bt      hs_above
    cmp/eq  #1,r0
    bt      hs_clamp
    cmp/eq  #2,r0
    bt      hs_clamp
    cmp/eq  #4,r0
    bt      hs_clamp
    bra     hs_spheres
    nop
hs_above:
    add     #10,r5
    mov.l   .Lhs_noh,r6
    cmp/eq  r6,r3
    bt      4f
    cmp/gt  r3,r5
    bf      4f
    bra     hs_sety
    mov     r3,r5
4:  cmp/gt  r4,r5
    bf      5f
    bra     hs_sety
    mov     r4,r5
5:  mov.l   @(12,r1),r6             ! the breeze
    mov.l   @(8,r10),r7
    add     r6,r7
    mov.l   r7,@(8,r10)
    bra     hs_sety
    nop
hs_clamp:
    cmp/gt  r5,r3                   ! below the water's top
    bf      6f
    bra     hs_sety
    mov     r3,r5
6:  cmp/gt  r4,r5
    bf      hs_sety
    mov     r4,r5
hs_sety:
    mov.l   r5,@(4,r10)

    ! Out of the body's spheres.
hs_spheres:
    mov     r8,r14
    mov.l   .Lhs_308,r0
    add     r0,r14
    mov     #5,r13
hs_sph:
    mov.l   @r10,r1
    mov.l   @r14,r0
    sub     r0,r1                   ! dx
    mov.l   @(4,r10),r2
    mov.l   @(4,r14),r0
    sub     r0,r2                   ! dy
    mov.l   @(8,r10),r3
    mov.l   @(8,r14),r0
    sub     r0,r3                   ! dz
    mul.l   r3,r3
    sts     macl,r4
    mul.l   r2,r2
    sts     macl,r0
    add     r0,r4
    mul.l   r1,r1
    sts     macl,r0
    add     r0,r4                   ! dz^2 + dy^2 + dx^2
    mov.l   @(12,r14),r5
    mul.l   r5,r5
    sts     macl,r0
    cmp/ge  r0,r4
    bt      hs_sphnext
    mov.l   r1,@(12,r15)            ! keep dx dy dz
    mov.l   r2,@(16,r15)
    mov.l   r3,@(20,r15)
    HCALL   .Lhs_sqrt
    mov     #1,r1
    cmp/ge  r1,r0
    bt      7f
    mov     #1,r0
7:  mov.l   r0,@(24,r15)            ! dist
    mov.l   @(12,r14),r4
    mov.l   @(12,r15),r1
    mul.l   r1,r4
    sts     macl,r4
    mov.l   @(24,r15),r5
    IDIV
    mov.l   @r14,r1
    add     r1,r0
    mov.l   r0,@r10
    mov.l   @(12,r14),r4
    mov.l   @(16,r15),r1
    mul.l   r1,r4
    sts     macl,r4
    mov.l   @(24,r15),r5
    IDIV
    mov.l   @(4,r14),r1
    add     r1,r0
    mov.l   r0,@(4,r10)
    mov.l   @(12,r14),r4
    mov.l   @(20,r15),r1
    mul.l   r1,r4
    sts     macl,r4
    mov.l   @(24,r15),r5
    IDIV
    mov.l   @(8,r14),r1
    add     r1,r0
    mov.l   r0,@(8,r10)
hs_sphnext:
    dt      r13
    bf/s    hs_sph
    add     #16,r14

    ! The parent turned towards it.
    mov.l   @r10,r1
    mov.l   @r11,r0
    sub     r0,r1                   ! dx
    mov.l   @(8,r10),r2
    mov.l   @(8,r11),r0
    sub     r0,r2                   ! dz
    mov.l   r1,@(12,r15)
    mov.l   r2,@(16,r15)
    mul.l   r1,r1
    sts     macl,r4
    mul.l   r2,r2
    sts     macl,r0
    add     r0,r4
    HCALL   .Lhs_sqrt
    mov.l   r0,@(20,r15)            ! the distance
    mov.l   @(16,r15),r4
    mov.l   @(12,r15),r5
    HCALL   .Lhs_atan
    exts.w  r0,r0
    mov     r12,r9
    add     #-1,r9
    shll2   r9
    shll    r9                      ! 8 (i - 1)
    mov.l   .Lhs_168,r1
    add     r1,r9
    add     r8,r9                   ! the parent's rot
    mov.l   r0,@r9                  ! rot.y
    mov.l   @(20,r15),r4
    mov.l   @(4,r10),r5
    mov.l   @(4,r11),r0
    sub     r0,r5
    HCALL   .Lhs_atan
    neg     r0,r0
    exts.w  r0,r0
    mov.l   r0,@(4,r9)              ! rot.x

    ! Its turn's sines and cosines.
    mov.l   @r9,r4
    HCALL   .Lhs_sin
    mov.l   r0,@(12,r15)            ! s
    mov.l   @r9,r4
    mov.l   .Lhs_90,r1
    add     r1,r4
    HCALL   .Lhs_sin
    mov.l   r0,@(16,r15)            ! c
    mov.l   @(4,r9),r4
    HCALL   .Lhs_sin
    mov.l   r0,@(20,r15)            ! sx
    mov.l   @(4,r9),r4
    mov.l   .Lhs_90,r1
    add     r1,r4
    HCALL   .Lhs_sin
    mov.l   r0,@(24,r15)            ! cx

    ! The bone (b + 224 + 12 i), turned, from the parent's place.
    mov     r12,r1
    shll2   r1
    mov     r1,r0
    add     r1,r1
    add     r0,r1                   ! 12 i
    mov.l   .Lhs_224,r0
    add     r0,r1
    add     r8,r1
    mov.l   @r1,r2                  ! bx
    mov.l   @(4,r1),r3              ! by
    mov.l   @(8,r1),r4              ! bz
    mov     #-14,r13
    ! x: bx c + by (s sx >> 14) + bz (s cx >> 14)
    mov.l   @(12,r15),r5            ! s
    mov.l   @(20,r15),r6            ! sx
    mul.l   r5,r6
    sts     macl,r6
    shad    r13,r6
    mul.l   r3,r6
    sts     macl,r6
    mov.l   @(24,r15),r7            ! cx
    mul.l   r5,r7
    sts     macl,r7
    shad    r13,r7
    mul.l   r4,r7
    sts     macl,r7
    add     r7,r6
    mov.l   @(16,r15),r7            ! c
    mul.l   r2,r7
    sts     macl,r7
    add     r7,r6
    shad    r13,r6
    mov.l   @r11,r0
    add     r0,r6
    mov.l   r6,@r10
    ! y: by cx - bz sx
    mov.l   @(24,r15),r6
    mul.l   r3,r6
    sts     macl,r6
    mov.l   @(20,r15),r7
    mul.l   r4,r7
    sts     macl,r7
    sub     r7,r6
    shad    r13,r6
    mov.l   @(4,r11),r0
    add     r0,r6
    mov.l   r6,@(4,r10)
    ! z: -bx s + by (c sx >> 14) + bz (c cx >> 14)
    mov.l   @(16,r15),r5            ! c
    mov.l   @(20,r15),r6
    mul.l   r5,r6
    sts     macl,r6
    shad    r13,r6
    mul.l   r3,r6
    sts     macl,r6
    mov.l   @(24,r15),r7
    mul.l   r5,r7
    sts     macl,r7
    shad    r13,r7
    mul.l   r4,r7
    sts     macl,r7
    add     r7,r6
    mov.l   @(12,r15),r7            ! s
    mul.l   r2,r7
    sts     macl,r7
    sub     r7,r6
    shad    r13,r6
    mov.l   @(8,r11),r0
    add     r0,r6
    mov.l   r6,@(8,r10)

    ! Its velocity: what it moved.
    mov     r10,r1
    add     #84,r1
    mov.l   @r10,r2
    mov.l   @r15,r0
    sub     r0,r2
    mov.l   r2,@r1
    mov.l   @(4,r10),r2
    mov.l   @(4,r15),r0
    sub     r0,r2
    mov.l   r2,@(4,r1)
    mov.l   @(8,r10),r2
    mov.l   @(8,r15),r0
    sub     r0,r2
    mov.l   r2,@(8,r1)

    add     #1,r12
    mov     r12,r0
    cmp/eq  #7,r0
    bt      hs_end
    bra     hs_seg
    nop
hs_end:
    add     #28,r15
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
.Lhs_noh:
    .long   -3251200
.Lhs_256:
    .long   256
.Lhs_308:
    .long   308
.Lhs_168:
    .long   168
.Lhs_224:
    .long   224
.Lhs_90:
    .long   16384
.Lhs_sqrt:
    .long   _Math_Sqrt
.Lhs_atan:
    .long   _Math_Atan
.Lhs_sin:
    .long   _Math_Sin
