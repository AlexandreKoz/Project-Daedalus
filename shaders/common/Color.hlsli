#ifndef DAEDALUS_COLOR_HLSLI
#define DAEDALUS_COLOR_HLSLI

float linear_to_srgb_channel(float value)
{
    value = max(value, 0.0);
    return value <= 0.0031308 ? 12.92 * value : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

float3 linear_to_srgb(float3 value)
{
    return float3(
        linear_to_srgb_channel(value.r),
        linear_to_srgb_channel(value.g),
        linear_to_srgb_channel(value.b));
}

float aces_fitted_channel(float value)
{
    // The fit is asymptotic. Cap the algebraic input far above the HDR target's finite
    // range so x*x cannot overflow if this helper is reused with a wider format later.
    const float x = min(max(value, 0.0), 1.0e10);
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 aces_fitted(float3 value)
{
    return float3(
        aces_fitted_channel(value.r),
        aces_fitted_channel(value.g),
        aces_fitted_channel(value.b));
}

#endif
