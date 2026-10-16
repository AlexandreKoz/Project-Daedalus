#pragma once

// Source-level convergence analysis for the Campaign E reference path tracer. Given a high-sample
// reference and captures at increasing sample counts, it reports linear-radiance error metrics and
// the fitted log-log slope of RMSE versus samples (an unbiased Monte Carlo estimator is expected to
// approach -0.5). The utility reports measurements; it never decides acceptance thresholds.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace daedalus
{
struct LinearImageError
{
    double mean_absolute_error = 0.0;
    double root_mean_square_error = 0.0;
    double relative_mean_square_error = 0.0; // mean((c - r)^2 / (r^2 + 1e-2)) over channels
    double mean_reference = 0.0;
    double mean_candidate = 0.0;
    std::uint64_t compared_channels = 0;
    std::uint64_t nonfinite_candidate_channels = 0;
};

// Both spans hold interleaved linear RGB floats of identical size; throws on mismatch/empty input.
[[nodiscard]] LinearImageError compare_linear_float_rgb(std::span<const float> reference, std::span<const float> candidate);

struct ConvergenceSample
{
    std::uint64_t samples_per_pixel = 0;
    LinearImageError error;
};

struct ConvergenceSummary
{
    std::vector<ConvergenceSample> samples;   // sorted by sample count
    double log_log_rmse_slope = 0.0;          // least-squares slope of ln(RMSE) over ln(spp)
    bool rmse_monotonically_non_increasing = false;
    bool slope_valid = false;
};

[[nodiscard]] ConvergenceSummary summarize_convergence(std::vector<ConvergenceSample> samples);
[[nodiscard]] std::string convergence_summary_json(const ConvergenceSummary& summary);
}
