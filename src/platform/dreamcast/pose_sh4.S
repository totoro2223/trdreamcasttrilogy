! Hand-written SH-4 for the pose blend of matrix.c (M_InterpolateFloat), the
! blend of each mesh's matrix between two keyframes.
!
!   int dc_interp_float(const MATRIX *m1, const MATRIX *m2, MATRIX *out,
!                       const float *t);
!
! The C's steps: each rotation part to floats with its scale taken out, to a
! quaternion, the slerp (the straight blend, normalised, for a narrow turn;
! DC_Shz_Acos and DC_Shz_Sin for a wider one, as the C; GCC passes a float
! argument in fr5), back to a matrix,
! made orthonormal again, in 14-bit fixed point; each axis of the position
! blended. t is frac / rate, held to 0..1. Returns 0 with nothing written for
! what the C refuses: an entry past 2^22, a step of the position past 2^30,
! a scale of 0. out may be m1: it is written last. The dot products go
! through fipr and ftrv, so a result may differ from the C's by one in its
! last bit (DC_EMIT_VERIFY counts them).
!
! MATRIX: twelve int64, row by row (_00 _01 _02 _03 at 0 8 16 24, the next
! row 32 on).
!
! The frame (r14, 8-aligned; r13 = r14 + 128):
!   r14+0  e1 (9 floats)   r14+36 e2   r14+72 q1 (x y z w)   r14+88 q2
!   r14+104 the blended position (3 int64)
!   r13+12 omega   r13+16 1 / sin(omega)   r13+20 scale0
!   r13+24 e, the blended rotation (9 floats)   r13+64 a 4x4 block for XMTRX

    .text
    .align 2

! Loads e[0..2] of a row of int64 into \fr, or fails.
    .macro ENT off, fr
    mov.l   @(\off,r4),r0
    mov.l   @(\off+4,r4),r1
    mov     r0,r2
    shll    r2
    subc    r2,r2                   ! the sign of the low word
    cmp/eq  r1,r2
    bf      il_no
    cmp/ge  r6,r0                   ! past 2^22
    bt      il_no
    cmp/ge  r0,r7
    bt      il_no
    lds     r0,fpul
    float   fpul,\fr
    .endm

! fv0, normalised (M_QuatFNormalize): fsrra of its squared length.
    .macro NORM
    fmov    fr0,fr12
    fmov    fr1,fr13
    fmov    fr2,fr14
    fmov    fr3,fr15
    fipr    fv0,fv12                ! fr15 = the squared length
    fldi0   fr12
    fcmp/gt fr12,fr15
    bf      91f
    fsrra   fr15
    fmul    fr15,fr0
    fmul    fr15,fr1
    fmul    fr15,fr2
    fmul    fr15,fr3
    bra     92f
    nop
91: fldi0   fr0
    fldi0   fr1
    fldi0   fr2
    fldi1   fr3
92:
    .endm

! \fr rounded half away from zero (M_RoundF) into r3; fr15 holds 0.5.
    .macro RND fr
    fldi0   fr12
    fcmp/gt \fr,fr12                ! below zero
    bt      93f
    fadd    fr15,\fr
    bra     94f
    nop
93: fsub    fr15,\fr
94: ftrc    \fr,fpul
    sts     fpul,r3
    .endm

! One axis of the position (M_LerpTranslation): \off in the matrices, \slot
! in the frame. Fails for a step past 2^30.
    .macro TRANS off, slot
    mov     r8,r1
    add     #\off,r1
    mov.l   @r1,r0                  ! from
    mov.l   @(4,r1),r2
    mov     r9,r1
    add     #\off,r1
    mov.l   @r1,r3                  ! to
    mov.l   @(4,r1),r5
    clrt
    subc    r0,r3
    subc    r2,r5                   ! the step
    mov     r3,r1
    shll    r1
    subc    r1,r1
    cmp/eq  r1,r5
    bf      if_fail
    mov.l   .Lif_step,r1
    cmp/ge  r1,r3
    bt      if_fail
    neg     r1,r1
    cmp/ge  r3,r1
    bt      if_fail
    lds     r3,fpul
    float   fpul,fr0
    fmov.s  @r11,fr1
    fmul    fr1,fr0
    RND     fr0
    mov     r3,r1
    shll    r1
    subc    r1,r1
    clrt
    addc    r3,r0
    addc    r1,r2
    mov     r14,r1
    add     #\slot,r1
    mov.l   r0,@r1
    mov.l   r2,@(4,r1)
    .endm

! One stored float, and on.
    .macro STO fr
    fmov.s  \fr,@r1
    add     #4,r1
    .endm

! The 16 floats at \reg (8-aligned) into XMTRX. Clobbers r1.
    .macro LOADX reg
    mov     \reg,r1
    fschg
    frchg
    fmov.d  @r1+,dr0
    fmov.d  @r1+,dr2
    fmov.d  @r1+,dr4
    fmov.d  @r1+,dr6
    fmov.d  @r1+,dr8
    fmov.d  @r1+,dr10
    fmov.d  @r1+,dr12
    fmov.d  @r1+,dr14
    frchg
    fschg
    .endm

    .global _dc_interp_float
    .align 2
_dc_interp_float:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r13,@-r15
    mov.l   r14,@-r15
    sts.l   pr,@-r15
    fmov.s  fr12,@-r15
    fmov.s  fr13,@-r15
    fmov.s  fr14,@-r15
    fmov.s  fr15,@-r15
    mov.l   .Lif_size,r0
    sub     r0,r15
    mov     r15,r14
    add     #7,r14
    mov     #-8,r0
    and     r0,r14                  ! the frame, 8-aligned
    mov     r14,r13
    add     #127,r13
    add     #1,r13
    mov     r4,r8
    mov     r5,r9
    mov     r6,r10
    mov     r7,r11

    mov     r8,r4
    bsr     if_load
    mov     r14,r5
    bf      if_fail
    mov     r9,r4
    mov     r14,r5
    bsr     if_load
    add     #36,r5
    bf      if_fail
    mov.l   .Lif_half,r0
    lds     r0,fpul
    fsts    fpul,fr15               ! 0.5, for the rounding
    TRANS   24, 104
    TRANS   56, 112
    TRANS   88, 120

    mov     r14,r4
    mov     r14,r5
    bsr     if_quat
    add     #72,r5
    mov     r14,r4
    add     #36,r4
    mov     r14,r5
    bsr     if_quat
    add     #88,r5
    bra     if_slerp
    nop

if_fail:
    bra     if_ret
    mov     #0,r0

    .align 2
.Lif_size:
    .long   264
.Lif_half:
    .long   0x3F000000
.Lif_step:
    .long   1073741823

! The slerp (M_QuatFSlerp): q1 in fv0, q2 in fv4.
if_slerp:
    mov     r14,r1
    add     #72,r1
    fmov.s  @r1+,fr0
    fmov.s  @r1+,fr1
    fmov.s  @r1+,fr2
    fmov.s  @r1+,fr3
    fmov.s  @r1+,fr4
    fmov.s  @r1+,fr5
    fmov.s  @r1+,fr6
    fmov.s  @r1+,fr7
    fmov    fr4,fr8
    fmov    fr5,fr9
    fmov    fr6,fr10
    fmov    fr7,fr11
    fipr    fv0,fv8                 ! fr11 = cos omega
    fldi0   fr12
    fcmp/gt fr11,fr12
    bf      1f
    fneg    fr11                    ! the shorter way round
    fneg    fr4
    fneg    fr5
    fneg    fr6
    fneg    fr7
1:  fldi1   fr12
    fcmp/gt fr12,fr11
    bf      1f
    fmov    fr12,fr11               ! not over 1
1:  fmov.s  @r11,fr10               ! t
    fmov    fr12,fr8
    fsub    fr10,fr8                ! scale0 = 1 - t
    fmov    fr10,fr9                ! scale1 = t
    mov.l   .Lif_0999,r1
    lds     r1,fpul
    fsts    fpul,fr13
    fcmp/gt fr11,fr13
    bt      if_wide
if_blend:
    fmul    fr8,fr0
    fmov    fr4,fr12
    fmul    fr9,fr12
    fadd    fr12,fr0
    fmul    fr8,fr1
    fmov    fr5,fr12
    fmul    fr9,fr12
    fadd    fr12,fr1
    fmul    fr8,fr2
    fmov    fr6,fr12
    fmul    fr9,fr12
    fadd    fr12,fr2
    fmul    fr8,fr3
    fmov    fr7,fr12
    fmul    fr9,fr12
    fadd    fr12,fr3
    NORM
    bra     if_tomat
    nop

! A wider turn: the arc's sines, through the C's own functions.
if_wide:
    mov     r14,r1
    add     #72,r1
    fmov.s  fr0,@r1
    add     #4,r1
    fmov.s  fr1,@r1
    add     #4,r1
    fmov.s  fr2,@r1
    add     #4,r1
    fmov.s  fr3,@r1
    add     #4,r1
    fmov.s  fr4,@r1
    add     #4,r1
    fmov.s  fr5,@r1
    add     #4,r1
    fmov.s  fr6,@r1
    add     #4,r1
    fmov.s  fr7,@r1
    fmov    fr11,fr4
    mov.l   .Lif_acos,r1
    jsr     @r1
    fmov    fr11,fr5                ! GCC's first float argument: fr5
    mov     r13,r1
    add     #12,r1
    fmov.s  fr0,@r1                 ! omega
    fmov    fr0,fr4
    mov.l   .Lif_sin,r1
    jsr     @r1
    fmov    fr0,fr5
    fldi1   fr1
    fdiv    fr0,fr1
    mov     r13,r1
    add     #16,r1
    fmov.s  fr1,@r1                 ! 1 / sin(omega)
    fldi1   fr4
    fmov.s  @r11,fr5
    fsub    fr5,fr4
    mov     r13,r1
    add     #12,r1
    fmov.s  @r1,fr5
    fmul    fr5,fr4                 ! (1 - t) * omega
    mov.l   .Lif_sin,r1
    jsr     @r1
    fmov    fr4,fr5
    mov     r13,r1
    add     #16,r1
    fmov.s  @r1,fr1
    fmul    fr1,fr0
    mov     r13,r1
    add     #20,r1
    fmov.s  fr0,@r1                 ! scale0
    fmov.s  @r11,fr4
    mov     r13,r1
    add     #12,r1
    fmov.s  @r1,fr5
    fmul    fr5,fr4                 ! t * omega
    mov.l   .Lif_sin,r1
    jsr     @r1
    fmov    fr4,fr5
    mov     r13,r1
    add     #16,r1
    fmov.s  @r1,fr1
    fmul    fr1,fr0
    fmov    fr0,fr9                 ! scale1
    mov     r13,r1
    add     #20,r1
    fmov.s  @r1,fr8
    mov     r14,r1
    add     #72,r1
    fmov.s  @r1+,fr0
    fmov.s  @r1+,fr1
    fmov.s  @r1+,fr2
    fmov.s  @r1+,fr3
    fmov.s  @r1+,fr4
    fmov.s  @r1+,fr5
    fmov.s  @r1+,fr6
    bra     if_blend
    fmov.s  @r1+,fr7

    .align 2
.Lif_0999:
    .long   0x3F7FBE77
.Lif_acos:
    .long   _DC_Shz_Acos
.Lif_sin:
    .long   _DC_Shz_Sin

! The quaternion (x y z w in fv0) back to a matrix, at r13+24.
if_tomat:
    fmov    fr0,fr4
    fmul    fr0,fr4                 ! xx
    fmov    fr1,fr5
    fmul    fr1,fr5                 ! yy
    fmov    fr2,fr6
    fmul    fr2,fr6                 ! zz
    fmov    fr0,fr7
    fmul    fr1,fr7                 ! xy
    fmov    fr0,fr8
    fmul    fr2,fr8                 ! xz
    fmov    fr1,fr9
    fmul    fr2,fr9                 ! yz
    fmov    fr3,fr10
    fmul    fr0,fr10                ! wx
    fmov    fr3,fr11
    fmul    fr1,fr11                ! wy
    fmov    fr3,fr12
    fmul    fr2,fr12                ! wz
    fldi1   fr14
    fmov    fr14,fr13
    fadd    fr13,fr13               ! 2
    mov     r13,r1
    add     #24,r1
    fmov    fr5,fr15                ! 1 - 2 (yy + zz)
    fadd    fr6,fr15
    fmul    fr13,fr15
    fmov    fr14,fr0
    fsub    fr15,fr0
    STO     fr0
    fmov    fr7,fr15                ! 2 (xy - wz)
    fsub    fr12,fr15
    fmul    fr13,fr15
    STO     fr15
    fmov    fr8,fr15                ! 2 (xz + wy)
    fadd    fr11,fr15
    fmul    fr13,fr15
    STO     fr15
    fmov    fr7,fr15                ! 2 (xy + wz)
    fadd    fr12,fr15
    fmul    fr13,fr15
    STO     fr15
    fmov    fr4,fr15                ! 1 - 2 (xx + zz)
    fadd    fr6,fr15
    fmul    fr13,fr15
    fmov    fr14,fr0
    fsub    fr15,fr0
    STO     fr0
    fmov    fr9,fr15                ! 2 (yz - wx)
    fsub    fr10,fr15
    fmul    fr13,fr15
    STO     fr15
    fmov    fr8,fr15                ! 2 (xz - wy)
    fsub    fr11,fr15
    fmul    fr13,fr15
    STO     fr15
    fmov    fr9,fr15                ! 2 (yz + wx)
    fadd    fr10,fr15
    fmul    fr13,fr15
    STO     fr15
    fmov    fr4,fr15                ! 1 - 2 (xx + yy)
    fadd    fr5,fr15
    fmul    fr13,fr15
    fmov    fr14,fr0
    fsub    fr15,fr0
    STO     fr0

! Orthonormal again (M_Float3x3Ortho): with R the rows above, S = R^T R and
! the result 0.5 (3 R - R S). XMTRX first holds R^T (R's rows as its
! columns), for S's columns; then S, symmetric, for each row of R S.
    mov     r13,r2
    add     #64,r2                  ! the block
    mov     r13,r4
    add     #24,r4                  ! e
    mov     r4,r1
    mov     #0,r0
    mov     #3,r3
1:  mov.l   @r1+,r5
    mov.l   r5,@r2
    mov.l   @r1+,r5
    mov.l   r5,@(4,r2)
    mov.l   @r1+,r5
    mov.l   r5,@(8,r2)
    mov.l   r0,@(12,r2)
    dt      r3
    bf/s    1b
    add     #16,r2
    mov.l   r0,@r2
    mov.l   r0,@(4,r2)
    mov.l   r0,@(8,r2)
    mov.l   r0,@(12,r2)
    add     #-48,r2
    LOADX   r2
    mov     r2,r5
    mov     #0,r3
1:  mov     r3,r0
    fmov.s  @(r0,r4),fr0            ! column r3 / 4 of R
    add     #12,r0
    fmov.s  @(r0,r4),fr1
    add     #12,r0
    fmov.s  @(r0,r4),fr2
    fldi0   fr3
    ftrv    xmtrx,fv0               ! a column of S
    fmov.s  fr0,@r5
    add     #4,r5
    fmov.s  fr1,@r5
    add     #4,r5
    fmov.s  fr2,@r5
    add     #4,r5
    fmov.s  fr3,@r5
    add     #4,r5
    add     #4,r3
    mov     r3,r0
    cmp/eq  #12,r0
    bf      1b
    LOADX   r2
    mov.l   .Lif_three,r0
    lds     r0,fpul
    fsts    fpul,fr14               ! 3
    mov.l   .Lif_8192,r0
    lds     r0,fpul
    fsts    fpul,fr13               ! 0.5 * 16384
    mov.l   .Lif_half2,r0
    lds     r0,fpul
    fsts    fpul,fr15               ! 0.5, for the rounding
    mov     r10,r6
    mov     #3,r7
1:  fmov.s  @r4+,fr4
    fmov.s  @r4+,fr5
    fmov.s  @r4+,fr6
    fmov    fr4,fr0
    fmov    fr5,fr1
    fmov    fr6,fr2
    fldi0   fr3
    ftrv    xmtrx,fv0               ! the row of R S
    fmov    fr14,fr8
    fmul    fr4,fr8
    fsub    fr0,fr8
    fmul    fr13,fr8
    RND     fr8
    mov     r3,r0
    shll    r0
    subc    r0,r0
    mov.l   r3,@r6
    mov.l   r0,@(4,r6)
    fmov    fr14,fr8
    fmul    fr5,fr8
    fsub    fr1,fr8
    fmul    fr13,fr8
    RND     fr8
    mov     r3,r0
    shll    r0
    subc    r0,r0
    mov.l   r3,@(8,r6)
    mov.l   r0,@(12,r6)
    fmov    fr14,fr8
    fmul    fr6,fr8
    fsub    fr2,fr8
    fmul    fr13,fr8
    RND     fr8
    mov     r3,r0
    shll    r0
    subc    r0,r0
    mov.l   r3,@(16,r6)
    mov.l   r0,@(20,r6)
    dt      r7
    bf/s    1b
    add     #32,r6
    mov     r14,r1                  ! the position, last
    add     #104,r1
    mov.l   @r1+,r0
    mov.l   r0,@(24,r10)
    mov.l   @r1+,r0
    mov.l   r0,@(28,r10)
    mov.l   @r1+,r0
    mov.l   r0,@(56,r10)
    mov.l   @r1+,r0
    mov.l   r0,@(60,r10)
    mov     r10,r2
    add     #88,r2
    mov.l   @r1+,r0
    mov.l   r0,@r2
    mov.l   @r1+,r0
    mov.l   r0,@(4,r2)
    mov     #1,r0

if_ret:
    mov.l   .Lif_size2,r1
    add     r1,r15
    fmov.s  @r15+,fr15
    fmov.s  @r15+,fr14
    fmov.s  @r15+,fr13
    fmov.s  @r15+,fr12
    lds.l   @r15+,pr
    mov.l   @r15+,r14
    mov.l   @r15+,r13
    mov.l   @r15+,r11
    mov.l   @r15+,r10
    mov.l   @r15+,r9
    rts
    mov.l   @r15+,r8

    .align 2
.Lif_size2:
    .long   264
.Lif_three:
    .long   0x40400000
.Lif_8192:
    .long   0x46000000
.Lif_half2:
    .long   0x3F000000

! A matrix's rotation part (r4) to floats at r5, its scale taken out
! (M_Float3x3FromMatrix). T clear when it cannot be taken.
if_load:
    mov.l   .Lif_lim,r6
    neg     r6,r7
    fldi0   fr8
    mov     #3,r3
il_row:
    ENT     0, fr0
    ENT     8, fr1
    ENT     16, fr2
    fldi0   fr3
    fipr    fv0,fv0                 ! fr3 = the row's squared length
    fsqrt   fr3
    fadd    fr3,fr8
    fmov.s  fr0,@r5
    add     #4,r5
    fmov.s  fr1,@r5
    add     #4,r5
    fmov.s  fr2,@r5
    add     #4,r5
    dt      r3
    bf/s    il_row
    add     #32,r4
    fldi0   fr9
    fcmp/gt fr9,fr8
    bf      il_no
    mov.l   .Lif_three3,r0
    lds     r0,fpul
    fsts    fpul,fr9
    fdiv    fr8,fr9                 ! 3 / the scale
    add     #-36,r5
    mov     #9,r3
1:  fmov.s  @r5,fr0
    fmul    fr9,fr0
    fmov.s  fr0,@r5
    dt      r3
    bf/s    1b
    add     #4,r5
    rts
    sett
il_no:
    rts
    clrt

! The case of M_Float3x3ToQuat where e[\i][\i] is the largest of the three.
    .macro QCASE i, j, k
    mov     #(\i*16),r0
    fmov.s  @(r0,r4),fr3
    mov     #(\j*16),r0
    fmov.s  @(r0,r4),fr5
    fsub    fr5,fr3
    mov     #(\k*16),r0
    fmov.s  @(r0,r4),fr5
    fsub    fr5,fr3
    fldi1   fr4
    fadd    fr4,fr3
    fsqrt   fr3
    fadd    fr3,fr3                 ! s
    fdiv    fr3,fr4                 ! 1 / s
    mov.l   .Lif_quarter,r0
    lds     r0,fpul
    fsts    fpul,fr5
    fmul    fr3,fr5
    mov     #(\i*4),r0
    fmov.s  fr5,@(r0,r5)
    mov     #((\j*3+\i)*4),r0
    fmov.s  @(r0,r4),fr5
    mov     #((\i*3+\j)*4),r0
    fmov.s  @(r0,r4),fr6
    fadd    fr6,fr5
    fmul    fr4,fr5
    mov     #(\j*4),r0
    fmov.s  fr5,@(r0,r5)
    mov     #((\k*3+\i)*4),r0
    fmov.s  @(r0,r4),fr5
    mov     #((\i*3+\k)*4),r0
    fmov.s  @(r0,r4),fr6
    fadd    fr6,fr5
    fmul    fr4,fr5
    mov     #(\k*4),r0
    fmov.s  fr5,@(r0,r5)
    mov     #((\k*3+\j)*4),r0
    fmov.s  @(r0,r4),fr5
    mov     #((\j*3+\k)*4),r0
    fmov.s  @(r0,r4),fr6
    fsub    fr6,fr5
    fmul    fr4,fr5
    mov     #12,r0
    fmov.s  fr5,@(r0,r5)
    bra     iq_load
    nop
    .endm

! The quaternion of the rotation at r4 (M_Float3x3ToQuat), normalised, to r5.
if_quat:
    fmov.s  @r4,fr0                 ! e00
    mov     #16,r0
    fmov.s  @(r0,r4),fr1            ! e11
    mov     #32,r0
    fmov.s  @(r0,r4),fr2            ! e22
    fmov    fr0,fr3
    fadd    fr1,fr3
    fadd    fr2,fr3                 ! the trace
    fldi0   fr4
    fcmp/gt fr4,fr3
    bt      iq_tr
    fcmp/gt fr0,fr1
    bt      1f
    fcmp/gt fr0,fr2
    bt      iq_2
    bra     iq_0
    nop
1:  fcmp/gt fr1,fr2
    bt      iq_2
    bra     iq_1
    nop
iq_tr:
    fldi1   fr4
    fadd    fr4,fr3
    fsqrt   fr3
    fadd    fr3,fr3                 ! s
    fdiv    fr3,fr4                 ! 1 / s
    mov.l   .Lif_quarter,r0
    lds     r0,fpul
    fsts    fpul,fr5
    fmul    fr3,fr5                 ! w
    mov     #28,r0
    fmov.s  @(r0,r4),fr0
    mov     #20,r0
    fmov.s  @(r0,r4),fr6
    fsub    fr6,fr0
    fmul    fr4,fr0                 ! x
    mov     #8,r0
    fmov.s  @(r0,r4),fr1
    mov     #24,r0
    fmov.s  @(r0,r4),fr6
    fsub    fr6,fr1
    fmul    fr4,fr1                 ! y
    mov     #12,r0
    fmov.s  @(r0,r4),fr2
    mov     #4,r0
    fmov.s  @(r0,r4),fr6
    fsub    fr6,fr2
    fmul    fr4,fr2                 ! z
    bra     iq_norm
    fmov    fr5,fr3
iq_0:
    QCASE   0, 1, 2
iq_1:
    QCASE   1, 2, 0
iq_2:
    QCASE   2, 0, 1
iq_load:
    fmov.s  @r5,fr0
    mov     #4,r0
    fmov.s  @(r0,r5),fr1
    mov     #8,r0
    fmov.s  @(r0,r5),fr2
    mov     #12,r0
    fmov.s  @(r0,r5),fr3
iq_norm:
    NORM
    fmov.s  fr0,@r5
    mov     #4,r0
    fmov.s  fr1,@(r0,r5)
    mov     #8,r0
    fmov.s  fr2,@(r0,r5)
    mov     #12,r0
    rts
    fmov.s  fr3,@(r0,r5)

    .align 2
.Lif_lim:
    .long   0x400000
.Lif_quarter:
    .long   0x3E800000
.Lif_three3:
    .long   0x40400000

! void dc_walk_joint(const DC_WALK_JOINT *c);   anims/walk.c M_EnterJoint
!
! One joint of an animation walk on the world stack alone (g_MatrixWorldOnly):
! the pop and push of the stacks (and of the blend's far end), the bone's
! offset and the pose's turn, as Matrix_Pop(_I), Matrix_Push(_I),
! Matrix_TranslateRel32(_I) and Matrix_Rot16 / Matrix_Rot16_ID do it, through
! the same routines (dc_mat_copy, dc_translate_rel, dc_sin, dc_rot_yxz_f).
! c: 0 &g_MatrixPtr  4 &g_WMatrixPtr  8 &m_IMMatrixPtr  12 &m_WIMMatrixPtr
!    16 the last view slot  20 the last world slot (a push past them is left
!    out, as Matrix_Push)  24 flags: 1 pop, 2 push, 4 blended, 8 a pose turn
!    28 x 32 y 36 z  40 the turn (XYZ_16)  44 the far end's turn
!    48 the sine routine (dc_sin, or dc_sin_coarse for TR3's table)

! The turn (XYZ_16 at r11) of the matrix at r10, as M_RotYXZ_Float: p (sines
! and cosines, then the mask) at r15, the matrix's address at r15+28.
    .macro ROTM
    mov     #0,r12
    mov.w   @(2,r11),r0             ! Y
    tst     r0,r0
    bt      81f
    mov.l   @(48,r8),r1             ! the sine (fine or coarse)
    jsr     @r1
    mov     r0,r4
    mov.l   r0,@(0,r15)
    mov.w   @(2,r11),r0
    mov.l   .Lwj_90,r4
    mov.l   @(48,r8),r1             ! the sine (fine or coarse)
    jsr     @r1
    add     r0,r4
    mov.l   r0,@(4,r15)
    add     #1,r12
81: mov.w   @(0,r11),r0             ! X
    tst     r0,r0
    bt      82f
    mov.l   @(48,r8),r1             ! the sine (fine or coarse)
    jsr     @r1
    mov     r0,r4
    mov.l   r0,@(8,r15)
    mov.w   @(0,r11),r0
    mov.l   .Lwj_90,r4
    mov.l   @(48,r8),r1             ! the sine (fine or coarse)
    jsr     @r1
    add     r0,r4
    mov.l   r0,@(12,r15)
    add     #2,r12
82: mov.w   @(4,r11),r0             ! Z
    tst     r0,r0
    bt      83f
    mov.l   @(48,r8),r1             ! the sine (fine or coarse)
    jsr     @r1
    mov     r0,r4
    mov.l   r0,@(16,r15)
    mov.w   @(4,r11),r0
    mov.l   .Lwj_90,r4
    mov.l   @(48,r8),r1             ! the sine (fine or coarse)
    jsr     @r1
    add     r0,r4
    mov.l   r0,@(20,r15)
    add     #4,r12
83: mov.l   r12,@(24,r15)
    mov.l   r10,@(28,r15)
    mov     r15,r4
    add     #28,r4
    mov     #1,r5
    mov.l   .Lwj_rot,r1
    jsr     @r1
    mov     r15,r6
    .endm

    .global _dc_walk_joint
    .align 2
_dc_walk_joint:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    sts.l   pr,@-r15
    add     #-32,r15
    mov     r4,r8
    mov.l   @(24,r8),r9             ! the flags
    mov     r9,r0
    tst     #1,r0
    bt      wj_push
    mov.l   @(0,r8),r1              ! pop
    mov.l   @r1,r2
    add     #-96,r2
    mov.l   r2,@r1
    mov.l   @(4,r8),r1
    mov.l   @r1,r2
    add     #-96,r2
    mov.l   r2,@r1
    tst     #4,r0
    bt      wj_push
    mov.l   @(8,r8),r1
    mov.l   @r1,r2
    add     #-96,r2
    mov.l   r2,@r1
    mov.l   @(12,r8),r1
    mov.l   @r1,r2
    add     #-96,r2
    mov.l   r2,@r1
wj_push:
    mov     r9,r0
    tst     #2,r0
    bt      wj_trans
    mov.l   @(0,r8),r1
    mov.l   @r1,r2
    mov.l   @(16,r8),r3
    cmp/hs  r3,r2                   ! no room on the view stack
    bt      wj_far
    mov.l   @(4,r8),r10
    mov.l   @r10,r11
    mov.l   @(20,r8),r3
    cmp/hs  r3,r11                  ! nor on the world one
    bt      wj_far
    add     #96,r2
    mov.l   r2,@r1                  ! the view stack: no copy
    mov     r11,r5
    mov     r11,r4
    add     #96,r4
    mov.l   r4,@r10
    mov.l   .Lwj_copy,r1
    jsr     @r1
    nop
wj_far:
    mov     r9,r0
    tst     #4,r0
    bt      wj_trans
    mov.l   @(8,r8),r1
    mov.l   @r1,r2
    add     #96,r2
    mov.l   r2,@r1
    mov.l   @(12,r8),r10
    mov.l   @r10,r5
    mov     r5,r4
    add     #96,r4
    mov.l   r4,@r10
    mov.l   .Lwj_copy,r1
    jsr     @r1
    nop
wj_trans:
    mov.l   @(4,r8),r1
    mov.l   @r1,r4
    mov.l   @(28,r8),r5
    mov.l   @(32,r8),r6
    mov.l   .Lwj_trans,r1
    jsr     @r1
    mov.l   @(36,r8),r7
    mov     r9,r0
    tst     #4,r0
    bt      wj_rot
    mov.l   @(12,r8),r1
    mov.l   @r1,r4
    mov.l   @(28,r8),r5
    mov.l   @(32,r8),r6
    mov.l   .Lwj_trans,r1
    jsr     @r1
    mov.l   @(36,r8),r7
wj_rot:
    mov     r9,r0
    tst     #8,r0
    bt      wj_end
    mov.l   @(4,r8),r1
    mov.l   @r1,r10
    mov.l   @(40,r8),r11
    ROTM
    mov     r9,r0
    tst     #4,r0
    bt      wj_end
    mov.l   @(12,r8),r1
    mov.l   @r1,r10
    mov.l   @(44,r8),r11
    ROTM
wj_end:
    add     #32,r15
    lds.l   @r15+,pr
    mov.l   @r15+,r12
    mov.l   @r15+,r11
    mov.l   @r15+,r10
    mov.l   @r15+,r9
    rts
    mov.l   @r15+,r8

    .align 2
.Lwj_sin:
    .long   _dc_sin
.Lwj_90:
    .long   16384
.Lwj_rot:
    .long   _dc_rot_yxz_f
.Lwj_copy:
    .long   _dc_mat_copy
.Lwj_trans:
    .long   _dc_translate_rel

! void dc_rot_yxz_n(MATRIX *const *ms, int32_t n, const XYZ_16 *rot,
!                   int32_t (*sin)(int32_t));
! matrix.c M_RotYXZ_N's integer path (both stacks, not the world one alone):
! about Y, then X, then Z, each axis left alone when its angle is 0, every
! matrix by dc_rot_y/x/z with the sine and the cosine (the sine 90 degrees on)
! from the table's routine (dc_sin, or dc_sin_coarse for TR3).
    .macro RYXZ_AXIS off, fn
    mov.w   @(\off,r10),r0          ! the angle, as Math_Sin gets it
    tst     r0,r0
    bt      77f
    mov     r0,r12
    mov.l   @r15,r1
    jsr     @r1
    mov     r12,r4
    mov     r0,r13                  ! sine
    mov.l   .Lry_q,r1
    mov     r12,r4
    add     r1,r4
    mov.l   @r15,r1
    jsr     @r1
    nop
    mov     r0,r14                  ! cosine
    mov     r8,r12
    mov     r9,r11
    cmp/pl  r11
    bf      77f
76: mov.l   @r12+,r4
    mov     r13,r5
    mov.l   \fn,r1
    jsr     @r1
    mov     r14,r6
    dt      r11
    bf      76b
77:
    .endm

    .text
    .align 2
    .global _dc_rot_yxz_n
_dc_rot_yxz_n:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    mov.l   r13,@-r15
    mov.l   r14,@-r15
    sts.l   pr,@-r15
    mov.l   r7,@-r15                ! the sine's routine
    mov     r4,r8
    mov     r5,r9
    mov     r6,r10
    RYXZ_AXIS 2, .Lry_y
    RYXZ_AXIS 0, .Lry_x
    RYXZ_AXIS 4, .Lry_z
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
.Lry_q:
    .long   16384
.Lry_y:
    .long   _dc_rot_y
.Lry_x:
    .long   _dc_rot_x
.Lry_z:
    .long   _dc_rot_z

! void dc_translate_n(MATRIX *const *ms, int32_t n, const int32_t xyz[3]);
! matrix.c Matrix_TranslateRel32_I / _ID with one offset: dc_translate_rel on
! each of the n matrices.
    .text
    .align 2
    .global _dc_translate_n
_dc_translate_n:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    sts.l   pr,@-r15
    mov     r4,r8
    mov     r5,r9
    mov     r6,r10
    cmp/pl  r9
    bf      2f
1:  mov.l   @r8+,r4
    mov.l   @r10,r5
    mov.l   @(4,r10),r6
    mov.l   .Ltn_rel,r1
    jsr     @r1
    mov.l   @(8,r10),r7
    dt      r9
    bf      1b
2:  lds.l   @r15+,pr
    mov.l   @r15+,r10
    mov.l   @r15+,r9
    rts
    mov.l   @r15+,r8

    .align 2
.Ltn_rel:
    .long   _dc_translate_rel

! void dc_walk_bones(const M_WALK_BONES *b);   anims/walk.c Anim_Walk_Next
! The joints first..last of an object's walk: each bone through dc_walk_joint,
! the depth counted, its extra rotations (Y, X, Z) through the C's Matrix_Rot*,
! then the caller's draw for the joint. TRX_DC_ASMOFF3 bit 67108864 turns it
! off.
! Block: 0 dc_walk_joint's block 4 bones (ANIM_BONE, 17 bytes: pop, push,
! rot x y z, pos packed) 8 rots_a 12 rots_b 16 &extra_rotation 20 first
! 24 last 28 interpolated 32 rot y 36 rot x 40 rot z 44 draw(ctx, joint)
! 48 ctx 52 &depth.
! r8 the block, r9 the joint's block, r10 the joint, r11 its bone, r12 last,
! r13 the extra rotations, r14 the flags every joint has.
    .text
    .align 2
    .global _dc_walk_bones
_dc_walk_bones:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    mov.l   r13,@-r15
    mov.l   r14,@-r15
    sts.l   pr,@-r15
    mov     r4,r8
    mov.l   @r8,r9
    mov.l   @(20,r8),r10
    mov.l   @(24,r8),r12
    mov.l   @(28,r8),r14
    shll2   r14                     ! interpolated: 4
    mov.l   @(8,r8),r0
    tst     r0,r0
    bt      1f
    add     #8,r14                  ! rotations: 8
1:  mov     r10,r0                  ! the bone: bones + (joint - 1) * 17
    add     #-1,r0
    mov     r0,r1
    shll2   r1
    shll2   r1
    add     r0,r1
    mov.l   @(4,r8),r11
    add     r1,r11
wb_loop:
    cmp/gt  r12,r10
    bt      wb_done
    mov.b   @r11,r0                 ! flags: pop 1, push 2
    mov     r0,r1
    mov.b   @(1,r11),r0
    shll    r0
    or      r0,r1
    or      r14,r1
    mov.l   r1,@(24,r9)
    mov     r11,r2                  ! the offset, packed at 5
    add     #5,r2
    mov     #28,r7
2:  mov.b   @r2+,r0
    extu.b  r0,r3
    mov.b   @r2+,r0
    extu.b  r0,r0
    shll8   r0
    or      r0,r3
    mov.b   @r2+,r0
    extu.b  r0,r0
    shll16  r0
    or      r0,r3
    mov.b   @r2+,r0
    shll16  r0
    shll8   r0
    or      r0,r3
    mov     r7,r0
    mov.l   r3,@(r0,r9)
    add     #4,r7
    mov     #40,r0
    cmp/ge  r0,r7
    bf      2b
    mov.l   @(8,r8),r2              ! rots + joint * 6
    tst     r2,r2
    bt      3f
    mov     r10,r1
    shll    r1
    mov     r1,r0
    shll    r0
    add     r0,r1
    add     r1,r2
    mov.l   @(12,r8),r3
    add     r1,r3
    bra     4f
    nop
3:  mov     #0,r3
4:  mov.l   r2,@(40,r9)
    mov.l   r3,@(44,r9)
    mov     r9,r4
    mov.l   .Lwb_joint,r1
    jsr     @r1
    nop
    mov.l   @(52,r8),r1             ! depth += push - pop
    mov.l   @r1,r2
    mov.b   @(1,r11),r0
    add     r0,r2
    mov.b   @r11,r0
    sub     r0,r2
    mov.l   r2,@r1
    mov.l   @(16,r8),r1             ! the extra rotations
    mov.l   @r1,r13
    tst     r13,r13
    bt      wb_draw
    mov.b   @(3,r11),r0
    tst     r0,r0
    bt      5f
    mov.w   @r13+,r4
    mov.l   @(32,r8),r1
    jsr     @r1
    nop
5:  mov.b   @(2,r11),r0
    tst     r0,r0
    bt      6f
    mov.w   @r13+,r4
    mov.l   @(36,r8),r1
    jsr     @r1
    nop
6:  mov.b   @(4,r11),r0
    tst     r0,r0
    bt      7f
    mov.w   @r13+,r4
    mov.l   @(40,r8),r1
    jsr     @r1
    nop
7:  mov.l   @(16,r8),r1
    mov.l   r13,@r1
wb_draw:
    mov.l   @(48,r8),r4
    mov     r10,r5
    mov.l   @(44,r8),r1
    jsr     @r1
    nop
    add     #1,r10
    bra     wb_loop
    add     #17,r11
wb_done:
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
.Lwb_joint:
    .long   _dc_walk_joint
