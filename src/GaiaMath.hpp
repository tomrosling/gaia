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

namespace math
{

// Pull in global functions (dot, cross, etc).
using namespace glm;

// Define some of our own:
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
