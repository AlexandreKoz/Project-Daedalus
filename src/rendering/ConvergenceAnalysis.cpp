#include "rendering/ConvergenceAnalysis.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace daedalus
{
LinearImageError compare_linear_float_rgb(std::span<const float> reference, std::span<const float> candidate)
{
    if (reference.empty() || reference.size() != candidate.size() || reference.size() % 3U != 0U)
        throw std::invalid_argument("convergence comparison requires equally sized, non-empty RGB float images");
    LinearImageError result;
    double absolute_sum = 0.0;
    double squared_sum = 0.0;
    double relative_sum = 0.0;
    double reference_sum = 0.0;
    double candidate_sum = 0.0;
    std::uint64_t compared = 0;
    for (std::size_t index = 0; index < reference.size(); ++index)
    {
        const double r = reference[index];
        const double c = candidate[index];
        if (!std::isfinite(c))
        {
            ++result.nonfinite_candidate_channels;
            continue;
        }
        if (!std::isfinite(r)) throw std::invalid_argument("reference image contains non-finite values");
        const double difference = c - r;
        absolute_sum += std::abs(difference);
        squared_sum += difference * difference;
        relative_sum += difference * difference / (r * r + 1.0e-2);
        reference_sum += r;
        candidate_sum += c;
        ++compared;
    }
    result.compared_channels = compared;
    if (compared == 0U) return result;
    const auto count = static_cast<double>(compared);
    result.mean_absolute_error = absolute_sum / count;
    result.root_mean_square_error = std::sqrt(squared_sum / count);
    result.relative_mean_square_error = relative_sum / count;
    result.mean_reference = reference_sum / count;
    result.mean_candidate = candidate_sum / count;
    return result;
}

ConvergenceSummary summarize_convergence(std::vector<ConvergenceSample> samples)
{
    ConvergenceSummary summary;
    std::sort(samples.begin(), samples.end(), [](const ConvergenceSample& left, const ConvergenceSample& right)
    {
        return left.samples_per_pixel < right.samples_per_pixel;
    });
    summary.samples = std::move(samples);
    summary.rmse_monotonically_non_increasing = !summary.samples.empty();
    for (std::size_t index = 1; index < summary.samples.size(); ++index)
        if (summary.samples[index].error.root_mean_square_error > summary.samples[index - 1U].error.root_mean_square_error)
            summary.rmse_monotonically_non_increasing = false;

    double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;
    std::size_t n = 0;
    for (const ConvergenceSample& sample : summary.samples)
    {
        if (sample.samples_per_pixel == 0U || !(sample.error.root_mean_square_error > 0.0)) continue;
        const double x = std::log(static_cast<double>(sample.samples_per_pixel));
        const double y = std::log(sample.error.root_mean_square_error);
        sum_x += x; sum_y += y; sum_xx += x * x; sum_xy += x * y;
        ++n;
    }
    const double denominator = static_cast<double>(n) * sum_xx - sum_x * sum_x;
    if (n >= 2U && std::abs(denominator) > 1.0e-12)
    {
        summary.log_log_rmse_slope = (static_cast<double>(n) * sum_xy - sum_x * sum_y) / denominator;
        summary.slope_valid = std::isfinite(summary.log_log_rmse_slope);
    }
    return summary;
}

std::string convergence_summary_json(const ConvergenceSummary& summary)
{
    std::ostringstream stream;
    stream << std::setprecision(9);
    stream << "{\n  \"schema\": \"daedalus.pathtrace-convergence/1\",\n  \"samples\": [\n";
    for (std::size_t index = 0; index < summary.samples.size(); ++index)
    {
        const ConvergenceSample& sample = summary.samples[index];
        stream << "    {\"spp\": " << sample.samples_per_pixel
               << ", \"mae\": " << sample.error.mean_absolute_error
               << ", \"rmse\": " << sample.error.root_mean_square_error
               << ", \"relmse\": " << sample.error.relative_mean_square_error
               << ", \"mean_reference\": " << sample.error.mean_reference
               << ", \"mean_candidate\": " << sample.error.mean_candidate
               << ", \"compared_channels\": " << sample.error.compared_channels
               << ", \"nonfinite_candidate_channels\": " << sample.error.nonfinite_candidate_channels << "}"
               << (index + 1U == summary.samples.size() ? "\n" : ",\n");
    }
    stream << "  ],\n  \"rmse_monotonically_non_increasing\": " << (summary.rmse_monotonically_non_increasing ? "true" : "false")
           << ",\n  \"log_log_rmse_slope\": ";
    if (summary.slope_valid) stream << summary.log_log_rmse_slope;
    else stream << "null";
    stream << ",\n  \"expected_unbiased_slope\": -0.5\n}\n";
    return stream.str();
}
}
