! Hand-written SH-4 for the per-instance numbers of the batcher (batcher.c):
! an instance's world matrix and its model-view matrix.
!
!   void dc_world_matrix(const MATRIX *m, float out[12]);
!   void dc_model_view(const float view[16], const float model[16],
!                      float out[16], float *scale);
!
! dc_world_matrix: M_WorldMatrix. MATRIX is twelve int64 (rows of four); each
! becomes (float)(int32)(v >> 16) * 65536 + (float)(v & 0xFFFF), times
! 1 / (1 << W2V_SHIFT) (W2V_SHIFT is 14), as Output_MatrixToFloat does.
!
! dc_model_view: M_ModelView. out[c][r] = sum over k of view[k][r] * model[c][k]
! (GL's layout, column first), and *scale = the length of model's first three
! entries. Uses only fr0-fr11, and leaves XMTRX alone.

    .text
    .align 2
    .global _dc_world_matrix
    .global _dc_model_view

_dc_world_matrix:
    mov.l   .Lc65536,r0
    lds     r0,fpul
    fsts    fpul,fr2
    mov.l   .Lkw2v,r0
    lds     r0,fpul
    fsts    fpul,fr3
    mov     #12,r3
1:  mov.l   @r4+,r1                 ! the low word
    mov.l   @r4+,r2                 ! the high word
    shll16  r2
    mov     r1,r6
    shlr16  r6
    or      r6,r2                   ! (int32)(v >> 16)
    extu.w  r1,r1                   ! v & 0xFFFF
    lds     r2,fpul
    float   fpul,fr0
    fmul    fr2,fr0
    lds     r1,fpul
    float   fpul,fr1
    fadd    fr1,fr0
    fmul    fr3,fr0
    fmov.s  fr0,@r5
    add     #4,r5
    dt      r3
    bf      1b
    rts
    nop

! dc_fill_matrix: Output_FillMatrix (render/../output/utils.c). The same
! numbers as dc_world_matrix, laid out as GL's columns: out[c][r] for the
! three rows, the fourth row 0 0 0 1.
!   void dc_fill_matrix(const MATRIX *m, float out[16]);
    .global _dc_fill_matrix
_dc_fill_matrix:
    mov.l   .Lc65536,r0
    lds     r0,fpul
    fsts    fpul,fr2
    mov.l   .Lkw2v,r0
    lds     r0,fpul
    fsts    fpul,fr3
    mov     r5,r7                   ! the row's first column
    mov     #3,r3
2:  mov     r7,r5
    mov     #4,r0
1:  mov.l   @r4+,r1                 ! the low word
    mov.l   @r4+,r2                 ! the high word
    shll16  r2
    mov     r1,r6
    shlr16  r6
    or      r6,r2                   ! (int32)(v >> 16)
    extu.w  r1,r1                   ! v & 0xFFFF
    lds     r2,fpul
    float   fpul,fr0
    fmul    fr2,fr0
    lds     r1,fpul
    float   fpul,fr1
    fadd    fr1,fr0
    fmul    fr3,fr0
    fmov.s  fr0,@r5
    dt      r0
    bf/s    1b
    add     #16,r5                  ! the next column
    dt      r3
    bf/s    2b
    add     #4,r7                   ! the next row
    ! r7 is on out + 12: the fourth row.
    fldi0   fr0
    fldi1   fr1
    fmov.s  fr0,@r7
    add     #16,r7
    fmov.s  fr0,@r7
    add     #16,r7
    fmov.s  fr0,@r7
    add     #16,r7
    rts
    fmov.s  fr1,@r7

_dc_model_view:
    mov     r5,r1
    fmov.s  @r1+,fr1
    fmov.s  @r1+,fr2
    fmov.s  @r1+,fr3
    fmul    fr1,fr1                 ! as the C does it: x*x, then fused y*y, z*z
    fmov    fr2,fr0
    fmac    fr0,fr2,fr1
    fmov    fr3,fr0
    fmac    fr0,fr3,fr1
    fsqrt   fr1
    fmov.s  fr1,@r7
    mov     #0,r2                   ! r: the byte offset of the row
2:  mov     r4,r1
    add     r2,r1                   ! &view[0][r]
    fmov.s  @r1,fr4
    add     #16,r1
    fmov.s  @r1,fr5
    add     #16,r1
    fmov.s  @r1,fr6
    add     #16,r1
    fmov.s  @r1,fr7
    mov     r5,r1                   ! the model's columns
    mov     r6,r3
    add     r2,r3                   ! &out[0][r]
    mov     #4,r7
3:  fmov.s  @r1+,fr8
    fmov.s  @r1+,fr9
    fmov.s  @r1+,fr10
    fmov.s  @r1+,fr11
    fmov    fr4,fr1                 ! the C's order: a0*b0, then fused a1*b1,
    fmul    fr8,fr1                 ! a2*b2, a3*b3
    fmov    fr5,fr0
    fmac    fr0,fr9,fr1
    fmov    fr6,fr0
    fmac    fr0,fr10,fr1
    fmov    fr7,fr0
    fmac    fr0,fr11,fr1
    fmov.s  fr1,@r3
    add     #16,r3
    dt      r7
    bf      3b
    add     #4,r2
    mov     #16,r0
    cmp/eq  r2,r0
    bf      2b
    rts
    nop

    .align 2
.Lc65536:
    .long   0x47800000
.Lkw2v:
    .long   0x38800000

! void dc_light_dir(const float *mv, const float *v, const float *k, float *out)
! void dc_light_dir_n(...)    the same, but v is scaled to length 1 first
!                             (when it has any length)
! void dc_light_dir_div(...)  the same as dc_light_dir, but divides by *k
!
! A light's direction from the view space into the object's: out[c] is the
! dot product of row c of the matrix (mv[c][0..2], rows of four floats) and v,
! times *k (batcher.c, M_ReadInstanceLight).
    .global _dc_light_dir
    .global _dc_light_dir_n
    .global _dc_light_dir_div
    .align 2
_dc_light_dir:
    bra     ld_common
    mov     #0,r3
_dc_light_dir_n:
    bra     ld_common
    mov     #1,r3
_dc_light_dir_div:
    mov     #2,r3
ld_common:
    mov     r5,r1
    fmov.s  @r1+,fr4
    fmov.s  @r1+,fr5
    fmov.s  @r1+,fr6
    mov     r3,r0
    cmp/eq  #1,r0
    bf      2f
    fmov    fr4,fr11
    fmul    fr4,fr11                ! x*x, then fused y*y and z*z: the C's way
    fmov    fr5,fr0
    fmac    fr0,fr5,fr11
    fmov    fr6,fr0
    fmac    fr0,fr6,fr11            ! fr11 = the length squared
    fldi0   fr7
    fcmp/gt fr7,fr11
    bf      2f
    fsrra   fr11
    fmul    fr11,fr4
    fmul    fr11,fr5
    fmul    fr11,fr6
2:  fmov.s  @r6,fr7
    mov     #3,r2
3:  fmov.s  @r4+,fr8
    fmov.s  @r4+,fr9
    fmov.s  @r4+,fr10
    add     #4,r4
    fmov    fr8,fr11
    fmul    fr4,fr11                ! m0*v0, then fused m1*v1 and m2*v2
    fmov    fr9,fr0
    fmac    fr0,fr5,fr11
    fmov    fr10,fr0
    fmac    fr0,fr6,fr11
    mov     r3,r0
    cmp/eq  #2,r0
    bf      4f
    fdiv    fr7,fr11
    bra     5f
    nop
4:  fmul    fr7,fr11
5:  fmov.s  fr11,@r7
    add     #4,r7
    dt      r2
    bf      3b
    rts
    nop

! void dc_room_sphere(const float world[12], const float bounds[6], float out[4]);
!
! An instance's bounding sphere in the world (batcher.c, M_PrepareRoomLights):
! out[0..2] the middle of the box (lo, hi) taken through the world matrix (rows
! of four), out[3] the radius, sqrt(half squared * the matrix's x scale squared).
    .global _dc_room_sphere
    .align 2
_dc_room_sphere:
    mov     r5,r1
    fmov.s  @r1+,fr0
    fmov.s  @r1+,fr1
    fmov.s  @r1+,fr2
    fmov.s  @r1+,fr4
    fmov.s  @r1+,fr5
    fmov.s  @r1+,fr6
    mov.l   .Lhalf2,r0
    lds     r0,fpul
    fsts    fpul,fr7
    fsub    fr0,fr4
    fsub    fr1,fr5
    fsub    fr2,fr6
    fmul    fr7,fr4
    fmul    fr7,fr5
    fmul    fr7,fr6
    fadd    fr4,fr0
    fadd    fr5,fr1
    fadd    fr6,fr2
    fmul    fr4,fr4
    fmul    fr5,fr5
    fmul    fr6,fr6
    fadd    fr5,fr4
    fadd    fr6,fr4                 ! half squared
    fldi1   fr3
    mov     r4,r1
    mov     #3,r2
1:  fmov.s  @r1+,fr8
    fmov.s  @r1+,fr9
    fmov.s  @r1+,fr10
    fmov.s  @r1+,fr11
    fipr    fv0,fv8
    fmov.s  fr11,@r6
    add     #4,r6
    dt      r2
    bf      1b
    mov     r4,r1
    fmov.s  @r1,fr8
    add     #16,r1
    fmov.s  @r1,fr9
    add     #16,r1
    fmov.s  @r1,fr10
    fldi0   fr11
    fipr    fv8,fv8                 ! the x scale squared
    fmul    fr11,fr4
    fsqrt   fr4
    fmov.s  fr4,@r6
    rts
    nop
    .align 2
.Lhalf2:
    .long   0x3F000000

! void dc_scan_vertices(const OUTPUT_MESH_VERTEX *v, int32_t n, int32_t *out);
!
! What a mesh's vertices say as a whole (batcher.c, MeshBatcher_AddMesh), n at
! least 1. In: out[10] a mask, out[11] the value its flags must show for
! "plain". Out (words): 0 the flags ANDed  1 the flags ORed  2 1 if every colour
! is white (all four bytes 255)  3 1 if (flags & mask) == value for every vertex
! 4-6 unused  7-9 hi... as laid out: 4 lo x  5 lo y  6 lo z  7 hi x  8 hi y
! 9 hi z (whole units). The vertex is 28 bytes: colour at 4, x y z at 8 10 12
! (int16), flags at 22 (uint16).
    .global _dc_scan_vertices
    .align 2
_dc_scan_vertices:
    mov.w   @(8,r4),r0
    mov.l   r0,@(16,r6)
    mov.l   r0,@(28,r6)
    mov.w   @(10,r4),r0
    mov.l   r0,@(20,r6)
    mov.l   r0,@(32,r6)
    mov.w   @(12,r4),r0
    mov.l   r0,@(24,r6)
    mov.l   r0,@(36,r6)
    mov     #-1,r0
    extu.w  r0,r0
    mov.l   r0,@(0,r6)
    mov     #0,r0
    mov.l   r0,@(4,r6)
    mov     #1,r0
    mov.l   r0,@(8,r6)
    mov.l   r0,@(12,r6)
    .macro BND p, lo, hi
    mov.w   @(\p,r4),r0
    mov.l   @(\lo,r6),r2
    cmp/gt  r0,r2
    bf      1f
    mov.l   r0,@(\lo,r6)
1:  mov.l   @(\hi,r6),r2
    cmp/gt  r2,r0
    bf      1f
    mov.l   r0,@(\hi,r6)
1:
    .endm
sv_loop:
    mov.w   @(22,r4),r0
    extu.w  r0,r1
    mov.l   @(0,r6),r2
    and     r1,r2
    mov.l   r2,@(0,r6)
    mov.l   @(4,r6),r2
    or      r1,r2
    mov.l   r2,@(4,r6)
    mov.l   @(40,r6),r2
    and     r1,r2
    mov.l   @(44,r6),r3
    cmp/eq  r3,r2
    movt    r2
    mov.l   @(12,r6),r3
    and     r2,r3
    mov.l   r3,@(12,r6)
    mov.l   @(4,r4),r2
    add     #1,r2
    tst     r2,r2
    movt    r2
    mov.l   @(8,r6),r3
    and     r2,r3
    mov.l   r3,@(8,r6)
    BND     8, 16, 28
    BND     10, 20, 32
    BND     12, 24, 36
    add     #28,r4
    dt      r5
    bf      sv_loop
    rts
    nop

! void dc_room_lights(const DC_ROOM_LIGHTS *c);   batcher.c M_PrepareRoomLights
!
! The room's dynamic lights that reach an instance's bounding sphere, as the
! C loop: a LUM light (kind 0) becomes an M_POINT_LIGHT (pos, radius^2,
! 2^shade, 1 / 2^(2 falloff - shade), its radius 2^falloff); an RGB light
! (kind 1) an M_RGB_LIGHT (pos, radius = falloff / 2, 1 / radius, colour)
! when its radius is 1 or more. At most 32 of each; the loop stops at 32 LUM.
! M_Exp2 is worked out here for a whole power from -30 to 30 (the float's
! bits, exact as the C's), else the C's own M_Exp2 (its argument in fr5).
! c: 0 the DC_UNIFORM_LIGHTS  4 the sphere's centre (x y z)  16 its radius
!    20 the M_POINT_LIGHTs  24 &num_lights  28 the M_RGB_LIGHTs  32 &num_rgb
! DC_UNIFORM_LIGHT (48 bytes): 0 pos  16 colour  32 shade  36 falloff
! 40 kind (a float). M_POINT_LIGHT 24 bytes, M_RGB_LIGHT 32 (pos, radius,
! 1 / radius, colour).

! 2^fr5 into fr0. Clobbers r0-r7, fr0-fr11 (the C on the slow path).
    .macro EXP2
    ftrc    fr5,fpul
    sts     fpul,r1
    float   fpul,fr0
    fcmp/eq fr5,fr0
    bf      71f
    mov     #-30,r2
    cmp/ge  r2,r1
    bf      71f
    mov     #30,r2
    cmp/gt  r2,r1
    bt      71f
    mov     r1,r2
    add     #127,r2
    shll16  r2
    shll2   r2
    shll2   r2
    shll2   r2
    shll    r2                      ! (127 + i) << 23
    lds     r2,fpul
    bra     72f
    fsts    fpul,fr0
71: mov.l   .Lrl_exp2,r1
    jsr     @r1
    nop
72:
    .endm

! The squared distance from the light at r9 to the centre (fr12-fr14) into
! fr1.
    .macro DIST
    fmov.s  @r9,fr1
    fsub    fr12,fr1
    fmul    fr1,fr1
    mov     r9,r1
    add     #4,r1
    fmov.s  @r1+,fr2
    fsub    fr13,fr2
    fmul    fr2,fr2
    fmov.s  @r1,fr3
    fsub    fr14,fr3
    fmul    fr3,fr3
    fadd    fr2,fr1
    fadd    fr3,fr1
    .endm

    .global _dc_room_lights
    .align 2
_dc_room_lights:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    mov.l   r13,@-r15
    mov.l   r14,@-r15
    sts.l   pr,@-r15
    fmov.s  fr12,@-r15
    fmov.s  fr13,@-r15
    fmov.s  fr14,@-r15
    fmov.s  fr15,@-r15
    mov     r4,r8
    mov.l   @r8,r9
    mov.l   @r9,r10                 ! how many lights
    add     #16,r9                  ! the first
    mov.l   @(24,r8),r1
    mov.l   @r1,r11                 ! num_lights
    mov.l   @(32,r8),r1
    mov.l   @r1,r12                 ! num_rgb
    mov     r8,r1
    add     #4,r1
    fmov.s  @r1+,fr12
    fmov.s  @r1+,fr13
    fmov.s  @r1+,fr14
    fmov.s  @r1+,fr15               ! the sphere's radius
    mov     #0,r13
rl_loop:
    cmp/ge  r10,r13
    bt      rl_done
    mov     #32,r0
    cmp/ge  r0,r11
    bt      rl_done
    mov     r9,r1
    add     #40,r1
    fmov.s  @r1,fr0                 ! kind
    fldi1   fr1
    fcmp/eq fr1,fr0
    bt      rl_rgb
    fldi0   fr1
    fcmp/eq fr1,fr0
    bf      rl_next
    mov     r9,r1                   ! LUM
    add     #36,r1
    fmov.s  @r1,fr5
    EXP2                            ! the radius
    DIST
    fmov    fr0,fr4
    fadd    fr15,fr4
    fmul    fr4,fr4                 ! reach^2
    fcmp/gt fr4,fr1
    bt      rl_next
    mov     r11,r14                 ! 24 n: 16 n + 8 n
    shll2   r14
    shll2   r14
    mov     r11,r1
    shll2   r1
    shll    r1                      ! 8 n
    add     r1,r14
    mov.l   @(20,r8),r1
    add     r1,r14                  ! the M_POINT_LIGHT
    mov.l   @r9,r1
    mov.l   r1,@r14
    mov.l   @(4,r9),r1
    mov.l   r1,@(4,r14)
    mov.l   @(8,r9),r1
    mov.l   r1,@(8,r14)
    fmul    fr0,fr0
    mov     r14,r1
    add     #12,r1
    fmov.s  fr0,@r1                 ! radius^2
    mov     r9,r1
    add     #32,r1
    fmov.s  @r1,fr5
    EXP2
    mov     r14,r1
    add     #16,r1
    fmov.s  fr0,@r1                 ! 2^shade
    mov     r9,r1
    add     #36,r1
    fmov.s  @r1,fr5
    fadd    fr5,fr5
    add     #-4,r1
    fmov.s  @r1,fr1
    fsub    fr1,fr5                 ! 2 falloff - shade
    EXP2
    fldi1   fr1
    fdiv    fr0,fr1
    mov     r14,r1
    add     #20,r1
    fmov.s  fr1,@r1
    bra     rl_next
    add     #1,r11
rl_rgb:
    mov     r9,r1
    add     #36,r1
    fmov.s  @r1,fr0
    mov.l   .Lrl_half,r1
    lds     r1,fpul
    fsts    fpul,fr1
    fmul    fr1,fr0                 ! the radius
    fldi1   fr1
    fcmp/gt fr0,fr1
    bt      rl_next
    mov     #32,r0
    cmp/ge  r0,r12
    bt      rl_next
    DIST
    fmov    fr0,fr4
    fadd    fr15,fr4
    fmul    fr4,fr4
    fcmp/gt fr4,fr1
    bt      rl_next
    mov     r12,r14
    shll2   r14
    shll2   r14
    shll    r14                     ! 32 n
    mov.l   @(28,r8),r1
    add     r1,r14                  ! the M_RGB_LIGHT
    mov.l   @r9,r1
    mov.l   r1,@r14
    mov.l   @(4,r9),r1
    mov.l   r1,@(4,r14)
    mov.l   @(8,r9),r1
    mov.l   r1,@(8,r14)
    mov     r14,r1
    add     #12,r1
    fmov.s  fr0,@r1                 ! radius
    fldi1   fr1
    fdiv    fr0,fr1
    add     #4,r1
    fmov.s  fr1,@r1                 ! 1 / radius
    mov.l   @(16,r9),r1
    mov.l   r1,@(20,r14)
    mov.l   @(20,r9),r1
    mov.l   r1,@(24,r14)
    mov.l   @(24,r9),r1
    mov.l   r1,@(28,r14)
    add     #1,r12
rl_next:
    add     #48,r9
    bra     rl_loop
    add     #1,r13
rl_done:
    mov.l   @(24,r8),r1
    mov.l   r11,@r1
    mov.l   @(32,r8),r1
    mov.l   r12,@r1
    fmov.s  @r15+,fr15
    fmov.s  @r15+,fr14
    fmov.s  @r15+,fr13
    fmov.s  @r15+,fr12
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
.Lrl_exp2:
    .long   _DC_Batcher_Exp2
.Lrl_half:
    .long   0x3F000000

! void dc_nat_water(const DC_NAT_WATER *w);   batcher.c M_NatWater
!
! A baked batch's water vertices for this frame, as the PSX does them: each
! one's own colour plus its lane's value (m_WaterTab), each byte held to
! 0..255, then M_Colour (times the tint and the curve, in 8.8, saturated).
! Vertices whose place is 0xFF have no water and are left alone.
! w: 0 the batch's first vertex (OUTPUT_MESH_VERTEX, 28 bytes, colour r g b a
!    at 4)  4 their places (bytes)  8 the batch's buffer (M_XVERT, argb at
!    24)  12 how many  16 the water's table (int32)  20 ka  24 kr  28 kg
!    32 kb

! A byte (r1) plus the lane's value (r2) held to 0..255, times \k (r4..r7
! loaded), >> 8, saturated, into r0.
    .macro WCH k
    add     r2,r1
    cmp/pz  r1
    bt      21f
    mov     #0,r1
21: mov.l   .Lnw_255,r0
    cmp/gt  r0,r1
    bf      22f
    mov     r0,r1
22: mul.l   \k,r1
    sts     macl,r1
    shlr8   r1
    mov     r1,r0
    shlr8   r0
    neg     r0,r0
    or      r1,r0
    extu.b  r0,r0                   ! M_Sat8
    .endm

    .global _dc_nat_water
    .align 2
_dc_nat_water:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    mov.l   r13,@-r15
    mov.l   r14,@-r15
    mov.l   @r4,r8                  ! vertices
    mov.l   @(4,r4),r9              ! places
    mov.l   @(8,r4),r10             ! buffer
    mov.l   @(12,r4),r11            ! count
    mov.l   @(16,r4),r12            ! the table
    mov.l   @(20,r4),r13            ! ka
    mov.l   @(24,r4),r5             ! kr
    mov.l   @(28,r4),r6             ! kg
    mov.l   @(32,r4),r7             ! kb
    tst     r11,r11
    bt      nw_done
nw_loop:
    mov.b   @r9+,r0
    extu.b  r0,r0
    mov.l   .Lnw_ff,r1
    cmp/eq  r1,r0                   ! 0xFF: no water on it
    bt      nw_next
    shll2   r0
    mov.l   @(r0,r12),r2            ! the lane's value
    mov     r8,r3
    add     #4,r3                   ! the colour
    mov.b   @(3,r3),r0
    extu.b  r0,r1                   ! a
    mul.l   r13,r1
    sts     macl,r1
    shlr8   r1
    mov     r1,r0
    shlr8   r0
    neg     r0,r0
    or      r1,r0
    extu.b  r0,r14
    shll16  r14
    shll8   r14                     ! alpha, in place
    mov.b   @r3,r0
    extu.b  r0,r1
    WCH     r5
    shll16  r0
    or      r0,r14
    mov.b   @(1,r3),r0
    extu.b  r0,r1
    WCH     r6
    shll8   r0
    or      r0,r14
    mov.b   @(2,r3),r0
    extu.b  r0,r1
    WCH     r7
    or      r0,r14
    mov     r10,r0
    add     #24,r0
    mov.l   r14,@r0
nw_next:
    add     #28,r8
    dt      r11
    bf/s    nw_loop
    add     #32,r10
nw_done:
    mov.l   @r15+,r14
    mov.l   @r15+,r13
    mov.l   @r15+,r12
    mov.l   @r15+,r11
    mov.l   @r15+,r10
    mov.l   @r15+,r9
    rts
    mov.l   @r15+,r8

    .align 2
.Lnw_255:
    .long   255
.Lnw_ff:
    .long   255

! void dc_tr3_light(const DC_TR3_LIGHT *b);   batcher.c M_ReadInstanceLight, TR3
!
! TR3's object light for an instance: the ambient copied, and for an object
! lit by the room the three lights with any colour, each one's direction
! taken into the object's space (dc_light_dir_n: to length 1, over 1 << 14),
! its colour copied, the brightest also as l (away from it, times 1 << 14,
! as TR1 and TR2's vector) for the cel shading.
! b: 0 the OUTPUT_LIGHT_INFO  4 the M_INSTANCE_LIGHT  8 the model-view
!    12 1: lit by the room's object light (else the ambient only)
! OUTPUT_LIGHT_INFO: 20 tr3_ambient  32 tr3_light_color[3]  68 dir_view[3]
!   (int32).  M_INSTANCE_LIGHT: 0 l  16 has_direction  24 tr3_ambient
!   36 tr3_count  40 tr3_dir[3]  76 tr3_color[3]
! Frame: 0 v (3 floats)  12 k  16 best  20 sum
    .global _dc_tr3_light
    .align 2
_dc_tr3_light:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    sts.l   pr,@-r15
    add     #-24,r15
    mov.l   @r4,r8                  ! info
    mov.l   @(4,r4),r9              ! out
    mov.l   @(8,r4),r10             ! mv
    mov.l   @(12,r4),r0
    mov.l   @(20,r8),r1             ! the ambient
    mov.l   r1,@(24,r9)
    mov.l   @(24,r8),r1
    mov.l   r1,@(28,r9)
    mov.l   @(28,r8),r1
    mov.l   r1,@(32,r9)
    tst     r0,r0
    bt      tl_done
    mov.l   .Ltl_k,r1
    mov.l   r1,@(12,r15)            ! 1 / (1 << 14)
    mov     #0,r1
    mov.l   r1,@(16,r15)            ! best: 0.0
    mov     #0,r11                  ! the light
tl_loop:
    mov     r11,r12
    shll2   r12
    mov     r12,r0
    add     r12,r12
    add     r0,r12                  ! 12 i
    mov     r8,r1
    add     #32,r1
    add     r12,r1                  ! its colour
    fmov.s  @r1+,fr0
    fmov.s  @r1+,fr1
    fmov.s  @r1,fr2
    fadd    fr1,fr0
    fadd    fr2,fr0                 ! sum
    fldi0   fr1
    fcmp/gt fr1,fr0
    bf      tl_next
    mov     r15,r1
    add     #20,r1
    fmov.s  fr0,@r1                 ! the sum
    mov     r8,r1
    add     #68,r1
    add     r12,r1                  ! its direction (int32)
    mov.l   @r1+,r0
    lds     r0,fpul
    float   fpul,fr0
    fmov.s  fr0,@r15
    mov.l   @r1+,r0
    lds     r0,fpul
    float   fpul,fr0
    mov     r15,r2
    add     #4,r2
    fmov.s  fr0,@r2
    mov.l   @r1,r0
    lds     r0,fpul
    float   fpul,fr0
    add     #4,r2
    fmov.s  fr0,@r2
    mov.l   @(36,r9),r0             ! where it goes: tr3_dir[count]
    mov     r0,r2
    shll2   r2
    mov     r2,r3
    add     r2,r2
    add     r3,r2
    mov     r9,r7
    add     #40,r7
    add     r2,r7
    mov     r10,r4
    mov     r15,r5
    mov     r15,r6
    add     #12,r6
    mov.l   .Ltl_dir,r1
    jsr     @r1
    nop
    mov.l   @(36,r9),r0             ! its colour: tr3_color[count]
    mov     r0,r2
    shll2   r2
    mov     r2,r3
    add     r2,r2
    add     r3,r2
    mov     r9,r3
    add     #76,r3
    add     r2,r3
    mov     r8,r1
    add     #32,r1
    add     r12,r1
    mov.l   @r1,r0
    mov.l   r0,@r3
    mov.l   @(4,r1),r0
    mov.l   r0,@(4,r3)
    mov.l   @(8,r1),r0
    mov.l   r0,@(8,r3)
    mov     r15,r1
    add     #20,r1
    fmov.s  @r1,fr0                 ! the sum
    add     #-4,r1
    fmov.s  @r1,fr1                 ! the best
    fcmp/gt fr1,fr0
    bf      1f
    fmov.s  fr0,@r1                 ! the brightest yet: also as l
    mov     r9,r3
    add     #40,r3
    add     r2,r3                   ! its direction
    mov.l   .Ltl_16k,r0
    lds     r0,fpul
    fsts    fpul,fr3
    fmov.s  @r3+,fr0
    fneg    fr0
    fmul    fr3,fr0
    fmov.s  fr0,@r9
    fmov.s  @r3+,fr0
    fneg    fr0
    fmul    fr3,fr0
    mov     r9,r0
    add     #4,r0
    fmov.s  fr0,@r0
    fmov.s  @r3,fr0
    fneg    fr0
    fmul    fr3,fr0
    add     #4,r0
    fmov.s  fr0,@r0
    mov     r9,r1
    add     #16,r1
    mov     #1,r0
    mov.b   r0,@r1                  ! has_direction
1:  mov.l   @(36,r9),r0
    add     #1,r0
    mov.l   r0,@(36,r9)             ! tr3_count
tl_next:
    add     #1,r11
    mov     r11,r0
    cmp/eq  #3,r0
    bf      tl_loop
tl_done:
    add     #24,r15
    lds.l   @r15+,pr
    mov.l   @r15+,r12
    mov.l   @r15+,r11
    mov.l   @r15+,r10
    mov.l   @r15+,r9
    rts
    mov.l   @r15+,r8

    .align 2
.Ltl_k:
    .long   0x38800000
.Ltl_16k:
    .long   0x46800000
.Ltl_dir:
    .long   _dc_light_dir_n

! dc_obj_key: M_ObjKey's hash (batcher.c), FNV-1a over the nine ints the C
! makes, then the light's floats with their low bits dropped (bits >> shift):
! the adder, l, and for TR3 the doubling, the ambient and each light's
! direction and colour by turns.
!   uint32_t dc_obj_key(const uint32_t ints[9], const M_INSTANCE_LIGHT *il,
!                       int32_t shift);
! M_INSTANCE_LIGHT: 0 l[3], 12 adder, 17 tr3 (bool), 20 tr3_double,
! 24 tr3_ambient[3], 36 tr3_count, 40 tr3_dir[3][3], 76 tr3_color[3][3].
    .macro OKH
    shld    r6,r2
    xor     r2,r0
    mul.l   r1,r0
    sts     macl,r0
    .endm
    .macro OKF off
    mov.l   @(\off,r5),r2
    OKH
    .endm

    .align 2
    .global _dc_obj_key
_dc_obj_key:
    mov.l   .Lokb,r0
    mov.l   .Lokp,r1
    neg     r6,r6                   ! shld right
    mov     #9,r3
1:  mov.l   @r4+,r2
    xor     r2,r0
    mul.l   r1,r0
    dt      r3
    bf/s    1b
    sts     macl,r0
    OKF     12
    OKF     0
    OKF     4
    OKF     8
    mov     r5,r2
    add     #17,r2
    mov.b   @r2,r2
    tst     r2,r2
    bt      9f
    OKF     20
    OKF     24
    OKF     28
    OKF     32
    mov.l   @(36,r5),r3
    cmp/pl  r3
    bf      9f
    mov     r5,r4
    add     #40,r4                  ! the directions
    mov     r5,r7
    add     #76,r7                  ! the colours
2:  mov.l   @r4+,r2
    OKH
    mov.l   @r7+,r2
    OKH
    mov.l   @r4+,r2
    OKH
    mov.l   @r7+,r2
    OKH
    mov.l   @r4+,r2
    OKH
    mov.l   @r7+,r2
    OKH
    dt      r3
    bf      2b
9:  rts
    or      #1,r0

    .align 2
.Lokb:
    .long   2166136261
.Lokp:
    .long   16777619

! void dc_occ_add(const M_OCC_ADD *b);   batcher.c DC_MeshBatcher_OcclAddRoom
! The room's painting vertices through XMTRX (loaded by the C) to cells and
! 1 / z, with the window's side bits (16: too near); then its triangles that
! face the camera and are not wholly to one side painted (M_OccTri): each
! cell whose centre a triangle covers keeps the larger of its 1 / z and the
! face's, the plane's value at the cell less half a cell's fall, no less than
! the face's farthest corner. The C is built without fmac contraction, so the
! two round alike.
! Block: 0 pos (float[3]) 4 vertices 8 tris (uint16 by threes) 12 triangles
! 16 pts (float[3]) 20 outs (uint8) 24 28 32 36 the window (cells: left top
! right bottom) 40 44 48 52 the clip (cells, ints) 56 kx 60 ky 64 bx 68 by
! 72 the buffer (float[24][32]) 76 the near z.
! Frame (r14): 0 y1.
    .text
    .align 2
    .global _dc_occ_add
_dc_occ_add:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    mov.l   r13,@-r15
    mov.l   r14,@-r15
    sts.l   pr,@-r15
    fmov.s  fr12,@-r15
    fmov.s  fr13,@-r15
    fmov.s  fr14,@-r15
    fmov.s  fr15,@-r15
    add     #-8,r15
    mov     r15,r14
    mov     r4,r8
    ! fr4 near, fr5 kx, fr6 ky, fr7 bx, fr8 by, fr9 left, fr10 top,
    ! fr11 right, fr3 bottom.
    mov     #76,r0
    fmov.s  @(r0,r8),fr4
    mov     #56,r0
    fmov.s  @(r0,r8),fr5
    add     #4,r0
    fmov.s  @(r0,r8),fr6
    add     #4,r0
    fmov.s  @(r0,r8),fr7
    add     #4,r0
    fmov.s  @(r0,r8),fr8
    mov     #24,r0
    fmov.s  @(r0,r8),fr9
    add     #4,r0
    fmov.s  @(r0,r8),fr10
    add     #4,r0
    fmov.s  @(r0,r8),fr11
    mov     #36,r0
    fmov.s  @(r0,r8),fr3
    mov.l   @(0,r8),r1
    mov.l   @(4,r8),r2
    mov.l   @(16,r8),r3
    mov.l   @(20,r8),r5
    cmp/pl  r2
    bf      oa_tris
oa_vert:
    fmov.s  @r1+,fr12
    fmov.s  @r1+,fr13
    fmov.s  @r1+,fr14
    fldi1   fr15
    ftrv    xmtrx,fv12
    fcmp/gt fr14,fr4                ! nearer than the near z
    bf      1f
    mov     #16,r0
    mov.b   r0,@r5
    add     #12,r3
    bra     2f
    add     #1,r5
1:  fldi1   fr0
    fdiv    fr14,fr0                ! w
    fmul    fr0,fr12
    fmul    fr5,fr12
    fmov    fr7,fr1
    fadd    fr12,fr1                ! x = bx + xv * w * kx
    fmul    fr0,fr13
    fmul    fr6,fr13
    fmov    fr8,fr2
    fadd    fr13,fr2                ! y
    fmov.s  fr1,@r3
    add     #4,r3
    fmov.s  fr2,@r3
    add     #4,r3
    fmov.s  fr0,@r3
    add     #4,r3
    fcmp/gt fr1,fr9                 ! x < left
    movt    r6
    fcmp/gt fr11,fr1                ! x > right
    movt    r7
    shll    r7
    or      r7,r6
    fcmp/gt fr2,fr10                ! y < top
    movt    r7
    shll2   r7
    or      r7,r6
    fcmp/gt fr3,fr2                 ! y > bottom
    movt    r7
    shll2   r7
    shll    r7
    or      r7,r6
    mov.b   r6,@r5
    add     #1,r5
2:  dt      r2
    bf      oa_vert
oa_tris:
    mov.l   @(8,r8),r9
    mov.l   @(12,r8),r10
    mov.l   @(16,r8),r11
    mov.l   @(20,r8),r12
    mov     #72,r0
    mov.l   @(r0,r8),r13
oa_tri:
    cmp/pl  r10
    bt      1f
    bra     oa_end
    nop
1:  mov.w   @r9,r0
    extu.w  r0,r0
    mov     r0,r4
    mov.b   @(r0,r12),r1
    mov.w   @(2,r9),r0
    extu.w  r0,r0
    mov     r0,r5
    mov.b   @(r0,r12),r2
    mov.w   @(4,r9),r0
    extu.w  r0,r0
    mov     r0,r6
    mov.b   @(r0,r12),r3
    mov     r1,r0
    or      r2,r0
    or      r3,r0
    tst     #16,r0
    bf      oa_next
    mov     r1,r0
    and     r2,r0
    and     r3,r0
    extu.b  r0,r0
    tst     r0,r0
    bf      oa_next
    ! The corners' points: index * 12.
    mov     r4,r0
    shll2   r4
    shll    r0
    shll2   r0
    add     r0,r4
    add     r11,r4
    mov     r5,r0
    shll2   r5
    shll    r0
    shll2   r0
    add     r0,r5
    add     r11,r5
    mov     r6,r0
    shll2   r6
    shll    r0
    shll2   r0
    add     r0,r6
    add     r11,r6
    ! Facing the camera: (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0) < 0.
    fmov.s  @r4,fr4
    fmov.s  @r5,fr5
    fmov.s  @r6,fr6
    mov     #4,r0
    fmov.s  @(r0,r4),fr1
    fmov.s  @(r0,r5),fr2
    fmov.s  @(r0,r6),fr3
    fmov    fr5,fr7
    fsub    fr4,fr7                 ! ex1
    fmov    fr2,fr8
    fsub    fr1,fr8                 ! ey1
    fmov    fr6,fr9
    fsub    fr4,fr9                 ! ex2
    fmov    fr3,fr10
    fsub    fr1,fr10                ! ey2
    fmov    fr7,fr11
    fmul    fr10,fr11
    fmov    fr9,fr0
    fmul    fr8,fr0
    fsub    fr0,fr11                ! the cross product, the det below
    fldi0   fr0
    fcmp/gt fr11,fr0
    bt      oa_paint
oa_next:
    add     #6,r9
    bra     oa_tri
    add     #-1,r10

    ! M_OccTri. fr1 fr2 fr3: the corners' y; fr4 fr5 fr6: their x;
    ! fr7 ex1, fr8 ey1, fr9 ex2, fr10 ey2, fr11 det (not 0 here).
oa_paint:
    fmov    fr3,fr0                 ! ymin = MIN(y0, MIN(y1, y2))
    fcmp/gt fr2,fr3
    bf      1f
    fmov    fr2,fr0
1:  fcmp/gt fr1,fr0
    bf      2f
    fmov    fr1,fr0
2:  fmov    fr3,fr12                ! ymax = MAX(y0, MAX(y1, y2))
    fcmp/gt fr3,fr2
    bf      3f
    fmov    fr2,fr12
3:  fcmp/gt fr12,fr1
    bf      4f
    fmov    fr1,fr12
4:  ftrc    fr0,fpul                ! y0 = MAX(clip top, ceil(ymin))
    sts     fpul,r1
    float   fpul,fr13
    fcmp/gt fr13,fr0
    movt    r0
    add     r0,r1
    mov.l   @(44,r8),r0
    cmp/gt  r1,r0
    bf      5f
    mov     r0,r1
5:  ftrc    fr12,fpul               ! y1 = MIN(clip bottom, floor(ymax))
    sts     fpul,r7
    float   fpul,fr13
    fcmp/gt fr12,fr13
    movt    r0
    sub     r0,r7
    mov.l   @(52,r8),r0
    cmp/gt  r0,r7
    bf      6f
    mov     r0,r7
6:  cmp/gt  r7,r1
    bt      oa_next
    mov.l   r7,@(0,r14)
    fldi1   fr0
    fdiv    fr11,fr0
    fmov    fr0,fr11                ! 1 / det
    mov     #8,r0
    fmov.s  @(r0,r4),fr12           ! w0
    fmov.s  @(r0,r5),fr13           ! w1
    fmov.s  @(r0,r6),fr14           ! w2
    fmov    fr14,fr15               ! w_far = MIN(w0, MIN(w1, w2))
    fcmp/gt fr13,fr14
    bf      1f
    fmov    fr13,fr15
1:  fcmp/gt fr12,fr15
    bf      2f
    fmov    fr12,fr15
2:  fmov.s  fr15,@-r15
    fsub    fr12,fr13               ! dw1
    fsub    fr12,fr14               ! dw2
    fmov    fr13,fr0
    fmul    fr10,fr0
    fmov    fr14,fr15
    fmul    fr8,fr15
    fsub    fr15,fr0
    fmul    fr11,fr0                ! gx
    fmov    fr14,fr15
    fmul    fr7,fr15
    fmul    fr9,fr13
    fsub    fr13,fr15
    fmul    fr11,fr15               ! gy
    fmov    fr0,fr13
    fabs    fr13
    fmov    fr15,fr14
    fabs    fr14
    fadd    fr14,fr13
    mov.l   .Loa_half,r2
    lds     r2,fpul
    fsts    fpul,fr14
    fmul    fr14,fr13               ! slack
    fmov    fr0,fr9                 ! w_base = w0 - gx * x0 - gy * y0 - slack
    fmul    fr4,fr9
    fsub    fr9,fr12
    fmov    fr15,fr9
    fmul    fr1,fr9
    fsub    fr9,fr12
    fsub    fr13,fr12
    fmov.s  fr0,@-r15
    fmov.s  fr15,@-r15              ! @r15 gy, 4 gx, 8 w_far
    mov     #4,r0                   ! the corners by y: a r4, b r5, c r6
    fmov.s  @(r0,r5),fr1
    fmov.s  @(r0,r4),fr2
    fcmp/gt fr1,fr2
    bf      1f
    mov     r4,r2
    mov     r5,r4
    mov     r2,r5
1:  fmov.s  @(r0,r6),fr1
    fmov.s  @(r0,r5),fr2
    fcmp/gt fr1,fr2
    bf      2f
    mov     r5,r2
    mov     r6,r5
    mov     r2,r6
    fmov.s  @(r0,r5),fr1
    fmov.s  @(r0,r4),fr2
    fcmp/gt fr1,fr2
    bf      2f
    mov     r4,r2
    mov     r5,r4
    mov     r2,r5
2:  fmov.s  @r4,fr4                 ! ax
    fmov.s  @(r0,r4),fr5            ! ay
    fmov.s  @r5,fr6                 ! bx
    fmov.s  @(r0,r5),fr7            ! by
    fmov.s  @r6,fr13                ! cx
    fmov.s  @(r0,r6),fr14           ! cy
    fmov    fr13,fr10
    fsub    fr4,fr10
    fmov    fr14,fr0
    fsub    fr5,fr0
    fdiv    fr0,fr10                ! s_ac
    fldi0   fr8
    fcmp/gt fr5,fr7
    bf      3f
    fmov    fr6,fr8
    fsub    fr4,fr8
    fmov    fr7,fr0
    fsub    fr5,fr0
    fdiv    fr0,fr8                 ! s_ab
3:  fldi0   fr3
    fcmp/gt fr7,fr14
    bf      4f
    fmov    fr13,fr3
    fsub    fr6,fr3
    fmov    fr14,fr0
    fsub    fr7,fr0
    fdiv    fr0,fr3                 ! s_bc
4:  fmov.s  @r15+,fr9               ! gy
    fmov.s  @r15+,fr15              ! gx
    fmov.s  @r15+,fr11              ! w_far
    lds     r1,fpul
    float   fpul,fr14               ! fy
    fmov    fr14,fr13
    fsub    fr5,fr13
    fmul    fr10,fr13
    fmov    fr4,fr0
    fadd    fr0,fr13                ! x_long = ax + (fy - ay) * s_ac
    fmov    fr9,fr0
    fmul    fr14,fr0
    fadd    fr0,fr12                ! w_row = w_base + gy * fy
    mov.l   @(0,r14),r7             ! y1
    mov     r1,r3
    shll2   r3
    shll2   r3
    shll2   r3
    shll    r3
    add     r13,r3                  ! the row
oa_row:
    fcmp/gt fr14,fr7                ! fy < by
    bf      1f
    fmov    fr14,fr1
    fsub    fr5,fr1
    fmul    fr8,fr1
    fmov    fr4,fr2
    bra     2f
    fadd    fr1,fr2
1:  fmov    fr14,fr1
    fsub    fr7,fr1
    fmul    fr3,fr1
    fmov    fr6,fr2
    fadd    fr1,fr2                 ! x_short
2:  fmov    fr2,fr0                 ! lo = MIN(x_long, x_short)
    fcmp/gt fr13,fr2
    bf      3f
    fmov    fr13,fr0
3:  fmov    fr2,fr1                 ! hi = MAX(x_long, x_short)
    fcmp/gt fr2,fr13
    bf      4f
    fmov    fr13,fr1
4:  ftrc    fr0,fpul                ! x0 = MAX(clip left, ceil(lo))
    sts     fpul,r2
    float   fpul,fr2
    fcmp/gt fr2,fr0
    movt    r0
    add     r0,r2
    mov.l   @(40,r8),r6
    cmp/gt  r2,r6
    bf      5f
    mov     r6,r2
5:  ftrc    fr1,fpul                ! x1 = MIN(clip right, floor(hi))
    sts     fpul,r4
    float   fpul,fr2
    fcmp/gt fr1,fr2
    movt    r0
    sub     r0,r4
    mov.l   @(48,r8),r6
    cmp/gt  r6,r4
    bf      6f
    mov     r6,r4
6:  cmp/gt  r4,r2
    bt      oa_rowend
    lds     r2,fpul
    float   fpul,fr0
    fmul    fr15,fr0
    fadd    fr12,fr0                ! cw = w_row + gx * x0
    sub     r2,r4
    add     #1,r4
    mov     r2,r5
    shll2   r5
    add     r3,r5
7:  fmov    fr11,fr1                ! MAX(cw, w_far)
    fcmp/gt fr11,fr0
    bf      8f
    fmov    fr0,fr1
8:  fmov.s  @r5,fr2
    fcmp/gt fr2,fr1
    bf      9f
    fmov.s  fr1,@r5
9:  add     #4,r5
    dt      r4
    bf/s    7b
    fadd    fr15,fr0
oa_rowend:
    fldi1   fr0
    fadd    fr0,fr14
    fadd    fr10,fr13
    fadd    fr9,fr12
    add     #64,r3
    add     #64,r3
    add     #1,r1
    cmp/gt  r7,r1
    bf      oa_row
    bra     oa_next
    nop
oa_end:
    add     #8,r15
    fmov.s  @r15+,fr15
    fmov.s  @r15+,fr14
    fmov.s  @r15+,fr13
    fmov.s  @r15+,fr12
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
.Loa_half:
    .long   0x3f000000

! void dc_ui_corners(const M_UI_CORNERS *b);   ui.c StageSprite / StageQuad
! The four corners of an interface sprite or quad (top left, top right,
! bottom left, bottom right) to the PowerVR's vertices: the rectangle's
! integer corners times the screen's scale, the depth worked out once, the
! texture's corners (sprites), and the colours packed to ARGB: a sprite's
! floats clamped to 0..1 and times 255 (M_PackF, its colours round the corners
! tl tr br bl), a quad's bytes as they are (M_Pack8).
! Block: 0 out (DC_PVR_VERTEX[4], 28 bytes each) 4 the rectangle (int32 x0
! y0 x1 y1) 8 the colours 12 mode (0 sprite, 1 quad) 16 sx 20 sy 24 z 28 u0
! 32 v0 36 u1 40 v1.
    .macro UIC_PACKF off
    mov     r7,r2
    add     #\off,r2
    mov     #0,r3
    mov     #4,r1                   ! r g b a: shifted in, a last on top
1:  fmov.s  @r2+,fr8
    fldi0   fr9
    fcmp/gt fr8,fr9                 ! below 0
    bf      2f
    bra     3f
    fmov    fr9,fr8
2:  fldi1   fr9
    fcmp/gt fr9,fr8                 ! past 1
    bf      3f
    fmov    fr9,fr8
3:  fmul    fr11,fr8
    ftrc    fr8,fpul
    sts     fpul,r0
    shll8   r3
    dt      r1
    bf/s    1b
    or      r0,r3
    ! r3 = r << 24 | g << 16 | b << 8 | a: to a r g b.
    mov     r3,r0
    shlr8   r0                      ! r g b
    shll16  r3
    shll8   r3                      ! a, on top
    or      r3,r0
    .endm

    .macro UIC_PACK8 off
    mov.l   @(\off,r7),r3           ! a b g r, the bytes r g b a
    mov.l   .Luic_ag,r1
    and     r3,r1                   ! a and g in place
    mov     r3,r0
    shll16  r0
    mov.l   .Luic_r,r2
    and     r2,r0                   ! r, to bits 16-23
    or      r0,r1
    mov     r3,r0
    shlr16  r0
    extu.b  r0,r0                   ! b, to the bottom
    or      r1,r0
    .endm

    .macro UIC_CORNER xr, yr, ur, vr, soff, qoff
    fmov.s  \xr,@r5
    add     #4,r5
    fmov.s  \yr,@r5
    add     #4,r5
    fmov.s  fr10,@r5
    add     #4,r5
    tst     r6,r6
    bf      4f
    fmov.s  \ur,@r5
    add     #4,r5
    fmov.s  \vr,@r5
    add     #4,r5
    UIC_PACKF \soff
    bra     5f
    nop
4:  add     #8,r5
    UIC_PACK8 \qoff
5:  mov.l   r0,@r5
    add     #8,r5
    .endm

    .text
    .align 2
    .global _dc_ui_corners
_dc_ui_corners:
    mov.l   @(0,r4),r5
    mov.l   @(4,r4),r1
    mov.l   @(8,r4),r7
    mov.l   @(12,r4),r6
    mov     r4,r0
    add     #16,r0
    fmov.s  @r0+,fr8                ! sx
    fmov.s  @r0+,fr9                ! sy
    fmov.s  @r0+,fr10               ! z
    fmov.s  @r0+,fr4                ! u0
    fmov.s  @r0+,fr5                ! v0
    fmov.s  @r0+,fr6                ! u1
    fmov.s  @r0+,fr7                ! v1
    mov.l   @r1,r2
    lds     r2,fpul
    float   fpul,fr0
    fmul    fr8,fr0                 ! x0
    mov.l   @(8,r1),r2
    lds     r2,fpul
    float   fpul,fr1
    fmul    fr8,fr1                 ! x1
    mov.l   @(4,r1),r2
    lds     r2,fpul
    float   fpul,fr2
    fmul    fr9,fr2                 ! y0
    mov.l   @(12,r1),r2
    lds     r2,fpul
    float   fpul,fr3
    fmul    fr9,fr3                 ! y1
    mov.l   .Luic_255,r2
    lds     r2,fpul
    fsts    fpul,fr11
    UIC_CORNER fr0, fr2, fr4, fr5, 0, 0
    UIC_CORNER fr1, fr2, fr6, fr5, 16, 4
    UIC_CORNER fr0, fr3, fr4, fr7, 48, 8
    UIC_CORNER fr1, fr3, fr6, fr7, 32, 12
    rts
    nop

    .align 2
.Luic_255:
    .long   0x437f0000
.Luic_ag:
    .long   0xff00ff00
.Luic_r:
    .long   0x00ff0000
