#include "gles_sw.h"
#include "Rasterizer_SW.h"
#include "FixedMath.h"
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdlib>

using namespace sf2000_sw;

// ============================================================================
// VBO Buffer Management
// ============================================================================
struct GlBuffer
{
    std::vector<uint8_t> data;
};

static std::vector<GlBuffer> s_buffers;
static GLuint s_boundArrayBuffer = 0;
static GLuint s_boundElementArrayBuffer = 0;

static inline GlBuffer* getBuffer(GLuint id)
{
    if (id == 0 || id >= s_buffers.size())
        return nullptr;
    return &s_buffers[id];
}

void glGenBuffers(GLsizei n, GLuint *buffers)
{
    if (!buffers) return;
    for (GLsizei i = 0; i < n; ++i)
    {
        GLuint id = (GLuint)s_buffers.size();
        if (id == 0)
        {
            s_buffers.resize(1); // reserve index 0
            id = 1;
        }
        s_buffers.emplace_back();
        buffers[i] = id;
    }
}

void anGenBuffers(GLsizei n, GLuint *buffers)
{
    glGenBuffers(n, buffers);
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    if (!buffers) return;
    for (GLsizei i = 0; i < n; ++i)
    {
        GLuint id = buffers[i];
        if (id < s_buffers.size())
        {
            s_buffers[id].data.clear();
            s_buffers[id].data.shrink_to_fit();
        }
        if (s_boundArrayBuffer == id) s_boundArrayBuffer = 0;
        if (s_boundElementArrayBuffer == id) s_boundElementArrayBuffer = 0;
    }
}

void glBindBuffer(GLenum target, GLuint buffer)
{
    if (target == GL_ARRAY_BUFFER)
        s_boundArrayBuffer = buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER)
        s_boundElementArrayBuffer = buffer;
}

void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
    (void)usage;
    GLuint bufId = (target == GL_ARRAY_BUFFER) ? s_boundArrayBuffer : s_boundElementArrayBuffer;
    if (bufId == 0) return;

    if (bufId >= s_buffers.size())
        s_buffers.resize(bufId + 1);

    GlBuffer& buf = s_buffers[bufId];
    buf.data.resize(size);
    if (data)
        std::memcpy(buf.data.data(), data, size);
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
    GLuint bufId = (target == GL_ARRAY_BUFFER) ? s_boundArrayBuffer : s_boundElementArrayBuffer;
    GlBuffer* buf = getBuffer(bufId);
    if (!buf || !data) return;

    if (offset + size <= (GLintptr)buf->data.size())
        std::memcpy(buf->data.data() + offset, data, size);
}

// ============================================================================
// Client Vertex Arrays
// ============================================================================
static bool s_clientVertexEnabled = false;
static GLint s_vertexSize = 3;
static GLenum s_vertexType = GL_FLOAT;
static GLsizei s_vertexStride = 0;
static const GLvoid* s_vertexPtr = nullptr;
static GLuint s_vertexVbo = 0;

static bool s_clientTexCoordEnabled = false;
static GLint s_texCoordSize = 2;
static GLenum s_texCoordType = GL_FLOAT;
static GLsizei s_texCoordStride = 0;
static const GLvoid* s_texCoordPtr = nullptr;
static GLuint s_texCoordVbo = 0;

static bool s_clientColorEnabled = false;
static GLint s_colorSize = 4;
static GLenum s_colorType = GL_UNSIGNED_BYTE;
static GLsizei s_colorStride = 0;
static const GLvoid* s_colorPtr = nullptr;
static GLuint s_colorVbo = 0;

static bool s_clientNormalEnabled = false;
static GLsizei s_normalStride = 0;
static const GLvoid* s_normalPtr = nullptr;
static GLuint s_normalVbo = 0;

void glEnableClientState(GLenum array)
{
    switch (array)
    {
        case GL_VERTEX_ARRAY:        s_clientVertexEnabled = true; break;
        case GL_TEXTURE_COORD_ARRAY: s_clientTexCoordEnabled = true; break;
        case GL_COLOR_ARRAY:         s_clientColorEnabled = true; break;
        case GL_NORMAL_ARRAY:        s_clientNormalEnabled = true; break;
        default: break;
    }
}

void glDisableClientState(GLenum array)
{
    switch (array)
    {
        case GL_VERTEX_ARRAY:        s_clientVertexEnabled = false; break;
        case GL_TEXTURE_COORD_ARRAY: s_clientTexCoordEnabled = false; break;
        case GL_COLOR_ARRAY:         s_clientColorEnabled = false; break;
        case GL_NORMAL_ARRAY:        s_clientNormalEnabled = false; break;
        default: break;
    }
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    s_vertexSize = size;
    s_vertexType = type;
    s_vertexStride = stride;
    s_vertexPtr = pointer;
    s_vertexVbo = s_boundArrayBuffer;
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    s_texCoordSize = size;
    s_texCoordType = type;
    s_texCoordStride = stride;
    s_texCoordPtr = pointer;
    s_texCoordVbo = s_boundArrayBuffer;
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    s_colorSize = size;
    s_colorType = type;
    s_colorStride = stride;
    s_colorPtr = pointer;
    s_colorVbo = s_boundArrayBuffer;
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    (void)type;
    s_normalStride = stride;
    s_normalPtr = pointer;
    s_normalVbo = s_boundArrayBuffer;
}

// ============================================================================
// State and Capabilities
// ============================================================================
static bool mapCapability(GLenum cap, RenderCapability* outCap)
{
    switch (cap)
    {
        case GL_TEXTURE_2D:     *outCap = RenderCapability::Texture2D; return true;
        case GL_CULL_FACE:      *outCap = RenderCapability::CullFace; return true;
        case GL_ALPHA_TEST:     *outCap = RenderCapability::AlphaTest; return true;
        case GL_BLEND:          *outCap = RenderCapability::Blend; return true;
        case GL_DEPTH_TEST:     *outCap = RenderCapability::DepthTest; return true;
        case GL_FOG:            *outCap = RenderCapability::Fog; return true;
        case GL_LIGHTING:       *outCap = RenderCapability::Lighting; return true;
        case GL_COLOR_MATERIAL: *outCap = RenderCapability::ColorMaterial; return true;
        case GL_NORMALIZE:      *outCap = RenderCapability::Normalize; return true;
        case GL_RESCALE_NORMAL: *outCap = RenderCapability::RescaleNormal; return true;
        case GL_POLYGON_OFFSET_FILL: *outCap = RenderCapability::PolygonOffsetFill; return true;
        default: return false;
    }
}

static RenderCompare mapCompare(GLenum func)
{
    switch (func)
    {
        case GL_NEVER:    return RenderCompare::Never;
        case GL_LESS:     return RenderCompare::Less;
        case GL_EQUAL:    return RenderCompare::Equal;
        case GL_LEQUAL:   return RenderCompare::LessEqual;
        case GL_GREATER:  return RenderCompare::Greater;
        case GL_NOTEQUAL: return RenderCompare::NotEqual;
        case GL_GEQUAL:   return RenderCompare::GreaterEqual;
        case GL_ALWAYS:   return RenderCompare::Always;
        default:          return RenderCompare::LessEqual;
    }
}

static RenderBlendFactor mapBlendFactor(GLenum factor)
{
    switch (factor)
    {
        case GL_ZERO:                return RenderBlendFactor::Zero;
        case GL_ONE:                 return RenderBlendFactor::One;
        case GL_SRC_COLOR:           return RenderBlendFactor::SrcColor;
        case GL_ONE_MINUS_SRC_COLOR: return RenderBlendFactor::OneMinusSrcColor;
        case GL_SRC_ALPHA:           return RenderBlendFactor::SrcAlpha;
        case GL_ONE_MINUS_SRC_ALPHA: return RenderBlendFactor::OneMinusSrcAlpha;
        case GL_DST_ALPHA:           return RenderBlendFactor::DstAlpha;
        case GL_ONE_MINUS_DST_ALPHA: return RenderBlendFactor::OneMinusDstAlpha;
        case GL_DST_COLOR:           return RenderBlendFactor::DstColor;
        case GL_ONE_MINUS_DST_COLOR: return RenderBlendFactor::OneMinusDstColor;
        default:                     return RenderBlendFactor::SrcAlpha;
    }
}

static RenderPrimitive mapPrimitive(GLenum mode)
{
    switch (mode)
    {
        case GL_POINTS:         return RenderPrimitive::Points;
        case GL_LINES:          return RenderPrimitive::Lines;
        case GL_LINE_LOOP:      return RenderPrimitive::LineLoop;
        case GL_LINE_STRIP:     return RenderPrimitive::LineStrip;
        case GL_TRIANGLES:      return RenderPrimitive::Triangles;
        case GL_TRIANGLE_STRIP: return RenderPrimitive::TriangleStrip;
        case GL_TRIANGLE_FAN:   return RenderPrimitive::TriangleFan;
        case GL_QUADS:          return RenderPrimitive::Quads;
        default:                return RenderPrimitive::Triangles;
    }
}

void glEnable(GLenum cap)
{
    RenderCapability c;
    if (mapCapability(cap, &c))
        SoftwareRasterizer::instance().enable(c);
}

void glDisable(GLenum cap)
{
    RenderCapability c;
    if (mapCapability(cap, &c))
        SoftwareRasterizer::instance().disable(c);
}

GLboolean glIsEnabled(GLenum cap)
{
    (void)cap;
    return GL_FALSE;
}

void glDepthFunc(GLenum func)
{
    SoftwareRasterizer::instance().depthFunc(mapCompare(func));
}

void glDepthMask(GLboolean flag)
{
    SoftwareRasterizer::instance().depthMask(flag != GL_FALSE);
}

void glDepthRangef(GLclampf zNear, GLclampf zFar)
{
    (void)zNear; (void)zFar;
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
    SoftwareRasterizer::instance().alphaFunc(mapCompare(func), ref);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    SoftwareRasterizer::instance().blendFunc(mapBlendFactor(sfactor), mapBlendFactor(dfactor));
}

void glCullFace(GLenum mode)
{
    RenderFace f = (mode == GL_FRONT) ? RenderFace::Front :
                   (mode == GL_FRONT_AND_BACK) ? RenderFace::FrontAndBack : RenderFace::Back;
    SoftwareRasterizer::instance().cullFace(f);
}

void glShadeModel(GLenum mode)
{
    (void)mode;
}

void glHint(GLenum target, GLenum mode)
{
    (void)target; (void)mode;
}

void glClear(GLbitfield mask)
{
    SoftwareRasterizer::instance().clear(mask);
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    SoftwareRasterizer::instance().clearColor(red, green, blue, alpha);
}

void glClearDepthf(GLclampf depth)
{
    SoftwareRasterizer::instance().clearDepth(depth);
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    SoftwareRasterizer::instance().setColor4f(red, green, blue, alpha);
}

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    (void)nx; (void)ny; (void)nz;
}

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    SoftwareRasterizer::instance().setColor4f(
        (float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f, (float)alpha / 255.0f
    );
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    SoftwareRasterizer::instance().setViewport(x, y, width, height);
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    (void)x; (void)y; (void)width; (void)height;
}

// ============================================================================
// Matrices
// ============================================================================
void glMatrixMode(GLenum mode)
{
    if (mode == GL_MODELVIEW)
        SoftwareRasterizer::instance().matrixMode(RenderMatrixMode::ModelView);
    else if (mode == GL_PROJECTION)
        SoftwareRasterizer::instance().matrixMode(RenderMatrixMode::Projection);
    else if (mode == GL_TEXTURE)
        SoftwareRasterizer::instance().matrixMode(RenderMatrixMode::Texture);
}

void glLoadIdentity(void)
{
    SoftwareRasterizer::instance().loadIdentity();
}

void glPushMatrix(void)
{
    SoftwareRasterizer::instance().pushMatrix();
}

void glPopMatrix(void)
{
    SoftwareRasterizer::instance().popMatrix();
}

void glMultMatrixf(const GLfloat *m)
{
    if (m)
        SoftwareRasterizer::instance().multMatrix(m);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    SoftwareRasterizer::instance().translate(x, y, z);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    SoftwareRasterizer::instance().rotate(angle, x, y, z);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    SoftwareRasterizer::instance().scale(x, y, z);
}

void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar)
{
    SoftwareRasterizer::instance().ortho(left, right, bottom, top, zNear, zFar);
}

void glOrtho(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar)
{
    SoftwareRasterizer::instance().ortho(left, right, bottom, top, zNear, zFar);
}

void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar)
{
    SoftwareRasterizer::instance().frustum(left, right, bottom, top, zNear, zFar);
}

void glFrustum(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar)
{
    SoftwareRasterizer::instance().frustum(left, right, bottom, top, zNear, zFar);
}

// ============================================================================
// Textures
// ============================================================================
void glGenTextures(GLsizei n, GLuint *textures)
{
    if (textures && n > 0)
        SoftwareRasterizer::instance().generateTextures(n, (int*)textures);
}

void glDeleteTextures(GLsizei n, const GLuint *textures)
{
    if (textures && n > 0)
        SoftwareRasterizer::instance().deleteTextures(n, (const int*)textures);
}

void glBindTexture(GLenum target, GLuint texture)
{
    (void)target;
    SoftwareRasterizer::instance().bindTexture((int)texture);
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    (void)target; (void)internalformat; (void)border;
    if (!pixels)
    {
        SoftwareRasterizer::instance().uploadTextureImage(level, width, height, nullptr);
        return;
    }

    if (format == GL_RGBA && type == GL_UNSIGNED_BYTE)
    {
        SoftwareRasterizer::instance().uploadTextureImage(level, width, height, pixels);
    }
    else if (format == GL_RGB && type == GL_UNSIGNED_BYTE)
    {
        // Expand RGB to RGBA
        std::vector<uint32_t> rgba(width * height);
        const uint8_t* src = (const uint8_t*)pixels;
        for (int i = 0; i < width * height; ++i)
        {
            uint8_t r = src[i * 3 + 0];
            uint8_t g = src[i * 3 + 1];
            uint8_t b = src[i * 3 + 2];
            rgba[i] = (255u << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
        }
        SoftwareRasterizer::instance().uploadTextureImage(level, width, height, rgba.data());
    }
    else
    {
        SoftwareRasterizer::instance().uploadTextureImage(level, width, height, pixels);
    }
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    (void)target;
    if (!pixels) return;

    if (format == GL_RGBA && type == GL_UNSIGNED_BYTE)
    {
        SoftwareRasterizer::instance().uploadTextureSubImage(level, xoffset, yoffset, width, height, pixels);
    }
    else if (format == GL_RGB && type == GL_UNSIGNED_BYTE)
    {
        std::vector<uint32_t> rgba(width * height);
        const uint8_t* src = (const uint8_t*)pixels;
        for (int i = 0; i < width * height; ++i)
        {
            uint8_t r = src[i * 3 + 0];
            uint8_t g = src[i * 3 + 1];
            uint8_t b = src[i * 3 + 2];
            rgba[i] = (255u << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
        }
        SoftwareRasterizer::instance().uploadTextureSubImage(level, xoffset, yoffset, width, height, rgba.data());
    }
    else
    {
        SoftwareRasterizer::instance().uploadTextureSubImage(level, xoffset, yoffset, width, height, pixels);
    }
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    (void)target; (void)pname; (void)param;
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    (void)target; (void)pname; (void)param;
}

// ============================================================================
// Queries
// ============================================================================
void glGetIntegerv(GLenum pname, GLint *params)
{
    if (!params) return;
    if (pname == GL_VIEWPORT)
    {
        SoftwareRasterizer::instance().getViewport(params);
    }
    else
    {
        params[0] = 0;
    }
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
    if (!params) return;
    if (pname == GL_MODELVIEW_MATRIX)
    {
        SoftwareRasterizer::instance().getMatrix(RenderMatrixQuery::ModelView, params);
    }
    else if (pname == GL_PROJECTION_MATRIX)
    {
        SoftwareRasterizer::instance().getMatrix(RenderMatrixQuery::Projection, params);
    }
}

const GLubyte* glGetString(GLenum name)
{
    switch (name)
    {
        case GL_VENDOR:   return (const GLubyte*)"DataFrog";
        case GL_RENDERER: return (const GLubyte*)"SF2000 Software Rasterizer";
        case GL_VERSION:  return (const GLubyte*)"OpenGL ES 1.1 (SF2000 SW)";
        default:          return (const GLubyte*)"";
    }
}

GLenum glGetError(void)
{
    return GL_NO_ERROR;
}

void glFogf(GLenum pname, GLfloat param) { (void)pname; (void)param; }
void glFogi(GLenum pname, GLint param) { (void)pname; (void)param; }
void glFogfv(GLenum pname, const GLfloat *params) { (void)pname; (void)params; }
void glFogx(GLenum pname, GLfixed param) { (void)pname; (void)param; }
void glFogxv(GLenum pname, const GLfixed *params) { (void)pname; (void)params; }

void glLineWidth(GLfloat width) { (void)width; }
void glPolygonOffset(GLfloat factor, GLfloat units) { (void)factor; (void)units; }
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    SoftwareRasterizer::instance().colorMask(red != GL_FALSE, green != GL_FALSE, blue != GL_FALSE, alpha != GL_FALSE);
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    (void)x; (void)y; (void)width; (void)height; (void)format; (void)type; (void)pixels;
}

// ============================================================================
// Drawing
// ============================================================================
void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (count <= 0) return;

    const uint8_t* basePtr = nullptr;
    if (s_vertexVbo > 0)
    {
        GlBuffer* buf = getBuffer(s_vertexVbo);
        if (!buf || buf->data.empty()) return;
        basePtr = buf->data.data() + (uintptr_t)s_vertexPtr;
    }
    else
    {
        basePtr = (const uint8_t*)s_vertexPtr;
    }
    if (!basePtr) return;

    RenderInterleavedMesh mesh;
    mesh.data = basePtr;
    mesh.stride = s_vertexStride ? s_vertexStride : (s_vertexSize * (int)sizeof(float));
    mesh.first = first;
    mesh.count = count;
    mesh.primitive = mapPrimitive(mode);

    if (s_clientTexCoordEnabled)
    {
        mesh.hasTexture = true;
        if (s_texCoordVbo == s_vertexVbo && s_vertexVbo > 0)
            mesh.texCoordOffset = (int)((uintptr_t)s_texCoordPtr - (uintptr_t)s_vertexPtr);
        else if (s_vertexVbo == 0)
            mesh.texCoordOffset = (int)((const uint8_t*)s_texCoordPtr - (const uint8_t*)s_vertexPtr);
        else
            mesh.texCoordOffset = 12;
    }
    else
    {
        mesh.hasTexture = false;
    }

    if (s_clientColorEnabled)
    {
        mesh.hasColor = true;
        if (s_colorVbo == s_vertexVbo && s_vertexVbo > 0)
            mesh.colorOffset = (int)((uintptr_t)s_colorPtr - (uintptr_t)s_vertexPtr);
        else if (s_vertexVbo == 0)
            mesh.colorOffset = (int)((const uint8_t*)s_colorPtr - (const uint8_t*)s_vertexPtr);
        else
            mesh.colorOffset = 20;
    }
    else
    {
        mesh.hasColor = false;
    }

    mesh.hasBrightness = false;
    SoftwareRasterizer::instance().drawInterleaved(mesh);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    (void)mode; (void)count; (void)type; (void)indices;
}

void drawArrayVT(int bufferId, int vertices, int vertexSize, unsigned int mode)
{
    GlBuffer* buf = getBuffer(bufferId);
    if (!buf || buf->data.empty() || vertices <= 0) return;

    RenderInterleavedMesh mesh;
    mesh.data = buf->data.data();
    mesh.stride = vertexSize;
    mesh.first = 0;
    mesh.count = vertices;
    mesh.primitive = mapPrimitive(mode);
    mesh.hasTexture = true;
    mesh.texCoordOffset = 12; // 3 floats for pos
    mesh.hasColor = false;
    mesh.hasBrightness = false;

    SoftwareRasterizer::instance().drawInterleaved(mesh);
}

void drawArrayVT_NoState(int bufferId, int vertices, int vertexSize)
{
    drawArrayVT(bufferId, vertices, vertexSize, GL_TRIANGLES);
}

void drawArrayVTC(int bufferId, int vertices, int vertexSize)
{
    GlBuffer* buf = getBuffer(bufferId);
    if (!buf || buf->data.empty() || vertices <= 0) return;

    RenderInterleavedMesh mesh;
    mesh.data = buf->data.data();
    mesh.stride = vertexSize;
    mesh.first = 0;
    mesh.count = vertices;
    mesh.primitive = RenderPrimitive::Triangles;
    mesh.hasTexture = true;
    mesh.texCoordOffset = 12; // 3 floats pos
    mesh.hasColor = true;
    mesh.colorOffset = 20;   // 3 floats pos + 2 floats uv = 20 bytes
    mesh.hasBrightness = false;

    SoftwareRasterizer::instance().drawInterleaved(mesh);
}

void drawArrayVTC_NoState(int bufferId, int vertices, int vertexSize)
{
    drawArrayVTC(bufferId, vertices, vertexSize);
}

void glInit(void)
{
    SoftwareRasterizer::instance().init();
}

// ============================================================================
// GLU Perspective & Project Helpers
// ============================================================================
static const float s_glPi = 3.14159265358979323846f;

void gluPerspective(GLfloat fovy, GLfloat aspect, GLfloat zNear, GLfloat zFar)
{
    GLfloat sine, deltaZ;
    GLfloat radians = (GLfloat)(fovy / 2.0f * s_glPi / 180.0f);

    deltaZ = zFar - zNear;
    sine = (GLfloat)std::sin(radians);
    if (deltaZ == 0.0f || sine == 0.0f || aspect == 0.0f)
        return;

    GLfloat cotangent = (GLfloat)(std::cos(radians) / sine);

    GLfloat m[16];
    std::memset(m, 0, sizeof(m));
    m[0]  = cotangent / aspect;
    m[5]  = cotangent;
    m[10] = -(zFar + zNear) / deltaZ;
    m[11] = -1.0f;
    m[14] = -2.0f * zNear * zFar / deltaZ;

    SoftwareRasterizer::instance().setPerspective();
    glMultMatrixf(m);
}

static void MultiplyMatrices4by4OpenGL_FLOAT(float *result, float *matrix1, float *matrix2)
{
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            result[j * 4 + i] = matrix1[i] * matrix2[j * 4] +
                                matrix1[4 + i] * matrix2[j * 4 + 1] +
                                matrix1[8 + i] * matrix2[j * 4 + 2] +
                                matrix1[12 + i] * matrix2[j * 4 + 3];
        }
    }
}

static void MultiplyMatrixByVector4by4OpenGL_FLOAT(float *resultvector, const float *matrix, const float *pvector)
{
    resultvector[0] = matrix[0] * pvector[0] + matrix[4] * pvector[1] + matrix[8] * pvector[2] + matrix[12] * pvector[3];
    resultvector[1] = matrix[1] * pvector[0] + matrix[5] * pvector[1] + matrix[9] * pvector[2] + matrix[13] * pvector[3];
    resultvector[2] = matrix[2] * pvector[0] + matrix[6] * pvector[1] + matrix[10] * pvector[2] + matrix[14] * pvector[3];
    resultvector[3] = matrix[3] * pvector[0] + matrix[7] * pvector[1] + matrix[11] * pvector[2] + matrix[15] * pvector[3];
}

#define MAT(m,r,c) (m)[(c)*4+(r)]
#define SWAP_ROWS_FLOAT(a, b) { float *_tmp = a; (a)=(b); (b)=_tmp; }

static int glhInvertMatrixf2(float *m, float *out)
{
    float wtmp[4][8];
    float m0, m1, m2, m3, s;
    float *r0 = &wtmp[0][0], *r1 = &wtmp[1][0], *r2 = &wtmp[2][0], *r3 = &wtmp[3][0];

    r0[0] = MAT(m, 0, 0); r0[1] = MAT(m, 0, 1); r0[2] = MAT(m, 0, 2); r0[3] = MAT(m, 0, 3);
    r0[4] = 1.0f; r0[5] = 0.0f; r0[6] = 0.0f; r0[7] = 0.0f;
    r1[0] = MAT(m, 1, 0); r1[1] = MAT(m, 1, 1); r1[2] = MAT(m, 1, 2); r1[3] = MAT(m, 1, 3);
    r1[5] = 1.0f; r1[4] = 0.0f; r1[6] = 0.0f; r1[7] = 0.0f;
    r2[0] = MAT(m, 2, 0); r2[1] = MAT(m, 2, 1); r2[2] = MAT(m, 2, 2); r2[3] = MAT(m, 2, 3);
    r2[6] = 1.0f; r2[4] = 0.0f; r2[5] = 0.0f; r2[7] = 0.0f;
    r3[0] = MAT(m, 3, 0); r3[1] = MAT(m, 3, 1); r3[2] = MAT(m, 3, 2); r3[3] = MAT(m, 3, 3);
    r3[7] = 1.0f; r3[4] = 0.0f; r3[5] = 0.0f; r3[6] = 0.0f;

    if (std::fabs(r3[0]) > std::fabs(r2[0])) SWAP_ROWS_FLOAT(r3, r2);
    if (std::fabs(r2[0]) > std::fabs(r1[0])) SWAP_ROWS_FLOAT(r2, r1);
    if (std::fabs(r1[0]) > std::fabs(r0[0])) SWAP_ROWS_FLOAT(r1, r0);
    if (0.0f == r0[0]) return 0;

    m1 = r1[0] / r0[0]; m2 = r2[0] / r0[0]; m3 = r3[0] / r0[0];
    s = r0[1]; r1[1] -= m1 * s; r2[1] -= m2 * s; r3[1] -= m3 * s;
    s = r0[2]; r1[2] -= m1 * s; r2[2] -= m2 * s; r3[2] -= m3 * s;
    s = r0[3]; r1[3] -= m1 * s; r2[3] -= m2 * s; r3[3] -= m3 * s;
    s = r0[4];
    if (s != 0.0f) { r1[4] -= m1 * s; r2[4] -= m2 * s; r3[4] -= m3 * s; }
    s = r0[5];
    if (s != 0.0f) { r1[5] -= m1 * s; r2[5] -= m2 * s; r3[5] -= m3 * s; }
    s = r0[6];
    if (s != 0.0f) { r1[6] -= m1 * s; r2[6] -= m2 * s; r3[6] -= m3 * s; }
    s = r0[7];
    if (s != 0.0f) { r1[7] -= m1 * s; r2[7] -= m2 * s; r3[7] -= m3 * s; }

    if (std::fabs(r3[1]) > std::fabs(r2[1])) SWAP_ROWS_FLOAT(r3, r2);
    if (std::fabs(r2[1]) > std::fabs(r1[1])) SWAP_ROWS_FLOAT(r2, r1);
    if (0.0f == r1[1]) return 0;

    m2 = r2[1] / r1[1]; m3 = r3[1] / r1[1];
    r2[2] -= m2 * r1[2]; r3[2] -= m3 * r1[2];
    r2[3] -= m2 * r1[3]; r3[3] -= m3 * r1[3];
    s = r1[4]; if (0.0f != s) { r2[4] -= m2 * s; r3[4] -= m3 * s; }
    s = r1[5]; if (0.0f != s) { r2[5] -= m2 * s; r3[5] -= m3 * s; }
    s = r1[6]; if (0.0f != s) { r2[6] -= m2 * s; r3[6] -= m3 * s; }
    s = r1[7]; if (0.0f != s) { r2[7] -= m2 * s; r3[7] -= m3 * s; }

    if (std::fabs(r3[2]) > std::fabs(r2[2])) SWAP_ROWS_FLOAT(r3, r2);
    if (0.0f == r2[2]) return 0;

    m3 = r3[2] / r2[2];
    r3[3] -= m3 * r2[3]; r3[4] -= m3 * r2[4]; r3[5] -= m3 * r2[5]; r3[6] -= m3 * r2[6]; r3[7] -= m3 * r2[7];
    if (0.0f == r3[3]) return 0;

    s = 1.0f / r3[3];
    r3[4] *= s; r3[5] *= s; r3[6] *= s; r3[7] *= s;
    m2 = r2[3]; s = 1.0f / r2[2];
    r2[4] = s * (r2[4] - r3[4] * m2); r2[5] = s * (r2[5] - r3[5] * m2);
    r2[6] = s * (r2[6] - r3[6] * m2); r2[7] = s * (r2[7] - r3[7] * m2);
    m1 = r1[3];
    r1[4] -= r3[4] * m1; r1[5] -= r3[5] * m1; r1[6] -= r3[6] * m1; r1[7] -= r3[7] * m1;
    m0 = r0[3];
    r0[4] -= r3[4] * m0; r0[5] -= r3[5] * m0; r0[6] -= r3[6] * m0; r0[7] -= r3[7] * m0;

    m1 = r1[2]; s = 1.0f / r1[1];
    r1[4] = s * (r1[4] - r2[4] * m1); r1[5] = s * (r1[5] - r2[5] * m1);
    r1[6] = s * (r1[6] - r2[6] * m1); r1[7] = s * (r1[7] - r2[7] * m1);
    m0 = r0[2];
    r0[4] -= r2[4] * m0; r0[5] -= r2[5] * m0; r0[6] -= r2[6] * m0; r0[7] -= r2[7] * m0;

    m0 = r0[1]; s = 1.0f / r0[0];
    r0[4] = s * (r0[4] - r1[4] * m0); r0[5] = s * (r0[5] - r1[5] * m0);
    r0[6] = s * (r0[6] - r1[6] * m0); r0[7] = s * (r0[7] - r1[7] * m0);

    MAT(out, 0, 0) = r0[4]; MAT(out, 0, 1) = r0[5]; MAT(out, 0, 2) = r0[6]; MAT(out, 0, 3) = r0[7];
    MAT(out, 1, 0) = r1[4]; MAT(out, 1, 1) = r1[5]; MAT(out, 1, 2) = r1[6]; MAT(out, 1, 3) = r1[7];
    MAT(out, 2, 0) = r2[4]; MAT(out, 2, 1) = r2[5]; MAT(out, 2, 2) = r2[6]; MAT(out, 2, 3) = r2[7];
    MAT(out, 3, 0) = r3[4]; MAT(out, 3, 1) = r3[5]; MAT(out, 3, 2) = r3[6]; MAT(out, 3, 3) = r3[7];
    return 1;
}

int glhUnProjectf(float winx, float winy, float winz,
                  float *modelview, float *projection,
                  int *viewport, float *objectCoordinate)
{
    float m[16], A[16];
    float in[4], out[4];

    MultiplyMatrices4by4OpenGL_FLOAT(A, projection, modelview);
    if (glhInvertMatrixf2(A, m) == 0)
        return 0;

    in[0] = (winx - (float)viewport[0]) / (float)viewport[2] * 2.0f - 1.0f;
    in[1] = (winy - (float)viewport[1]) / (float)viewport[3] * 2.0f - 1.0f;
    in[2] = 2.0f * winz - 1.0f;
    in[3] = 1.0f;

    MultiplyMatrixByVector4by4OpenGL_FLOAT(out, m, in);
    if (out[3] == 0.0f)
        return 0;

    out[3] = 1.0f / out[3];
    objectCoordinate[0] = out[0] * out[3];
    objectCoordinate[1] = out[1] * out[3];
    objectCoordinate[2] = out[2] * out[3];
    return 1;
}
