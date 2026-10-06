#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include "FixedMath.h"
#include "RenderTypes.h"

namespace sf2000_sw
{

constexpr int SCREEN_WIDTH = 320;
constexpr int SCREEN_HEIGHT = 240;
constexpr int SCREEN_PIXELS = SCREEN_WIDTH * SCREEN_HEIGHT;

struct SwTexture
{
    int width = 0;
    int height = 0;
    std::vector<uint16_t> pixels565;
    std::vector<uint8_t> alpha; // 0..255
    bool hasAlpha = false;
};

struct Vertex3D
{
    float x, y, z;
    float u, v;
    uint32_t color; // RGBA
    int brightness;
};

struct TransformedVert
{
    float sx, sy, sz;
    float u, v;
    bool valid;
};

// Lightweight instrumentation. Counts are independent of host speed, so they can
// be measured on the PC build and are equally valid for the MIPS target.
struct RasterStats
{
    long long meshes = 0;
    long long verts = 0;
    long long tris = 0;
    long long trisRejected = 0;   // invalid / back-facing / off-screen
    long long trisRasterized = 0;
    long long spans = 0;
    long long pixels = 0;         // pixels entering the inner loop
};

class SoftwareRasterizer
{
public:
    static SoftwareRasterizer& instance();

    void init();
    void reset();

    // Framebuffer access
    uint16_t* getFramebuffer();
    const uint16_t* getFramebuffer() const;
    uint16_t* getDepthBuffer() { return m_depthBuffer; }

    void clear(unsigned int mask);
    void clearColor(float r, float g, float b, float a);
    void clearDepth(float depth);

    void setBlockResolution(int res);
    int getBlockResolution() const { return m_blockResolution; }
    // active raster target size (differs from 320x240 in the low-res 3D modes)
    int getRasterWidth() const { return m_curWidth; }
    int getRasterHeight() const { return m_curHeight; }
    bool isOrtho() const { return m_isOrtho; }

    // Viewport
    void setViewport(int x, int y, int width, int height);
    void getViewport(int* values);

    // Matrix operations
    void matrixMode(RenderMatrixMode mode);
    void loadIdentity();
    void pushMatrix();
    void popMatrix();
    void translate(float x, float y, float z);
    void rotate(float angle, float x, float y, float z);
    void scale(float x, float y, float z);
    void multMatrix(const float* m);
    void frustum(float left, float right, float bottom, float top, float nearVal, float farVal);
    void ortho(float left, float right, float bottom, float top, float nearVal, float farVal);
    void setPerspective();
    void getMatrix(RenderMatrixQuery query, float* values);

    // State
    void enable(RenderCapability cap);
    void disable(RenderCapability cap);
    void depthMask(bool enabled) { m_depthMask = enabled; }
    void depthFunc(RenderCompare func) { m_depthFunc = func; }
    void alphaFunc(RenderCompare func, float ref) { m_alphaFunc = func; m_alphaRef = (uint8_t)(ref * 255.0f); }
    void cullFace(RenderFace face) { m_cullFaceMode = face; }
    void blendFunc(RenderBlendFactor src, RenderBlendFactor dst) { m_blendSrc = src; m_blendDst = dst; }
    void colorMask(bool r, bool g, bool b, bool a) { (void)r; (void)g; (void)b; (void)a; }
    void setColor4f(float r, float g, float b, float a);

    // Textures
    void generateTextures(int count, int* textures);
    void deleteTextures(int count, const int* textures);
    void bindTexture(int texture);
    void uploadTextureImage(int level, int width, int height, const void* pixels);
    void uploadTextureSubImage(int level, int x, int y, int width, int height, const void* pixels);
    bool isTextureValid(int texture) const;

    // Drawing
    bool drawInterleaved(const RenderInterleavedMesh& mesh);

    void flush3DToFramebuffer();
    void flushPendingColorClear();
    void updateTargetBuffers();

    // Instrumentation (cheap counters; used to validate optimisation work)
    const RasterStats& getStats() const { return m_stats; }
    void resetStats() { m_stats = RasterStats(); }

private:
    SoftwareRasterizer();
    ~SoftwareRasterizer();

    void updateMvp();
    void initLightTable();

    // Perspective-correct-enough affine rasterizer over pre-transformed vertices
    void drawTriangleTransformed(const TransformedVert& v0, const TransformedVert& v1, const TransformedVert& v2,
                                uint32_t color, int brightness);
    void drawSpan(int y, int x0, int x1, fixed_t u0, fixed_t v0, fixed_t z0,
                  fixed_t dudx, fixed_t dvdx, fixed_t dzdx, uint8_t lightLevel, const SwTexture* tex,
                  uint16_t flatColor565, uint8_t flatAlpha);

    void upscale_80x60_to_320x240();
    void upscale_160x120_to_320x240();
    void upscale_240x180_to_320x240();

    uint16_t* m_framebuffer = nullptr;   // 320x240, always full size
    uint16_t* m_depthBuffer = nullptr;   // 320x240, shared by 3D and GUI (pitch == SCREEN_WIDTH)
    uint16_t* m_color3D = nullptr;       // 3D color target (low-res modes only)

    uint16_t* m_curColorBuffer = nullptr;
    int m_curWidth = SCREEN_WIDTH;
    int m_curHeight = SCREEN_HEIGHT;
    bool m_3dRendered = false;
    bool m_pendingColorClear = false;

    // Per-light-level RGB565 scaling coefficients (register math replaces the old 2 MB LUT)
    uint32_t m_lightR[16] = {0};
    uint32_t m_lightG[16] = {0};
    uint32_t m_lightB[16] = {0};

    uint16_t m_clearColor565 = 0;
    uint16_t m_clearDepthVal = 0xFFFF;

    int m_vpX = 0, m_vpY = 0, m_vpWidth = SCREEN_WIDTH, m_vpHeight = SCREEN_HEIGHT;

    // Matrix stacks
    RenderMatrixMode m_currentMatrixMode = RenderMatrixMode::ModelView;
    FixedMat4 m_modelView;
    FixedMat4 m_projection;
    FixedMat4 m_mvp;
    bool m_mvpDirty = true;
    std::vector<FixedMat4> m_modelViewStack;
    std::vector<FixedMat4> m_projectionStack;

    // States
    bool m_depthTest = true;
    bool m_depthMask = true;
    RenderCompare m_depthFunc = RenderCompare::LessEqual;
    bool m_alphaTest = false;
    RenderCompare m_alphaFunc = RenderCompare::Greater;
    uint8_t m_alphaRef = 25; // ~0.1f
    bool m_cullFace = false;
    RenderFace m_cullFaceMode = RenderFace::Back;
    bool m_blend = false;
    RenderBlendFactor m_blendSrc = RenderBlendFactor::SrcAlpha;
    RenderBlendFactor m_blendDst = RenderBlendFactor::OneMinusSrcAlpha;
    bool m_texture2D = true;
    bool m_lighting = true;
    uint32_t m_currentColor = 0xFFFFFFFF; // RGBA8888
    uint16_t m_currentColor565 = 0xFFFF;

    // Textures
    std::vector<SwTexture> m_textures;
    int m_boundTexture = 0;

    int m_blockResolution = 1; // 0 = 1/4 (80x60), 1 = 2/4 (160x120), 2 = 3/4 (240x180), 3 = 4/4 (320x240)
    bool m_isOrtho = false;

    RasterStats m_stats;
};

} // namespace sf2000_sw
