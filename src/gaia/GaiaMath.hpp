#pragma once
#define GLM_FORCE_EXPLICIT_CTOR
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>

namespace gaia
{

// Types:
using Vec2f = glm::vec2;
using Vec3f = glm::vec3;
using Vec4f = glm::vec4;
using Mat3f = glm::mat3;
using Mat4f = glm::mat4;
using Quatf = glm::quat;

using Vec2i = glm::i32vec2;
using Vec3i = glm::i32vec3;
using Vec4i = glm::i32vec4;

using Vec2i8 = glm::i8vec2;
using Vec3i8 = glm::i8vec3;
using Vec4i8 = glm::i8vec4;

using Vec2u8 = glm::u8vec2;
using Vec3u8 = glm::u8vec3;
using Vec4u8 = glm::u8vec4;

// Constants:
static constexpr float Pif = glm::pi<float>();

static constexpr Vec2f Vec2fZero(0.f, 0.f);
static constexpr Vec2f Vec2fX(1.f, 0.f);
static constexpr Vec2f Vec2fY(0.f, 1.f);

static constexpr Vec3f Vec3fZero(0.f, 0.f, 0.f);
static constexpr Vec3f Vec3fX(1.f, 0.f, 0.f);
static constexpr Vec3f Vec3fY(0.f, 1.f, 0.f);
static constexpr Vec3f Vec3fZ(0.f, 0.f, 1.f);

static constexpr Vec4f Vec4fZero(0.f, 0.f, 0.f, 0.f);
static constexpr Vec4f Vec4fX(1.f, 0.f, 0.f, 0.f);
static constexpr Vec4f Vec4fY(0.f, 1.f, 0.f, 0.f);
static constexpr Vec4f Vec4fZ(0.f, 0.f, 1.f, 0.f);
static constexpr Vec4f Vec4fW(0.f, 0.f, 0.f, 1.f);

static constexpr Mat3f Mat3fIdentity = glm::identity<Mat3f>();
static constexpr Mat4f Mat4fIdentity = glm::identity<Mat4f>();
static constexpr Quatf QuatfIdentity = glm::identity<Quatf>();

static constexpr Vec2i Vec2iZero(0, 0);
static constexpr Vec3i Vec3iZero(0, 0, 0);
static constexpr Vec4i Vec4iZero(0, 0, 0, 0);

static constexpr Vec2i8 Vec2i8Zero(0, 0);
static constexpr Vec3i8 Vec3i8Zero(0, 0, 0);
static constexpr Vec4i8 Vec4i8Zero(0, 0, 0, 0);

static constexpr Vec2u8 Vec2u8Zero(0, 0);
static constexpr Vec3u8 Vec3u8Zero(0, 0, 0);
static constexpr Vec4u8 Vec4u8Zero(0, 0, 0, 0);

namespace math
{

// Pull in global functions (dot, cross, etc).
using namespace glm;

// Define some of our own:
template<typename T>
constexpr inline T Square(T x)
{
    return x * x;
}

template<typename T>
constexpr inline T Lerp(T a, T b, float t)
{
    Assert(0.f <= t && t <= 1.f);
    return a + (T)(t * (b - a));
}

template<typename T>
constexpr inline bool IsPow2(T n)
{
    Assert(n != T(0));
    return (n & (n - 1)) == T(0);
}

template<typename T>
constexpr inline T RoundUpPow2(T n, T align)
{
    Assert(IsPow2(align));
    return (n + align - 1) & ~(align - 1);
}

template <typename T>
constexpr inline T RoundDownPow2(T n, T align)
{
    Assert(IsPow2(align));
    return n & ~(align - 1);
}

inline int IFloorF(float x)
{
    return (int)floorf(x);
}

// https://stackoverflow.com/a/24748637
constexpr inline int ILog2(int32 n)
{
#define S(k)           \
    if (n >= (1 << k)) \
    {                  \
        i += k;        \
        n >>= k;       \
    }

    int i = -(n == 0);
    S(16);
    S(8);
    S(4);
    S(2);
    S(1);
    return i;

#undef S
}

inline Vec2i Vec2Floor(Vec2f v)
{
    return Vec2i(IFloorF(v.x), IFloorF(v.y));
}

inline Vec3f Mat4fTransformVec3f(const Mat4f& m, const Vec3f& v)
{
    return Vec3f(m * Vec4f(v, 1.f));
}

inline Mat4f Mat4fCompose(const Mat3f& m3, const Vec3f& translation)
{
    return Mat4f(Vec4f(m3[0], 0.f),
                 Vec4f(m3[1], 0.f),
                 Vec4f(m3[2], 0.f),
                 Vec4f(translation, 1.f));
}

inline Mat4f Mat4fMakeTranslation(const Vec3f& translation)
{
    return Mat4fCompose(Mat3fIdentity, translation);
}

inline Mat3f Mat3fMakeRotationX(float rx)
{
    // TODO: inline and optimise.
    return Mat3f(glm::rotate(Mat4fIdentity, rx, Vec3fX));
}

inline Mat3f Mat3fMakeRotationY(float ry)
{
    // TODO: inline and optimise.
    return Mat3f(glm::rotate(Mat4fIdentity, ry, Vec3fY));
}

inline Mat3f Mat3fMakeRotationZ(float rz)
{
    // TODO: inline and optimise.
    return Mat3f(glm::rotate(Mat4fIdentity, rz, Vec3fZ));
}

} // namespace math

} // namespace gaia


// Standard library overloads:
namespace std
{

// Hash specialisations:
template<typename T>
struct hash<glm::vec<2, T>>
{
    size_t operator()(glm::vec<2, T> v) const
    {
        return hash<T>()(v.x) ^ hash<T>()(v.y);
    }
};

// Clamp overloads:
template<typename T>
glm::vec<2, T> clamp(glm::vec<2, T> v, glm::vec<2, T> min, glm::vec<2, T> max)
{
    return glm::vec<2, T>(clamp(v.x, min.x, max.x), clamp(v.y, min.y, max.y));
}

} // namespace std
