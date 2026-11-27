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
