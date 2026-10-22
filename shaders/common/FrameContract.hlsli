#ifndef DAEDALUS_FRAME_CONTRACT_HLSLI
#define DAEDALUS_FRAME_CONTRACT_HLSLI

// HLSL side of src/rendering/TemporalContract.h. Field order and packing must match
// GpuViewConstants exactly (static_asserted on the C++ side; 704 bytes).
//
// Pixel convention: x right, y down, origin at the top-left corner of the top-left pixel,
// pixel centres at +0.5, units = internal render pixels.
// Motion vectors: motion = previous_pixel - current_pixel, computed from UNJITTERED current and
// previous view-projections (jitter-free). History lookup: (pixel + 0.5 + motion) / render_size.
struct ViewConstantsData
{
    column_major float4x4 view;
    column_major float4x4 projection;
    column_major float4x4 view_projection;
    column_major float4x4 view_projection_unjittered;
    column_major float4x4 inverse_view_projection_unjittered;
    column_major float4x4 previous_view_projection_unjittered;
    column_major float4x4 previous_view_projection;
    column_major float4x4 inverse_view;
    column_major float4x4 inverse_projection;
    float4 camera_position_near;
    float4 previous_camera_position_far;
    float4 jitter_pixels;
    float4 render_size;
    float4 display_size;
    uint frame_index;
    uint jitter_index;
    uint scene_revision;
    uint reset_flags;
    float exposure_ev;
    float environment_intensity;
    float emissive_scale;
    uint history_valid;
    uint hybrid_flags;
    float reflection_max_roughness;
    uint view_padding0;
    uint view_padding1;
};

static const uint HYBRID_FLAG_RT_SHADOWS = 1u << 0u;
static const uint HYBRID_FLAG_RT_REFLECTIONS = 1u << 1u;
static const uint HYBRID_FLAG_GEOMETRY_BUFFER = 1u << 2u;
static const uint TEMPORAL_DRAW_FLAG_HYBRID_ELIGIBLE = 1u << 0u;

// G-buffer identifier word w: bits 0-15 = f32tof16(ambient occlusion), bits 16-31 = flags.
static const uint GBUFFER_FLAG_HYBRID_ELIGIBLE = 1u << 0u;
static const uint GBUFFER_FLAG_DOUBLE_SIDED = 1u << 1u;

float2 clip_to_pixel(float4 clip, float2 render_size)
{
    const float2 ndc = clip.xy / clip.w;
    return float2((ndc.x * 0.5 + 0.5) * render_size.x, (0.5 - ndc.y * 0.5) * render_size.y);
}

float2 octahedral_wrap(float2 value)
{
    return (1.0 - abs(value.yx)) * float2(value.x >= 0.0 ? 1.0 : -1.0, value.y >= 0.0 ? 1.0 : -1.0);
}

// Unit vector -> 2x16-bit snorm octahedral encoding packed in one uint.
uint encode_octahedral_normal(float3 normal)
{
    float3 n = normal / max(abs(normal.x) + abs(normal.y) + abs(normal.z), 1.0e-20);
    float2 encoded = n.z >= 0.0 ? n.xy : octahedral_wrap(n.xy);
    const int2 quantized = int2(round(clamp(encoded, -1.0, 1.0) * 32767.0));
    return (uint(quantized.x) & 0xffffu) | ((uint(quantized.y) & 0xffffu) << 16u);
}

float3 decode_octahedral_normal(uint packed)
{
    const int2 quantized = int2(int(packed << 16u) >> 16, int(packed) >> 16);
    float2 encoded = float2(quantized) / 32767.0;
    float3 n = float3(encoded, 1.0 - abs(encoded.x) - abs(encoded.y));
    if (n.z < 0.0)
        n.xy = octahedral_wrap(n.xy);
    return normalize(n);
}

#endif
