#pragma once

#include <array>
#include <cstddef>
#include <limits>

namespace daedalus
{
struct Vec2
{
    float x = 0.0F;
    float y = 0.0F;
};

struct Vec3
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Vec4
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

struct Quat
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

// Column-major matrix using column vectors. Composition is parent * local.
struct Mat4
{
    std::array<float, 16> values{};

    [[nodiscard]] float& at(std::size_t row, std::size_t column) noexcept;
    [[nodiscard]] float at(std::size_t row, std::size_t column) const noexcept;
};

struct Aabb
{
    Vec3 minimum{
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity()};
    Vec3 maximum{
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()};

    [[nodiscard]] bool empty() const noexcept;
};

[[nodiscard]] Vec3 operator+(Vec3 left, Vec3 right) noexcept;
[[nodiscard]] Vec3 operator-(Vec3 left, Vec3 right) noexcept;
[[nodiscard]] Vec3 operator*(Vec3 value, float scalar) noexcept;
[[nodiscard]] Vec3 operator/(Vec3 value, float scalar);
[[nodiscard]] float dot(Vec3 left, Vec3 right) noexcept;
[[nodiscard]] Vec3 cross(Vec3 left, Vec3 right) noexcept;
[[nodiscard]] float length(Vec3 value) noexcept;
[[nodiscard]] Vec3 normalize(Vec3 value, Vec3 fallback = {0.0F, 0.0F, 1.0F}) noexcept;

[[nodiscard]] bool finite(Vec2 value) noexcept;
[[nodiscard]] bool finite(Vec3 value) noexcept;
[[nodiscard]] bool finite(Vec4 value) noexcept;
[[nodiscard]] bool finite(Quat value) noexcept;
[[nodiscard]] bool finite(const Mat4& value) noexcept;

[[nodiscard]] Mat4 identity_matrix() noexcept;
[[nodiscard]] Mat4 translation_matrix(Vec3 translation) noexcept;
[[nodiscard]] Mat4 scale_matrix(Vec3 scale) noexcept;
[[nodiscard]] Mat4 rotation_matrix(Quat rotation) noexcept;
[[nodiscard]] Mat4 compose_trs(Vec3 translation, Quat rotation, Vec3 scale) noexcept;
[[nodiscard]] Mat4 multiply(const Mat4& left, const Mat4& right) noexcept;
[[nodiscard]] Vec3 transform_point(const Mat4& matrix, Vec3 point) noexcept;
[[nodiscard]] Vec3 transform_vector(const Mat4& matrix, Vec3 vector) noexcept;
[[nodiscard]] float determinant3x3(const Mat4& matrix) noexcept;
[[nodiscard]] Mat4 inverse(const Mat4& matrix, bool* invertible = nullptr) noexcept;
[[nodiscard]] Mat4 transpose(const Mat4& matrix) noexcept;
[[nodiscard]] Mat4 inverse_transpose(const Mat4& matrix, bool* invertible = nullptr) noexcept;
[[nodiscard]] Mat4 look_at_rh(Vec3 eye, Vec3 target, Vec3 up) noexcept;
[[nodiscard]] Mat4 perspective_rh(float vertical_fov_radians, float aspect, float near_plane, float far_plane) noexcept;

void expand(Aabb& bounds, Vec3 point) noexcept;
void expand(Aabb& bounds, const Aabb& other) noexcept;
[[nodiscard]] Aabb transform_bounds(const Aabb& bounds, const Mat4& matrix) noexcept;
[[nodiscard]] Vec3 center(const Aabb& bounds) noexcept;
[[nodiscard]] Vec3 extent(const Aabb& bounds) noexcept;
}
