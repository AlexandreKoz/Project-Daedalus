#include "rendering/PathTracingMath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace daedalus
{
std::uint32_t pcg_hash(std::uint32_t value) noexcept
{
    const std::uint32_t state = value * 747796405U + 2891336453U;
    const std::uint32_t word = ((state >> ((state >> 28U) + 4U)) ^ state) * 277803737U;
    return (word >> 22U) ^ word;
}

std::array<std::uint32_t, 4> pcg4d(std::array<std::uint32_t, 4> v) noexcept
{
    // Jarzynski & Olano 2020, "Hash Functions for GPU Rendering".
    for (auto& component : v) component = component * 1664525U + 1013904223U;
    v[0] += v[1] * v[3];
    v[1] += v[2] * v[0];
    v[2] += v[0] * v[1];
    v[3] += v[1] * v[2];
    for (auto& component : v) component ^= component >> 16U;
    v[0] += v[1] * v[3];
    v[1] += v[2] * v[0];
    v[2] += v[0] * v[1];
    v[3] += v[1] * v[2];
    return v;
}

std::uint32_t PathRng::next_uint() noexcept
{
    state = state * 747796405U + 2891336453U;
    const std::uint32_t word = ((state >> ((state >> 28U) + 4U)) ^ state) * 277803737U;
    return (word >> 22U) ^ word;
}

float PathRng::next_float() noexcept
{
    return static_cast<float>(next_uint() >> 8U) * (1.0F / 16777216.0F);
}

PathRng make_path_rng(std::uint32_t pixel_x, std::uint32_t pixel_y, std::uint32_t sample_index, std::uint32_t seed) noexcept
{
    const std::array<std::uint32_t, 4> hashed = pcg4d({pixel_x, pixel_y, sample_index, seed});
    return PathRng{hashed[0] ^ (hashed[1] * 0x9E3779B9U)};
}

Vec3 sample_cosine_hemisphere(float u1, float u2) noexcept
{
    const float radius = std::sqrt(std::max(u1, 0.0F));
    const float phi = 2.0F * kPathPi * u2;
    return {radius * std::cos(phi), radius * std::sin(phi), std::sqrt(std::max(1.0F - u1, 0.0F))};
}

float cosine_hemisphere_pdf(float cos_theta) noexcept
{
    return cos_theta > 0.0F ? cos_theta / kPathPi : 0.0F;
}

float ggx_distribution(float n_dot_h, float alpha) noexcept
{
    if (n_dot_h <= 0.0F) return 0.0F;
    const float alpha_squared = alpha * alpha;
    const float term = n_dot_h * n_dot_h * (alpha_squared - 1.0F) + 1.0F;
    return alpha_squared / std::max(kPathPi * term * term, 1.0e-20F);
}

float smith_g1_ggx(float n_dot_v, float alpha) noexcept
{
    if (n_dot_v <= 0.0F) return 0.0F;
    const float alpha_squared = alpha * alpha;
    return 2.0F * n_dot_v / (n_dot_v + std::sqrt(alpha_squared + (1.0F - alpha_squared) * n_dot_v * n_dot_v));
}

Vec3 sample_ggx_vndf(Vec3 view_local, float alpha, float u1, float u2) noexcept
{
    const Vec3 vh = normalize(Vec3{alpha * view_local.x, alpha * view_local.y, view_local.z}, {0.0F, 0.0F, 1.0F});
    const float length_squared = vh.x * vh.x + vh.y * vh.y;
    const Vec3 t1 = length_squared > 0.0F ? Vec3{-vh.y, vh.x, 0.0F} * (1.0F / std::sqrt(length_squared)) : Vec3{1.0F, 0.0F, 0.0F};
    const Vec3 t2 = cross(vh, t1);
    const float radius = std::sqrt(std::max(u1, 0.0F));
    const float phi = 2.0F * kPathPi * u2;
    const float p1 = radius * std::cos(phi);
    float p2 = radius * std::sin(phi);
    const float s = 0.5F * (1.0F + vh.z);
    p2 = (1.0F - s) * std::sqrt(std::max(1.0F - p1 * p1, 0.0F)) + s * p2;
    const Vec3 nh = t1 * p1 + t2 * p2 + vh * std::sqrt(std::max(0.0F, 1.0F - p1 * p1 - p2 * p2));
    return normalize(Vec3{alpha * nh.x, alpha * nh.y, std::max(0.0F, nh.z)}, {0.0F, 0.0F, 1.0F});
}

float ggx_vndf_reflection_pdf(float n_dot_v, float n_dot_h, float alpha) noexcept
{
    if (n_dot_v <= 0.0F || n_dot_h <= 0.0F) return 0.0F;
    return smith_g1_ggx(n_dot_v, alpha) * ggx_distribution(n_dot_h, alpha) / (4.0F * n_dot_v);
}

float power_heuristic(float pdf_a, float pdf_b) noexcept
{
    const float a = pdf_a * pdf_a;
    const float b = pdf_b * pdf_b;
    return a + b > 0.0F && std::isfinite(a + b) ? a / (a + b) : (pdf_a > 0.0F ? 1.0F : 0.0F);
}

float balance_heuristic(float pdf_a, float pdf_b) noexcept
{
    return pdf_a + pdf_b > 0.0F ? pdf_a / (pdf_a + pdf_b) : 0.0F;
}

float fresnel_dielectric(float cos_incident, float eta) noexcept
{
    cos_incident = std::clamp(cos_incident, 0.0F, 1.0F);
    const float sin_t_squared = (1.0F - cos_incident * cos_incident) / (eta * eta);
    if (sin_t_squared >= 1.0F) return 1.0F;
    const float cos_t = std::sqrt(1.0F - sin_t_squared);
    const float rs = (cos_incident - eta * cos_t) / (cos_incident + eta * cos_t);
    const float rp = (eta * cos_incident - cos_t) / (eta * cos_incident + cos_t);
    return 0.5F * (rs * rs + rp * rp);
}

std::optional<Vec3> refract_direction(Vec3 wi, Vec3 normal, float eta) noexcept
{
    const float cos_i = dot(normal, wi);
    const float sin_t_squared = (1.0F - cos_i * cos_i) / (eta * eta);
    if (sin_t_squared >= 1.0F) return std::nullopt;
    const float cos_t = std::sqrt(1.0F - sin_t_squared);
    return normalize(wi * (-1.0F / eta) + normal * (cos_i / eta - cos_t));
}

float russian_roulette_survival(Vec3 throughput) noexcept
{
    const float maximum = std::max({throughput.x, throughput.y, throughput.z});
    if (!std::isfinite(maximum)) return 1.0F;
    return std::clamp(maximum, 0.05F, 1.0F);
}

Vec3 sample_triangle_barycentrics(float u1, float u2) noexcept
{
    const float root = std::sqrt(std::max(u1, 0.0F));
    const float b0 = 1.0F - root;
    const float b1 = u2 * root;
    return {b0, b1, 1.0F - b0 - b1};
}

void orthonormal_basis(Vec3 normal, Vec3& tangent, Vec3& bitangent) noexcept
{
    const float sign = std::copysign(1.0F, normal.z);
    const float a = -1.0F / (sign + normal.z);
    const float b = normal.x * normal.y * a;
    tangent = {1.0F + sign * normal.x * normal.x * a, sign * b, -sign * normal.x};
    bitangent = {b, sign + normal.y * normal.y * a, -normal.y};
}

Vec3 sample_environment_direction(const EnvironmentSamplerParameters& parameters, float u_select, float u1, float u2) noexcept
{
    const float phi = 2.0F * kPathPi * u2;
    if (u_select < parameters.sun_probability)
    {
        const float cos_theta = std::pow(std::max(u1, 1.0e-30F), 1.0F / (parameters.sun_exponent + 1.0F));
        const float sin_theta = std::sqrt(std::max(0.0F, 1.0F - cos_theta * cos_theta));
        const Vec3 axis = normalize(parameters.sun_direction);
        Vec3 tangent{};
        Vec3 bitangent{};
        orthonormal_basis(axis, tangent, bitangent);
        return normalize(tangent * (sin_theta * std::cos(phi)) + bitangent * (sin_theta * std::sin(phi)) + axis * cos_theta);
    }
    const float z = 1.0F - 2.0F * u1;
    const float r = std::sqrt(std::max(0.0F, 1.0F - z * z));
    return {r * std::cos(phi), r * std::sin(phi), z};
}

float environment_direction_pdf(const EnvironmentSamplerParameters& parameters, Vec3 direction) noexcept
{
    const float cos_sun = dot(normalize(parameters.sun_direction), normalize(direction));
    const float lobe = cos_sun > 0.0F
        ? (parameters.sun_exponent + 1.0F) / (2.0F * kPathPi) * std::pow(cos_sun, parameters.sun_exponent)
        : 0.0F;
    return parameters.sun_probability * lobe + (1.0F - parameters.sun_probability) / (4.0F * kPathPi);
}

std::vector<AliasEntry> build_alias_table(std::span<const double> weights)
{
    if (weights.empty()) throw std::invalid_argument("alias table requires at least one weight");
    if (weights.size() > 0xFFFFFFFFULL) throw std::invalid_argument("alias table is too large");
    double total = 0.0;
    for (const double weight : weights)
    {
        if (!std::isfinite(weight) || weight < 0.0) throw std::invalid_argument("alias table weights must be finite and non-negative");
        total += weight;
    }
    if (!(total > 0.0) || !std::isfinite(total)) throw std::invalid_argument("alias table weights must not all be zero");

    const std::size_t count = weights.size();
    std::vector<AliasEntry> table(count);
    std::vector<double> scaled(count);
    std::vector<std::uint32_t> small;
    std::vector<std::uint32_t> large;
    small.reserve(count);
    large.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        table[index].pmf = static_cast<float>(weights[index] / total);
        scaled[index] = weights[index] / total * static_cast<double>(count);
        (scaled[index] < 1.0 ? small : large).push_back(static_cast<std::uint32_t>(index));
    }
    while (!small.empty() && !large.empty())
    {
        const std::uint32_t less = small.back();
        small.pop_back();
        const std::uint32_t more = large.back();
        large.pop_back();
        table[less].threshold = static_cast<float>(scaled[less]);
        table[less].alias = more;
        scaled[more] = (scaled[more] + scaled[less]) - 1.0;
        (scaled[more] < 1.0 ? small : large).push_back(more);
    }
    for (const std::uint32_t index : large) { table[index].threshold = 1.0F; table[index].alias = index; }
    for (const std::uint32_t index : small) { table[index].threshold = 1.0F; table[index].alias = index; } // round-off residue
    return table;
}

std::uint32_t sample_alias_table(std::span<const AliasEntry> table, float u_bucket, float u_accept) noexcept
{
    if (table.empty()) return 0U;
    const auto count = static_cast<std::uint32_t>(table.size());
    const std::uint32_t bucket = std::min(static_cast<std::uint32_t>(u_bucket * static_cast<float>(count)), count - 1U);
    return u_accept < table[bucket].threshold ? bucket : table[bucket].alias;
}
}
