#include <trx/game/output/utils.h>

#include <trx/game/const.h>
#include <trx/core/log.h>

#include <string.h>

#if defined(__DREAMCAST__)
// In assembly (render/inst_sh4.S, dc_fill_matrix); TRX_DC_ASMOFF3 bit 65536
// turns it off. The C below is its reference.
extern void dc_fill_matrix(const MATRIX *m, float out[16]);
extern uint32_t g_DC_AsmOff3;
static void M_FillMatrixC(GLfloat m[4][4], const MATRIX *source);

void Output_FillMatrix(GLfloat m[4][4], const MATRIX *const source)
{
    if ((g_DC_AsmOff3 & 65536) != 0) {
        M_FillMatrixC(m, source);
        return;
    }
    dc_fill_matrix(source, &m[0][0]);
    #if defined(DC_EMIT_VERIFY)
    GLfloat ref[4][4];
    M_FillMatrixC(ref, source);
    static uint32_t runs;
    static uint32_t bad;
    if (memcmp(ref, m, sizeof(ref)) != 0 && bad++ < 6) {
        LOG_WARNING(
            "[fillmat-verify] MISMATCH %08x %08x", *(const uint32_t *)&ref[3][0],
            *(const uint32_t *)&m[3][0]);
    }
    if (++runs % 20000 == 0) {
        LOG_WARNING(
            "[fillmat-verify] %u runs, %u bad", (unsigned)runs, (unsigned)bad);
    }
    #endif
}

static void M_FillMatrixC(GLfloat m[4][4], const MATRIX *const source)
#else
void Output_FillMatrix(GLfloat m[4][4], const MATRIX *const source)
#endif
{
#if defined(__DREAMCAST__)
    const float k = 1.0f / (float)(1 << W2V_SHIFT);
    const int64_t *const rows[3][4] = {
        { &source->_00, &source->_01, &source->_02, &source->_03 },
        { &source->_10, &source->_11, &source->_12, &source->_13 },
        { &source->_20, &source->_21, &source->_22, &source->_23 },
    };
    for (int32_t c = 0; c < 4; c++) {
        for (int32_t r = 0; r < 3; r++) {
            m[c][r] = Output_MatrixToFloat(*rows[r][c]) * k;
        }
        m[c][3] = c == 3 ? 1.0f : 0.0f;
    }
    return;
#endif
    m[0][0] = source->_00 / (float)(1 << W2V_SHIFT);
    m[0][1] = source->_10 / (float)(1 << W2V_SHIFT);
    m[0][2] = source->_20 / (float)(1 << W2V_SHIFT);
    m[0][3] = 0.0;

    m[1][0] = source->_01 / (float)(1 << W2V_SHIFT);
    m[1][1] = source->_11 / (float)(1 << W2V_SHIFT);
    m[1][2] = source->_21 / (float)(1 << W2V_SHIFT);
    m[1][3] = 0.0;

    m[2][0] = source->_02 / (float)(1 << W2V_SHIFT);
    m[2][1] = source->_12 / (float)(1 << W2V_SHIFT);
    m[2][2] = source->_22 / (float)(1 << W2V_SHIFT);
    m[2][3] = 0.0;

    m[3][0] = source->_03 / (float)(1 << W2V_SHIFT);
    m[3][1] = source->_13 / (float)(1 << W2V_SHIFT);
    m[3][2] = source->_23 / (float)(1 << W2V_SHIFT);
    m[3][3] = 1.0;
}
