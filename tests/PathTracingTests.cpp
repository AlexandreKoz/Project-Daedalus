// Campaign E portable sampling/estimator tests (source only in the D-H drop; not executed).
// These check the CPU mirror of the HLSL integrator math; the GPU code is separate and unverified.
#include "TestHarness.h"
#include "rendering/ConvergenceAnalysis.h"
#include "rendering/PathTracingMath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <vector>

namespace
{
using namespace daedalus;
using namespace daedalus::tests;

void test_rng_determinism()
{
    PathRng a = make_path_rng(17U, 42U, 3U, 7U);
    PathRng b = make_path_rng(17U, 42U, 3U, 7U);
    PathRng other_sample = make_path_rng(17U, 42U, 4U, 7U);
    PathRng other_seed = make_path_rng(17U, 42U, 3U, 8U);
    bool sample_differs = false;
    bool seed_differs = false;
    for (int index = 0; index < 64; ++index)
    {
        const float value = a.next_float();
        require(value >= 0.0F && value < 1.0F, "next_float in [0, 1)");
        require(value == b.next_float(), "identical inputs produce identical streams");
        sample_differs = sample_differs || value != other_sample.next_float();
        seed_differs = seed_differs || value != other_seed.next_float();
    }
    require(sample_differs, "sample index decorrelates streams");
    require(seed_differs, "seed decorrelates streams");
}

void test_rng_uniformity()
{
    // Coarse 16-bin histogram over many pixels: every bin within 10% of the expectation.
    std::vector<std::uint32_t> bins(16U, 0U);
    constexpr std::uint32_t kSamples = 64U * 64U * 4U;
    for (std::uint32_t y = 0; y < 64U; ++y)
        for (std::uint32_t x = 0; x < 64U; ++x)
        {
            PathRng rng = make_path_rng(x, y, 0U, 1U);
            for (int i = 0; i < 4; ++i) ++bins[static_cast<std::size_t>(rng.next_float() * 16.0F)];
        }
    for (const std::uint32_t count : bins)
        require(std::abs(static_cast<double>(count) - kSamples / 16.0) < 0.1 * kSamples / 16.0, "RNG histogram is roughly uniform");
}

void test_alias_table()
{
    const std::vector<double> weights{1.0, 0.0, 3.0, 4.0};
    const std::vector<AliasEntry> table = build_alias_table(weights);
    require(table.size() == weights.size(), "one entry per weight");
    require_near(table[0].pmf, 0.125F, 1.0e-6F, "pmf 0");
    require_near(table[1].pmf, 0.0F, 0.0F, "zero weight has zero pmf");
    require_near(table[3].pmf, 0.5F, 1.0e-6F, "pmf 3");
    std::vector<std::uint32_t> counts(weights.size(), 0U);
    constexpr std::uint32_t kGrid = 256U;
    for (std::uint32_t i = 0; i < kGrid; ++i)
        for (std::uint32_t j = 0; j < kGrid; ++j)
            ++counts[sample_alias_table(table, (static_cast<float>(i) + 0.5F) / kGrid, (static_cast<float>(j) + 0.5F) / kGrid)];
    require(counts[1] == 0U, "zero-weight entry is never selected");
    require_near(static_cast<float>(counts[3]) / (kGrid * kGrid), 0.5F, 0.01F, "stratified selection frequency matches pmf");
    require_throws<std::invalid_argument>([] { (void)build_alias_table(std::vector<double>{}); }, "empty weights rejected");
    require_throws<std::invalid_argument>([] { (void)build_alias_table(std::vector<double>{0.0, 0.0}); }, "all-zero weights rejected");
    require_throws<std::invalid_argument>([] { (void)build_alias_table(std::vector<double>{1.0, -1.0}); }, "negative weights rejected");
}

void test_environment_pdf_normalization()
{
    // Deterministic stratified integral of the mixture pdf over the sphere must be ~1.
    const EnvironmentSamplerParameters parameters;
    constexpr int kTheta = 512;
    constexpr int kPhi = 1024;
    double integral = 0.0;
    for (int t = 0; t < kTheta; ++t)
    {
        const double z = 1.0 - 2.0 * (t + 0.5) / kTheta;
        const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
        for (int p = 0; p < kPhi; ++p)
        {
            const double phi = 2.0 * 3.14159265358979323846 * (p + 0.5) / kPhi;
            const Vec3 direction{static_cast<float>(r * std::cos(phi)), static_cast<float>(r * std::sin(phi)), static_cast<float>(z)};
            integral += environment_direction_pdf(parameters, direction);
        }
    }
    integral *= 4.0 * 3.14159265358979323846 / (static_cast<double>(kTheta) * kPhi);
    require(std::abs(integral - 1.0) < 0.02, "environment sampling pdf integrates to one over the sphere");
    const Vec3 sampled = sample_environment_direction(parameters, 0.1F, 0.37F, 0.81F);
    require_near(length(sampled), 1.0F, 1.0e-4F, "sampled environment direction is unit length");
    require(environment_direction_pdf(parameters, sampled) > 0.0F, "sampled direction has positive pdf");
}

void test_bsdf_helpers()
{
    require_near(fresnel_dielectric(1.0F, 1.5F), 0.04F, 1.0e-4F, "normal-incidence Fresnel for IOR 1.5");
    require_near(fresnel_dielectric(0.05F, 1.0F / 1.5F), 1.0F, 0.0F, "grazing exit beyond the critical angle is TIR");
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    require(refract_direction(normalize(Vec3{0.99F, 0.0F, 0.05F}), normal, 1.0F / 1.5F) == std::nullopt, "TIR has no refracted direction");
    const std::optional<Vec3> straight = refract_direction(normal, normal, 1.5F);
    require(straight.has_value(), "normal incidence refracts");
    require_near(straight->z, -1.0F, 1.0e-5F, "normal incidence passes straight through");
    require_near(power_heuristic(1.0F, 3.0F) + power_heuristic(3.0F, 1.0F), 1.0F, 1.0e-6F, "power heuristic weights sum to one");
    require_near(balance_heuristic(2.0F, 2.0F), 0.5F, 1.0e-6F, "balance heuristic of equal pdfs");
    const Vec3 cosine = sample_cosine_hemisphere(0.25F, 0.5F);
    require(cosine.z >= 0.0F, "cosine samples lie in the upper hemisphere");
    require_near(cosine_hemisphere_pdf(cosine.z), cosine.z / kPathPi, 1.0e-6F, "cosine pdf");
    const float survival = russian_roulette_survival({0.01F, 0.02F, 0.03F});
    require(survival > 0.0F && survival <= 1.0F, "russian roulette survival is a probability");
    Vec3 tangent;
    Vec3 bitangent;
    orthonormal_basis(normalize(Vec3{0.3F, -0.4F, 0.866F}), tangent, bitangent);
    require_near(dot(tangent, bitangent), 0.0F, 1.0e-5F, "basis is orthogonal");
    require_near(length(tangent), 1.0F, 1.0e-5F, "basis tangent is unit length");
}

void test_convergence_summary()
{
    // Synthetic RMSE = c / sqrt(spp) must yield slope -0.5 exactly.
    std::vector<ConvergenceSample> samples;
    for (const std::uint64_t spp : {1ULL, 4ULL, 16ULL, 64ULL, 256ULL})
    {
        ConvergenceSample sample;
        sample.samples_per_pixel = spp;
        sample.error.root_mean_square_error = 0.8 / std::sqrt(static_cast<double>(spp));
        samples.push_back(sample);
    }
    const ConvergenceSummary summary = summarize_convergence(samples);
    require(summary.slope_valid, "slope computed from five points");
    require(std::abs(summary.log_log_rmse_slope + 0.5) < 1.0e-9, "ideal Monte Carlo slope is -0.5");
    require(summary.rmse_monotonically_non_increasing, "ideal RMSE is non-increasing");
    const std::vector<float> reference{1.0F, 1.0F, 1.0F, 0.5F, 0.5F, 0.5F};
    const std::vector<float> candidate{1.0F, 1.0F, 1.0F, 0.5F, 0.5F, 1.5F};
    const LinearImageError error = compare_linear_float_rgb(reference, candidate);
    require(error.compared_channels == 6U, "all channels compared");
    require(std::abs(error.mean_absolute_error - 1.0 / 6.0) < 1.0e-9, "mean absolute error");
}
}

int main()
{
    return daedalus::tests::run({
        {"RNG determinism", test_rng_determinism},
        {"RNG uniformity", test_rng_uniformity},
        {"alias table", test_alias_table},
        {"environment pdf normalization", test_environment_pdf_normalization},
        {"BSDF helpers", test_bsdf_helpers},
        {"convergence summary", test_convergence_summary}});
}
