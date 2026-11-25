// Compresses the texture pages of TR2 levels and TRX injections to the
// PowerVR's VQ format, for the disc (make_disc.sh), and with -b makes the
// relief of the level pages the rooms use. Built and run on the host:
//
//   texvq [-b <output.bump>] <output.vq> <file.tr2|file.bin>...
//
// Each ARGB1555 page becomes a 2 KiB codebook of 256 2x2 texel blocks and a
// byte per block (18 KiB instead of 128). The game finds a page here by the
// FNV-1a hash of its 16-bit texels as it holds them once the level is loaded
// (DC_PVR_CommitTexturePages); pages changed after that (texture edits) and
// ARGB4444 pages (soft-edged, which VQ would smear) have no entry and stay as
// they are.
//
// Output: "TVQ2", u32 count, count x {u32 hash, u32 position}, sorted by
// hash, then count x 23896 bytes (the codebook, the indices of the mipmaps
// from 1x1 up to 128x128, the page's own, two bytes of padding; a page with
// no mipmaps would take 18432) in the order the pages first came (a
// level's pages one after another, so that loading it reads ahead, not back
// and forth); position counts those.
//
// texvq -l <levelpages.bin> <level.tr2>... writes the hashes of each level's
// pages, so that the game skips reading them (sections/textures.c).
//
// texvq -s <textures.vq> <outdir> <file.bin>... rewrites the injections whose
// pages are all in that file: each ARGB1555 page's 256 KiB of RGBA and 64 KiB
// of indices become its hash (the game's key into the file), so that the load
// neither inflates, reads nor encodes the page (IDT_DC_PAGE_HASHES). The new
// files go to <outdir>; one with nothing to change is not written.
//
// The relief (pvr.c's M_LoadDiscBumps): "TBP2" and the same index, then
// count x 131072 bytes (256 by 256 texels; "TBP1" was 128 by 128, 32768), each page's relief as the PowerVR takes it (bump
// texels, twiddled), made by bump_page.h as the game makes it, only for the
// pages some room face uses (the game puts relief on room faces only).

#define DC_BUMP_SIZE 256
#include "../../src/platform/dreamcast/render/bump_page.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define PAGE_W 256
#define PAGE_SIZE (PAGE_W * PAGE_W)
#define BLOCKS (PAGE_SIZE / 4)
#define CODES 256
#define DIMS 16
#define VQ_BYTES (CODES * 8 + BLOCKS)
#define ITERATIONS 16
// The indices of the levels under the whole page (1x1 to 128x128), which the
// PowerVR keeps in front of the whole page's: 1 + 1 + 4 + ... + 4096.
#define MIP_BYTES 5462
// A page on the disc: the codebook, the small levels, the whole page, and two
// bytes so that the next starts on a word.
#define MIP_PAGE_BYTES (CODES * 8 + MIP_BYTES + BLOCKS + 2)
// Alpha weighs more than colour: a cut-out edge that moves shows more than a
// shade that does.
#define ALPHA_WEIGHT 4.0f

typedef struct {
    uint32_t hash;
    uint32_t format;
    uint16_t texels[PAGE_SIZE];
    uint8_t vq[VQ_BYTES];
    uint8_t mip[MIP_BYTES];
} PAGE;

static PAGE *m_Pages = NULL;
static int m_Count = 0;

#define BUMP_TEXELS (DC_BUMP_SIZE * DC_BUMP_SIZE)
#define MAX_TEXTURES 32768

typedef struct {
    uint32_t hash;
    uint16_t texels[BUMP_TEXELS];
} BUMP;

static const char *m_BumpPath = NULL;
// The relief's detail filter (bump_page.h), off with -d0 to make the plain one
// for a comparison; and the zones to flatten or tune (-o <file>), by level
// and page, in page pixels: "flat <level> <page> x0 y0 x1 y1" lies flat (a
// painted sign, a face), "scale <level> <page> x0 y0 x1 y1 <factor>" gives the
// relief of the zone that factor (negative turns it over: a bump that is
// dark). A level is its file's name without the extension, in lower case.
static int m_NoDetail = 0;
// -D <dir>: each page's relief as pictures (PGM: the colour's brightness, the
// heights the relief is made from, the slope's length) to see what it follows.
static const char *m_DumpDir = NULL;
// -P <dir>: each page of the levels as a PNG (RGBA, 256 by 256) named by the
// hash the game keys pages by, to make a relief of one's own.
static const char *m_PngDir = NULL;
// -C <dir>: a relief made by hand, <hash>.png in the dir, for the page of that
// hash (the names -P gives): a grey picture is its heights (white high), an
// RGB one its normals (blue up; green up as OpenGL's, or down with
// TEXVQ_NORMAL_FLIP=1 as DirectX's; TEXVQ_NORMAL_SCALE the lean's strength).
// Any size: it is brought to 256 by 256.
static const char *m_CustomDir = NULL;
static int m_CustomCount = 0;
static uint8_t *ReadFile(const char *const path, size_t *const size);

// An 8 or 16 bit PNG, not interlaced, grey, RGB, grey+alpha or RGBA, as 8-bit
// samples a pixel by channels; NULL when it is not one.
static uint8_t *ReadPng(const char *const path, int *const w, int *const h, int *const ch)
{
    size_t size;
    uint8_t *const data = ReadFile(path, &size);
    if (data == NULL || size < 33 || memcmp(data, "\x89PNG\r\n\x1a\n", 8) != 0) {
        free(data);
        return NULL;
    }
    int depth = 0;
    int color = -1;
    int interlace = 1;
    uint8_t *idat = malloc(size);
    size_t idat_size = 0;
    size_t at = 8;
    while (at + 12 <= size) {
        const uint32_t len = ((uint32_t)data[at] << 24) | ((uint32_t)data[at + 1] << 16)
            | ((uint32_t)data[at + 2] << 8) | data[at + 3];
        const uint8_t *const type = data + at + 4;
        const uint8_t *const body = data + at + 8;
        if (at + 12 + len > size) {
            break;
        }
        if (memcmp(type, "IHDR", 4) == 0 && len >= 13) {
            *w = (int)(((uint32_t)body[0] << 24) | (body[1] << 16) | (body[2] << 8) | body[3]);
            *h = (int)(((uint32_t)body[4] << 24) | (body[5] << 16) | (body[6] << 8) | body[7]);
            depth = body[8];
            color = body[9];
            interlace = body[12];
        } else if (memcmp(type, "IDAT", 4) == 0) {
            memcpy(idat + idat_size, body, len);
            idat_size += len;
        }
        at += 12 + len;
    }
    const int channels = color == 0 ? 1 : color == 2 ? 3 : color == 4 ? 2 : color == 6 ? 4 : 0;
    if (channels == 0 || interlace != 0 || (depth != 8 && depth != 16) || *w <= 0
        || *h <= 0 || *w > 8192 || *h > 8192) {
        free(idat);
        free(data);
        return NULL;
    }
    const int bpp = channels * depth / 8;
    const size_t row = (size_t)*w * bpp;
    uLongf raw_size = (row + 1) * (size_t)*h;
    uint8_t *const raw = malloc(raw_size);
    if (uncompress(raw, &raw_size, idat, (uLong)idat_size) != Z_OK
        || raw_size < (row + 1) * (size_t)*h) {
        free(raw);
        free(idat);
        free(data);
        return NULL;
    }
    // The filters, in place: each row on the one above it.
    for (int y = 0; y < *h; y++) {
        uint8_t *const cur = raw + (row + 1) * y + 1;
        const uint8_t *const up = y > 0 ? raw + (row + 1) * (y - 1) + 1 : NULL;
        const int f = raw[(row + 1) * y];
        for (size_t i = 0; i < row; i++) {
            const int a = i >= (size_t)bpp ? cur[i - bpp] : 0;
            const int b = up != NULL ? up[i] : 0;
            const int c = (up != NULL && i >= (size_t)bpp) ? up[i - bpp] : 0;
            int pred = 0;
            if (f == 1) {
                pred = a;
            } else if (f == 2) {
                pred = b;
            } else if (f == 3) {
                pred = (a + b) / 2;
            } else if (f == 4) {
                const int p = a + b - c;
                const int pa = abs(p - a);
                const int pb = abs(p - b);
                const int pc = abs(p - c);
                pred = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
            }
            cur[i] = (uint8_t)(cur[i] + pred);
        }
    }
    uint8_t *const out = malloc((size_t)*w * *h * channels);
    for (int y = 0; y < *h; y++) {
        const uint8_t *const cur = raw + (row + 1) * y + 1;
        for (int x = 0; x < *w * channels; x++) {
            out[(size_t)y * *w * channels + x] = depth == 16 ? cur[x * 2] : cur[x];
        }
    }
    *ch = channels;
    free(raw);
    free(idat);
    free(data);
    return out;
}

// One channel of an image as a 256 by 256 picture: a box mean down, a
// bilinear up.
static void Resample256(
    const uint8_t *const src, const int w, const int h, const int ch,
    const int channel, float *const dst)
{
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            const float fx0 = (float)x * w / 256.0f;
            const float fx1 = (float)(x + 1) * w / 256.0f;
            const float fy0 = (float)y * h / 256.0f;
            const float fy1 = (float)(y + 1) * h / 256.0f;
            float sum = 0.0f;
            float wsum = 0.0f;
            for (int sy = (int)fy0; sy < (int)ceilf(fy1) && sy < h; sy++) {
                for (int sx = (int)fx0; sx < (int)ceilf(fx1) && sx < w; sx++) {
                    const float ox = fminf((float)sx + 1.0f, fx1) - fmaxf((float)sx, fx0);
                    const float oy = fminf((float)sy + 1.0f, fy1) - fmaxf((float)sy, fy0);
                    const float wt = ox * oy;
                    if (wt > 0.0f) {
                        sum += wt * src[((size_t)sy * w + sx) * ch + channel];
                        wsum += wt;
                    }
                }
            }
            dst[y * 256 + x] = wsum > 0.0f ? sum / wsum : 128.0f;
        }
    }
}

static void PngChunk(
    FILE *const f, const char *const type, const uint8_t *const data,
    const uint32_t size)
{
    uint8_t head[8] = { (uint8_t)(size >> 24), (uint8_t)(size >> 16),
                        (uint8_t)(size >> 8), (uint8_t)size };
    memcpy(head + 4, type, 4);
    fwrite(head, 1, 8, f);
    uLong crc = crc32(0L, (const Bytef *)type, 4);
    if (size > 0) {
        fwrite(data, 1, size, f);
        crc = crc32(crc, data, size);
    }
    const uint8_t tail[4] = { (uint8_t)(crc >> 24), (uint8_t)(crc >> 16),
                              (uint8_t)(crc >> 8), (uint8_t)crc };
    fwrite(tail, 1, 4, f);
}

// A 16-bit 1555 page as a PNG, the see-through texels with no alpha.
static void WritePagePng(const char *const path, const uint16_t *const texels)
{
    const int w = 256;
    const size_t row = 1 + (size_t)w * 4;
    uint8_t *const raw = malloc(row * w);
    for (int y = 0; y < w; y++) {
        uint8_t *const out = raw + row * y;
        out[0] = 0;
        for (int x = 0; x < w; x++) {
            const uint16_t t = texels[y * 256 + x];
            const int r = (t >> 10) & 31;
            const int g = (t >> 5) & 31;
            const int b = t & 31;
            out[1 + x * 4] = (uint8_t)((r << 3) | (r >> 2));
            out[2 + x * 4] = (uint8_t)((g << 3) | (g >> 2));
            out[3 + x * 4] = (uint8_t)((b << 3) | (b >> 2));
            out[4 + x * 4] = (t & 0x8000) != 0 ? 255 : 0;
        }
    }
    uLongf zsize = compressBound((uLong)(row * w));
    uint8_t *const z = malloc(zsize);
    if (compress2(z, &zsize, raw, (uLong)(row * w), 6) == Z_OK) {
        FILE *const f = fopen(path, "wb");
        if (f != NULL) {
            static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
            fwrite(sig, 1, 8, f);
            const uint8_t ihdr[13] = { 0, 0, 1, 0, 0, 0, 1, 0, 8, 6, 0, 0, 0 };
            PngChunk(f, "IHDR", ihdr, 13);
            PngChunk(f, "IDAT", z, (uint32_t)zsize);
            PngChunk(f, "IEND", NULL, 0);
            fclose(f);
        }
    }
    free(z);
    free(raw);
}
typedef struct {
    int flat;
    char level[64];
    int page;
    int x0, y0, x1, y1;
    float factor;
} OVERRIDE;
#define MAX_OVERRIDES 512
static OVERRIDE m_Over[MAX_OVERRIDES];
static int m_OverCount = 0;
static char m_LevelName[64] = "";

static void LoadOverrides(const char *const path)
{
    FILE *const fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "texvq: cannot read %s\n", path);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL && m_OverCount < MAX_OVERRIDES) {
        char kind[16];
        OVERRIDE o = { 0 };
        o.factor = 1.0f;
        const int got = sscanf(
            line, "%15s %63s %d %d %d %d %d %f", kind, o.level, &o.page, &o.x0,
            &o.y0, &o.x1, &o.y1, &o.factor);
        if (got >= 7 && strcmp(kind, "flat") == 0) {
            o.flat = 1;
            m_Over[m_OverCount++] = o;
        } else if (got >= 8 && strcmp(kind, "scale") == 0) {
            m_Over[m_OverCount++] = o;
        }
    }
    fclose(fp);
}
static BUMP *m_Bumps = NULL;
static int m_BumpCount = 0;

// An object texture as the game holds it once the level is loaded.
typedef struct {
    uint16_t page;
    uint16_t draw;
    uint16_t u[4];
    uint16_t v[4];
    int corners;
} TEXTURE;

// What the relief needs of a level: which textures its rooms use (1 on a
// quad, 2 on a triangle) and the object textures.
typedef struct {
    uint8_t used[MAX_TEXTURES];
    TEXTURE *textures;
    int32_t count;
    // The pages sprites use (a bit a page, the first 256).
} LEVEL;

static LEVEL m_Level;

// FNV-1a a 32-bit word at a time (little-endian, as on the Dreamcast).
static uint32_t Hash(const void *const data, const size_t size)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i += 4) {
        uint32_t w;
        memcpy(&w, (const uint8_t *)data + i, 4);
        h = (h ^ w) * 16777619u;
    }
    return h;
}

static int AddPage(const uint16_t *const texels, const uint32_t format)
{
    const uint32_t hash = Hash(texels, PAGE_SIZE * 2);
    for (int i = 0; i < m_Count; i++) {
        if (m_Pages[i].hash == hash && m_Pages[i].format == format) {
            return i;
        }
    }
    m_Pages = realloc(m_Pages, sizeof(PAGE) * (m_Count + 1));
    PAGE *const page = &m_Pages[m_Count++];
    page->hash = hash;
    page->format = format;
    memcpy(page->texels, texels, PAGE_SIZE * 2);
    return m_Count - 1;
}

// As texture_pages_16.h does it.
static uint16_t ToARGB1555(const uint8_t *const c)
{
    return (c[3] >= 128 ? 0x8000 : 0) | ((c[0] >> 3) << 10)
        | ((c[1] >> 3) << 5) | (c[2] >> 3);
}

static uint16_t ToARGB4444(const uint8_t *const c)
{
    return ((c[3] >> 4) << 12) | ((c[0] >> 4) << 8) | ((c[1] >> 4) << 4)
        | (c[2] >> 4);
}

static void AddRGBAPage(const uint8_t *const rgba)
{
    uint32_t format = 0;
    for (int i = 0; i < PAGE_SIZE; i++) {
        if (rgba[i * 4 + 3] != 0 && rgba[i * 4 + 3] != 255) {
            format = 1;
            break;
        }
    }
    static uint16_t texels[PAGE_SIZE];
    for (int i = 0; i < PAGE_SIZE; i++) {
        texels[i] = format ? ToARGB4444(&rgba[i * 4]) : ToARGB1555(&rgba[i * 4]);
    }
    AddPage(texels, format);
}

static uint8_t *ReadFile(const char *const path, size_t *const size)
{
    FILE *const fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    *size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *const data = malloc(*size);
    if (fread(data, 1, *size, fp) != *size) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    return data;
}

static int32_t S32(const uint8_t *const p)
{
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}

// A level read from the start, as level/format/format_tr*.c walk it.
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t at;
    int bad;
} CURSOR;

static int32_t CursorS32(CURSOR *const c)
{
    if (c->at + 4 > c->size) {
        c->bad = 1;
        return 0;
    }
    const int32_t v = S32(c->data + c->at);
    c->at += 4;
    return v;
}

static int32_t CursorU16(CURSOR *const c)
{
    if (c->at + 2 > c->size) {
        c->bad = 1;
        return 0;
    }
    const int32_t v = c->data[c->at] | (c->data[c->at + 1] << 8);
    c->at += 2;
    return v;
}

static void CursorSkip(CURSOR *const c, const int64_t bytes)
{
    if (bytes < 0 || c->at + (uint64_t)bytes > c->size) {
        c->bad = 1;
        return;
    }
    c->at += (size_t)bytes;
}

// A count of s32, then that many items of the size.
static void SkipArrS32(CURSOR *const c, const int64_t item)
{
    CursorSkip(c, (int64_t)CursorS32(c) * item);
}

static void SkipArrU16(CURSOR *const c, const int64_t item)
{
    CursorSkip(c, (int64_t)CursorU16(c) * item);
}

// The sprite textures (16 bytes each), skipped.
static void ReadSprites(CURSOR *const c)
{
    const int32_t count = CursorS32(c);
    CursorSkip(c, (int64_t)count * 16);
}

// The rooms, noting the textures their faces use (sections/rooms.c): each
// room's mesh is vertices (8 bytes in TR1, 12 after), quads and triangles
// (their vertices and a texture, whose top bit is double-sidedness), and
// sprites.
static void ReadRooms(CURSOR *const c, const int version)
{
    memset(m_Level.used, 0, sizeof(m_Level.used));
    const int32_t rooms = CursorU16(c);
    for (int32_t i = 0; i < rooms && !c->bad; i++) {
        CursorSkip(c, 16);
        const int64_t mesh_size = (int64_t)CursorS32(c) * 2;
        if (mesh_size < 0 || c->at + (uint64_t)mesh_size > c->size) {
            c->bad = 1;
            return;
        }
        CURSOR mesh = { c->data, c->at + (size_t)mesh_size, c->at, 0 };
        CursorSkip(&mesh, (int64_t)CursorU16(&mesh) * (version == 1 ? 8 : 12));
        for (int corners = 4; corners >= 3; corners--) {
            const int32_t faces = CursorU16(&mesh);
            for (int32_t j = 0; j < faces && !mesh.bad; j++) {
                CursorSkip(&mesh, corners * 2);
                m_Level.used[CursorU16(&mesh) & 0x7FFF] |= corners == 4 ? 1 : 2;
            }
        }
        c->bad |= mesh.bad;
        CursorSkip(c, mesh_size);
        SkipArrU16(c, 32); // portals
        const int32_t size_z = (int16_t)CursorU16(c);
        const int32_t size_x = (int16_t)CursorU16(c);
        CursorSkip(c, (int64_t)size_z * size_x * 8); // sectors
        if (version == 1) {
            CursorSkip(c, 2);
            SkipArrU16(c, 18); // lights
            SkipArrU16(c, 18); // static meshes
            CursorSkip(c, 4);
        } else {
            CursorSkip(c, version == 2 ? 6 : 4);
            SkipArrU16(c, 24); // lights
            SkipArrU16(c, 20); // static meshes
            CursorSkip(c, version == 2 ? 4 : 7);
        }
    }
}

// As sections/textures.c's M_DecodeTR3ObjectTextureUVs.
static void DecodeTR3UVs(int16_t *const uv)
{
    uint8_t flags = 0;
    for (int i = 0; i < 8; i++) {
        if ((uv[i] & 0x80) != 0) {
            uv[i] |= 0x00FF;
            flags |= 1 << i;
        } else {
            uv[i] &= 0xFF00;
        }
    }
    for (int i = 0; i < 8; i++) {
        uv[i] += (flags & 1) != 0 ? -256 : 256;
        flags >>= 1;
    }
}

// The object textures (sections/textures.c). A texture is a triangle's
// (three corners) when a room triangle uses it, as finalize/render_assets.c
// makes it, or when its fourth corner is nil, as the triangles of the
// objects' meshes, which are not read here, leave it.
// shortcut: a texture with a fourth corner that only an object mesh's
// triangle uses keeps four corners here (the game drops the fourth); read the
// meshes if a relief ever looks off at such a texture's edge.
static void ReadTextures(CURSOR *const c, const int version)
{
    const int32_t count = CursorS32(c);
    if (count < 0 || count > MAX_TEXTURES
        || c->at + (uint64_t)count * 20 > c->size) {
        c->bad = 1;
        return;
    }
    m_Level.textures = realloc(m_Level.textures, sizeof(TEXTURE) * (count + 1));
    m_Level.count = count;
    for (int32_t i = 0; i < count; i++) {
        TEXTURE *const texture = &m_Level.textures[i];
        texture->draw = (uint16_t)CursorU16(c);
        texture->page = (uint16_t)CursorU16(c) & 0x7FFF;
        int16_t uv[8];
        for (int k = 0; k < 8; k++) {
            uv[k] = (int16_t)CursorU16(c);
        }
        texture->corners =
            (m_Level.used[i] & 2) != 0 || (uv[6] == 0 && uv[7] == 0) ? 3 : 4;
        if (version == 3) {
            DecodeTR3UVs(uv);
        }
        for (int k = 0; k < 4; k++) {
            texture->u[k] = (uint16_t)uv[k * 2];
            texture->v[k] = (uint16_t)uv[k * 2 + 1];
        }
    }
}

static int Twiddle(int x, int y);

// The relief of one of the level's pages, if its rooms use it and no page
// alike has one yet (by the hash the game keys pages by), as pvr.c's
// M_GetBumpPage makes it.
// shortcut: a page alike in two levels gets the relief of the first, made
// with that level's textures; key by the textures too if they ever differ.
static void AddBump(const int32_t page, const uint16_t *const texels)
{
    if (m_BumpPath == NULL) {
        return;
    }
    // Every page that has a texture of the level, not only those a room's own
    // faces use: the relief goes on the static meshes the room's lights reach,
    // too, and the game made those pages in play, a stall of a frame each.
    int used = 0;
    for (int32_t i = 0; i < m_Level.count && !used; i++) {
        used = m_Level.textures[i].page == page;
    }
    const uint32_t hash = Hash(texels, PAGE_SIZE * 2);
    for (int i = 0; used && i < m_BumpCount; i++) {
        used = m_Bumps[i].hash != hash;
    }
    if (!used) {
        return;
    }
    if (m_PngDir != NULL) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%08x.png", m_PngDir, (unsigned)hash);
        WritePagePng(path, texels);
    }
    static uint8_t height[BUMP_TEXELS];
    static uint16_t owner[BUMP_TEXELS];
    int32_t lo = 255;
    int32_t hi = 0;
    for (int y = 0; y < DC_BUMP_SIZE; y++) {
        for (int x = 0; x < DC_BUMP_SIZE; x++) {
            int32_t luma;
            // A relief texel for each of the page's: its own brightness.
            const uint8_t h =
                DC_Bump_Height(texels[y * PAGE_W + x], false, &luma);
            height[y * DC_BUMP_SIZE + x] = h;
            if (h != 0) {
                lo = luma < lo ? luma : lo;
                hi = luma > hi ? luma : hi;
            }
        }
    }
    DC_Bump_Stretch(height, lo, hi);
    memset(owner, 0, sizeof(owner));
    for (int32_t i = 0; i < m_Level.count; i++) {
        const TEXTURE *const texture = &m_Level.textures[i];
        if (texture->page == page) {
            DC_Bump_Own(
                owner, (uint16_t)(i + 1), texture->u, texture->v,
                texture->corners);
        }
    }
    // A relief made by hand, if there is one for this page.
    static float custom[3][BUMP_TEXELS];
    int custom_kind = 0; // 1 heights, 2 normals
    if (m_CustomDir != NULL) {
        char cpath[512];
        snprintf(cpath, sizeof(cpath), "%s/%08x.png", m_CustomDir, (unsigned)hash);
        int cw = 0;
        int chh = 0;
        int cch = 0;
        uint8_t *const img = ReadPng(cpath, &cw, &chh, &cch);
        if (img != NULL) {
            if (cch >= 3) {
                for (int k = 0; k < 3; k++) {
                    Resample256(img, cw, chh, cch, k, custom[k]);
                }
                double mr = 0, mg = 0, mb = 0;
                for (int i = 0; i < BUMP_TEXELS; i++) {
                    mr += custom[0][i];
                    mg += custom[1][i];
                    mb += custom[2][i];
                }
                mr /= BUMP_TEXELS;
                mg /= BUMP_TEXELS;
                mb /= BUMP_TEXELS;
                // Normals are blue (up) with red and green about the middle; a grey
                // picture (all three alike), however bright, is heights.
                custom_kind = (mb > 150 && mb - fmax(mr, mg) > 40 && fabs(mr - 128) < 40
                               && fabs(mg - 128) < 40)
                    ? 2
                    : 1;
                if (custom_kind == 1) {
                    for (int i = 0; i < BUMP_TEXELS; i++) {
                        custom[0][i] = 0.299f * custom[0][i] + 0.587f * custom[1][i]
                            + 0.114f * custom[2][i];
                    }
                }
            } else {
                Resample256(img, cw, chh, cch, 0, custom[0]);
                custom_kind = 1;
            }
            free(img);
            m_CustomCount++;
        }
    }
    if (custom_kind == 1) {
        for (int i = 0; i < BUMP_TEXELS; i++) {
            if (height[i] != 0) {
                const float v = custom[0][i];
                height[i] = (uint8_t)(v < 1.0f ? 1 : v > 255.0f ? 255 : (int)(v + 0.5f));
            }
        }
    }
    DC_BumpCoherenceOn = custom_kind == 0;
    if (!m_NoDetail && custom_kind == 0) {
        static uint8_t scratch[DC_BUMP_DETAIL_SCRATCH];
        DC_Bump_Detail(height, owner, scratch);
    }
    static uint8_t flat[BUMP_TEXELS];
    memset(flat, 0, sizeof(flat));
    for (int k = 0; k < m_OverCount; k++) {
        const OVERRIDE *const o = &m_Over[k];
        if (o->page != page || strcmp(o->level, m_LevelName) != 0) {
            continue;
        }
        const int x0 = o->x0 < 0 ? 0 : o->x0;
        const int y0 = o->y0 < 0 ? 0 : o->y0;
        const int x1 = o->x1 >= DC_BUMP_SIZE ? DC_BUMP_SIZE - 1 : o->x1;
        const int y1 = o->y1 >= DC_BUMP_SIZE ? DC_BUMP_SIZE - 1 : o->y1;
        int sum = 0;
        int n = 0;
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                if (height[y * DC_BUMP_SIZE + x] != 0) {
                    sum += height[y * DC_BUMP_SIZE + x];
                    n++;
                }
            }
        }
        const float mean = n > 0 ? (float)sum / n : 128.0f;
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                const int at = y * DC_BUMP_SIZE + x;
                if (o->flat) {
                    flat[at] = 1;
                } else if (height[at] != 0) {
                    float h = mean + o->factor * ((float)height[at] - mean);
                    h = h < 1.0f ? 1.0f : h > 255.0f ? 255.0f : h;
                    height[at] = (uint8_t)(h + 0.5f);
                }
            }
        }
    }
    static float lean[2 * BUMP_TEXELS];
    DC_Bump_Slopes(height, owner, lean);
    if (custom_kind == 2) {
        const char *const flip_env = getenv("TEXVQ_NORMAL_FLIP");
        const char *const scale_env = getenv("TEXVQ_NORMAL_SCALE");
        const int flip = flip_env != NULL && flip_env[0] == '1';
        const float scale = scale_env != NULL ? (float)atof(scale_env) : 1.0f;
        for (int i = 0; i < BUMP_TEXELS; i++) {
            lean[2 * i] = 0.0f;
            lean[2 * i + 1] = 0.0f;
            if (height[i] == 0) {
                continue;
            }
            const float nx = custom[0][i] / 127.5f - 1.0f;
            const float ny = custom[1][i] / 127.5f - 1.0f;
            float nz = custom[2][i] / 127.5f - 1.0f;
            nz = nz < 0.15f ? 0.15f : nz;
            float sx = nx / nz * scale;
            float sy = (flip ? ny : -ny) / nz * scale;
            const float len = sqrtf(sx * sx + sy * sy);
            if (len > 1.0e-6f) {
                const float k = DC_BUMP_LEAN_CAP * tanhf(len / DC_BUMP_LEAN_CAP) / len;
                sx *= k;
                sy *= k;
            }
            lean[2 * i] = sx;
            lean[2 * i + 1] = sy;
        }
    }
    DC_BumpCoherenceOn = 1;
    if (m_DumpDir != NULL) {
        static uint8_t pic[BUMP_TEXELS];
        const char *const names[3] = { "luma", "height", "lean" };
        for (int k = 0; k < 3; k++) {
            for (int y = 0; y < DC_BUMP_SIZE; y++) {
                for (int x = 0; x < DC_BUMP_SIZE; x++) {
                    const int at = y * DC_BUMP_SIZE + x;
                    float v;
                    if (k == 0) {
                        int32_t luma;
                        DC_Bump_Height(texels[y * PAGE_W + x], false, &luma);
                        v = (float)luma;
                    } else if (k == 1) {
                        v = (float)height[at];
                    } else {
                        v = sqrtf(lean[2 * at] * lean[2 * at] + lean[2 * at + 1] * lean[2 * at + 1]) * (255.0f / 3.0f);
                    }
                    pic[at] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                }
            }
            char path[512];
            snprintf(path, sizeof(path), "%s/%08x_%s.pgm", m_DumpDir, (unsigned)hash, names[k]);
            FILE *const f = fopen(path, "wb");
            if (f != NULL) {
                fprintf(f, "P5 %d %d 255\n", DC_BUMP_SIZE, DC_BUMP_SIZE);
                fwrite(pic, 1, BUMP_TEXELS, f);
                fclose(f);
            }
        }
    }
    m_Bumps = realloc(m_Bumps, sizeof(BUMP) * (m_BumpCount + 1));
    BUMP *const bump = &m_Bumps[m_BumpCount++];
    bump->hash = hash;
    for (int y = 0; y < DC_BUMP_SIZE; y++) {
        for (int x = 0; x < DC_BUMP_SIZE; x++) {
            const int at = y * DC_BUMP_SIZE + x;
            bump->texels[Twiddle(x, y)] = !flat[at] && height[at] != 0
                ? DC_Bump_Encode(lean[2 * at], lean[2 * at + 1])
                : DC_BUMP_FLAT;
        }
    }
}

// TR2 and TR3: version, 8-bit palette, 16-bit palette, page count, 8-bit
// pages, then the 16-bit pages the game keeps; for the relief, on to the
// object textures (format_tr2.c and format_tr3.c's probes).
static void ReadLevel(
    const uint8_t *const data, const size_t size, const int version)
{
    const size_t at = 4 + 768 + 1024;
    if (size < at + 4) {
        return;
    }
    const int32_t count = S32(data + at);
    const size_t pages_16 = at + 4 + (size_t)count * PAGE_SIZE;
    if (count <= 0 || size < pages_16 + (size_t)count * PAGE_SIZE * 2) {
        return;
    }
    m_Level.count = 0;
    if (m_BumpPath != NULL) {
        CURSOR c = { data, size, pages_16 + (size_t)count * PAGE_SIZE * 2 + 4,
                     0 };
        ReadRooms(&c, version);
        // From the floor data to the static objects.
        static const int items[] = { 2, 2, 4, 32, 6, 8, 2, 4, 2, 18, 32 };
        for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
            SkipArrS32(&c, items[i]);
        }
        if (version == 3) {
            ReadSprites(&c);
            SkipArrS32(&c, 8); // sprite sequences
            SkipArrS32(&c, 16); // cameras
            SkipArrS32(&c, 16); // sound sources
            const int32_t boxes = CursorS32(&c);
            CursorSkip(&c, (int64_t)boxes * 8);
            SkipArrS32(&c, 2); // overlaps
            CursorSkip(&c, (int64_t)boxes * 20); // zones
            SkipArrS32(&c, 2); // animated textures
        }
        ReadTextures(&c, version);
        if (version == 2) {
            ReadSprites(&c);
        }
        if (c.bad) {
            fprintf(
                stderr, "texvq: a TR%d level that does not add up; no relief\n",
                version);
            m_Level.count = 0;
        }
    }
    for (int32_t i = 0; i < count; i++) {
        uint16_t texels[PAGE_SIZE];
        memcpy(texels, data + pages_16 + (size_t)i * PAGE_SIZE * 2, sizeof(texels));
        AddPage(texels, 0);
        AddBump(i, texels);
    }
}

// TR1: version 32, the 8-bit pages, and further on the palette, which the game
// needs to make the pages 16-bit before it keys them (level/format/format_tr1.c
// walks the same layout; sections/textures.c does the conversion).
static void ReadLevelTR1(const uint8_t *const data, const size_t size)
{
    CURSOR c = { data, size, 4, 0 };
    m_Level.count = 0;
    const int32_t count = CursorS32(&c);
    const size_t tiles = c.at;
    CursorSkip(&c, (int64_t)count * PAGE_SIZE);
    CursorSkip(&c, 4);
    ReadRooms(&c, 1);
    // From the floor data to the sound sources; the object textures are the
    // twelfth.
    static const int items[] = { 2, 2, 4, 32, 6, 8, 2, 4, 2, 18, 32, 20, 16, 8,
                                 16, 16 };
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]) && !c.bad; i++) {
        if (i == 11) {
            ReadTextures(&c, 1);
        } else if (i == 12) {
            ReadSprites(&c);
        } else {
            SkipArrS32(&c, items[i]);
        }
    }
    const int32_t boxes = CursorS32(&c);
    CursorSkip(&c, (int64_t)boxes * 20);
    SkipArrS32(&c, 2); // overlaps
    CursorSkip(&c, (int64_t)boxes * 12); // zones
    SkipArrS32(&c, 2); // animated textures
    SkipArrS32(&c, 22); // items
    CursorSkip(&c, 32 * 256); // light table
    const size_t palette = c.at;
    CursorSkip(&c, 768);
    if (c.bad || count <= 0) {
        fprintf(stderr, "texvq: a TR1 level that does not add up\n");
        return;
    }
    uint8_t rgb[256][3];
    for (int i = 0; i < 256; i++) {
        for (int k = 0; k < 3; k++) {
            const uint8_t v = i == 0 ? 0 : data[palette + i * 3 + k];
            rgb[i][k] = i == 0 ? 0 : (uint8_t)((v << 2) | (v >> 4));
        }
    }
    for (int32_t i = 0; i < count; i++) {
        uint16_t texels[PAGE_SIZE];
        const uint8_t *const page = data + tiles + (size_t)i * PAGE_SIZE;
        for (int j = 0; j < PAGE_SIZE; j++) {
            const uint8_t index = page[j];
            const uint8_t rgba[4] = { rgb[index][0], rgb[index][1],
                                      rgb[index][2], index == 0 ? 0 : 255 };
            texels[j] = ToARGB1555(rgba);
        }
        AddPage(texels, 0);
        AddBump(i, texels);
    }
}

// The texture pages of an injection (see inject/common.c and
// inject/data/textures.c for the layout).
static void ReadInjection(const uint8_t *const data, const size_t size)
{
    const int trxi = memcmp(data, "TRXI", 4) == 0;
    const uLongf unpacked_size = (uint32_t)S32(data + 12);
    const uLong packed_size = (uint32_t)S32(data + 16);
    if (20 + packed_size > size) {
        return;
    }
    uint8_t *const payload = malloc(unpacked_size);
    uLongf out_size = unpacked_size;
    if (uncompress(payload, &out_size, data + 20, packed_size) != Z_OK) {
        free(payload);
        return;
    }
    const uint8_t *p = payload;
    if (trxi) {
        const int32_t tests = S32(p);
        p += 4;
        for (int32_t i = 0; i < tests; i++) {
            p += 8;
            p += 4 + S32(p);
        }
    } else {
        p += 4;
        p += 4 + S32(p);
    }
    const int32_t chunks = S32(p);
    p += 4;
    for (int32_t i = 0; i < chunks; i++) {
        const int32_t type = S32(p);
        p += trxi ? 8 : 4;
        const int32_t blocks = S32(p);
        const int32_t chunk_size = S32(p + 4);
        p += 8;
        const uint8_t *const end = p + chunk_size;
        // ICT_TEXTURE_DATA, and in it IDT_TEXTURE_PAGES: RGBA pages first.
        for (int32_t j = 0; type == 0 && j < blocks; j++) {
            const int32_t data_type = S32(p);
            const int32_t count = S32(p + 4);
            const int32_t block_size = S32(p + 8);
            p += 12;
            for (int32_t k = 0; data_type == 1 && k < count; k++) {
                AddRGBAPage(p + (size_t)k * PAGE_SIZE * 4);
            }
            p += block_size;
        }
        p = end;
    }
    free(payload);
}

// The bits of y and x in turn, y's lowest first (pvr.c's M_Twiddle).
static int Twiddle(const int x, const int y)
{
    int r = 0;
    for (int bit = 0; bit < 8; bit++) {
        r |= ((y >> bit) & 1) << (2 * bit);
        r |= ((x >> bit) & 1) << (2 * bit + 1);
    }
    return r;
}

static void Decode(const uint16_t t, float *const out)
{
    out[0] = (float)((t >> 10) & 31) * (255.0f / 31.0f);
    out[1] = (float)((t >> 5) & 31) * (255.0f / 31.0f);
    out[2] = (float)(t & 31) * (255.0f / 31.0f);
    out[3] = (t & 0x8000) ? 255.0f * ALPHA_WEIGHT : 0.0f;
}

static uint16_t Encode(const float *const c)
{
    int v[3];
    for (int k = 0; k < 3; k++) {
        v[k] = (int)(c[k] * (31.0f / 255.0f) + 0.5f);
        v[k] = v[k] < 0 ? 0 : (v[k] > 31 ? 31 : v[k]);
    }
    return (c[3] >= 127.5f * ALPHA_WEIGHT ? 0x8000 : 0) | (v[0] << 10)
        | (v[1] << 5) | v[2];
}

static float Distance(const float *const a, const float *const b, const float limit)
{
    float d = 0.0f;
    for (int k = 0; k < DIMS; k++) {
        const float e = a[k] - b[k];
        d += e * e;
        if (d >= limit) {
            break;
        }
    }
    return d;
}

static int Nearest(const float *const v, float (*const codes)[DIMS], const int n, float *const dist)
{
    int best = 0;
    float best_d = 3.4e38f;
    for (int c = 0; c < n; c++) {
        const float d = Distance(v, codes[c], best_d);
        if (d < best_d) {
            best_d = d;
            best = c;
        }
    }
    if (dist != NULL) {
        *dist = best_d;
    }
    return best;
}

static int CompareU64(const void *const a, const void *const b)
{
    const uint64_t x = *(const uint64_t *)a;
    const uint64_t y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

// The levels under the page, each halved by averaging, in the page's own
// codebook (a block of the 2x2 level is one byte, the 1x1 level is that same
// block). The PowerVR keeps their indices from the smallest up, each twiddled:
// the 2x2 level at 1, 4x4 at 2, 8x8 at 6, 16x16 at 0x16, and so on.
// Averaged across the whole page: a texture of the atlas bleeds into its
// neighbours in the far levels.
static void Mipmaps(PAGE *const page, float (*const codes)[DIMS], const int code_count)
{
    float(*cur)[4] = malloc(sizeof(float) * 4 * PAGE_SIZE);
    float(*next)[4] = malloc(sizeof(float) * 4 * PAGE_SIZE);
    for (int i = 0; i < PAGE_SIZE; i++) {
        Decode(page->texels[i], cur[i]);
    }
    int offset = MIP_BYTES;
    for (int size = PAGE_W; size > 2; size /= 2) {
        const int half = size / 2;
        for (int y = 0; y < half; y++) {
            for (int x = 0; x < half; x++) {
                // The colour of a see-through texel is whatever the artist left
                // (black): only the solid ones give theirs to the average.
                const float *const t[4] = {
                    cur[2 * y * size + 2 * x],
                    cur[2 * y * size + 2 * x + 1],
                    cur[(2 * y + 1) * size + 2 * x],
                    cur[(2 * y + 1) * size + 2 * x + 1],
                };
                float solid = 0.0f;
                for (int c = 0; c < 4; c++) {
                    solid += t[c][3] > 0.0f ? 1.0f : 0.0f;
                }
                for (int k = 0; k < 3; k++) {
                    float sum = 0.0f;
                    for (int c = 0; c < 4; c++) {
                        sum += (solid > 0.0f ? (t[c][3] > 0.0f ? 1.0f : 0.0f)
                                             : 1.0f)
                            * t[c][k];
                    }
                    next[y * half + x][k] = sum / (solid > 0.0f ? solid : 4.0f);
                }
                next[y * half + x][3] =
                    (t[0][3] + t[1][3] + t[2][3] + t[3][3]) * 0.25f;
            }
        }
        // next is a level of half x half texels, in blocks of 2x2.
        const int blocks = half / 2;
        offset -= blocks * blocks;
        for (int by = 0; by < blocks; by++) {
            for (int bx = 0; bx < blocks; bx++) {
                float v[DIMS];
                const float *const t[4] = {
                    next[2 * by * half + 2 * bx],
                    next[(2 * by + 1) * half + 2 * bx],
                    next[2 * by * half + 2 * bx + 1],
                    next[(2 * by + 1) * half + 2 * bx + 1],
                };
                for (int c = 0; c < 4; c++) {
                    memcpy(&v[c * 4], t[c], sizeof(float) * 4);
                }
                page->mip[offset + Twiddle(bx, by)] =
                    (uint8_t)Nearest(v, codes, code_count, NULL);
            }
        }
        float(*const swap)[4] = cur;
        cur = next;
        next = swap;
    }
    page->mip[0] = page->mip[1];
    free(cur);
    free(next);
}

static void Compress(PAGE *const page)
{
    // Blocks in the order the PowerVR keeps them: block i holds texels
    // (0,0), (0,1), (1,0), (1,1) of its 2x2, as a twiddled page would.
    uint64_t *const blocks = malloc(sizeof(uint64_t) * BLOCKS);
    for (int by = 0; by < PAGE_W / 2; by++) {
        for (int bx = 0; bx < PAGE_W / 2; bx++) {
            const uint16_t *const t = &page->texels[by * 2 * PAGE_W + bx * 2];
            blocks[Twiddle(bx, by)] = (uint64_t)t[0] | (uint64_t)t[PAGE_W] << 16
                | (uint64_t)t[1] << 32 | (uint64_t)t[PAGE_W + 1] << 48;
        }
    }
    // The distinct blocks and how often each comes.
    uint64_t *const unique = malloc(sizeof(uint64_t) * BLOCKS);
    memcpy(unique, blocks, sizeof(uint64_t) * BLOCKS);
    qsort(unique, BLOCKS, sizeof(uint64_t), CompareU64);
    float *const weight = malloc(sizeof(float) * BLOCKS);
    int n = 0;
    for (int i = 0; i < BLOCKS; i++) {
        if (n > 0 && unique[n - 1] == unique[i]) {
            weight[n - 1] += 1.0f;
        } else {
            unique[n] = unique[i];
            weight[n++] = 1.0f;
        }
    }
    float (*const vec)[DIMS] = malloc(sizeof(float) * DIMS * n);
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 4; k++) {
            Decode((uint16_t)(unique[i] >> (16 * k)), &vec[i][k * 4]);
        }
    }

    static _Thread_local float codes[CODES][DIMS];
    int code_count = n < CODES ? n : CODES;
    float *const near = malloc(sizeof(float) * n);
    int *const label = malloc(sizeof(int) * n);
    if (n <= CODES) {
        memcpy(codes, vec, sizeof(float) * DIMS * n);
    } else {
        // Seeds far from those chosen, the common blocks first.
        int first = 0;
        for (int i = 1; i < n; i++) {
            first = weight[i] > weight[first] ? i : first;
        }
        memcpy(codes[0], vec[first], sizeof(codes[0]));
        for (int i = 0; i < n; i++) {
            near[i] = Distance(vec[i], codes[0], 3.4e38f);
        }
        for (int c = 1; c < CODES; c++) {
            int pick = 0;
            for (int i = 1; i < n; i++) {
                pick = near[i] * weight[i] > near[pick] * weight[pick] ? i : pick;
            }
            memcpy(codes[c], vec[pick], sizeof(codes[0]));
            for (int i = 0; i < n; i++) {
                const float d = Distance(vec[i], codes[c], near[i]);
                near[i] = d < near[i] ? d : near[i];
            }
        }
        for (int it = 0; it < ITERATIONS; it++) {
            static _Thread_local double sum[CODES][DIMS];
            static _Thread_local double total[CODES];
            memset(sum, 0, sizeof(sum));
            memset(total, 0, sizeof(total));
            int worst = 0;
            for (int i = 0; i < n; i++) {
                label[i] = Nearest(vec[i], codes, CODES, &near[i]);
                worst = near[i] * weight[i] > near[worst] * weight[worst] ? i : worst;
                total[label[i]] += weight[i];
                for (int k = 0; k < DIMS; k++) {
                    sum[label[i]][k] += vec[i][k] * weight[i];
                }
            }
            for (int c = 0; c < CODES; c++) {
                if (total[c] == 0.0) {
                    // An empty code takes the block worst served.
                    memcpy(codes[c], vec[worst], sizeof(codes[0]));
                    near[worst] = 0.0f;
                    continue;
                }
                for (int k = 0; k < DIMS; k++) {
                    codes[c][k] = (float)(sum[c][k] / total[c]);
                }
            }
        }
    }
    // The codebook as the page's texels, and each block given the nearest of
    // those.
    uint16_t book[CODES][4] = {};
    for (int c = 0; c < code_count; c++) {
        for (int k = 0; k < 4; k++) {
            book[c][k] = Encode(&codes[c][k * 4]);
            Decode(book[c][k], &codes[c][k * 4]);
        }
    }
    memcpy(page->vq, book, sizeof(book));
    for (int i = 0; i < BLOCKS; i++) {
        float v[DIMS];
        for (int k = 0; k < 4; k++) {
            Decode((uint16_t)(blocks[i] >> (16 * k)), &v[k * 4]);
        }
        page->vq[CODES * 8 + i] = (uint8_t)Nearest(v, codes, code_count, NULL);
    }
    Mipmaps(page, codes, code_count);
    free(blocks);
    free(unique);
    free(weight);
    free(vec);
    free(near);
    free(label);
}

static int CompareHash(const void *const a, const void *const b)
{
    const uint32_t x = *(const uint32_t *)a;
    const uint32_t y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

// The reliefs, in the order the pages first came, under the VQ file's index.
static int WriteBumps(void)
{
    uint32_t (*const index)[2] = malloc(sizeof(*index) * (m_BumpCount + 1));
    for (int i = 0; i < m_BumpCount; i++) {
        index[i][0] = m_Bumps[i].hash;
        index[i][1] = i;
    }
    qsort(index, m_BumpCount, sizeof(*index), CompareHash);
    FILE *const fp = fopen(m_BumpPath, "wb");
    if (fp == NULL) {
        fprintf(stderr, "texvq: cannot write %s\n", m_BumpPath);
        free(index);
        return 1;
    }
    const uint32_t count = m_BumpCount;
    // "TBP2": the relief is 256 by 256 a page (131072 bytes); "TBP1" was 128.
    fwrite("TBP2", 1, 4, fp);
    fwrite(&count, 4, 1, fp);
    fwrite(index, sizeof(*index), m_BumpCount, fp);
    for (int i = 0; i < m_BumpCount; i++) {
        fwrite(m_Bumps[i].texels, 2, BUMP_TEXELS, fp);
    }
    fclose(fp);
    free(index);
    printf("texvq: %d page reliefs (%d of them made by hand)\n", m_BumpCount, m_CustomCount);
    return 0;
}

// ---- -s: injections with their pages replaced by hashes ----

#define IDT_TEXTURE_PAGES 1
#define IDT_DC_PAGE_HASHES 45
#define IDT_DC_PAGES_16 46

static uint32_t (*m_VqIndex)[2] = NULL;
static uint32_t m_VqCount = 0;

static int LoadVqIndex(const char *const path)
{
    size_t size;
    uint8_t *const data = ReadFile(path, &size);
    if (data == NULL || size < 8 || memcmp(data, "TVQ", 3) != 0) {
        free(data);
        return 0;
    }
    memcpy(&m_VqCount, data + 4, 4);
    if (8 + (size_t)m_VqCount * 8 > size) {
        free(data);
        return 0;
    }
    m_VqIndex = malloc((size_t)m_VqCount * 8);
    memcpy(m_VqIndex, data + 8, (size_t)m_VqCount * 8);
    free(data);
    return 1;
}

static int InVq(const uint32_t hash)
{
    for (uint32_t i = 0; i < m_VqCount; i++) {
        if (m_VqIndex[i][0] == hash) {
            return 1;
        }
    }
    return 0;
}

// The hash of a page as the game holds it; *ok is 0 when it is not an
// ARGB1555 page that the VQ file has.
static uint32_t PageHash(const uint8_t *const rgba, int *const ok)
{
    for (int i = 0; i < PAGE_SIZE; i++) {
        if (rgba[i * 4 + 3] != 0 && rgba[i * 4 + 3] != 255) {
            *ok = 0;
            return 0;
        }
    }
    static uint16_t texels[PAGE_SIZE];
    for (int i = 0; i < PAGE_SIZE; i++) {
        texels[i] = ToARGB1555(&rgba[i * 4]);
    }
    const uint32_t hash = Hash(texels, PAGE_SIZE * 2);
    *ok = InVq(hash);
    return hash;
}

static void PutS32(uint8_t *const p, const int32_t v)
{
    memcpy(p, &v, 4);
}

// Returns 1 and writes <outdir>/<name> when a block was replaced.
static int StripInjection(
    const char *const name, const uint8_t *const data, const size_t size,
    const char *const outdir)
{
    const uLongf unpacked_size = (uint32_t)S32(data + 12);
    const uLong packed_size = (uint32_t)S32(data + 16);
    if (20 + packed_size > size) {
        return 0;
    }
    uint8_t *const payload = malloc(unpacked_size);
    uLongf out_size = unpacked_size;
    if (uncompress(payload, &out_size, data + 20, packed_size) != Z_OK) {
        free(payload);
        return 0;
    }
    uint8_t *const out = malloc(unpacked_size + 4096);
    uint8_t *o = out;
    const uint8_t *p = payload;
    int changed = 0;

    const uint8_t *const head = p;
    const int32_t tests = S32(p);
    p += 4;
    for (int32_t i = 0; i < tests; i++) {
        p += 8;
        p += 4 + S32(p);
    }
    memcpy(o, head, (size_t)(p - head));
    o += p - head;
    const int32_t chunks = S32(p);
    memcpy(o, p, 4);
    o += 4;
    p += 4;
    for (int32_t i = 0; i < chunks; i++) {
        const int32_t type = S32(p);
        const int32_t blocks = S32(p + 8);
        const int32_t chunk_size = S32(p + 12);
        memcpy(o, p, 16);
        uint8_t *const out_chunk_size = o + 12;
        o += 16;
        p += 16;
        const uint8_t *const end = p + chunk_size;
        uint8_t *const body = o;
        for (int32_t j = 0; j < blocks; j++) {
            const int32_t data_type = S32(p);
            const int32_t count = S32(p + 4);
            const int32_t block_size = S32(p + 8);
            int strip = type == 0 && data_type == IDT_TEXTURE_PAGES && count > 0
                && count <= 64 && block_size >= count * PAGE_SIZE * 5;
            uint32_t hashes[64];
            for (int32_t k = 0; strip && k < count; k++) {
                int ok;
                hashes[k] = PageHash(p + 12 + (size_t)k * PAGE_SIZE * 4, &ok);
                strip = ok;
            }
            if (!strip && type == 0 && data_type == IDT_TEXTURE_PAGES
                && count > 0 && block_size >= count * PAGE_SIZE * 5) {
                // Some page is not in the VQ: all as the 16-bit texels and
                // their format, 128 KiB a page, not the 320 the file holds.
                PutS32(o, IDT_DC_PAGES_16);
                PutS32(o + 4, count);
                PutS32(o + 8, count * (4 + PAGE_SIZE * 2));
                o += 12;
                for (int32_t k = 0; k < count; k++) {
                    const uint8_t *const rgba =
                        p + 12 + (size_t)k * PAGE_SIZE * 4;
                    uint32_t format = 0;
                    for (int i2 = 0; i2 < PAGE_SIZE; i2++) {
                        if (rgba[i2 * 4 + 3] != 0 && rgba[i2 * 4 + 3] != 255) {
                            format = 1;
                            break;
                        }
                    }
                    PutS32(o, (int32_t)format);
                    o += 4;
                    for (int i2 = 0; i2 < PAGE_SIZE; i2++) {
                        const uint16_t t = format ? ToARGB4444(&rgba[i2 * 4])
                                                  : ToARGB1555(&rgba[i2 * 4]);
                        memcpy(o, &t, 2);
                        o += 2;
                    }
                }
                changed = 1;
            } else if (strip) {
                PutS32(o, IDT_DC_PAGE_HASHES);
                PutS32(o + 4, count);
                PutS32(o + 8, count * 8);
                o += 12;
                for (int32_t k = 0; k < count; k++) {
                    PutS32(o, (int32_t)hashes[k]);
                    PutS32(o + 4, 0);
                    o += 8;
                }
                changed = 1;
            } else {
                memcpy(o, p, 12 + (size_t)block_size);
                o += 12 + block_size;
            }
            p += 12 + block_size;
        }
        PutS32(out_chunk_size, (int32_t)(o - body));
        p = end;
    }
    free(payload);
    if (!changed) {
        free(out);
        return 0;
    }
    const uLong new_unpacked = (uLong)(o - out);
    uLongf new_packed = compressBound(new_unpacked);
    uint8_t *const packed = malloc(new_packed);
    if (compress2(packed, &new_packed, out, new_unpacked, 9) != Z_OK) {
        free(out);
        free(packed);
        return 0;
    }
    free(out);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", outdir, name);
    FILE *const fp = fopen(path, "wb");
    if (fp == NULL) {
        free(packed);
        return 0;
    }
    uint8_t header[20];
    memcpy(header, data, 20);
    PutS32(header + 12, (int32_t)new_unpacked);
    PutS32(header + 16, (int32_t)new_packed);
    fwrite(header, 1, 20, fp);
    fwrite(packed, 1, new_packed, fp);
    fwrite(data + 20 + packed_size, 1, size - 20 - packed_size, fp);
    fclose(fp);
    free(packed);
    return 1;
}

// argv: <textures.vq> <outdir> <file.bin>...
static int StripMain(const int argc, char **const argv)
{
    if (argc < 3 || !LoadVqIndex(argv[0])) {
        fprintf(
            stderr, "usage: texvq -s <textures.vq> <outdir> <file.bin>...\n");
        return 1;
    }
    int stripped = 0;
    for (int i = 2; i < argc; i++) {
        size_t size;
        uint8_t *const data = ReadFile(argv[i], &size);
        if (data == NULL || size < 20 || memcmp(data, "TRXI", 4) != 0) {
            free(data);
            continue;
        }
        const char *name = strrchr(argv[i], '/');
        name = name != NULL ? name + 1 : argv[i];
        stripped += StripInjection(name, data, size, argv[1]);
        free(data);
    }
    printf("texvq: %d injections with their pages as hashes\n", stripped);
    return 0;
}

// ---- -l: the hashes of each level's pages, so that the game need not read them ----

// Writes "LPG1", u32 count, then per level {char name[32] (lowercase file
// name), u32 file size, u32 pages, u32 hash x pages}. Only TR2 and TR3 levels,
// whose 16-bit pages the game keys as they lie (level/sections/textures.c).
static int LevelPagesMain(const int argc, char **const argv)
{
    // argv: <output> <level.tr2>...
    if (argc < 2) {
        fprintf(stderr, "usage: texvq -l <levelpages.bin> <level.tr2>...\n");
        return 1;
    }
    FILE *const fp = fopen(argv[0], "wb");
    if (fp == NULL) {
        return 1;
    }
    uint32_t written = 0;
    fwrite("LPG1", 1, 4, fp);
    fwrite(&written, 4, 1, fp);
    for (int i = 1; i < argc; i++) {
        size_t size;
        uint8_t *const data = ReadFile(argv[i], &size);
        const size_t at = 4 + 768 + 1024;
        if (data == NULL || size < at + 4
            || !(S32(data) == 0x2D || (uint32_t)S32(data) == 0xFF080038u
                 || (uint32_t)S32(data) == 0xFF180038u)) {
            free(data);
            continue;
        }
        const int32_t count = S32(data + at);
        const size_t pages_16 = at + 4 + (size_t)count * PAGE_SIZE;
        if (count <= 0 || count > 4096
            || size < pages_16 + (size_t)count * PAGE_SIZE * 2) {
            free(data);
            continue;
        }
        char name[32] = { 0 };
        const char *base = strrchr(argv[i], '/');
        base = base != NULL ? base + 1 : argv[i];
        for (int k = 0; k < 31 && base[k] != '\0'; k++) {
            name[k] = (char)((base[k] >= 'A' && base[k] <= 'Z') ? base[k] + 32
                                                                : base[k]);
        }
        const uint32_t size32 = (uint32_t)size;
        const uint32_t pages = (uint32_t)count;
        fwrite(name, 1, 32, fp);
        fwrite(&size32, 4, 1, fp);
        fwrite(&pages, 4, 1, fp);
        for (int32_t k = 0; k < count; k++) {
            const uint32_t hash =
                Hash(data + pages_16 + (size_t)k * PAGE_SIZE * 2, PAGE_SIZE * 2);
            fwrite(&hash, 4, 1, fp);
        }
        written++;
        free(data);
    }
    fseek(fp, 4, SEEK_SET);
    fwrite(&written, 4, 1, fp);
    fclose(fp);
    printf("texvq: %u levels with their page hashes\n", written);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "-l") == 0) {
        return LevelPagesMain(argc - 2, argv + 2);
    }
    if (argc >= 2 && strcmp(argv[1], "-s") == 0) {
        return StripMain(argc - 2, argv + 2);
    }
    if (argc >= 3 && strcmp(argv[1], "-b") == 0) {
        m_BumpPath = argv[2];
        argc -= 2;
        argv += 2;
        while (argc >= 2 && argv[1][0] == '-') {
            if (strcmp(argv[1], "-C") == 0 && argc >= 3) {
                m_CustomDir = argv[2];
                argc -= 2;
                argv += 2;
            } else if (strcmp(argv[1], "-P") == 0 && argc >= 3) {
                m_PngDir = argv[2];
                argc -= 2;
                argv += 2;
            } else if (strcmp(argv[1], "-D") == 0 && argc >= 3) {
                m_DumpDir = argv[2];
                argc -= 2;
                argv += 2;
            } else if (strcmp(argv[1], "-d0") == 0) {
                m_NoDetail = 1;
                argc -= 1;
                argv += 1;
            } else if (strcmp(argv[1], "-o") == 0 && argc >= 3) {
                LoadOverrides(argv[2]);
                argc -= 2;
                argv += 2;
            } else {
                break;
            }
        }
    }
    if (argc < 3) {
        fprintf(
            stderr,
            "usage: texvq [-b <output.bump>] <output.vq> "
            "<file.tr2|file.bin>...\n");
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        size_t size;
        uint8_t *const data = ReadFile(argv[i], &size);
        if (data == NULL || size < 20) {
            fprintf(stderr, "texvq: cannot read %s\n", argv[i]);
            free(data);
            return 1;
        }
        {
            const char *base = strrchr(argv[i], '/');
            base = base != NULL ? base + 1 : argv[i];
            int n = 0;
            while (base[n] != '\0' && base[n] != '.' && n < 63) {
                m_LevelName[n] = (char)((base[n] >= 'A' && base[n] <= 'Z')
                                            ? base[n] + 32
                                            : base[n]);
                n++;
            }
            m_LevelName[n] = '\0';
        }
        if (memcmp(data, "TRXJ", 4) == 0 || memcmp(data, "TRXI", 4) == 0) {
            ReadInjection(data, size);
        } else if (
            S32(data) == 0x2D || (uint32_t)S32(data) == 0xFF080038u
            || (uint32_t)S32(data) == 0xFF180038u) {
            // TR2, and TR3 (level/format/format_tr3.c), which lays its pages
            // out the same way. DC build 313 shipped TR3 with only its
            // injections' pages in VQ.
            ReadLevel(data, size, S32(data) == 0x2D ? 2 : 3);
        } else if (S32(data) == 32) {
            ReadLevelTR1(data, size);
        }
        free(data);
    }
    // Only the pages VQ suits (see the top).
    int kept = 0;
    for (int i = 0; i < m_Count; i++) {
        if (m_Pages[i].format == 0) {
            m_Pages[kept++] = m_Pages[i];
        }
    }
    printf("texvq: %d pages (%d ARGB4444 left out)\n", kept, m_Count - kept);
    m_Count = kept;
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < m_Count; i++) {
        Compress(&m_Pages[i]);
    }
    // Every page keeps its mipmaps. (Until 11/2026 bit 31 of the position
    // left out those drawn with alpha, which Flycast showed corrupt: the
    // game read their texels without skipping the small levels.)
    uint32_t (*const index)[2] = malloc(sizeof(*index) * m_Count);
    for (int i = 0; i < m_Count; i++) {
        index[i][0] = m_Pages[i].hash;
        index[i][1] = (uint32_t)i;
    }
    qsort(index, m_Count, sizeof(*index), CompareHash);

    FILE *const fp = fopen(argv[1], "wb");
    if (fp == NULL) {
        fprintf(stderr, "texvq: cannot write %s\n", argv[1]);
        return 1;
    }
    const uint32_t count = m_Count;
    // TRX_MIP=0 writes the pages without mipmaps ("TVQ1"), as before.
    const char *const mip_env = getenv("TRX_MIP");
    const bool no_mip = mip_env != NULL && strcmp(mip_env, "0") == 0;
    if (!no_mip) {
        printf("texvq: %d pages with mipmaps\n", m_Count);
    }
    fwrite(no_mip ? "TVQ1" : "TVQ2", 1, 4, fp);
    fwrite(&count, 4, 1, fp);
    fwrite(index, sizeof(*index), m_Count, fp);
    for (int i = 0; i < m_Count; i++) {
        fwrite(m_Pages[i].vq, 1, CODES * 8, fp);
        if (!no_mip) {
            fwrite(m_Pages[i].mip, 1, MIP_BYTES, fp);
        }
        fwrite(m_Pages[i].vq + CODES * 8, 1, BLOCKS, fp);
        if (!no_mip)
        fwrite("\0\0", 1, 2, fp);
    }
    fclose(fp);
    return m_BumpPath != NULL ? WriteBumps() : 0;
}
