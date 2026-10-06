#pragma once

#include <cstdint>
#include <cstddef>

enum class RenderPrimitive
{
    Points = 0x0000,
    Lines = 0x0001,
    LineLoop = 0x0002,
    LineStrip = 0x0003,
    Triangles = 0x0004,
    TriangleStrip = 0x0005,
    TriangleFan = 0x0006,
    Quads = 0x0007
};

enum class RenderCapability
{
    Texture2D,
    ColorMaterial,
    CullFace,
    AlphaTest,
    Blend,
    DepthTest,
    Fog,
    Lighting,
    Normalize,
    RescaleNormal,
    Light0,
    Light1,
    PolygonOffsetFill
};

enum class RenderCompare
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

enum class RenderBlendFactor
{
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    DstColor,
    OneMinusDstColor
};

enum class RenderFace
{
    Front,
    Back,
    FrontAndBack
};

namespace RenderClearMask
{
    enum
    {
        Color = 0x00004000,
        Depth = 0x00000100,
        Stencil = 0x00000400
    };
}

enum class RenderMatrixMode
{
    ModelView,
    Projection,
    Texture
};

enum class RenderMatrixQuery
{
    ModelView,
    Projection,
    Texture
};

struct RenderInterleavedMesh
{
    const void* data = nullptr;
    int stride = 0;
    int first = 0;
    int count = 0;
    RenderPrimitive primitive = RenderPrimitive::Triangles;
    bool positionShort = false;

    bool hasTexture = false;
    int texCoordOffset = 0;

    bool hasColor = false;
    int colorOffset = 0;

    bool hasNormals = false;
    int normalOffset = 0;

    bool hasBrightness = false;
    int brightnessOffset = 0;
};
