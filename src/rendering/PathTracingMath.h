#pragma once

// CPU reference implementations of the sampling/estimator math used by the DXR path tracer.
// shaders/common/Sampling.hlsli and shaders/raytracing/PathTracingBsdf.hlsli implement the same
// formulas; portable tests exercise these versions (normalization, determinism, MIS identities).

#include "scene/Math.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace daedalus
{
inline constexpr float kPathPi = 3.14159265358979323846F;

// ---- Deterministic per-pixel random streams --------------------------------------------------
// Stream key: (pixel x, pixel y, global sample index, seed). The global sample index is the number
// of samples accumulated for the pixel before this sample, so the stream depends only on the key
// and never on frame scheduling, dispatch batching or the swap-chain frame-slot count.
// Seeding: state = pcg4d(x, y, sample_index, seed).x ^ pcg4d(...).y * 0x9E3779B9 (see make_path_rng).
// Sequence: 32-bit PCG-RXS-M-XS (O'Neill) state update state = state * 747796405 + 2891336453.
[[nodiscard]] std::uint32_t pcg_hash(std::uint32_t value) noexcept;
[[nodiscard]] std::array<std::uint32_t, 4> pcg4d(std::array<std::uint32_t, 4> value) noexcept;

struct PathRng
{
    std::uint32_t state = 0;
    [[nodiscard]] std::uint32_t next_uint() noexcept;
    // Uniform in [0, 1) with 24-bit resolution (exactly representable in float).
    [[nodiscard]] float next_float() noexcept;
};

[[nodiscard]] PathRng make_path_rng(std::uint32_t pixel_x, std::uint32_t pixel_y,
                                    std::uint32_t sample_index, std::uint32_t seed) noexcept;

// ---- Hemisphere / microfacet sampling (local frame, +Z = shading normal) ---------------------
[[nodiscard]] Vec3 sample_cosine_hemisphere(float u1, float u2) noexcept;
[[nodiscard]] float cosine_hemisphere_pdf(float cos_theta) noexcept;
[[nodiscard]] float ggx_distribution(float n_dot_h, float alpha) noexcept;
[[nodiscard]] float smith_g1_ggx(float n_dot_v, float alpha) noexcept;
// Heitz 2018 visible-normal sampling; returns the sampled microfacet normal in the local frame.
[[nodiscard]] Vec3 sample_ggx_vndf(Vec3 view_local, float alpha, float u1, float u2) noexcept;
// Solid-angle pdf of the reflected direction produced by VNDF sampling: G1(v) D(h) / (4 n.v).
[[nodiscard]] float ggx_vndf_reflection_pdf(float n_dot_v, float n_dot_h, float alpha) noexcept;

[[nodiscard]] float power_heuristic(float pdf_a, float pdf_b) noexcept;
[[nodiscard]] float balance_heuristic(float pdf_a, float pdf_b) noexcept;

// Exact unpolarized dielectric Fresnel reflectance; eta = n_transmitted / n_incident. Returns 1
// for total internal reflection.
[[nodiscard]] float fresnel_dielectric(float cos_incident, float eta) noexcept;
// Refracts the direction `wi` (pointing away from the surface, same side as `normal`) through an
// interface with relative IOR eta = n_t / n_i. Returns std::nullopt on total internal reflection.
[[nodiscard]] std::optional<Vec3> refract_direction(Vec3 wi, Vec3 normal, float eta) noexcept;

// Russian roulette survival probability from the path throughput (after min depth).
[[nodiscard]] float russian_roulette_survival(Vec3 throughput) noexcept;

// Uniform barycentric triangle sampling (square-root parameterization).
[[nodiscard]] Vec3 sample_triangle_barycentrics(float u1, float u2) noexcept;

// ---- Analytic environment importance sampler --------------------------------------------------
// The daedalus-procedural-sky-v1 environment contains a sun lobe pow(max(cos,0), 512) * 18 around
// a fixed direction plus a smooth sky. The sampler is a two-component mixture:
//   p(w) = q * (n + 1) / (2 pi) * max(cos(w, sun), 0)^n  +  (1 - q) / (4 pi)
// with n = 512. Both components are exactly normalized, so p(w) is a valid solid-angle density.
struct EnvironmentSamplerParameters
{
    Vec3 sun_direction{-0.35F, 0.82F, 0.45F}; // normalized on use; matches Environment.hlsli
    float sun_exponent = 512.0F;
    float sun_probability = 0.5F;
};
[[nodiscard]] Vec3 sample_environment_direction(const EnvironmentSamplerParameters& parameters,
                                                float u_select, float u1, float u2) noexcept;
[[nodiscard]] float environment_direction_pdf(const EnvironmentSamplerParameters& parameters, Vec3 direction) noexcept;

// ---- Discrete alias tables (Vose) --------------------------------------------------------------
struct AliasEntry
{
    float threshold = 1.0F;
    std::uint32_t alias = 0;
    float pmf = 0.0F;
};
// Throws std::invalid_argument when the weights are empty, negative, non-finite or all zero.
[[nodiscard]] std::vector<AliasEntry> build_alias_table(std::span<const double> weights);
[[nodiscard]] std::uint32_t sample_alias_table(std::span<const AliasEntry> table, float u_bucket, float u_accept) noexcept;

// Builds an orthonormal basis around a unit normal (Duff et al. 2017, branchless).
void orthonormal_basis(Vec3 normal, Vec3& tangent, Vec3& bitangent) noexcept;
}
