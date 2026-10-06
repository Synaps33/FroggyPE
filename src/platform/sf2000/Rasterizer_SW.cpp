#include "Rasterizer_SW.h"
#include <algorithm>
#include <cmath>

namespace sf2000_sw
{

static const int RES_WIDTH[4]  = {  80, 160, 240, 320 };
static const int RES_HEIGHT[4] = {  60, 120, 180, 240 };

/* RGB565 lighting via integer register math (replaces the old 2 MB lookup table) */
static inline uint16_t applyLightScale(uint16_t c, uint32_t sr, uint32_t sg, uint32_t sb)
{
    const uint32_t r = (c >> 11) & 0x1F;
    const uint32_t g = (c >> 5) & 0x3F;
    const uint32_t b = c & 0x1F;
    const uint32_t rl = (r * sr + 16) >> 5;
    const uint32_t gl = (g * sg + 32) >> 6;
    const uint32_t bl = (b * sb + 16) >> 5;
    return (uint16_t)((rl << 11) | (gl << 5) | bl);
}

/* RGB565 colour modulation (biome tint / flat colour multiply) */
static inline uint16_t applyColorMod(uint16_t c, uint32_t cr, uint32_t cg, uint32_t cb)
{
    const uint32_t r = (c >> 11) & 0x1F;
    const uint32_t g = (c >> 5) & 0x3F;
    const uint32_t b = c & 0x1F;
    const uint32_t rm = (r * cr) >> 5;
    const uint32_t gm = (g * cg) >> 6;
    const uint32_t bm = (b * cb) >> 5;
    return (uint16_t)((rm << 11) | (gm << 5) | bm);
}

SoftwareRasterizer& SoftwareRasterizer::instance()
{
    static SoftwareRasterizer s_instance;
    return s_instance;
}

SoftwareRasterizer::SoftwareRasterizer()
{
    init();
}

SoftwareRasterizer::~SoftwareRasterizer()
{
    delete[] m_framebuffer;
    delete[] m_depthBuffer;
    delete[] m_color3D;
}

void SoftwareRasterizer::init()
{
    if (!m_framebuffer)
        m_framebuffer = new uint16_t[SCREEN_PIXELS]();
    if (!m_depthBuffer)
        m_depthBuffer = new uint16_t[SCREEN_PIXELS]();
    if (!m_color3D)
        m_color3D = new uint16_t[SCREEN_PIXELS]();

    initLightTable();
    m_textures.resize(256);
    reset();
}

/*
 * The previous implementation used a 16 x 65536 RGB565 LUT (2 MB of heap).
 * On the GB300 that table thrashes the 32 KB L1 cache on every shaded pixel.
 * We keep only the 16 per-level scale factors and do the RGB565 scaling with
 * integer multiplies in registers instead, which is both faster and frees 2 MB.
 */
void SoftwareRasterizer::initLightTable()
{
    for (int level = 0; level < 16; ++level)
    {
        float factor = (float)level / 15.0f;
        factor = factor / (4.0f - 3.0f * factor);
        const float ambient = 0.08f;
        factor = factor * (1.0f - ambient) + ambient;
        if (factor > 1.0f) factor = 1.0f;

        uint32_t sr = (uint32_t)(factor * 32.0f + 0.5f);
        uint32_t sg = (uint32_t)(factor * 64.0f + 0.5f);
        uint32_t sb = sr;
        if (sr > 32) sr = 32;
        if (sg > 64) sg = 64;
        if (sb > 32) sb = 32;

        m_lightR[level] = sr;
        m_lightG[level] = sg;
        m_lightB[level] = sb;
    }
    // full daylight must be an exact identity for the fast path
    m_lightR[15] = 32;
    m_lightG[15] = 64;
    m_lightB[15] = 32;
}

void SoftwareRasterizer::reset()
{
    m_modelView.setIdentity();
    m_projection.setIdentity();
    m_modelViewStack.clear();
    m_projectionStack.clear();
    m_mvpDirty = true;

    m_depthTest = true;
    m_depthMask = true;
    m_depthFunc = RenderCompare::LessEqual;
    m_alphaTest = false;
    m_alphaFunc = RenderCompare::Greater;
    m_alphaRef = 25;
    m_cullFace = false;
    m_cullFaceMode = RenderFace::Back;
    m_blend = false;
    m_texture2D = true;
    m_lighting = true;
    m_boundTexture = 0;

    m_vpX = 0;
    m_vpY = 0;
    m_vpWidth = SCREEN_WIDTH;
    m_vpHeight = SCREEN_HEIGHT;

    m_isOrtho = false;
    m_3dRendered = false;
    m_pendingColorClear = false;
    updateTargetBuffers();
}

/* ------------------------------------------------------------------ */
/* Resolution scaling                                                  */
/* ------------------------------------------------------------------ */

void SoftwareRasterizer::setBlockResolution(int res)
{
    if (res < 0) res = 0;
    if (res > 3) res = 3;
    if (res == m_blockResolution)
        return;

    if (m_3dRendered)
        flush3DToFramebuffer();

    m_blockResolution = res;
    updateTargetBuffers();
}

void SoftwareRasterizer::updateTargetBuffers()
{
    if (m_isOrtho || m_blockResolution == 3)
    {
        m_curColorBuffer = m_framebuffer;
        m_curWidth = SCREEN_WIDTH;
        m_curHeight = SCREEN_HEIGHT;
    }
    else
    {
        m_curColorBuffer = m_color3D;
        m_curWidth = RES_WIDTH[m_blockResolution];
        m_curHeight = RES_HEIGHT[m_blockResolution];
    }
}

uint16_t* SoftwareRasterizer::getFramebuffer()
{
    if (m_3dRendered)
        flush3DToFramebuffer();
    else
        flushPendingColorClear();
    return m_framebuffer;
}

const uint16_t* SoftwareRasterizer::getFramebuffer() const
{
    return m_framebuffer;
}

void SoftwareRasterizer::flush3DToFramebuffer()
{
    if (!m_3dRendered)
        return;

    switch (m_blockResolution)
    {
        case 0:  upscale_80x60_to_320x240();   break;
        case 1:  upscale_160x120_to_320x240(); break;
        case 2:  upscale_240x180_to_320x240(); break;
        default: break; // 4/4 already renders straight into the framebuffer
    }

    m_3dRendered = false;
    // the upscaled 3D image supersedes any colour clear that was still pending
    m_pendingColorClear = false;
}

/* 80x60 -> 320x240 : each source pixel becomes a 4x4 block (pure 32-bit stores) */
void SoftwareRasterizer::upscale_80x60_to_320x240()
{
    const uint16_t* src = m_color3D;
    uint16_t* dst = m_framebuffer;

    for (int y = 0; y < SCREEN_HEIGHT; ++y)
    {
        const uint16_t* s = src + (y >> 2) * 80;
        uint32_t* d = reinterpret_cast<uint32_t*>(dst + y * SCREEN_WIDTH);

        for (int x = 0; x < 80; ++x)
        {
            const uint16_t c = s[x];
            const uint32_t w = (uint32_t)c | ((uint32_t)c << 16);
            uint32_t* p = d + x * 2;
            p[0] = w;
            p[1] = w;
            p[2] = w;
            p[3] = w;
        }
    }
}

/* 160x120 -> 320x240 : each source pixel becomes a 2x2 block */
void SoftwareRasterizer::upscale_160x120_to_320x240()
{
    const uint16_t* src = m_color3D;
    uint16_t* dst = m_framebuffer;

    for (int y = 0; y < SCREEN_HEIGHT; ++y)
    {
        const uint16_t* s = src + (y >> 1) * 160;
        uint32_t* d = reinterpret_cast<uint32_t*>(dst + y * SCREEN_WIDTH);

        for (int x = 0; x < 160; ++x)
        {
            const uint16_t c = s[x];
            const uint32_t w = (uint32_t)c | ((uint32_t)c << 16);
            d[x] = w;
            d[x + 160] = w;
        }
    }
}

/* 240x180 -> 320x240 : non-integer 4/3 scale, replicated nearest-neighbour.
 * Every group of 3 source pixels expands to 4 destination pixels:
 *   d0 = s0, d1 = s1, d2 = s1, d3 = s2                                        */
void SoftwareRasterizer::upscale_240x180_to_320x240()
{
    const uint16_t* src = m_color3D;
    uint16_t* dst = m_framebuffer;

    for (int y = 0; y < SCREEN_HEIGHT; ++y)
    {
        const uint16_t* s = src + ((y * 3) >> 2) * 240;
        uint16_t* d = dst + y * SCREEN_WIDTH;

        for (int x = 0; x < 80; ++x)
        {
            const uint16_t c0 = s[x * 3 + 0];
            const uint16_t c1 = s[x * 3 + 1];
            const uint16_t c2 = s[x * 3 + 2];
            uint16_t* p = d + x * 4;
            p[0] = c0;
            p[1] = c1;
            p[2] = c1;
            p[3] = c2;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Buffers / clears                                                    */
/* ------------------------------------------------------------------ */

static inline void fillRows16(uint16_t* buf, int pitch, int width, int height, uint16_t value)
{
    uint32_t v32 = ((uint32_t)value << 16) | value;
    for (int y = 0; y < height; ++y)
    {
        uint32_t* row = reinterpret_cast<uint32_t*>(buf + y * pitch);
        const int words = width >> 1;
        for (int i = 0; i < words; ++i)
            row[i] = v32;
        if (width & 1)
            buf[y * pitch + (width - 1)] = value;
    }
}

void SoftwareRasterizer::clear(unsigned int mask)
{
    if (mask & RenderClearMask::Color)
    {
        /*
         * The colour clear is deferred, because GameRenderer issues glClear()
         * *before* it switches to the perspective projection. At that moment we
         * are still flagged as orthographic (left over from the previous
         * frame's GUI pass), so clearing eagerly would wipe the full framebuffer
         * and then the 3D pass would render into a different, never-cleared
         * buffer -- leaving the sky black. The clear is instead applied to
         * whichever colour buffer is active when the first real draw happens.
         */
        m_pendingColorClear = true;
    }
    if (mask & RenderClearMask::Depth)
    {
        // depth is always full-size: the GUI shares it and needs a 320x240 pitch
        fillRows16(m_depthBuffer, SCREEN_WIDTH, SCREEN_WIDTH, SCREEN_HEIGHT, m_clearDepthVal);
    }
}

void SoftwareRasterizer::flushPendingColorClear()
{
    if (!m_pendingColorClear)
        return;
    m_pendingColorClear = false;

    if (!m_isOrtho && m_blockResolution != 3)
        m_3dRendered = true;

    fillRows16(m_curColorBuffer, m_curWidth, m_curWidth, m_curHeight, m_clearColor565);
}

void SoftwareRasterizer::clearColor(float r, float g, float b, float a)
{
    (void)a;
    uint32_t ri = (uint32_t)(r * 31.0f);
    uint32_t gi = (uint32_t)(g * 63.0f);
    uint32_t bi = (uint32_t)(b * 31.0f);
    if (ri > 31) ri = 31;
    if (gi > 63) gi = 63;
    if (bi > 31) bi = 31;
    m_clearColor565 = (uint16_t)((ri << 11) | (gi << 5) | bi);
}

void SoftwareRasterizer::clearDepth(float depth)
{
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;
    m_clearDepthVal = (uint16_t)(depth * 65535.0f);
}

void SoftwareRasterizer::setViewport(int x, int y, int width, int height)
{
    m_vpX = x;
    m_vpY = y;
    m_vpWidth = width;
    m_vpHeight = height;
}

void SoftwareRasterizer::getViewport(int* values)
{
    if (values)
    {
        values[0] = m_vpX;
        values[1] = m_vpY;
        values[2] = m_curWidth;
        values[3] = m_curHeight;
    }
}

/* ------------------------------------------------------------------ */
/* Matrices                                                            */
/* ------------------------------------------------------------------ */

void SoftwareRasterizer::matrixMode(RenderMatrixMode mode)
{
    m_currentMatrixMode = mode;
}

void SoftwareRasterizer::loadIdentity()
{
    if (m_currentMatrixMode == RenderMatrixMode::ModelView)
        m_modelView.setIdentity();
    else if (m_currentMatrixMode == RenderMatrixMode::Projection)
        m_projection.setIdentity();
    m_mvpDirty = true;
}

void SoftwareRasterizer::pushMatrix()
{
    if (m_currentMatrixMode == RenderMatrixMode::ModelView)
        m_modelViewStack.push_back(m_modelView);
    else if (m_currentMatrixMode == RenderMatrixMode::Projection)
        m_projectionStack.push_back(m_projection);
}

void SoftwareRasterizer::popMatrix()
{
    if (m_currentMatrixMode == RenderMatrixMode::ModelView && !m_modelViewStack.empty())
    {
        m_modelView = m_modelViewStack.back();
        m_modelViewStack.pop_back();
        m_mvpDirty = true;
    }
    else if (m_currentMatrixMode == RenderMatrixMode::Projection && !m_projectionStack.empty())
    {
        m_projection = m_projectionStack.back();
        m_projectionStack.pop_back();
        m_mvpDirty = true;
    }
}

void SoftwareRasterizer::translate(float x, float y, float z)
{
    if (m_currentMatrixMode == RenderMatrixMode::ModelView)
        m_modelView.translate(FX_FROM_FLOAT(x), FX_FROM_FLOAT(y), FX_FROM_FLOAT(z));
    else if (m_currentMatrixMode == RenderMatrixMode::Projection)
        m_projection.translate(FX_FROM_FLOAT(x), FX_FROM_FLOAT(y), FX_FROM_FLOAT(z));
    m_mvpDirty = true;
}

void SoftwareRasterizer::scale(float x, float y, float z)
{
    if (m_currentMatrixMode == RenderMatrixMode::ModelView)
        m_modelView.scale(FX_FROM_FLOAT(x), FX_FROM_FLOAT(y), FX_FROM_FLOAT(z));
    else if (m_currentMatrixMode == RenderMatrixMode::Projection)
        m_projection.scale(FX_FROM_FLOAT(x), FX_FROM_FLOAT(y), FX_FROM_FLOAT(z));
    m_mvpDirty = true;
}

void SoftwareRasterizer::multMatrix(const float* m)
{
    FixedMat4 mat;
    for (int i = 0; i < 16; ++i)
        mat.m[i] = FX_FROM_FLOAT(m[i]);

    if (m_currentMatrixMode == RenderMatrixMode::ModelView)
        m_modelView = m_modelView * mat;
    else if (m_currentMatrixMode == RenderMatrixMode::Projection)
        m_projection = m_projection * mat;
    m_mvpDirty = true;
}

void SoftwareRasterizer::rotate(float angle, float x, float y, float z)
{
    float rad = angle * 0.0174532925f;
    float c = std::cos(rad);
    float s = std::sin(rad);
    float len = std::sqrt(x * x + y * y + z * z);
    if (len > 0.0f)
    {
        x /= len; y /= len; z /= len;
    }

    FixedMat4 rot;
    rot.m[0] = FX_FROM_FLOAT(x * x * (1.0f - c) + c);
    rot.m[1] = FX_FROM_FLOAT(y * x * (1.0f - c) + z * s);
    rot.m[2] = FX_FROM_FLOAT(x * z * (1.0f - c) - y * s);
    rot.m[3] = 0;

    rot.m[4] = FX_FROM_FLOAT(x * y * (1.0f - c) - z * s);
    rot.m[5] = FX_FROM_FLOAT(y * y * (1.0f - c) + c);
    rot.m[6] = FX_FROM_FLOAT(y * z * (1.0f - c) + x * s);
    rot.m[7] = 0;

    rot.m[8]  = FX_FROM_FLOAT(x * z * (1.0f - c) + y * s);
    rot.m[9]  = FX_FROM_FLOAT(y * z * (1.0f - c) - x * s);
    rot.m[10] = FX_FROM_FLOAT(z * z * (1.0f - c) + c);
    rot.m[11] = 0;

    rot.m[12] = 0;
    rot.m[13] = 0;
    rot.m[14] = 0;
    rot.m[15] = FX_ONE;

    if (m_currentMatrixMode == RenderMatrixMode::ModelView)
        m_modelView = m_modelView * rot;
    else if (m_currentMatrixMode == RenderMatrixMode::Projection)
        m_projection = m_projection * rot;
    m_mvpDirty = true;
}

void SoftwareRasterizer::frustum(float l, float r, float b, float t, float n, float f)
{
    FixedMat4 mat;
    mat.m[0] = FX_FROM_FLOAT((2.0f * n) / (r - l));
    mat.m[1] = 0;
    mat.m[2] = 0;
    mat.m[3] = 0;

    mat.m[4] = 0;
    mat.m[5] = FX_FROM_FLOAT((2.0f * n) / (t - b));
    mat.m[6] = 0;
    mat.m[7] = 0;

    mat.m[8] = FX_FROM_FLOAT((r + l) / (r - l));
    mat.m[9] = FX_FROM_FLOAT((t + b) / (t - b));
    mat.m[10] = FX_FROM_FLOAT(-(f + n) / (f - n));
    mat.m[11] = FX_FROM_FLOAT(-1.0f);

    mat.m[12] = 0;
    mat.m[13] = 0;
    mat.m[14] = FX_FROM_FLOAT(-(2.0f * f * n) / (f - n));
    mat.m[15] = 0;

    if (m_currentMatrixMode == RenderMatrixMode::Projection)
    {
        m_projection = mat;
        m_isOrtho = false;
        updateTargetBuffers();
    }
    else if (m_currentMatrixMode == RenderMatrixMode::ModelView)
        m_modelView = mat;
    m_mvpDirty = true;
}

void SoftwareRasterizer::ortho(float l, float r, float b, float t, float n, float f)
{
    FixedMat4 mat;
    mat.m[0] = FX_FROM_FLOAT(2.0f / (r - l));
    mat.m[1] = 0;
    mat.m[2] = 0;
    mat.m[3] = 0;

    mat.m[4] = 0;
    mat.m[5] = FX_FROM_FLOAT(2.0f / (t - b));
    mat.m[6] = 0;
    mat.m[7] = 0;

    mat.m[8] = 0;
    mat.m[9] = 0;
    mat.m[10] = FX_FROM_FLOAT(-2.0f / (f - n));
    mat.m[11] = 0;

    mat.m[12] = FX_FROM_FLOAT(-(r + l) / (r - l));
    mat.m[13] = FX_FROM_FLOAT(-(t + b) / (t - b));
    mat.m[14] = FX_FROM_FLOAT(-(f + n) / (f - n));
    mat.m[15] = FX_ONE;

    if (m_currentMatrixMode == RenderMatrixMode::Projection)
    {
        // switching to 2D: publish the low-res 3D image to the real framebuffer first
        if (m_3dRendered)
            flush3DToFramebuffer();
        m_projection = mat;
        m_isOrtho = true;
        updateTargetBuffers();
        // a pure-GUI frame (menus) still needs its colour clear applied
        flushPendingColorClear();
    }
    else if (m_currentMatrixMode == RenderMatrixMode::ModelView)
        m_modelView = mat;
    m_mvpDirty = true;
}

/*
 * gluPerspective() assembles the projection matrix itself and pushes it with
 * glMultMatrixf, so it never reaches frustum(). Without this explicit hook the
 * rasterizer would stay in orthographic mode for the whole 3D pass and every
 * resolution setting would silently render at full size.
 */
void SoftwareRasterizer::setPerspective()
{
    if (!m_isOrtho)
        return;
    m_isOrtho = false;
    updateTargetBuffers();
    flushPendingColorClear();
}

void SoftwareRasterizer::getMatrix(RenderMatrixQuery query, float* values)
{
    if (!values) return;
    const FixedMat4* target = (query == RenderMatrixQuery::Projection) ? &m_projection : &m_modelView;
    for (int i = 0; i < 16; ++i)
        values[i] = FX_TO_FLOAT(target->m[i]);
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

void SoftwareRasterizer::enable(RenderCapability cap)
{
    switch (cap)
    {
        case RenderCapability::DepthTest: m_depthTest = true; break;
        case RenderCapability::CullFace:  m_cullFace = true; break;
        case RenderCapability::AlphaTest: m_alphaTest = true; break;
        case RenderCapability::Blend:     m_blend = true; break;
        case RenderCapability::Texture2D: m_texture2D = true; break;
        case RenderCapability::Lighting:  m_lighting = true; break;
        default: break;
    }
}

void SoftwareRasterizer::disable(RenderCapability cap)
{
    switch (cap)
    {
        case RenderCapability::DepthTest: m_depthTest = false; break;
        case RenderCapability::CullFace:  m_cullFace = false; break;
        case RenderCapability::AlphaTest: m_alphaTest = false; break;
        case RenderCapability::Blend:     m_blend = false; break;
        case RenderCapability::Texture2D: m_texture2D = false; break;
        case RenderCapability::Lighting:  m_lighting = false; break;
        default: break;
    }
}

void SoftwareRasterizer::setColor4f(float r, float g, float b, float a)
{
    uint32_t ri = (uint32_t)(r * 255.0f);
    uint32_t gi = (uint32_t)(g * 255.0f);
    uint32_t bi = (uint32_t)(b * 255.0f);
    uint32_t ai = (uint32_t)(a * 255.0f);
    if (ri > 255) ri = 255;
    if (gi > 255) gi = 255;
    if (bi > 255) bi = 255;
    if (ai > 255) ai = 255;
    m_currentColor = (ai << 24) | (bi << 16) | (gi << 8) | ri;
    m_currentColor565 = (uint16_t)(((ri >> 3) << 11) | ((gi >> 2) << 5) | (bi >> 3));
}

/* ------------------------------------------------------------------ */
/* Textures                                                            */
/* ------------------------------------------------------------------ */

void SoftwareRasterizer::generateTextures(int count, int* textures)
{
    static int s_nextId = 1;
    for (int i = 0; i < count; ++i)
    {
        int id = s_nextId++;
        textures[i] = id;
        if ((size_t)id >= m_textures.size())
            m_textures.resize(id + 16);
    }
}

void SoftwareRasterizer::deleteTextures(int count, const int* textures)
{
    for (int i = 0; i < count; ++i)
    {
        int id = textures[i];
        if (id > 0 && (size_t)id < m_textures.size())
        {
            m_textures[id].pixels565.clear();
            m_textures[id].alpha.clear();
            m_textures[id].width = 0;
            m_textures[id].height = 0;
        }
    }
}

void SoftwareRasterizer::bindTexture(int texture)
{
    m_boundTexture = texture;
}

bool SoftwareRasterizer::isTextureValid(int texture) const
{
    return texture >= 0 && (size_t)texture < m_textures.size();
}

void SoftwareRasterizer::uploadTextureImage(int level, int width, int height, const void* pixels)
{
    if (level != 0 || m_boundTexture <= 0 || width <= 0 || height <= 0 || !pixels)
        return;

    if ((size_t)m_boundTexture >= m_textures.size())
        m_textures.resize(m_boundTexture + 16);

    SwTexture& tex = m_textures[m_boundTexture];
    tex.width = width;
    tex.height = height;
    size_t count = (size_t)width * (size_t)height;
    tex.pixels565.resize(count);
    tex.alpha.resize(count);
    tex.hasAlpha = false;

    const uint8_t* src = static_cast<const uint8_t*>(pixels);
    for (size_t i = 0; i < count; ++i)
    {
        uint8_t r = src[i * 4 + 0];
        uint8_t g = src[i * 4 + 1];
        uint8_t b = src[i * 4 + 2];
        uint8_t a = src[i * 4 + 3];

        tex.pixels565[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        tex.alpha[i] = a;
        if (a < 250)
            tex.hasAlpha = true;
    }
}

void SoftwareRasterizer::uploadTextureSubImage(int level, int x, int y, int width, int height, const void* pixels)
{
    if (level != 0 || m_boundTexture <= 0 || width <= 0 || height <= 0 || !pixels)
        return;

    if ((size_t)m_boundTexture >= m_textures.size())
        return;

    SwTexture& tex = m_textures[m_boundTexture];
    if (tex.width <= 0 || tex.height <= 0)
        return;

    const uint8_t* src = static_cast<const uint8_t*>(pixels);
    for (int row = 0; row < height; ++row)
    {
        int dstY = y + row;
        if (dstY >= tex.height) break;
        for (int col = 0; col < width; ++col)
        {
            int dstX = x + col;
            if (dstX >= tex.width) break;

            size_t srcIdx = (size_t)row * width + col;
            size_t dstIdx = (size_t)dstY * tex.width + dstX;

            uint8_t r = src[srcIdx * 4 + 0];
            uint8_t g = src[srcIdx * 4 + 1];
            uint8_t b = src[srcIdx * 4 + 2];
            uint8_t a = src[srcIdx * 4 + 3];

            tex.pixels565[dstIdx] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            tex.alpha[dstIdx] = a;
            if (a < 250)
                tex.hasAlpha = true;
        }
    }
}

void SoftwareRasterizer::updateMvp()
{
    if (m_mvpDirty)
    {
        m_mvp = m_projection * m_modelView;
        m_mvpDirty = false;
    }
}

/* ------------------------------------------------------------------ */
/* Mesh submission                                                     */
/* ------------------------------------------------------------------ */

bool SoftwareRasterizer::drawInterleaved(const RenderInterleavedMesh& mesh)
{
    if (!mesh.data || mesh.count <= 0 || mesh.stride <= 0)
        return false;

    updateMvp();

    // any colour clear requested earlier in the frame belongs to *this* target
    flushPendingColorClear();

    if (!m_isOrtho && m_blockResolution != 3)
        m_3dRendered = true;

    const uint8_t* base = static_cast<const uint8_t*>(mesh.data) + mesh.first * mesh.stride;

    static std::vector<TransformedVert> s_verts;
    if (s_verts.size() < (size_t)mesh.count)
        s_verts.resize(mesh.count);
    TransformedVert* verts = s_verts.data();

    const fixed_t* m = m_mvp.m;
    // screen mapping constants, precomputed once per mesh in Q16.16
    const fixed_t vpXfx = (fixed_t)m_vpX << FX_SHIFT;
    const fixed_t vpYfx = (fixed_t)m_vpY << FX_SHIFT;
    const fixed_t vpHalfW = ((fixed_t)m_curWidth << FX_SHIFT) >> 1;
    const fixed_t vpHalfH = ((fixed_t)m_curHeight << FX_SHIFT) >> 1;

    // Transform every vertex exactly once per mesh. Previously each vertex was
    // re-transformed for every triangle that referenced it (6 transforms per
    // quad instead of 4), which was pure overhead in the hottest loop.
    //
    // The whole clip -> NDC -> screen path stays in Q16.16 fixed point. Only the
    // three incoming floats need converting, and the perspective divide becomes
    // a single 32-bit integer division (MIPS has a hardware divider) instead of
    // a soft-float division, which costs several dozen instructions.
    for (int i = 0; i < mesh.count; ++i)
    {
        const uint8_t* vData = base + i * mesh.stride;
        TransformedVert& tv = verts[i];

        const float* pos = reinterpret_cast<const float*>(vData);
        const fixed_t vx = FX_FROM_FLOAT(pos[0]);
        const fixed_t vy = FX_FROM_FLOAT(pos[1]);
        const fixed_t vz = FX_FROM_FLOAT(pos[2]);

        const fixed_t cx = fx_mul(m[0], vx) + fx_mul(m[4], vy) + fx_mul(m[8], vz)  + m[12];
        const fixed_t cy = fx_mul(m[1], vx) + fx_mul(m[5], vy) + fx_mul(m[9], vz)  + m[13];
        const fixed_t cz = fx_mul(m[2], vx) + fx_mul(m[6], vy) + fx_mul(m[10], vz) + m[14];
        const fixed_t w  = fx_mul(m[3], vx) + fx_mul(m[7], vy) + fx_mul(m[11], vz) + m[15];

        // w <= 0.05 puts the vertex behind (or almost on) the eye plane
        if (w <= 3277)
        {
            tv.valid = false;
            continue;
        }

        // Q16.16 reciprocal. 0xFFFFFFFF/w lands within 1 LSB of the exact value
        // and uses the 32-bit hardware divider rather than a float division.
        const fixed_t invW = (fixed_t)(0xFFFFFFFFu / (uint32_t)w);

        const fixed_t nx = fx_mul(cx, invW);
        const fixed_t ny = fx_mul(cy, invW);
        const fixed_t nz = fx_mul(cz, invW);

        tv.sx = FX_TO_FLOAT(vpXfx + fx_mul(nx + FX_ONE, vpHalfW));
        tv.sy = FX_TO_FLOAT(vpYfx + fx_mul(FX_ONE - ny, vpHalfH));
        tv.sz = FX_TO_FLOAT(fx_mul(nz + FX_ONE, FX_HALF));

        if (mesh.hasTexture)
        {
            const float* uv = reinterpret_cast<const float*>(vData + mesh.texCoordOffset);
            tv.u = uv[0];
            tv.v = uv[1];
        }
        else
        {
            tv.u = 0.0f;
            tv.v = 0.0f;
        }
        tv.valid = true;
    }

    const int triStep = (mesh.primitive == RenderPrimitive::Quads) ? 4 : 3;

    m_stats.meshes++;
    m_stats.verts += mesh.count;

    for (int i = 0; i + triStep - 1 < mesh.count; i += triStep)
    {
        m_stats.tris++;
        const uint8_t* v0Data = base + i * mesh.stride;
        uint32_t color = mesh.hasColor ? *reinterpret_cast<const uint32_t*>(v0Data + mesh.colorOffset)
                                       : m_currentColor;
        int brightness = mesh.hasBrightness ? *reinterpret_cast<const int*>(v0Data + mesh.brightnessOffset)
                                            : 0x00F000F0;

        drawTriangleTransformed(verts[i], verts[i + 1], verts[i + 2], color, brightness);

        if (mesh.primitive == RenderPrimitive::Quads)
            drawTriangleTransformed(verts[i], verts[i + 2], verts[i + 3], color, brightness);
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Triangle rasterization                                              */
/* ------------------------------------------------------------------ */

/*
 * Fit an affine plane  attr = a*x + b*y + c  through the three vertices.
 * Because the attribute is planar in screen space, this gives constant
 * per-pixel gradients: the scanline loop then needs zero divisions and
 * zero per-pixel multiplies (only fixed-point adds).
 *
 * The determinant depends only on the screen-space x/y of the triangle, so it is
 * computed once by the caller and shared by the u, v and z planes.
 */
static inline void fitPlaneInv(float dx1, float dy1, float dx2, float dy2,
                               float x0, float y0, float t0, float t1, float t2,
                               float invDet, float& a, float& b, float& c)
{
    if (invDet == 0.0f)
    {
        a = 0.0f;
        b = 0.0f;
        c = t0;
        return;
    }

    const float dt1 = t1 - t0, dt2 = t2 - t0;
    a = (dt1 * dy2 - dt2 * dy1) * invDet;
    b = (dx1 * dt2 - dx2 * dt1) * invDet;
    c = t0 - a * x0 - b * y0;
}

void SoftwareRasterizer::drawTriangleTransformed(const TransformedVert& v0, const TransformedVert& v1,
                                                 const TransformedVert& v2,
                                                 uint32_t color, int brightness)
{
    if (!v0.valid || !v1.valid || !v2.valid)
    {
        m_stats.trisRejected++;
        return;
    }

    // Back-face culling. Screen Y is inverted, so a CCW triangle has a negative cross.
    if (m_cullFace)
    {
        const float cross = (v1.sx - v0.sx) * (v2.sy - v0.sy) - (v2.sx - v0.sx) * (v1.sy - v0.sy);
        if (m_cullFaceMode == RenderFace::Back && cross >= 0.0f)
        {
            m_stats.trisRejected++;
            return;
        }
        if (m_cullFaceMode == RenderFace::Front && cross <= 0.0f)
        {
            m_stats.trisRejected++;
            return;
        }
    }

    // Screen-space bounding box rejection: skips triangles that are fully
    // clipped or sub-pixel without touching the rasterizer at all.
    const float minXf = v0.sx < v1.sx ? (v0.sx < v2.sx ? v0.sx : v2.sx) : (v1.sx < v2.sx ? v1.sx : v2.sx);
    const float maxXf = v0.sx > v1.sx ? (v0.sx > v2.sx ? v0.sx : v2.sx) : (v1.sx > v2.sx ? v1.sx : v2.sx);
    const float minYf = v0.sy < v1.sy ? (v0.sy < v2.sy ? v0.sy : v2.sy) : (v1.sy < v2.sy ? v1.sy : v2.sy);
    const float maxYf = v0.sy > v1.sy ? (v0.sy > v2.sy ? v0.sy : v2.sy) : (v1.sy > v2.sy ? v1.sy : v2.sy);

    // We sample at the pixel centre (y + 0.5), so the covered rows are those
    // whose centre lies inside [minYf, maxYf). Using ceil(maxYf) here would
    // include one row too many and, for a flat bottom edge, make the short-edge
    // walk collapse the span.
    int yStart = (int)std::ceil(minYf - 0.5f);
    int yEnd = (int)std::ceil(maxYf - 0.5f);
    if (yStart < 0) yStart = 0;
    if (yEnd > m_curHeight) yEnd = m_curHeight;
    if (yStart >= yEnd)
    {
        m_stats.trisRejected++;
        return;
    }

    if (maxXf < 0.0f || minXf > (float)m_curWidth)
    {
        m_stats.trisRejected++;
        return;
    }

    const SwTexture* tex = (m_texture2D && m_boundTexture > 0 && (size_t)m_boundTexture < m_textures.size())
        ? &m_textures[m_boundTexture] : nullptr;
    if (tex && (tex->width <= 0 || tex->height <= 0))
        tex = nullptr;

    uint8_t lightLevel = 15;
    if (m_lighting && brightness != 0x00F000F0)
    {
        const int blockLight = (brightness >> 4) & 0x0F;
        const int skyLight = (brightness >> 20) & 0x0F;
        int lvl = blockLight > skyLight ? blockLight : skyLight;
        if (lvl > 15) lvl = 15;
        lightLevel = (uint8_t)lvl;
    }

    uint16_t flatColor565 = m_currentColor565;
    uint8_t flatAlpha = (m_currentColor >> 24) & 0xFF;
    if (color != 0 && color != 0xFFFFFFFF)
    {
        const uint8_t r = color & 0xFF;
        const uint8_t g = (color >> 8) & 0xFF;
        const uint8_t b = (color >> 16) & 0xFF;
        flatAlpha = (color >> 24) & 0xFF;
        if (flatAlpha == 0) flatAlpha = 255;
        flatColor565 = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    const float texW = tex ? (float)tex->width : 1.0f;
    const float texH = tex ? (float)tex->height : 1.0f;

    // one shared reciprocal for all three attribute planes
    const float pdx1 = v1.sx - v0.sx, pdy1 = v1.sy - v0.sy;
    const float pdx2 = v2.sx - v0.sx, pdy2 = v2.sy - v0.sy;
    const float det = pdx1 * pdy2 - pdx2 * pdy1;
    const float invDet = (det > -1e-7f && det < 1e-7f) ? 0.0f : (1.0f / det);

    float au, bu, cu, av, bv, cv, az, bz, cz;
    fitPlaneInv(pdx1, pdy1, pdx2, pdy2, v0.sx, v0.sy, v0.u, v1.u, v2.u, invDet, au, bu, cu);
    fitPlaneInv(pdx1, pdy1, pdx2, pdy2, v0.sx, v0.sy, v0.v, v1.v, v2.v, invDet, av, bv, cv);
    fitPlaneInv(pdx1, pdy1, pdx2, pdy2, v0.sx, v0.sy, v0.sz, v1.sz, v2.sz, invDet, az, bz, cz);

    const fixed_t dudx = FX_FROM_FLOAT(au * texW);
    const fixed_t dvdx = FX_FROM_FLOAT(av * texH);
    const fixed_t dzdx = FX_FROM_FLOAT(az);

    // Sort by Y so the scanline walk always has a long edge (a->c) plus one
    // short edge (a->b above b, b->c below b).
    TransformedVert a = v0, b = v1, c = v2;
    if (b.sy < a.sy) { const TransformedVert t = a; a = b; b = t; }
    if (c.sy < a.sy) { const TransformedVert t = a; a = c; c = t; }
    if (c.sy < b.sy) { const TransformedVert t = b; b = c; c = t; }

    const float invLong = (c.sy > a.sy) ? (1.0f / (c.sy - a.sy)) : 0.0f;
    const bool hasUp = (b.sy > a.sy);
    const bool hasLow = (c.sy > b.sy);
    const float invUp = hasUp ? (1.0f / (b.sy - a.sy)) : 0.0f;
    const float invLow = hasLow ? (1.0f / (c.sy - b.sy)) : 0.0f;

    const float yc0 = (float)yStart + 0.5f;

    const float gxLong = (c.sx - a.sx) * invLong;
    float xLong = a.sx + (c.sx - a.sx) * (yc0 - a.sy) * invLong;

    float xShort;
    float gxShort;
    int ySwitch;
    if (hasUp && yc0 < b.sy)
    {
        xShort = a.sx + (b.sx - a.sx) * (yc0 - a.sy) * invUp;
        gxShort = (b.sx - a.sx) * invUp;
        ySwitch = (int)std::ceil(b.sy - 0.5f);
    }
    else if (hasLow)
    {
        xShort = b.sx + (c.sx - b.sx) * (yc0 - b.sy) * invLow;
        gxShort = (c.sx - b.sx) * invLow;
        ySwitch = yEnd;
    }
    else
    {
        xShort = a.sx;
        gxShort = 0.0f;
        ySwitch = yEnd;
    }

    // Running b*y + c terms: one add per scanline instead of a divide.
    float buRow = bu * yc0 + cu;
    float bvRow = bv * yc0 + cv;
    float bzRow = bz * yc0 + cz;

    m_stats.trisRasterized++;

    for (int y = yStart; y < yEnd; ++y)
    {
        if (y == ySwitch)
        {
            const float yc = (float)y + 0.5f;
            xShort = b.sx + (c.sx - b.sx) * (yc - b.sy) * invLow;
            gxShort = (c.sx - b.sx) * invLow;
        }

        float xa = xLong;
        float xb2 = xShort;
        if (xa > xb2) { const float tmp = xa; xa = xb2; xb2 = tmp; }

        int x0 = (int)std::ceil(xa - 0.001f);
        int x1 = (int)std::ceil(xb2 - 0.001f);
        if (x0 < 0) x0 = 0;
        if (x1 > m_curWidth) x1 = m_curWidth;

        if (x0 < x1)
        {
            const float rowU = au * (float)x0 + buRow;
            const float rowV = av * (float)x0 + bvRow;
            const float rowZ = az * (float)x0 + bzRow;

            drawSpan(y, x0, x1,
                     FX_FROM_FLOAT(rowU * texW), FX_FROM_FLOAT(rowV * texH), FX_FROM_FLOAT(rowZ),
                     dudx, dvdx, dzdx, lightLevel, tex, flatColor565, flatAlpha);
        }

        xLong += gxLong;
        xShort += gxShort;
        buRow += bu;
        bvRow += bv;
        bzRow += bz;
    }
}

void SoftwareRasterizer::drawSpan(int y, int x0, int x1, fixed_t u, fixed_t v, fixed_t z,
                                  fixed_t dudx, fixed_t dvdx, fixed_t dzdx,
                                  uint8_t lightLevel, const SwTexture* tex,
                                  uint16_t flatColor565, uint8_t flatAlpha)
{
    if (x0 < 0) x0 = 0;
    if (x1 > m_curWidth) x1 = m_curWidth;
    const int count = x1 - x0;
    if (count <= 0)
        return;

    m_stats.spans++;
    m_stats.pixels += count;

    uint16_t* cDst = m_curColorBuffer + y * m_curWidth + x0;
    uint16_t* zDst = m_depthBuffer + y * SCREEN_WIDTH + x0;

    const uint32_t sr = m_lightR[lightLevel];
    const uint32_t sg = m_lightG[lightLevel];
    const uint32_t sb = m_lightB[lightLevel];
    const bool identityLight = (lightLevel == 15);

    uint32_t cr_plus1 = 32, cg_plus1 = 64, cb_plus1 = 32;
    const bool hasColorMod = (flatColor565 != 0xFFFF);
    if (hasColorMod)
    {
        cr_plus1 = ((flatColor565 >> 11) & 0x1F) + 1;
        cg_plus1 = ((flatColor565 >> 5)  & 0x3F) + 1;
        cb_plus1 = (flatColor565 & 0x1F) + 1;
    }

    int texW = 0, texH = 0, maskU = 0, maskV = 0;
    bool isPow2 = false;
    if (tex)
    {
        texW = tex->width;
        texH = tex->height;
        maskU = texW - 1;
        maskV = texH - 1;
        isPow2 = ((texW & maskU) == 0) && ((texH & maskV) == 0);
        /*
         * The modulo path in the span loops below is dead code on this build:
         * every texture that gets sampled is a power of two (a 16x16 tile atlas
         * is 256x256), so the mask branch always wins. Measured over a full run:
         * 1014986 texels took the mask path and 0 took the modulo. It is kept
         * only for correctness should a non-po2 texture ever be loaded.
         */
    }

    // Single alpha cutoff covering all three original cases.
    uint8_t alphaCut;
    if (m_blend)            alphaCut = 17;
    else if (m_alphaTest)   alphaCut = m_alphaRef ? m_alphaRef : 1;
    else                    alphaCut = 1;

    const bool doDepthTest = m_depthTest;
    const bool doDepthMask = m_depthMask;

    // Everything the fast path above relies on. Computed once per span.
    const bool fastOpaque = tex && isPow2 && identityLight && !hasColorMod &&
                            doDepthTest && doDepthMask && !m_blend && alphaCut <= 1;

    if (tex)
    {
        const uint16_t* pix = tex->pixels565.data();
        const uint8_t* alp = tex->alpha.data();

        if (m_blend)
        {
            for (int i = 0; i < count; ++i)
            {
                int tu, tv;
                if (isPow2) { tu = (int)(u >> FX_SHIFT) & maskU; tv = (int)(v >> FX_SHIFT) & maskV; }
                else
                {
                    tu = (int)(u >> FX_SHIFT) % texW; if (tu < 0) tu += texW;
                    tv = (int)(v >> FX_SHIFT) % texH; if (tv < 0) tv += texH;
                }
                const int idx = tv * texW + tu;
                const uint8_t a = alp[idx];
                if (a >= alphaCut)
                {
                    const uint16_t curZ = (z >= FX_ONE) ? 0xFFFF : ((z <= 0) ? 0 : (uint16_t)z);
                    if (!doDepthTest || curZ <= zDst[i])
                    {
                        uint16_t s = pix[idx];
                        if (!identityLight)
                            s = applyLightScale(s, sr, sg, sb);
                        if (hasColorMod)
                            s = applyColorMod(s, cr_plus1, cg_plus1, cb_plus1);
                        if (a >= 240)
                        {
                            if (doDepthMask) zDst[i] = curZ;
                            cDst[i] = s;
                        }
                        else
                        {
                            const uint16_t d = cDst[i];
                            const uint32_t sa = a >> 3;
                            const uint32_t da = 32 - sa;
                            const uint32_t r = (((s >> 11) & 0x1F) * sa + ((d >> 11) & 0x1F) * da) >> 5;
                            const uint32_t g = (((s >> 5)  & 0x3F) * sa + ((d >> 5)  & 0x3F) * da) >> 5;
                            const uint32_t b = (((s & 0x1F)         * sa + (d & 0x1F)         * da) >> 5);
                            cDst[i] = (uint16_t)((r << 11) | (g << 5) | b);
                        }
                    }
                }
                u += dudx; v += dvdx; z += dzdx;
            }
        }
        else if (fastOpaque)
        {
            /*
             * Fast path for the case that dominates the 3D view: an opaque,
             * fully lit, power-of-two texture with depth test and mask on.
             *
             * Every flag tested below is invariant across the span, yet the
             * general loop re-tests them per pixel and the compiler cannot hoist
             * them because they sit behind the texture fetch. On this MIPS core,
             * with no branch predictor to speak of, that is several branches and
             * a test-and-mask per pixel for conditions that never change. The
             * lighting multiply is skipped too: lightLevel 15 is identity, so
             * applyLightScale() would multiply by 32 and shift back to the same
             * value.
             */
            for (int i = 0; i < count; ++i)
            {
                const int tu = (int)(u >> FX_SHIFT) & maskU;
                const int tv = (int)(v >> FX_SHIFT) & maskV;
                const int idx = tv * texW + tu;
                if (alp[idx] >= alphaCut)
                {
                    const uint16_t curZ = (z >= FX_ONE) ? 0xFFFF : ((z <= 0) ? 0 : (uint16_t)z);
                    if (curZ <= zDst[i])
                    {
                        zDst[i] = curZ;
                        cDst[i] = pix[idx];
                    }
                }
                u += dudx; v += dvdx; z += dzdx;
            }
        }
        else
        {
            for (int i = 0; i < count; ++i)
            {
                int tu, tv;
                if (isPow2) { tu = (int)(u >> FX_SHIFT) & maskU; tv = (int)(v >> FX_SHIFT) & maskV; }
                else
                {
                    tu = (int)(u >> FX_SHIFT) % texW; if (tu < 0) tu += texW;
                    tv = (int)(v >> FX_SHIFT) % texH; if (tv < 0) tv += texH;
                }
                const int idx = tv * texW + tu;
                if (alp[idx] >= alphaCut)
                {
                    const uint16_t curZ = (z >= FX_ONE) ? 0xFFFF : ((z <= 0) ? 0 : (uint16_t)z);
                    if (!doDepthTest || curZ <= zDst[i])
                    {
                        uint16_t s = pix[idx];
                        if (!identityLight)
                            s = applyLightScale(s, sr, sg, sb);
                        if (hasColorMod)
                            s = applyColorMod(s, cr_plus1, cg_plus1, cb_plus1);
                        if (doDepthMask) zDst[i] = curZ;
                        cDst[i] = s;
                    }
                }
                u += dudx; v += dvdx; z += dzdx;
            }
        }
    }
    else
    {
        // untextured: flat colour
        if (m_blend)
        {
            for (int i = 0; i < count; ++i)
            {
                if (flatAlpha >= alphaCut)
                {
                    const uint16_t curZ = (z >= FX_ONE) ? 0xFFFF : ((z <= 0) ? 0 : (uint16_t)z);
                    if (!doDepthTest || curZ <= zDst[i])
                    {
                        uint16_t s = flatColor565;
                        if (!identityLight)
                            s = applyLightScale(s, sr, sg, sb);
                        if (flatAlpha >= 240)
                        {
                            if (doDepthMask) zDst[i] = curZ;
                            cDst[i] = s;
                        }
                        else
                        {
                            const uint16_t d = cDst[i];
                            const uint32_t sa = flatAlpha >> 3;
                            const uint32_t da = 32 - sa;
                            const uint32_t r = (((s >> 11) & 0x1F) * sa + ((d >> 11) & 0x1F) * da) >> 5;
                            const uint32_t g = (((s >> 5)  & 0x3F) * sa + ((d >> 5)  & 0x3F) * da) >> 5;
                            const uint32_t b = (((s & 0x1F)         * sa + (d & 0x1F)         * da) >> 5);
                            cDst[i] = (uint16_t)((r << 11) | (g << 5) | b);
                        }
                    }
                }
                z += dzdx;
            }
        }
        else
        {
            for (int i = 0; i < count; ++i)
            {
                if (flatAlpha >= alphaCut)
                {
                    const uint16_t curZ = (z >= FX_ONE) ? 0xFFFF : ((z <= 0) ? 0 : (uint16_t)z);
                    if (!doDepthTest || curZ <= zDst[i])
                    {
                        uint16_t s = flatColor565;
                        if (!identityLight)
                            s = applyLightScale(s, sr, sg, sb);
                        if (doDepthMask) zDst[i] = curZ;
                        cDst[i] = s;
                    }
                }
                z += dzdx;
            }
        }
    }
}

} // namespace sf2000_sw
