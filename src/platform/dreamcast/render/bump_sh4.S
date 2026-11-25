! Hand-written SH-4 for the relief's offset colour at one vertex (batcher.c,
! M_BumpVertex with M_BumpGatherLights, M_BumpAt, M_BumpShade and M_Atan2).
!
!   uint32_t dc_bump_vertex(M_BUMPRUN *run);
!
! The C (M_BumpVertexC) stays as the reference: a build with DC_EMIT_VERIFY runs
! both and compares them. The same maths, in the same order: fsrra is the C
! M_InvSqrt; dot products are fipr's.
!
! Struct M_BUMPRUN (batcher.c):
!    0 lights  4 frame  8 vert  12 glow  16 fade out  20 by_light out
!   24 K (the constants)  28 view[1][0]  32 view[1][1]  36 view[1][2]
!   40 light count  44 radius  48 power  52 passing (the arrays of the lights)
! M_BUMP_LIGHTS: pos[16][3] 0, radius 192, power 256, passing 320 (bytes)
! M_BUMP_FRAME: t 0, bt 12, n 24, tl 36, bl 40, nl 44
! M_XVERT: x 0, y 4, z 8, argb 24
!
! Registers (r8-r14 and fr12-fr15 are kept):
!   r8 run  r9 lights  r10 frame  r11 vert  r12 glow  r13 locals
!   r14 K + 128 (so that the 34 constants lie within a signed byte)
!   fr14 1.0  fr15 0.0
! The locals, in bytes from r13: 0 pos  12 room_pos  24 passing_pos
!   36 room_total  40 passing  44 light  56 scale  60 tilt  64 room_dir
!   76 passing_dir  (from the BumpAt on: 12 l  24 steady_smooth  28 shine_smooth
!   32 strength  76 h  88 hz  92 detail)

    .macro JT target
    bf      9f
    bra     \target
    nop
9:
    .endm

    .macro JF target
    bt      9f
    bra     \target
    nop
9:
    .endm

! A constant of the table, by index.
    .macro KL fr, idx
    mov     #(4*\idx-128),r0
    fmov.s  @(r0,r14),\fr
    .endm

    .macro LDL fr, off
    mov     #\off,r0
    fmov.s  @(r0,r13),\fr
    .endm

    .macro STL fr, off
    mov     #\off,r0
    fmov.s  \fr,@(r0,r13)
    .endm

! Three floats at base + off into fv0 (w 0) or fv4.
    .macro LV0 base, off
    mov     \base,r1
    add     #\off,r1
    fmov.s  @r1+,fr0
    fmov.s  @r1+,fr1
    fmov.s  @r1+,fr2
    fldi0   fr3
    .endm

    .macro LV4 base, off
    mov     \base,r1
    add     #\off,r1
    fmov.s  @r1+,fr4
    fmov.s  @r1+,fr5
    fmov.s  @r1+,fr6
    fldi0   fr7
    .endm

! M_Clamp(x, lo, hi), x in \f.
    .macro CLAMP f, lo, hi
    fcmp/gt \f,\lo
    bf      1f
    fmov    \lo,\f
    bra     2f
    nop
1:  fcmp/gt \hi,\f
    bf      2f
    fmov    \hi,\f
2:
    .endm

    .macro CLAMP01 f
    CLAMP   \f, fr15, fr14
    .endm

! M_PackUnit(\src) into r1 (destroys fr12, fr13).
    .macro PACKU src
    fmov    \src,fr12
    CLAMP01 fr12
    KL      fr13, 12
    fmul    fr13,fr12
    KL      fr13, 13
    fadd    fr13,fr12
    ftrc    fr12,fpul
    sts     fpul,r1
    .endm

! The dot product of the two vectors at fv0 and fv4 into fr7.
    .macro DOT
    fipr    fv0,fv4
    .endm

    .text
    .align 2
    .global _dc_bump_vertex
_dc_bump_vertex:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    mov.l   r13,@-r15
    mov.l   r14,@-r15
    fmov.s  fr12,@-r15
    fmov.s  fr13,@-r15
    fmov.s  fr14,@-r15
    fmov.s  fr15,@-r15
    add     #-128,r15
    mov     r4,r8
    mov.l   @(0,r8),r9
    mov.l   @(4,r8),r10
    mov.l   @(8,r8),r11
    mov.l   @(12,r8),r12
    mov.l   @(24,r8),r14
    add     #127,r14
    add     #1,r14
    mov     r15,r13
    fldi1   fr14
    fldi0   fr15

    ! the vertex's place, and the sums of the lights at zero
    fmov.s  @r11,fr0
    mov     #4,r0
    fmov.s  @(r0,r11),fr1
    mov     #8,r0
    fmov.s  @(r0,r11),fr2
    STL     fr0, 0
    STL     fr1, 4
    STL     fr2, 8
    STL     fr15, 12
    STL     fr15, 16
    STL     fr15, 20
    STL     fr15, 24
    STL     fr15, 28
    STL     fr15, 32
    STL     fr15, 36
    STL     fr15, 40

    ! M_BumpGatherLights
    mov.l   @(40,r8),r5
    tst     r5,r5
    JT      bv_gathered
    mov     r9,r1
    mov.l   @(44,r8),r2
    mov.l   @(48,r8),r3
    mov.l   @(52,r8),r4
bv_gl:
    fmov.s  @r1+,fr4
    fmov.s  @r1+,fr5
    fmov.s  @r1+,fr6
    fmov    fr4,fr7
    fsub    fr0,fr7
    fmov    fr5,fr8
    fsub    fr1,fr8
    fmov    fr6,fr9
    fsub    fr2,fr9
    fmov    fr7,fr10
    fmul    fr7,fr10
    fmov    fr8,fr11
    fmul    fr8,fr11
    fadd    fr11,fr10
    fmov    fr9,fr11
    fmul    fr9,fr11
    fadd    fr11,fr10               ! dist_sq
    fmov.s  @r2+,fr11
    fmov    fr11,fr12
    fmul    fr11,fr12               ! radius squared
    fcmp/gt fr10,fr12
    bf      bv_gnext
    ! fsrra in place of fdiv, fsqrt and fdiv (M_Recip, M_Sqrt0): 4 cycles
    ! each against 13, 23 and 13 that the next instruction waits for
    fsrra   fr12
    fmul    fr12,fr12
    fmul    fr12,fr10               ! ratio_sq
    KL      fr11, 33
    fadd    fr10,fr11
    fsrra   fr11
    fmul    fr10,fr11               ! its root
    fmov    fr14,fr12
    fsub    fr11,fr12               ! near
    fmov.s  @r3,fr11
    fmul    fr12,fr11
    fmul    fr12,fr11
    fmov    fr14,fr13
    fadd    fr10,fr13
    fsrra   fr13
    fmul    fr13,fr13
    fmul    fr13,fr11               ! weight
    mov.b   @r4,r0
    tst     r0,r0
    mov     r13,r6
    add     #12,r6
    mov     r13,r7
    add     #36,r7
    bt      bv_groom
    add     #12,r6
    add     #4,r7
bv_groom:
    fmov.s  @r6,fr8
    fmov    fr4,fr9
    fmul    fr11,fr9
    fadd    fr9,fr8
    fmov.s  fr8,@r6
    add     #4,r6
    fmov.s  @r6,fr8
    fmov    fr5,fr9
    fmul    fr11,fr9
    fadd    fr9,fr8
    fmov.s  fr8,@r6
    add     #4,r6
    fmov.s  @r6,fr8
    fmov    fr6,fr9
    fmul    fr11,fr9
    fadd    fr9,fr8
    fmov.s  fr8,@r6
    fmov.s  @r7,fr8
    fadd    fr11,fr8
    fmov.s  fr8,@r7
bv_gnext:
    add     #4,r3
    add     #1,r4
    dt      r5
    JF      bv_gl
bv_gathered:

    ! the blend of the lights: scale, and the place the light is taken at
    LDL     fr0, 36                 ! room_total
    LDL     fr1, 40                 ! passing
    fmov    fr0,fr2
    fadd    fr1,fr2                 ! total
    fmov    fr14,fr3                ! scale
    fcmp/gt fr15,fr2
    bf      bv_scaled
    KL      fr4, 1
    fmul    fr2,fr4
    CLAMP01 fr4
    fmov    fr4,fr3
bv_scaled:
    STL     fr3, 56
    fcmp/gt fr15,fr0
    bf      bv_nolight
    fmov    fr0,fr5
    fsrra   fr5
    fmul    fr5,fr5                 ! 1 / room_total
    LDL     fr4, 12
    fmul    fr5,fr4
    STL     fr4, 44
    LDL     fr4, 16
    fmul    fr5,fr4
    STL     fr4, 48
    LDL     fr4, 20
    fmul    fr5,fr4
    STL     fr4, 52
    bra     bv_lit
    nop
bv_nolight:
    KL      fr6, 2
    LDL     fr4, 0
    mov     #28,r0
    fmov.s  @(r0,r8),fr5
    fmul    fr6,fr5
    fsub    fr5,fr4
    STL     fr4, 44
    LDL     fr4, 4
    mov     #32,r0
    fmov.s  @(r0,r8),fr5
    fmul    fr6,fr5
    fsub    fr5,fr4
    STL     fr4, 48
    LDL     fr4, 8
    mov     #36,r0
    fmov.s  @(r0,r8),fr5
    fmul    fr6,fr5
    fsub    fr5,fr4
    STL     fr4, 52
bv_lit:

    ! a passing light turns the direction
    LDL     fr1, 40
    fcmp/gt fr15,fr1
    JF      bv_tilted
    KL      fr4, 1
    fmul    fr1,fr4
    CLAMP01 fr4
    KL      fr5, 3
    fmul    fr5,fr4
    STL     fr4, 60                 ! tilt
    LDL     fr4, 44
    LDL     fr5, 0
    fsub    fr5,fr4
    STL     fr4, 64
    LDL     fr4, 48
    LDL     fr5, 4
    fsub    fr5,fr4
    STL     fr4, 68
    LDL     fr4, 52
    LDL     fr5, 8
    fsub    fr5,fr4
    STL     fr4, 72
    fsrra   fr1
    fmul    fr1,fr1                 ! 1 / passing
    LDL     fr4, 24
    fmul    fr1,fr4
    LDL     fr5, 0
    fsub    fr5,fr4
    STL     fr4, 76
    LDL     fr4, 28
    fmul    fr1,fr4
    LDL     fr5, 4
    fsub    fr5,fr4
    STL     fr4, 80
    LDL     fr4, 32
    fmul    fr1,fr4
    LDL     fr5, 8
    fsub    fr5,fr4
    STL     fr4, 84
    LV0     r13, 64
    LV4     r13, 64
    DOT
    fmov    fr7,fr8                 ! room_sq
    LV4     r13, 76
    fmov    fr4,fr9
    fmov    fr5,fr10
    fmov    fr6,fr11
    LV0     r13, 76
    DOT
    fmov    fr7,fr9                 ! passing_sq
    fcmp/gt fr14,fr8
    JF      bv_tilted
    fcmp/gt fr14,fr9
    JF      bv_tilted
    fsrra   fr8                     ! room_inv
    fsrra   fr9                     ! passing_inv
    LDL     fr10, 60                ! tilt
    fmov    fr14,fr11
    fsub    fr10,fr11               ! 1 - tilt
    LDL     fr0, 64
    fmul    fr8,fr0
    fmul    fr11,fr0
    LDL     fr1, 76
    fmul    fr9,fr1
    fmul    fr10,fr1
    fadd    fr1,fr0
    STL     fr0, 64
    LDL     fr0, 68
    fmul    fr8,fr0
    fmul    fr11,fr0
    LDL     fr1, 80
    fmul    fr9,fr1
    fmul    fr10,fr1
    fadd    fr1,fr0
    STL     fr0, 68
    LDL     fr0, 72
    fmul    fr8,fr0
    fmul    fr11,fr0
    LDL     fr1, 84
    fmul    fr9,fr1
    fmul    fr10,fr1
    fadd    fr1,fr0
    STL     fr0, 72
    LV0     r13, 64
    LV4     r13, 64
    DOT
    fmov    fr7,fr8                 ! mixed_sq
    KL      fr0, 4
    fcmp/gt fr0,fr8
    JF      bv_tilted
    fsrra   fr8
    KL      fr0, 2
    fmul    fr0,fr8                 ! far
    LDL     fr0, 0
    LDL     fr1, 64
    fmul    fr8,fr1
    fadd    fr1,fr0
    STL     fr0, 44
    LDL     fr0, 4
    LDL     fr1, 68
    fmul    fr8,fr1
    fadd    fr1,fr0
    STL     fr0, 48
    LDL     fr0, 8
    LDL     fr1, 72
    fmul    fr8,fr1
    fadd    fr1,fr0
    STL     fr0, 52
bv_tilted:

    ! how much of the relief the vertex takes
    mov     #8,r0
    fmov.s  @(r0,r11),fr0           ! z
    tst     r12,r12
    bt      bv_plain
    KL      fr1, 7
    KL      fr2, 8
    bra     bv_range
    nop
bv_plain:
    KL      fr1, 5
    KL      fr2, 6
bv_range:
    fsub    fr0,fr1
    fmul    fr2,fr1
    CLAMP01 fr1                     ! near
    LDL     fr2, 40
    KL      fr3, 1
    fmul    fr2,fr3
    CLAMP01 fr3                     ! lit_by_light
    fmov    fr1,fr4
    fmul    fr1,fr4
    KL      fr5, 20
    fmul    fr1,fr5
    KL      fr6, 19
    fsub    fr5,fr6
    fmul    fr6,fr4
    LDL     fr7, 56
    fmul    fr7,fr4
    fcmp/gt fr3,fr4
    bt      bv_fade
    fmov    fr3,fr4
bv_fade:
    mov.l   @(16,r8),r1
    fmov.s  fr4,@r1
    mov.l   @(20,r8),r2
    fmov.s  fr3,@r2
    tst     r12,r12
    bt      bv_depth
    KL      fr5, 10
    fmul    fr4,fr5                 ! strength
    mov     #24,r0
    mov.l   @(r0,r11),r1            ! argb
    mov     r1,r2
    shlr16  r2
    extu.b  r2,r2                   ! r
    mov     r1,r3
    shlr8   r3
    extu.b  r3,r3                   ! g
    extu.b  r1,r1                   ! b
    cmp/gt  r2,r3
    bf      bv_m1
    mov     r3,r2
bv_m1:
    cmp/gt  r2,r1
    bf      bv_m2
    mov     r1,r2
bv_m2:
    lds     r2,fpul
    float   fpul,fr6
    KL      fr7, 11
    fmul    fr7,fr6                 ! lit
    fmul    fr6,fr5
    bra     bv_strength
    nop
bv_depth:
    KL      fr5, 9
    fmul    fr4,fr5
bv_strength:
    STL     fr5, 32

    ! M_BumpAt: l, the light from the vertex
    LDL     fr0, 44
    LDL     fr1, 0
    fsub    fr1,fr0
    STL     fr0, 12
    LDL     fr0, 48
    LDL     fr1, 4
    fsub    fr1,fr0
    STL     fr0, 16
    LDL     fr0, 52
    LDL     fr1, 8
    fsub    fr1,fr0
    STL     fr0, 20
    LV0     r13, 12
    LV4     r13, 12
    DOT
    fmov    fr7,fr8                 ! l_sq
    LV4     r13, 0
    LV0     r13, 0
    DOT
    fmov    fr7,fr9                 ! c_sq
    STL     fr15, 88                ! hz
    STL     fr15, 92                ! detail
    tst     r12,r12
    JT      bv_noglow
    fcmp/gt fr15,fr8
    JF      bv_noglow
    fcmp/gt fr15,fr9
    JF      bv_noglow
    fmov    fr8,fr10
    fsrra   fr10                    ! li
    fmov    fr9,fr11
    fsrra   fr11                    ! ci
    ! facing = -(pos . n) * ci / sqrt(nl)
    LV0     r13, 0
    LV4     r10, 24
    DOT
    mov     #44,r0
    fmov.s  @(r0,r10),fr4
    fsrra   fr4
    fmul    fr11,fr7
    fmul    fr4,fr7
    fneg    fr7                     ! facing
    ! near, from the shine's detail range
    KL      fr0, 34
    LDL     fr1, 8
    fsub    fr1,fr0
    KL      fr1, 35
    fmul    fr1,fr0
    CLAMP01 fr0                     ! near
    KL      fr1, 19
    fmul    fr1,fr7
    CLAMP01 fr7
    fmul    fr7,fr0                 ! detail
    STL     fr0, 92
    ! h = l * li - pos * ci
    LDL     fr0, 12
    fmul    fr10,fr0
    LDL     fr1, 0
    fmul    fr11,fr1
    fsub    fr1,fr0
    STL     fr0, 76
    LDL     fr0, 16
    fmul    fr10,fr0
    LDL     fr1, 4
    fmul    fr11,fr1
    fsub    fr1,fr0
    STL     fr0, 80
    LDL     fr0, 20
    fmul    fr10,fr0
    LDL     fr1, 8
    fmul    fr11,fr1
    fsub    fr1,fr0
    STL     fr0, 84
    LV0     r13, 76
    LV4     r13, 76
    DOT
    fmov    fr7,fr8                 ! hh
    KL      fr0, 14
    fcmp/gt fr0,fr8
    JF      bv_noglow
    LV0     r13, 76
    LV4     r10, 24
    DOT                             ! h . n
    mov     #44,r0
    fmov.s  @(r0,r10),fr4
    fmul    fr8,fr4
    fsrra   fr4
    fmul    fr4,fr7
    fcmp/gt fr15,fr7
    bt      1f
    fmov    fr15,fr7
1:  STL     fr7, 88                 ! hz
bv_noglow:
    LV0     r13, 12
    LV4     r13, 12
    DOT
    fmov    fr7,fr8                 ! ll
    fcmp/gt fr15,fr8
    JF      bv_ret0
    LV4     r10, 0
    DOT
    fmov    fr7,fr9
    mov     #36,r0
    fmov.s  @(r0,r10),fr4
    fmul    fr8,fr4
    fsrra   fr4
    fmul    fr4,fr9                 ! lx
    LV4     r10, 12
    DOT
    fmov    fr7,fr10
    mov     #40,r0
    fmov.s  @(r0,r10),fr4
    fmul    fr8,fr4
    fsrra   fr4
    fmul    fr4,fr10                ! ly
    LV4     r10, 24
    DOT
    fmov    fr7,fr11
    mov     #44,r0
    fmov.s  @(r0,r10),fr4
    fmul    fr8,fr4
    fsrra   fr4
    fmul    fr4,fr11                ! lz
    fmov    fr11,fr0
    fmul    fr11,fr0
    fmov    fr14,fr1
    fsub    fr0,fr1
    fcmp/gt fr15,fr1
    bt      bv_off
    fmov    fr15,fr1
bv_off:
    KL      fr2, 33
    fadd    fr1,fr2
    fsrra   fr2
    fmul    fr2,fr1                 ! off
    KL      fr2, 15
    fmov    fr1,fr3
    fsub    fr2,fr3
    KL      fr2, 16
    fmul    fr2,fr3
    CLAMP01 fr3
    fmov    fr3,fr4
    fmul    fr3,fr4
    KL      fr5, 20
    fmul    fr3,fr5
    KL      fr6, 19
    fsub    fr5,fr6
    fmul    fr6,fr4
    STL     fr4, 24                 ! steady_smooth
    KL      fr2, 17
    fmov    fr1,fr3
    fsub    fr2,fr3
    KL      fr2, 18
    fmul    fr2,fr3
    CLAMP01 fr3
    fmov    fr3,fr4
    fmul    fr3,fr4
    KL      fr5, 20
    fmul    fr3,fr5
    KL      fr6, 19
    fsub    fr5,fr6
    fmul    fr6,fr4
    STL     fr4, 28                 ! shine_smooth
    KL      fr2, 21
    CLAMP   fr11, fr2, fr14         ! lz

    ! the direction Q, by M_Atan2
    fmov    fr9,fr0
    fabs    fr0                     ! |x|
    fmov    fr10,fr1
    fabs    fr1                     ! |y|
    fcmp/gt fr0,fr1
    bt      bv_ylarger
    fmov    fr1,fr2
    fmov    fr0,fr3
    bra     bv_lohi
    nop
bv_ylarger:
    fmov    fr0,fr2
    fmov    fr1,fr3
bv_lohi:
    fldi0   fr6
    fcmp/gt fr15,fr3
    bf      bv_atan_done
    fmov    fr3,fr5
    fsrra   fr5
    fmul    fr5,fr5
    fmov    fr2,fr4
    fmul    fr5,fr4                 ! a
    fmov    fr4,fr5
    fmul    fr4,fr5                 ! s
    KL      fr6, 22
    fmul    fr5,fr6
    KL      fr7, 23
    fadd    fr7,fr6
    fmul    fr5,fr6
    KL      fr7, 24
    fsub    fr7,fr6
    fmul    fr5,fr6
    fmul    fr4,fr6
    fadd    fr4,fr6                 ! r
    fcmp/gt fr0,fr1
    bf      bv_atan_a
    KL      fr7, 25
    fsub    fr6,fr7
    fmov    fr7,fr6
bv_atan_a:
    fcmp/gt fr9,fr15
    bf      bv_atan_b
    KL      fr7, 26
    fsub    fr6,fr7
    fmov    fr7,fr6
bv_atan_b:
    fcmp/gt fr10,fr15
    bf      bv_atan_done
    fneg    fr6
bv_atan_done:
    fcmp/gt fr6,fr15
    bf      bv_q
    KL      fr7, 27
    fadd    fr7,fr6
bv_q:
    KL      fr7, 28
    fmul    fr7,fr6
    ftrc    fr6,fpul
    sts     fpul,r7
    extu.b  r7,r7                   ! q8

    tst     r12,r12
    bt      bv_shade
    ! the shine: spot = depth * hz^8, K2 its share flat, K3 with the slopes
    LDL     fr0, 88
    fmul    fr0,fr0                 ! h2
    fmul    fr0,fr0                 ! h4
    LDL     fr8, 32
    fmul    fr0,fr8
    fmul    fr0,fr8                 ! spot
    KL      fr4, 36
    fmul    fr8,fr4
    PACKU   fr4
    shll16  r1
    mov     r1,r2
    LDL     fr4, 24                 ! steady_smooth
    fmul    fr8,fr4
    LDL     fr5, 92                 ! detail
    fmul    fr5,fr4
    PACKU   fr4
    shll8   r1
    or      r1,r2
    or      r7,r2
    bra     bv_exit
    mov     r2,r0

    ! M_BumpShade
bv_shade:
    LDL     fr0, 32                 ! depth
    LDL     fr1, 24                 ! steady_smooth
    KL      fr4, 31
    KL      fr5, 32
    fmov    fr0,fr6
    fmul    fr5,fr6
    fmul    fr6,fr4
    fmov    fr14,fr7
    fsub    fr4,fr7                 ! flat
    fmov    fr0,fr8
    fmul    fr11,fr8
    fcmp/gt fr8,fr7
    bt      bv_k2
    fmov    fr7,fr8
bv_k2:                              ! fr8: k2
    fmov    fr11,fr9
    fmul    fr9,fr9
    fmov    fr14,fr10
    fsub    fr9,fr10
    KL      fr9, 33
    fadd    fr10,fr9
    fsrra   fr9
    fmul    fr9,fr10
    fmov    fr0,fr9
    fmul    fr10,fr9
    fmul    fr1,fr9                 ! k3
    KL      fr10, 30                ! keep
    fmov    fr7,fr4
    fsub    fr8,fr4
    fmul    fr10,fr4
    KL      fr5, 29
    fadd    fr5,fr4
    PACKU   fr4
    shll16  r1
    shll8   r1
    mov     r1,r2
    fmov    fr8,fr4
    fmul    fr10,fr4
    PACKU   fr4
    shll16  r1
    or      r1,r2
    fmov    fr9,fr4
    fmul    fr10,fr4
    PACKU   fr4
    shll8   r1
    or      r1,r2
    or      r7,r2
    bra     bv_exit
    mov     r2,r0

bv_ret0:
    mov     #0,r0
bv_exit:
    add     #127,r15
    add     #1,r15
    fmov.s  @r15+,fr15
    fmov.s  @r15+,fr14
    fmov.s  @r15+,fr13
    fmov.s  @r15+,fr12
    mov.l   @r15+,r14
    mov.l   @r15+,r13
    mov.l   @r15+,r12
    mov.l   @r15+,r11
    mov.l   @r15+,r10
    mov.l   @r15+,r9
    rts
    mov.l   @r15+,r8


! void dc_bump_gather(const M_BUMP_LIGHTS *lights, const float pos[3],
!                     float out[8]);
!
! M_BumpGatherLights with its sums in registers: out gets the room lights'
! places weighted and their weights' sum (0-3), then the passing ones' (4-7).
! The same operations in the same order as the C; the dot product is fipr's.
! M_BUMP_LIGHTS: pos 0, radius 192, power 256, passing 320, count 336.
!
! r1 the light's place  r2 its radius  r3 its power  r5 its passing byte
! r7 lights left  fr0-2 pos  fr8-11 the room's sums  fr12-15 the passing's
    .global _dc_bump_gather
    .align 2
_dc_bump_gather:
    fmov.s  fr12,@-r15
    fmov.s  fr13,@-r15
    fmov.s  fr14,@-r15
    fmov.s  fr15,@-r15
    fmov.s  @r5+,fr0
    fmov.s  @r5+,fr1
    fmov.s  @r5,fr2
    fldi0   fr8
    fldi0   fr9
    fldi0   fr10
    fldi0   fr11
    fldi0   fr12
    fldi0   fr13
    fldi0   fr14
    fldi0   fr15
    mov     r4,r1
    mov     #48,r2
    shll2   r2
    add     r4,r2                   ! radius
    mov     #64,r3
    shll2   r3
    add     r4,r3                   ! power
    mov     #80,r5
    shll2   r5
    add     r4,r5                   ! passing
    mov     #84,r0
    shll2   r0
    mov.l   @(r0,r4),r7             ! count
bg_loop:
    tst     r7,r7
    bt      bg_done
    fmov.s  @r1+,fr4
    fmov.s  @r1+,fr5
    fmov.s  @r1+,fr6
    fsub    fr0,fr4
    fsub    fr1,fr5
    fsub    fr2,fr6
    fldi0   fr7
    fipr    fv4,fv4                 ! fr7 = dist_sq
    fmov.s  @r2+,fr3
    fmul    fr3,fr3                 ! radius squared
    fcmp/gt fr7,fr3
    bf      bg_next
    fsrra   fr3
    fmul    fr3,fr3
    fmul    fr7,fr3                 ! ratio_sq
    mov.l   .Lbg_tiny,r0
    lds     r0,fpul
    fsts    fpul,fr4
    fadd    fr3,fr4
    fsrra   fr4
    fmul    fr3,fr4                 ! its root
    fldi1   fr5
    fsub    fr4,fr5                 ! near
    fmov.s  @r3,fr6
    fmul    fr5,fr6
    fmul    fr5,fr6
    fldi1   fr4
    fadd    fr3,fr4
    fsrra   fr4
    fmul    fr4,fr4
    fmul    fr4,fr6                 ! weight
    mov     r1,r0
    add     #-12,r0
    fmov.s  @r0+,fr3
    fmov.s  @r0+,fr4
    fmov.s  @r0,fr5                 ! the light's place
    fmul    fr6,fr3
    fmul    fr6,fr4
    fmul    fr6,fr5
    mov.b   @r5,r0
    tst     r0,r0
    bf      bg_passing
    fadd    fr3,fr8
    fadd    fr4,fr9
    fadd    fr5,fr10
    bra     bg_next
    fadd    fr6,fr11
bg_passing:
    fadd    fr3,fr12
    fadd    fr4,fr13
    fadd    fr5,fr14
    fadd    fr6,fr15
bg_next:
    add     #4,r3
    add     #1,r5
    bra     bg_loop
    add     #-1,r7
bg_done:
    fmov.s  fr8,@r6
    add     #4,r6
    fmov.s  fr9,@r6
    add     #4,r6
    fmov.s  fr10,@r6
    add     #4,r6
    fmov.s  fr11,@r6
    add     #4,r6
    fmov.s  fr12,@r6
    add     #4,r6
    fmov.s  fr13,@r6
    add     #4,r6
    fmov.s  fr14,@r6
    add     #4,r6
    fmov.s  fr15,@r6
    fmov.s  @r15+,fr15
    fmov.s  @r15+,fr14
    fmov.s  @r15+,fr13
    rts
    fmov.s  @r15+,fr12
    .align 2
.Lbg_tiny:
    .long   0x2B8CBCCC              ! 1.0e-12

! uint32_t dc_bump_shine(M_BUMPRUN *run);
!
! The relief's shine at one vertex (M_BumpVertexC with glow, and the shine of
! M_BumpAt), written for the SH-4 rather than after the C: everything in
! registers, every dot product an fipr. A passing light (a flash, a flare)
! has its sums on the stack and turns the direction in a block of its own,
! as the C does. Writes *fade and *by_light; returns the offset colour.
!
! fv12 the vertex (then the half way vector h)  fv8 the light from it (l)
! fv4 the frame's n, t, bt in turn  fr15, fr11 the zero w of fv12, fv8
! r4 run  r5 passing flags  r6 frame  r7 vertex  r8 lights left
! r14 K + 128
! Stack: 0 depth (the strength)  4 scale, then detail  8 hz  12 the passing
! lights' places weighted (x y z)  24 their weights' sum

! M_Clamp(\f, 0, 1), with \t for the 1.
    .macro SH_CLAMP01 f, t
    fldi1   \t
    CLAMP   \f, fr15, \t
    .endm

! M_PackUnit(\f) into r1 (\f and \t are clobbered).
    .macro SH_PACK f, t
    SH_CLAMP01 \f, \t
    KL      \t, 12
    fmul    \t,\f
    KL      \t, 13
    fadd    \t,\f
    ftrc    \f,fpul
    sts     fpul,r1
    .endm

    .global _dc_bump_shine
    .align 2
_dc_bump_shine:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r14,@-r15
    fmov.s  fr12,@-r15
    fmov.s  fr13,@-r15
    fmov.s  fr14,@-r15
    fmov.s  fr15,@-r15
    add     #-28,r15
    mov.l   @(0,r4),r5
    mov.l   @(4,r4),r6
    mov.l   @(8,r4),r7
    mov.l   @(24,r4),r14
    add     #127,r14
    add     #1,r14
    mov     r7,r0
    fmov.s  @r0+,fr12
    fmov.s  @r0+,fr13
    fmov.s  @r0,fr14                ! the vertex
    fldi0   fr15
    mov     #12,r0
    fmov.s  fr15,@(r0,r15)
    mov     #16,r0
    fmov.s  fr15,@(r0,r15)
    mov     #20,r0
    fmov.s  fr15,@(r0,r15)
    mov     #24,r0
    fmov.s  fr15,@(r0,r15)

    ! M_BumpGatherLights: the room's sums in fr8-fr11, the passing ones'
    ! on the stack
    fldi0   fr8
    fldi0   fr9
    fldi0   fr10
    fldi0   fr11
    mov.l   @(40,r4),r8
    mov     r5,r1
    mov.l   @(56,r4),r2             ! radius squared
    mov.l   @(60,r4),r9             ! and its reciprocal
    mov.l   @(48,r4),r3
    mov.l   @(52,r4),r5
sh_gl:
    tst     r8,r8
    bt      sh_gathered
    fmov.s  @r1+,fr0
    fmov.s  @r1+,fr1
    fmov.s  @r1+,fr2                ! p
    fmov    fr0,fr4
    fsub    fr12,fr4
    fmov    fr1,fr5
    fsub    fr13,fr5
    fmov    fr2,fr6
    fsub    fr14,fr6
    fldi0   fr7
    fipr    fv4,fv4                 ! fr7 = dist_sq
    ! the loads while fipr works; then the two roots side by side
    fmov.s  @r2+,fr3                ! radius squared
    fmov.s  @r9+,fr4                ! its reciprocal
    fmov.s  @r3,fr6                 ! power
    fcmp/gt fr7,fr3
    bf      sh_gnext
    fmul    fr7,fr4                 ! ratio_sq
    KL      fr5, 33
    fldi1   fr3
    fadd    fr4,fr5
    fadd    fr4,fr3                 ! 1 + ratio_sq
    fsrra   fr5
    fsrra   fr3
    fmul    fr4,fr5                 ! the root of ratio_sq
    fmul    fr3,fr3                 ! 1 / (1 + ratio_sq)
    fldi1   fr7
    fsub    fr5,fr7                 ! near
    fmul    fr7,fr6
    fmul    fr7,fr6
    fmul    fr3,fr6                 ! weight
    fmul    fr6,fr0
    fmul    fr6,fr1
    fmul    fr6,fr2
    mov.b   @r5,r0
    tst     r0,r0
    bf      sh_gpass
    fadd    fr0,fr8
    fadd    fr1,fr9
    fadd    fr2,fr10
    bra     sh_gnext
    fadd    fr6,fr11
sh_gpass:
    mov     r15,r0
    add     #12,r0
    fmov.s  @r0,fr4
    fadd    fr0,fr4
    fmov.s  fr4,@r0
    add     #4,r0
    fmov.s  @r0,fr4
    fadd    fr1,fr4
    fmov.s  fr4,@r0
    add     #4,r0
    fmov.s  @r0,fr4
    fadd    fr2,fr4
    fmov.s  fr4,@r0
    add     #4,r0
    fmov.s  @r0,fr4
    fadd    fr6,fr4
    fmov.s  fr4,@r0
sh_gnext:
    add     #4,r3
    add     #1,r5
    bra     sh_gl
    add     #-1,r8
sh_gathered:

    ! scale (fr3), and the place the light is taken at (fr8-fr10)
    mov     #24,r0
    fmov.s  @(r0,r15),fr0
    fadd    fr11,fr0                ! total = room_total + passing
    fldi1   fr3
    fcmp/gt fr15,fr0
    bf      1f
    KL      fr1, 1
    fmul    fr1,fr0                 ! total * 4
    fcmp/gt fr3,fr0
    bf      2f
    fmov    fr3,fr0
2:  fmov    fr0,fr3                 ! scale
1:  mov     #4,r0
    fmov.s  fr3,@(r0,r15)           ! kept there until the fade
    fcmp/gt fr15,fr11
    bf      sh_nolight
    fmov    fr11,fr1
    fsrra   fr1
    fmul    fr1,fr1                 ! 1 / room_total
    fmul    fr1,fr8
    fmul    fr1,fr9
    fmul    fr1,fr10
    bra     sh_lit
    nop
sh_nolight:
    KL      fr0, 2
    mov     #28,r0
    fmov.s  @(r0,r4),fr1
    fmul    fr0,fr1
    fmov    fr12,fr8
    fsub    fr1,fr8
    mov     #32,r0
    fmov.s  @(r0,r4),fr1
    fmul    fr0,fr1
    fmov    fr13,fr9
    fsub    fr1,fr9
    mov     #36,r0
    fmov.s  @(r0,r4),fr1
    fmul    fr0,fr1
    fmov    fr14,fr10
    fsub    fr1,fr10
sh_lit:
    mov     #24,r0
    fmov.s  @(r0,r15),fr11          ! passing
    fcmp/gt fr15,fr11
    JF      sh_tilted
    ! a passing light turns the direction: room_dir (fv0), passing_dir (fv4)
    fmov    fr8,fr0
    fsub    fr12,fr0
    fmov    fr9,fr1
    fsub    fr13,fr1
    fmov    fr10,fr2
    fsub    fr14,fr2
    fldi0   fr3
    fmov    fr11,fr7
    fsrra   fr7
    fmul    fr7,fr7                 ! 1 / passing
    mov     #12,r0
    fmov.s  @(r0,r15),fr4
    fmul    fr7,fr4
    fsub    fr12,fr4
    mov     #16,r0
    fmov.s  @(r0,r15),fr5
    fmul    fr7,fr5
    fsub    fr13,fr5
    mov     #20,r0
    fmov.s  @(r0,r15),fr6
    fmul    fr7,fr6
    fsub    fr14,fr6
    fldi0   fr7
    fipr    fv0,fv0                 ! fr3 = room_sq
    fipr    fv4,fv4                 ! fr7 = passing_sq
    fldi1   fr11
    fcmp/gt fr11,fr3
    JF      sh_tilted
    fcmp/gt fr11,fr7
    JF      sh_tilted
    fsrra   fr3                     ! room_inv
    fsrra   fr7                     ! passing_inv
    fmul    fr3,fr0
    fmul    fr3,fr1
    fmul    fr3,fr2
    fmul    fr7,fr4
    fmul    fr7,fr5
    fmul    fr7,fr6
    mov     #24,r0
    fmov.s  @(r0,r15),fr11
    KL      fr7, 1
    fmul    fr7,fr11
    SH_CLAMP01 fr11, fr7
    KL      fr7, 3
    fmul    fr7,fr11                ! tilt
    fldi1   fr3
    fsub    fr11,fr3                ! 1 - tilt
    fmul    fr3,fr0
    fmul    fr11,fr4
    fadd    fr4,fr0
    fmul    fr3,fr1
    fmul    fr11,fr5
    fadd    fr5,fr1
    fmul    fr3,fr2
    fmul    fr11,fr6
    fadd    fr6,fr2                 ! mixed
    fldi0   fr3
    fipr    fv0,fv0                 ! fr3 = mixed_sq
    KL      fr7, 4
    fcmp/gt fr7,fr3
    JF      sh_tilted
    fsrra   fr3
    KL      fr7, 2
    fmul    fr7,fr3                 ! far
    fmul    fr3,fr0
    fmov    fr12,fr8
    fadd    fr0,fr8
    fmul    fr3,fr1
    fmov    fr13,fr9
    fadd    fr1,fr9
    fmul    fr3,fr2
    fmov    fr14,fr10
    fadd    fr2,fr10
sh_tilted:
    fsub    fr12,fr8
    fsub    fr13,fr9
    fsub    fr14,fr10
    fldi0   fr11                    ! l

    ! fade (no passing light: by_light 0) and the strength
    KL      fr0, 7
    fsub    fr14,fr0
    KL      fr1, 8
    fmul    fr1,fr0
    SH_CLAMP01 fr0, fr1             ! near
    fmov    fr0,fr1
    fmul    fr0,fr1
    KL      fr2, 20
    fmul    fr0,fr2
    KL      fr4, 19
    fsub    fr2,fr4
    fmul    fr4,fr1
    mov     #4,r0
    fmov.s  @(r0,r15),fr3           ! scale
    fmul    fr3,fr1
    mov     #24,r0
    fmov.s  @(r0,r15),fr2
    KL      fr4, 1
    fmul    fr4,fr2
    SH_CLAMP01 fr2, fr4             ! lit_by_light
    fcmp/gt fr2,fr1
    bt      1f
    fmov    fr2,fr1
1:  mov.l   @(16,r4),r0
    fmov.s  fr1,@r0                 ! fade
    mov.l   @(20,r4),r0
    fmov.s  fr2,@r0                 ! by_light
    KL      fr2, 10
    fmul    fr1,fr2                 ! GLOW * fade
    mov     #24,r0
    mov.l   @(r0,r7),r1             ! argb
    mov     r1,r2
    shlr16  r2
    extu.b  r2,r2
    mov     r1,r3
    shlr8   r3
    extu.b  r3,r3
    extu.b  r1,r1
    cmp/gt  r2,r3
    bf      1f
    mov     r3,r2
1:  cmp/gt  r2,r1
    bf      1f
    mov     r1,r2
1:  lds     r2,fpul
    float   fpul,fr4
    KL      fr5, 11
    fmul    fr5,fr4                 ! lit
    fmul    fr4,fr2                 ! depth
    fmov.s  fr2,@r15

    ! the shine of M_BumpAt: hz and detail
    fipr    fv8,fv8
    fmov    fr11,fr0                ! ll
    fldi0   fr11
    fcmp/gt fr15,fr0
    JF      sh_zero
    fipr    fv12,fv12
    fmov    fr15,fr1                ! c_sq
    fldi0   fr15
    mov     #4,r0
    fmov.s  fr15,@(r0,r15)          ! detail 0
    mov     #8,r0
    fmov.s  fr15,@(r0,r15)          ! hz 0
    fcmp/gt fr15,fr1
    JF      sh_noglow
    fmov    fr0,fr2
    fsrra   fr2                     ! li
    fmov    fr1,fr3
    fsrra   fr3                     ! ci
    LV4     r6, 24                  ! n
    fipr    fv12,fv4                ! fr7 = pos . n
    mov     #44,r0
    fmov.s  @(r0,r6),fr1
    fsrra   fr1                     ! 1 / sqrt(nl)
    fmul    fr3,fr7
    fmul    fr1,fr7
    fneg    fr7                     ! facing
    KL      fr1, 34
    fsub    fr14,fr1
    KL      fr11, 35
    fmul    fr11,fr1
    SH_CLAMP01 fr1, fr11            ! near
    KL      fr11, 19
    fmul    fr11,fr7
    SH_CLAMP01 fr7, fr11
    fmul    fr7,fr1                 ! detail
    fldi0   fr11
    mov     #4,r0
    fmov.s  fr1,@(r0,r15)
    fmul    fr3,fr12                ! h = l * li - pos * ci
    fmov    fr8,fr1
    fmul    fr2,fr1
    fsub    fr12,fr1
    fmov    fr1,fr12
    fmul    fr3,fr13
    fmov    fr9,fr1
    fmul    fr2,fr1
    fsub    fr13,fr1
    fmov    fr1,fr13
    fmul    fr3,fr14
    fmov    fr10,fr1
    fmul    fr2,fr1
    fsub    fr14,fr1
    fmov    fr1,fr14
    fipr    fv12,fv12
    fmov    fr15,fr1                ! hh
    fldi0   fr15
    KL      fr7, 14
    fcmp/gt fr7,fr1
    JF      sh_noglow
    fldi0   fr7
    fipr    fv12,fv4                ! fr7 = h . n
    mov     #44,r0
    fmov.s  @(r0,r6),fr3
    fmul    fr1,fr3                 ! nl * hh
    fsrra   fr3
    fmul    fr3,fr7
    fcmp/gt fr15,fr7
    bt      1f
    fmov    fr15,fr7
1:  mov     #8,r0
    fmov.s  fr7,@(r0,r15)           ! hz
sh_noglow:

    ! the light against the face: lz (fr3), lx (fr2), ly (fr1)
    LV4     r6, 24
    fipr    fv8,fv4
    mov     #44,r0
    fmov.s  @(r0,r6),fr1
    fmul    fr0,fr1
    fsrra   fr1
    fmul    fr1,fr7
    fmov    fr7,fr3                 ! lz
    LV4     r6, 0
    fipr    fv8,fv4
    mov     #36,r0
    fmov.s  @(r0,r6),fr1
    fmul    fr0,fr1
    fsrra   fr1
    fmul    fr1,fr7
    fmov    fr7,fr2                 ! lx
    LV4     r6, 12
    fipr    fv8,fv4
    mov     #40,r0
    fmov.s  @(r0,r6),fr1
    fmul    fr0,fr1
    fsrra   fr1
    fmul    fr7,fr1                 ! ly
    ! off, steady_smooth (fr12)
    fmov    fr3,fr4
    fmul    fr3,fr4
    fldi1   fr5
    fsub    fr4,fr5
    fcmp/gt fr15,fr5
    bt      1f
    fmov    fr15,fr5
1:  KL      fr4, 33
    fadd    fr5,fr4
    fsrra   fr4
    fmul    fr5,fr4                 ! off
    KL      fr5, 15
    fmov    fr4,fr6
    fsub    fr5,fr6
    KL      fr5, 16
    fmul    fr5,fr6
    SH_CLAMP01 fr6, fr5             ! steady
    fmov    fr6,fr12
    fmul    fr6,fr12
    KL      fr5, 20
    fmul    fr6,fr5
    KL      fr4, 19
    fsub    fr5,fr4
    fmul    fr4,fr12                ! steady_smooth
    ! Q = M_Atan2(ly, lx) in fr8
    fmov    fr2,fr4
    fabs    fr4                     ! ax
    fmov    fr1,fr5
    fabs    fr5                     ! ay
    fcmp/gt fr4,fr5
    bt      1f
    fmov    fr5,fr6                 ! lo
    bra     2f
    fmov    fr4,fr7                 ! hi
1:  fmov    fr4,fr6
    fmov    fr5,fr7
2:  fldi0   fr8
    fcmp/gt fr15,fr7
    bf      sh_atan_done
    fmov    fr7,fr9
    fsrra   fr9
    fmul    fr9,fr9
    fmov    fr6,fr10
    fmul    fr9,fr10                ! a
    fmov    fr10,fr11
    fmul    fr10,fr11               ! s
    KL      fr8, 22
    fmul    fr11,fr8
    KL      fr9, 23
    fadd    fr9,fr8
    fmul    fr11,fr8
    KL      fr9, 24
    fsub    fr9,fr8
    fmul    fr11,fr8
    fmul    fr10,fr8
    fadd    fr10,fr8                ! r
    fcmp/gt fr4,fr5
    bf      1f
    KL      fr9, 25
    fsub    fr8,fr9
    fmov    fr9,fr8
1:  fcmp/gt fr2,fr15
    bf      1f
    KL      fr9, 26
    fsub    fr8,fr9
    fmov    fr9,fr8
1:  fcmp/gt fr1,fr15
    bf      sh_atan_done
    fneg    fr8
sh_atan_done:
    fcmp/gt fr8,fr15
    bf      1f
    KL      fr9, 27
    fadd    fr9,fr8
1:  KL      fr9, 28
    fmul    fr9,fr8
    ftrc    fr8,fpul
    sts     fpul,r3
    extu.b  r3,r3                   ! q8
    ! the spot: K2 its share flat, K3 with the slopes
    mov     #8,r0
    fmov.s  @(r0,r15),fr0
    fmul    fr0,fr0
    fmul    fr0,fr0                 ! hz^4
    fmov.s  @r15,fr4
    fmul    fr0,fr4
    fmul    fr0,fr4                 ! spot
    KL      fr5, 36
    fmul    fr4,fr5
    SH_PACK fr5, fr6
    shll16  r1
    mov     r1,r2
    fmov    fr4,fr5
    fmul    fr12,fr5
    mov     #4,r0
    fmov.s  @(r0,r15),fr6
    fmul    fr6,fr5
    SH_PACK fr5, fr6
    shll8   r1
    or      r1,r2
    or      r3,r2
    bra     sh_exit
    mov     r2,r0
sh_zero:
    mov     #0,r0
sh_exit:
    add     #28,r15
    fmov.s  @r15+,fr15
    fmov.s  @r15+,fr14
    fmov.s  @r15+,fr13
    fmov.s  @r15+,fr12
    mov.l   @r15+,r14
    mov.l   @r15+,r9
    rts
    mov.l   @r15+,r8
