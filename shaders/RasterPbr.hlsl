#include "common/PbrLighting.hlsli"
#include "common/Environment.hlsli"
#include "common/FrameContract.hlsli"

static const uint FLAG_VERTEX_COLOR = 1u << 0u;
static const uint FLAG_BASE_COLOR_TEXTURE = 1u << 1u;
static const uint FLAG_METALLIC_ROUGHNESS_TEXTURE = 1u << 2u;
static const uint FLAG_NORMAL_TEXTURE = 1u << 3u;
static const uint FLAG_OCCLUSION_TEXTURE = 1u << 4u;
static const uint FLAG_EMISSIVE_TEXTURE = 1u << 5u;
static const uint FLAG_DOUBLE_SIDED = 1u << 6u;
static const uint FLAG_HAS_TANGENTS = 1u << 7u;
static const uint FLAG_BOUNDS_OVERLAY = 1u << 8u;

static const uint DIAGNOSTIC_SHADED = 0u;
static const uint DIAGNOSTIC_NORMALS = 1u;
static const uint DIAGNOSTIC_UV = 2u;
static const uint DIAGNOSTIC_TANGENTS = 3u;
static const uint DIAGNOSTIC_BASE_COLOR = 5u;
static const uint DIAGNOSTIC_METALLIC = 6u;
static const uint DIAGNOSTIC_ROUGHNESS = 7u;
static const uint DIAGNOSTIC_EMISSIVE = 8u;
static const uint DIAGNOSTIC_MATERIAL_ID = 9u;
static const uint DIAGNOSTIC_DEPTH = 10u;
static const uint DIAGNOSTIC_OVERDRAW = 11u;

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 world_position : POSITION0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 color : COLOR0;
    // Campaign F temporal outputs: unjittered current/previous clip positions for jitter-free
    // motion vectors and positive linear view depth. PSMain ignores them; PSMainGBuffer uses them.
    float4 current_clip : TEXCOORD2;
    float4 previous_clip : TEXCOORD3;
    float view_depth : TEXCOORD4;
};

struct GpuLight
{
    float4 position_range;
    float4 direction_outer_cos;
    float4 color_intensity;
    uint type;
    float inner_cone_cos;
    uint padding0;
    uint padding1;
};

cbuffer FrameConstants : register(b0)
{
    column_major float4x4 view_projection;
    column_major float4x4 shadow_view_projection;
    float4 camera_position;
    float4 environment_shadow; // x IBL intensity; y constant bias; z normal bias; w inverse shadow size
    uint diagnostic_mode;
    uint light_count;
    uint shadow_light_index;
    uint frame_flags;
};

cbuffer DrawConstants : register(b1)
{
    column_major float4x4 world;
    column_major float4x4 normal_matrix;
    float4 base_color_factor;
    float4 emissive_metallic;
    float4 roughness_normal_ao_cutoff;
    uint flags;
    uint material_diagnostic_id;
    uint alpha_mode;
    uint base_color_texcoord;
    uint metallic_roughness_texcoord;
    uint normal_texcoord;
    uint occlusion_texcoord;
    uint emissive_texcoord;
    float world_handedness;
    uint draw_padding0;
    uint draw_padding1;
    uint draw_padding2;
};

cbuffer LightConstants : register(b2)
{
    GpuLight lights[32];
};

// Campaign D/F per-draw temporal block (src/rendering/TemporalContract.h RasterTemporalDrawConstants).
cbuffer TemporalDrawConstants : register(b3)
{
    column_major float4x4 previous_world;
    uint instance_id;
    uint material_table_index;
    uint temporal_flags;
    uint temporal_padding0;
};

ConstantBuffer<ViewConstantsData> g_view : register(b4);

Texture2D<float4> base_color_texture : register(t0);
Texture2D<float4> metallic_roughness_texture : register(t1);
Texture2D<float4> normal_texture : register(t2);
Texture2D<float4> occlusion_texture : register(t3);
Texture2D<float4> emissive_texture : register(t4);
Texture2D<float> shadow_map : register(t5);
SamplerState base_color_sampler : register(s0);
SamplerState metallic_roughness_sampler : register(s1);
SamplerState normal_sampler : register(s2);
SamplerState occlusion_sampler : register(s3);
SamplerState emissive_sampler : register(s4);
SamplerComparisonState shadow_sampler : register(s5);

float2 select_uv(uint set_index, float2 uv0, float2 uv1)
{
    return set_index == 1u ? uv1 : uv0;
}

float3 material_id_color(uint identifier)
{
    if (identifier == 0u)
        return float3(1.0, 0.0, 1.0); // canonical default material sentinel
    uint hash = identifier * 747796405u + 2891336453u;
    hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
    hash = (hash >> 22u) ^ hash;
    return float3(
        0.2 + 0.8 * float((hash >> 0u) & 255u) / 255.0,
        0.2 + 0.8 * float((hash >> 8u) & 255u) / 255.0,
        0.2 + 0.8 * float((hash >> 16u) & 255u) / 255.0);
}


float shadow_visibility(float3 world_position, float3 shading_normal, float3 light_direction)
{
    if ((frame_flags & 1u) == 0u || shadow_light_index == 0xffffffffu)
        return 1.0;
    const float4 clip = mul(shadow_view_projection, float4(world_position, 1.0));
    if (clip.w <= 1.0e-20)
        return 1.0;
    const float3 ndc = clip.xyz / clip.w;
    const float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 || ndc.z <= 0.0 || ndc.z >= 1.0)
        return 1.0;
    const float n_dot_l = saturate(dot(shading_normal, light_direction));
    const float bias = environment_shadow.y + environment_shadow.z * (1.0 - n_dot_l);
    const float compare_depth = ndc.z - bias;
    float visibility = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            visibility += shadow_map.SampleCmpLevelZero(shadow_sampler, uv + float2(x, y) * environment_shadow.w, compare_depth);
        }
    }
    return visibility / 9.0;
}

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float4 world_position4 = mul(world, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position4);
    output.world_position = world_position4.xyz / max(abs(world_position4.w), 1.0e-20);
    output.normal = mul((float3x3)normal_matrix, input.normal);
    output.tangent.xyz = mul((float3x3)world, input.tangent.xyz);
    output.tangent.w = input.tangent.w * world_handedness;
    output.uv0 = input.uv0;
    output.uv1 = input.uv1;
    output.color = input.color;
    const float4 previous_world_position4 = mul(previous_world, float4(input.position, 1.0));
    output.current_clip = mul(g_view.view_projection_unjittered, world_position4);
    output.previous_clip = mul(g_view.previous_view_projection_unjittered, previous_world_position4);
    output.view_depth = -mul(g_view.view, world_position4).z;
    return output;
}

struct GeometrySurface
{
    float3 shading_normal;
    float3 geometric_normal;
    float3 base_color;
    float metallic;
    float roughness;
    float ambient_occlusion;
};

// Shared C2 shading body. With defer_direct/defer_specular false it is the unchanged Campaign C2
// forward shading. In hybrid mode, eligible OPAQUE/MASK pixels omit punctual direct lighting
// (re-evaluated with ray-traced visibility) and, for roughness <= reflection_max_roughness, the IBL
// specular term (replaced by ray-traced reflections); both predicates are reproduced exactly by
// the DXR ray-generation shader from the G-buffer values written below.
float4 shade_pixel(VertexOutput input, bool is_front_face, bool defer_direct, bool defer_specular, out GeometrySurface surface)
{
    surface.shading_normal = float3(0.0, 0.0, 1.0);
    surface.geometric_normal = float3(0.0, 0.0, 1.0);
    surface.base_color = 0.0;
    surface.metallic = 0.0;
    surface.roughness = 1.0;
    surface.ambient_occlusion = 1.0;
    if ((flags & FLAG_BOUNDS_OVERLAY) != 0u)
        return float4(1.0, 0.8, 0.1, 1.0);

    const float4 base_texture_sample = (flags & FLAG_BASE_COLOR_TEXTURE) != 0u
        ? base_color_texture.Sample(base_color_sampler, select_uv(base_color_texcoord, input.uv0, input.uv1))
        : 1.0.xxxx;
    const float4 vertex_color = (flags & FLAG_VERTEX_COLOR) != 0u ? input.color : 1.0.xxxx;
    const float4 base_color = base_color_factor * base_texture_sample * vertex_color;

    if (alpha_mode == 1u && base_color.a < roughness_normal_ao_cutoff.w)
        discard;

    if (diagnostic_mode == DIAGNOSTIC_OVERDRAW)
        return 1.0.xxxx;
    if (diagnostic_mode == DIAGNOSTIC_DEPTH)
        return float4(input.position.zzz, 1.0);

    const float4 metallic_roughness_sample = (flags & FLAG_METALLIC_ROUGHNESS_TEXTURE) != 0u
        ? metallic_roughness_texture.Sample(metallic_roughness_sampler,
                                            select_uv(metallic_roughness_texcoord, input.uv0, input.uv1))
        : 1.0.xxxx;
    const float roughness = saturate(roughness_normal_ao_cutoff.x * metallic_roughness_sample.g);
    const float metallic = saturate(emissive_metallic.w * metallic_roughness_sample.b);

    const float3 emissive_sample = (flags & FLAG_EMISSIVE_TEXTURE) != 0u
        ? emissive_texture.Sample(emissive_sampler, select_uv(emissive_texcoord, input.uv0, input.uv1)).rgb
        : 1.0.xxx;
    const float3 emissive = emissive_metallic.xyz * emissive_sample;

    const float ao_sample = (flags & FLAG_OCCLUSION_TEXTURE) != 0u
        ? occlusion_texture.Sample(occlusion_sampler, select_uv(occlusion_texcoord, input.uv0, input.uv1)).r
        : 1.0;
    const float ambient_occlusion = lerp(1.0, ao_sample, saturate(roughness_normal_ao_cutoff.z));

    const float face_sign = ((flags & FLAG_DOUBLE_SIDED) != 0u && !is_front_face) ? -1.0 : 1.0;
    float3 geometric_normal = input.normal;
    const float normal_length_squared = dot(geometric_normal, geometric_normal);
    geometric_normal = normal_length_squared > 1.0e-20 ? geometric_normal * rsqrt(normal_length_squared) : float3(0.0, 0.0, 1.0);
    geometric_normal *= face_sign;

    // UV tangent directions are surface derivatives and do not reverse merely because the
    // fragment is viewed from the back. Reverse N and the handedness sign instead; that keeps
    // T/B aligned with the authored UV parameterization while reversing the shading normal.
    float3 tangent = input.tangent.xyz;
    float tangent_sign = input.tangent.w * face_sign;
    tangent -= geometric_normal * dot(geometric_normal, tangent);
    const float tangent_length_squared = dot(tangent, tangent);
    const bool tangent_valid = tangent_length_squared > 1.0e-20;
    if (tangent_valid)
        tangent *= rsqrt(tangent_length_squared);

    float3 shading_normal = geometric_normal;
    if ((flags & FLAG_NORMAL_TEXTURE) != 0u && (flags & FLAG_HAS_TANGENTS) != 0u && tangent_valid)
    {
        float3 tangent_normal = normal_texture.Sample(normal_sampler, select_uv(normal_texcoord, input.uv0, input.uv1)).xyz * 2.0 - 1.0;
        tangent_normal.xy *= roughness_normal_ao_cutoff.y;
        const float tangent_normal_length_squared = dot(tangent_normal, tangent_normal);
        if (tangent_normal_length_squared > 1.0e-20)
        {
            tangent_normal *= rsqrt(tangent_normal_length_squared);
            const float3 bitangent = normalize(cross(geometric_normal, tangent)) * tangent_sign;
            const float3 mapped = tangent * tangent_normal.x + bitangent * tangent_normal.y + geometric_normal * tangent_normal.z;
            const float mapped_length_squared = dot(mapped, mapped);
            if (mapped_length_squared > 1.0e-20)
                shading_normal = mapped * rsqrt(mapped_length_squared);
        }
    }

    surface.shading_normal = shading_normal;
    surface.geometric_normal = geometric_normal;
    surface.base_color = max(base_color.rgb, 0.0);
    surface.metallic = metallic;
    surface.roughness = roughness;
    surface.ambient_occlusion = ambient_occlusion;

    if (diagnostic_mode == DIAGNOSTIC_NORMALS)
        return float4(shading_normal * 0.5 + 0.5, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_UV)
        return float4(frac(select_uv(base_color_texcoord, input.uv0, input.uv1)), 0.0, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_TANGENTS)
    {
        const float3 tangent_visual = tangent_valid ? tangent * 0.5 + 0.5 : float3(1.0, 0.0, 1.0);
        return float4(tangent_visual * (tangent_sign < 0.0 ? 0.35 : 1.0), 1.0);
    }
    if (diagnostic_mode == DIAGNOSTIC_BASE_COLOR)
        return float4(base_color.rgb, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_METALLIC)
        return float4(metallic.xxx, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_ROUGHNESS)
        return float4(roughness.xxx, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_EMISSIVE)
        return float4(emissive, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_MATERIAL_ID)
        return float4(material_id_color(material_diagnostic_id), 1.0);

    const float3 view_delta = camera_position.xyz - input.world_position;
    const float view_length_squared = dot(view_delta, view_delta);
    const float3 view_direction = view_length_squared > 1.0e-20 ? view_delta * rsqrt(view_length_squared) : shading_normal;
    float3 direct = 0.0;

    const uint shaded_light_count = defer_direct ? 0u : min(light_count, 32u);
    [loop]
    for (uint index = 0u; index < shaded_light_count; ++index)
    {
        const GpuLight light = lights[index];
        float3 light_direction = 0.0;
        float attenuation = 1.0;
        if (light.type == 0u)
        {
            light_direction = -light.direction_outer_cos.xyz;
            const float len2 = dot(light_direction, light_direction);
            if (len2 <= 1.0e-20) continue;
            light_direction *= rsqrt(len2);
        }
        else
        {
            const float3 to_light = light.position_range.xyz - input.world_position;
            const float distance_squared = dot(to_light, to_light);
            if (distance_squared <= 1.0e-20) continue;
            const float distance_to_light = sqrt(distance_squared);
            light_direction = to_light / distance_to_light;
            attenuation = distance_attenuation(distance_to_light, light.position_range.w);
            if (light.type == 2u)
            {
                const float3 emitted_direction = normalize(light.direction_outer_cos.xyz);
                const float cos_angle = dot(emitted_direction, -light_direction);
                attenuation *= spot_attenuation(cos_angle, light.inner_cone_cos, light.direction_outer_cos.w);
            }
        }
        if (attenuation <= 0.0) continue;
        float visibility = 1.0;
        if (index == shadow_light_index && light.type != 1u)
            visibility = shadow_visibility(input.world_position, shading_normal, light_direction);
        const float3 incident = light.color_intensity.rgb * (light.color_intensity.w * attenuation * visibility);
        direct += evaluate_brdf_times_n_dot_l(base_color.rgb, metallic, roughness,
                                               shading_normal, view_direction, light_direction) * incident;
    }

    const float n_dot_v = saturate(dot(shading_normal, view_direction));
    const float3 f0 = lerp(DAEDALUS_DIELECTRIC_F0.xxx, max(base_color.rgb, 0.0), metallic);
    const float3 fresnel_ibl = fresnel_schlick_roughness(f0, n_dot_v, roughness);
    const float3 diffuse_irradiance = procedural_diffuse_irradiance(shading_normal);
    const float3 diffuse_ibl = diffuse_irradiance * max(base_color.rgb, 0.0) * (1.0 - metallic) * (1.0 - fresnel_ibl);
    const float3 reflection = reflect(-view_direction, shading_normal);
    const float3 prefiltered = procedural_prefiltered_specular(reflection, roughness);
    const float2 brdf = environment_brdf_approximation(n_dot_v, roughness);
    const bool specular_ray_traced = defer_specular && roughness <= g_view.reflection_max_roughness;
    const float3 specular_ibl = specular_ray_traced ? 0.0.xxx : prefiltered * (f0 * brdf.x + brdf.y);
    const float3 indirect = (diffuse_ibl + specular_ibl) * ambient_occlusion * max(environment_shadow.x, 0.0);
    const float3 hdr_color = direct + indirect + emissive;
    const float output_alpha = alpha_mode == 2u ? saturate(base_color.a) : 1.0;
    // Validation intentionally preserves NaN/Inf in the HDR target. ToneMap.hlsl makes
    // non-finite presentation conspicuous, while --validate-hdr performs the authoritative readback scan.
    return float4(hdr_color, output_alpha);
}

float4 PSMain(VertexOutput input, bool is_front_face : SV_IsFrontFace) : SV_Target0
{
    GeometrySurface surface;
    return shade_pixel(input, is_front_face, false, false, surface);
}

struct GBufferOutput
{
    float4 color : SV_Target0;
    float4 normal_roughness : SV_Target1;
    float4 albedo_metallic : SV_Target2;
    float4 position_depth : SV_Target3;
    float4 motion : SV_Target4;
    uint4 identifiers : SV_Target5;
};

// OPAQUE/MASK G-buffer variant (hybrid primary surface, temporal inputs, upscaler inputs).
GBufferOutput PSMainGBuffer(VertexOutput input, bool is_front_face : SV_IsFrontFace)
{
    const bool eligible = (temporal_flags & TEMPORAL_DRAW_FLAG_HYBRID_ELIGIBLE) != 0u;
    const bool defer_direct = eligible && (g_view.hybrid_flags & HYBRID_FLAG_RT_SHADOWS) != 0u;
    const bool defer_specular = eligible && (g_view.hybrid_flags & HYBRID_FLAG_RT_REFLECTIONS) != 0u;
    // True facet normal from screen-space position derivatives (computed before any discard),
    // oriented toward the camera. Used for robust ray-origin offsets and denoiser plane tests.
    float3 facet_normal = cross(ddy(input.world_position), ddx(input.world_position));
    const float facet_length_squared = dot(facet_normal, facet_normal);
    facet_normal = facet_length_squared > 1.0e-30 ? facet_normal * rsqrt(facet_length_squared) : float3(0.0, 0.0, 1.0);
    if (dot(facet_normal, camera_position.xyz - input.world_position) < 0.0)
        facet_normal = -facet_normal;
    GeometrySurface surface;
    GBufferOutput output;
    output.color = shade_pixel(input, is_front_face, defer_direct, defer_specular, surface);
    output.normal_roughness = float4(surface.shading_normal, surface.roughness);
    output.albedo_metallic = float4(surface.base_color, surface.metallic);
    output.position_depth = float4(input.world_position, max(input.view_depth, 1.0e-6));
    const float2 current_pixel = clip_to_pixel(input.current_clip, g_view.render_size.xy);
    const float2 previous_pixel = clip_to_pixel(input.previous_clip, g_view.render_size.xy);
    // Previous clip w of the unjittered projection is the point's previous linear view depth.
    output.motion = float4(previous_pixel - current_pixel, max(input.previous_clip.w, 1.0e-6), 1.0);
    uint surface_flags = eligible ? GBUFFER_FLAG_HYBRID_ELIGIBLE : 0u;
    if ((flags & FLAG_DOUBLE_SIDED) != 0u)
        surface_flags |= GBUFFER_FLAG_DOUBLE_SIDED;
    output.identifiers = uint4(instance_id + 1u,
                               material_table_index + 1u,
                               encode_octahedral_normal(facet_normal),
                               (f32tof16(saturate(surface.ambient_occlusion)) & 0xffffu) | (surface_flags << 16u));
    return output;
}
