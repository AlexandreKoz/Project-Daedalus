#include "scene/Math.h"

#include <algorithm>
#include <cmath>

namespace daedalus
{
float& Mat4::at(std::size_t row, std::size_t column) noexcept
{
    return values[column * 4U + row];
}

float Mat4::at(std::size_t row, std::size_t column) const noexcept
{
    return values[column * 4U + row];
}

bool Aabb::empty() const noexcept
{
    return minimum.x > maximum.x || minimum.y > maximum.y || minimum.z > maximum.z;
}

Vec3 operator+(Vec3 left, Vec3 right) noexcept { return {left.x + right.x, left.y + right.y, left.z + right.z}; }
Vec3 operator-(Vec3 left, Vec3 right) noexcept { return {left.x - right.x, left.y - right.y, left.z - right.z}; }
Vec3 operator*(Vec3 value, float scalar) noexcept { return {value.x * scalar, value.y * scalar, value.z * scalar}; }
Vec3 operator/(Vec3 value, float scalar)
{
    return scalar == 0.0F ? Vec3{} : Vec3{value.x / scalar, value.y / scalar, value.z / scalar};
}
float dot(Vec3 left, Vec3 right) noexcept { return left.x * right.x + left.y * right.y + left.z * right.z; }
Vec3 cross(Vec3 left, Vec3 right) noexcept
{
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}
float length(Vec3 value) noexcept { return std::sqrt(dot(value, value)); }
Vec3 normalize(Vec3 value, Vec3 fallback) noexcept
{
    const float magnitude = length(value);
    return magnitude > 1.0e-20F && std::isfinite(magnitude) ? value / magnitude : fallback;
}

bool finite(Vec2 value) noexcept { return std::isfinite(value.x) && std::isfinite(value.y); }
bool finite(Vec3 value) noexcept { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
bool finite(Vec4 value) noexcept { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w); }
bool finite(Quat value) noexcept { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w); }
bool finite(const Mat4& value) noexcept
{
    return std::all_of(value.values.begin(), value.values.end(), [](float component) { return std::isfinite(component); });
}

Mat4 identity_matrix() noexcept
{
    Mat4 result{};
    result.at(0, 0) = 1.0F;
    result.at(1, 1) = 1.0F;
    result.at(2, 2) = 1.0F;
    result.at(3, 3) = 1.0F;
    return result;
}

Mat4 translation_matrix(Vec3 translation) noexcept
{
    Mat4 result = identity_matrix();
    result.at(0, 3) = translation.x;
    result.at(1, 3) = translation.y;
    result.at(2, 3) = translation.z;
    return result;
}

Mat4 scale_matrix(Vec3 scale) noexcept
{
    Mat4 result{};
    result.at(0, 0) = scale.x;
    result.at(1, 1) = scale.y;
    result.at(2, 2) = scale.z;
    result.at(3, 3) = 1.0F;
    return result;
}

Mat4 rotation_matrix(Quat rotation) noexcept
{
    const float magnitude = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w);
    if (!(magnitude > 1.0e-20F) || !std::isfinite(magnitude)) return identity_matrix();
    const float x = rotation.x / magnitude;
    const float y = rotation.y / magnitude;
    const float z = rotation.z / magnitude;
    const float w = rotation.w / magnitude;

    Mat4 result = identity_matrix();
    result.at(0, 0) = 1.0F - 2.0F * (y * y + z * z);
    result.at(0, 1) = 2.0F * (x * y - z * w);
    result.at(0, 2) = 2.0F * (x * z + y * w);
    result.at(1, 0) = 2.0F * (x * y + z * w);
    result.at(1, 1) = 1.0F - 2.0F * (x * x + z * z);
    result.at(1, 2) = 2.0F * (y * z - x * w);
    result.at(2, 0) = 2.0F * (x * z - y * w);
    result.at(2, 1) = 2.0F * (y * z + x * w);
    result.at(2, 2) = 1.0F - 2.0F * (x * x + y * y);
    return result;
}

Mat4 compose_trs(Vec3 translation, Quat rotation, Vec3 scale) noexcept
{
    return multiply(translation_matrix(translation), multiply(rotation_matrix(rotation), scale_matrix(scale)));
}

Mat4 multiply(const Mat4& left, const Mat4& right) noexcept
{
    Mat4 result{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 4; ++inner) value += left.at(row, inner) * right.at(inner, column);
            result.at(row, column) = value;
        }
    }
    return result;
}

Vec3 transform_point(const Mat4& matrix, Vec3 point) noexcept
{
    const float x = matrix.at(0, 0) * point.x + matrix.at(0, 1) * point.y + matrix.at(0, 2) * point.z + matrix.at(0, 3);
    const float y = matrix.at(1, 0) * point.x + matrix.at(1, 1) * point.y + matrix.at(1, 2) * point.z + matrix.at(1, 3);
    const float z = matrix.at(2, 0) * point.x + matrix.at(2, 1) * point.y + matrix.at(2, 2) * point.z + matrix.at(2, 3);
    const float w = matrix.at(3, 0) * point.x + matrix.at(3, 1) * point.y + matrix.at(3, 2) * point.z + matrix.at(3, 3);
    return std::abs(w) > 1.0e-20F && w != 1.0F ? Vec3{x / w, y / w, z / w} : Vec3{x, y, z};
}

Vec3 transform_vector(const Mat4& matrix, Vec3 vector) noexcept
{
    return {matrix.at(0, 0) * vector.x + matrix.at(0, 1) * vector.y + matrix.at(0, 2) * vector.z,
            matrix.at(1, 0) * vector.x + matrix.at(1, 1) * vector.y + matrix.at(1, 2) * vector.z,
            matrix.at(2, 0) * vector.x + matrix.at(2, 1) * vector.y + matrix.at(2, 2) * vector.z};
}

float determinant3x3(const Mat4& matrix) noexcept
{
    const float a = matrix.at(0, 0), b = matrix.at(0, 1), c = matrix.at(0, 2);
    const float d = matrix.at(1, 0), e = matrix.at(1, 1), f = matrix.at(1, 2);
    const float g = matrix.at(2, 0), h = matrix.at(2, 1), i = matrix.at(2, 2);
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

Mat4 inverse(const Mat4& matrix, bool* invertible) noexcept
{
    const auto& m = matrix.values;
    std::array<float, 16> inv{};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const float determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    const bool valid = std::isfinite(determinant) && std::abs(determinant) > 1.0e-20F;
    if (invertible != nullptr) *invertible = valid;
    if (!valid) return identity_matrix();
    Mat4 result{};
    for (std::size_t index = 0; index < 16; ++index) result.values[index] = inv[index] / determinant;
    return result;
}

Mat4 transpose(const Mat4& matrix) noexcept
{
    Mat4 result{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            result.at(row, column) = matrix.at(column, row);
    return result;
}

Mat4 inverse_transpose(const Mat4& matrix, bool* invertible) noexcept
{
    return transpose(inverse(matrix, invertible));
}

Mat4 look_at_rh(Vec3 eye, Vec3 target, Vec3 up) noexcept
{
    const Vec3 forward = normalize(eye - target, {0.0F, 0.0F, 1.0F});
    const Vec3 right = normalize(cross(up, forward), {1.0F, 0.0F, 0.0F});
    const Vec3 corrected_up = cross(forward, right);
    Mat4 result = identity_matrix();
    result.at(0, 0) = right.x; result.at(0, 1) = right.y; result.at(0, 2) = right.z; result.at(0, 3) = -dot(right, eye);
    result.at(1, 0) = corrected_up.x; result.at(1, 1) = corrected_up.y; result.at(1, 2) = corrected_up.z; result.at(1, 3) = -dot(corrected_up, eye);
    result.at(2, 0) = forward.x; result.at(2, 1) = forward.y; result.at(2, 2) = forward.z; result.at(2, 3) = -dot(forward, eye);
    return result;
}

Mat4 perspective_rh(float vertical_fov_radians, float aspect, float near_plane, float far_plane) noexcept
{
    Mat4 result{};
    const float y_scale = 1.0F / std::tan(vertical_fov_radians * 0.5F);
    const float x_scale = y_scale / std::max(aspect, 1.0e-6F);
    result.at(0, 0) = x_scale;
    result.at(1, 1) = y_scale;
    result.at(2, 2) = far_plane / (near_plane - far_plane);
    result.at(2, 3) = (near_plane * far_plane) / (near_plane - far_plane);
    result.at(3, 2) = -1.0F;
    return result;
}

void expand(Aabb& bounds, Vec3 point) noexcept
{
    bounds.minimum.x = std::min(bounds.minimum.x, point.x);
    bounds.minimum.y = std::min(bounds.minimum.y, point.y);
    bounds.minimum.z = std::min(bounds.minimum.z, point.z);
    bounds.maximum.x = std::max(bounds.maximum.x, point.x);
    bounds.maximum.y = std::max(bounds.maximum.y, point.y);
    bounds.maximum.z = std::max(bounds.maximum.z, point.z);
}

void expand(Aabb& bounds, const Aabb& other) noexcept
{
    if (!other.empty())
    {
        expand(bounds, other.minimum);
        expand(bounds, other.maximum);
    }
}

Aabb transform_bounds(const Aabb& bounds, const Mat4& matrix) noexcept
{
    Aabb result;
    if (bounds.empty()) return result;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                expand(result, transform_point(matrix, {
                    x == 0 ? bounds.minimum.x : bounds.maximum.x,
                    y == 0 ? bounds.minimum.y : bounds.maximum.y,
                    z == 0 ? bounds.minimum.z : bounds.maximum.z}));
    return result;
}

Vec3 center(const Aabb& bounds) noexcept { return bounds.empty() ? Vec3{} : (bounds.minimum + bounds.maximum) * 0.5F; }
Vec3 extent(const Aabb& bounds) noexcept { return bounds.empty() ? Vec3{} : bounds.maximum - bounds.minimum; }
}
