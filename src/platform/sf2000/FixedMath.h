#pragma once

#include <cstdint>
#include <cmath>

// Q16.16 fixed-point math library for MIPS32 without FPU.
// Screen coordinate space: 320x240 RGB565.

using fixed_t = int32_t;

constexpr int FX_SHIFT = 16;
constexpr fixed_t FX_ONE = 1 << FX_SHIFT;
constexpr fixed_t FX_HALF = 1 << (FX_SHIFT - 1);

#define FX_FROM_INT(x)   ((fixed_t)((x) << FX_SHIFT))
#define FX_TO_INT(x)     ((int)((x) >> FX_SHIFT))
#define FX_ROUND(x)      ((int)(((x) + FX_HALF) >> FX_SHIFT))
#define FX_FROM_FLOAT(x) ((fixed_t)((x) * (float)FX_ONE))
#define FX_TO_FLOAT(x)   ((float)(x) / (float)FX_ONE)

namespace sf2000_fixed
{
inline fixed_t fx_from_int(int x) { return FX_FROM_INT(x); }
inline int fx_to_int(fixed_t x) { return FX_TO_INT(x); }
inline fixed_t fx_from_float(float x) { return FX_FROM_FLOAT(x); }
inline float fx_to_float(fixed_t x) { return FX_TO_FLOAT(x); }
}

inline fixed_t fx_mul(fixed_t a, fixed_t b)
{
    return (fixed_t)(((int64_t)a * (int64_t)b) >> FX_SHIFT);
}

inline fixed_t fx_div(fixed_t a, fixed_t b)
{
    if (b == 0) return (a >= 0) ? 0x7FFFFFFF : -0x7FFFFFFF;
    return (fixed_t)(((int64_t)a << FX_SHIFT) / b);
}

inline fixed_t fx_rcp(fixed_t x)
{
    if (x == 0) return 0x7FFFFFFF;
    return (fixed_t)(((int64_t)FX_ONE << FX_SHIFT) / x);
}

inline fixed_t fx_abs(fixed_t a)
{
    return a >= 0 ? a : -a;
}

inline fixed_t fx_min(fixed_t a, fixed_t b)
{
    return a < b ? a : b;
}

inline fixed_t fx_max(fixed_t a, fixed_t b)
{
    return a > b ? a : b;
}

inline fixed_t fx_clamp(fixed_t v, fixed_t min_val, fixed_t max_val)
{
    return v < min_val ? min_val : (v > max_val ? max_val : v);
}

// Fast integer square root of 64-bit integer
inline uint32_t isqrt64(uint64_t n)
{
    uint64_t root = 0;
    uint64_t bit = 1ULL << 62;
    while (bit > n)
        bit >>= 2;
    while (bit != 0)
    {
        if (n >= root + bit)
        {
            n -= root + bit;
            root = (root >> 1) + bit;
        }
        else
        {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)root;
}

// Fixed-point sqrt(x)
inline fixed_t fx_sqrt(fixed_t x)
{
    if (x <= 0) return 0;
    return (fixed_t)isqrt64((uint64_t)x << FX_SHIFT);
}

struct FixedVec3
{
    fixed_t x, y, z;

    FixedVec3() : x(0), y(0), z(0) {}
    FixedVec3(fixed_t _x, fixed_t _y, fixed_t _z) : x(_x), y(_y), z(_z) {}

    static FixedVec3 fromFloat(float fx, float fy, float fz)
    {
        return FixedVec3(FX_FROM_FLOAT(fx), FX_FROM_FLOAT(fy), FX_FROM_FLOAT(fz));
    }

    FixedVec3 operator+(const FixedVec3& o) const { return FixedVec3(x + o.x, y + o.y, z + o.z); }
    FixedVec3 operator-(const FixedVec3& o) const { return FixedVec3(x - o.x, y - o.y, z - o.z); }
    FixedVec3 operator*(fixed_t s) const { return FixedVec3(fx_mul(x, s), fx_mul(y, s), fx_mul(z, s)); }

    fixed_t dot(const FixedVec3& o) const
    {
        return fx_mul(x, o.x) + fx_mul(y, o.y) + fx_mul(z, o.z);
    }

    FixedVec3 cross(const FixedVec3& o) const
    {
        return FixedVec3(
            fx_mul(y, o.z) - fx_mul(z, o.y),
            fx_mul(z, o.x) - fx_mul(x, o.z),
            fx_mul(x, o.y) - fx_mul(y, o.x)
        );
    }
};

// 4x4 matrix in column-major order (standard OpenGL layout)
struct FixedMat4
{
    fixed_t m[16];

    FixedMat4()
    {
        setIdentity();
    }

    void setIdentity()
    {
        for (int i = 0; i < 16; ++i)
            m[i] = (i % 5 == 0) ? FX_ONE : 0;
    }

    static FixedMat4 identity()
    {
        FixedMat4 res;
        res.setIdentity();
        return res;
    }

    static FixedMat4 translation(fixed_t tx, fixed_t ty, fixed_t tz)
    {
        FixedMat4 res;
        res.translate(tx, ty, tz);
        return res;
    }

    FixedVec3 transformPoint(const FixedVec3& p) const
    {
        fixed_t rx = fx_mul(m[0], p.x) + fx_mul(m[4], p.y) + fx_mul(m[8],  p.z) + m[12];
        fixed_t ry = fx_mul(m[1], p.x) + fx_mul(m[5], p.y) + fx_mul(m[9],  p.z) + m[13];
        fixed_t rz = fx_mul(m[2], p.x) + fx_mul(m[6], p.y) + fx_mul(m[10], p.z) + m[14];
        return FixedVec3(rx, ry, rz);
    }

    FixedMat4 operator*(const FixedMat4& r) const
    {
        FixedMat4 out;
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                int64_t sum = 0;
                for (int k = 0; k < 4; ++k)
                {
                    sum += (int64_t)m[k * 4 + row] * r.m[col * 4 + k];
                }
                out.m[col * 4 + row] = (fixed_t)(sum >> FX_SHIFT);
            }
        }
        return out;
    }

    void translate(fixed_t tx, fixed_t ty, fixed_t tz)
    {
        m[12] += fx_mul(m[0], tx) + fx_mul(m[4], ty) + fx_mul(m[8],  tz);
        m[13] += fx_mul(m[1], tx) + fx_mul(m[5], ty) + fx_mul(m[9],  tz);
        m[14] += fx_mul(m[2], tx) + fx_mul(m[6], ty) + fx_mul(m[10], tz);
        m[15] += fx_mul(m[3], tx) + fx_mul(m[7], ty) + fx_mul(m[11], tz);
    }

    void scale(fixed_t sx, fixed_t sy, fixed_t sz)
    {
        for (int i = 0; i < 4; ++i)
        {
            m[i]     = fx_mul(m[i],     sx);
            m[4 + i] = fx_mul(m[4 + i], sy);
            m[8 + i] = fx_mul(m[8 + i], sz);
        }
    }
};
