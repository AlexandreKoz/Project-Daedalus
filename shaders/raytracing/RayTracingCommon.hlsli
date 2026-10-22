#ifndef DAEDALUS_RAY_TRACING_COMMON_HLSLI
#define DAEDALUS_RAY_TRACING_COMMON_HLSLI

// Shared DXR bindings, payloads, hit/miss shaders and surface reconstruction for every Daedalus
// ray-tracing library. Binding layout: src/graphics/raytracing/RaytracingScene.h. Structure layouts:
// src/rendering/RayTracingContract.h (static_asserted).

#include "common/PbrLighting.hlsli"
#include "common/Environment.hlsli"
#include "common/FrameContract.hlsli"
#include "common/Sampling.hlsli"

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

struct RtInstanceData
{
    float4 object_to_world[3];
    float4 previous_object_to_world[3];
    float4 normal_to_world[3];
    uint geometry_index;
    uint material_index;
    uint flags;
    uint emissive_triangle_base;
};

struct RtGeometryData
{
    // Both descriptor fields index g_buffers[] directly: they are relative to the table base the CPU
    // binds (GpuSceneDescriptorLayout::raw_base()), not absolute heap indices. Never add raw_base here.
    uint vertex_buffer_descriptor;
    uint index_buffer_descriptor;
    uint vertex_count;
    uint index_count;
    uint vertex_stride;
    uint flags;
    uint primitive_index;
    uint padding0;
};

struct RtMaterialData
{
    float4 base_color_factor;
    float4 emissive_metallic;
    float4 roughness_normal_ao_cutoff;
    float4 transmission_ior;
    float4 attenuation_color;
    uint base_color_texture;
    uint metallic_roughness_texture;
    uint normal_texture;
    uint occlusion_texture;
    uint emissive_texture;
    uint base_color_sampler;
    uint metallic_roughness_sampler;
    uint normal_sampler;
    uint occlusion_sampler;
    uint emissive_sampler;
    uint texcoord_sets;
    uint flags;
    uint diagnostic_id;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct RtEmissiveTriangle
{
    uint instance_index;
    uint triangle_index;
    float alias_threshold;
    uint alias;
    float pmf;
    float padding0;
    float padding1;
    float padding2;
};

struct RtPassConstantsData
{
    uint light_count;
    uint instance_count;
    uint emissive_triangle_count;
    uint flags;
    uint sample_base;
    uint samples_per_dispatch;
    uint max_depth;
    uint min_russian_roulette_depth;
    uint seed;
    uint debug_view;
    uint frame_index;
    uint accumulation_reset;
    float firefly_clamp;
    float reflection_max_roughness;
    float ray_extent;
    float environment_sun_probability;
    float probability_punctual;
    float probability_emissive;
    float probability_environment;
    uint display_sample_target;
};

ConstantBuffer<ViewConstantsData> g_view : register(b0);
cbuffer LightConstants : register(b1)
{
    GpuLight g_lights[32];
};
ConstantBuffer<RtPassConstantsData> g_pass : register(b2);
RaytracingAccelerationStructure g_scene : register(t0);
StructuredBuffer<RtInstanceData> g_instances : register(t1);
StructuredBuffer<RtGeometryData> g_geometries : register(t2);
StructuredBuffer<RtMaterialData> g_materials : register(t3);
StructuredBuffer<RtEmissiveTriangle> g_emissive_triangles : register(t4);
Texture2D<float4> g_textures[] : register(t0, space1);
ByteAddressBuffer g_buffers[] : register(t0, space2);
SamplerState g_samplers[] : register(s0, space1);

static const uint INSTANCE_NEGATIVE_DETERMINANT = 1u << 0u;
static const uint INSTANCE_DOUBLE_SIDED = 1u << 1u;
static const uint INSTANCE_ALPHA_MASK = 1u << 2u;
static const uint INSTANCE_ALPHA_BLEND = 1u << 3u;
static const uint INSTANCE_EMISSIVE = 1u << 4u;
static const uint GEOMETRY_HAS_NORMALS = 1u << 0u;
static const uint GEOMETRY_HAS_TANGENTS = 1u << 1u;
static const uint GEOMETRY_HAS_TEXCOORD1 = 1u << 2u;
static const uint GEOMETRY_HAS_COLORS = 1u << 3u;
static const uint MATERIAL_ALPHA_MODE_MASK = 0x3u;
static const uint MATERIAL_DOUBLE_SIDED = 1u << 2u;
static const uint MATERIAL_BASE_COLOR_TEXTURE = 1u << 3u;
static const uint MATERIAL_METALLIC_ROUGHNESS_TEXTURE = 1u << 4u;
static const uint MATERIAL_NORMAL_TEXTURE = 1u << 5u;
static const uint MATERIAL_OCCLUSION_TEXTURE = 1u << 6u;
static const uint MATERIAL_EMISSIVE_TEXTURE = 1u << 7u;
static const uint MATERIAL_TRANSMISSION = 1u << 8u;
static const uint MATERIAL_VOLUME = 1u << 9u;
static const uint PASS_SHADOWS = 1u << 0u;
static const uint PASS_REFLECTIONS = 1u << 1u;
static const uint PASS_PUNCTUAL_NEE = 1u << 2u;
static const uint PASS_EMISSIVE_NEE = 1u << 3u;
static const uint PASS_ENVIRONMENT_NEE = 1u << 4u;
static const uint PASS_DENOISER_OUTPUTS = 1u << 5u;
static const uint PASS_ACCUMULATE = 1u << 6u;
static const uint MASK_OPAQUE_OR_MASKED = 0x01u;
static const uint MASK_ALL = 0xffu;
static const uint RAY_TYPE_RADIANCE = 0u;
static const uint RAY_TYPE_VISIBILITY = 1u;
static const uint RAY_TYPE_COUNT = 2u;
static const uint PAYLOAD_HIT = 1u << 0u;
static const uint PAYLOAD_FRONT_FACE = 1u << 1u;

// Counter slots (src/rendering/RayTracingContract.h RtCounter).
static const uint COUNTER_PRIMARY_SAMPLES = 0u;
static const uint COUNTER_PATH_SEGMENTS = 1u;
static const uint COUNTER_NONFINITE_RADIANCE = 2u;
static const uint COUNTER_NONFINITE_THROUGHPUT = 3u;
static const uint COUNTER_INVALID_PDF = 4u;
static const uint COUNTER_NEGATIVE_RADIANCE = 5u;
static const uint COUNTER_CLAMPED_SAMPLES = 6u;
static const uint COUNTER_TERMINATED_RUSSIAN_ROULETTE = 7u;
static const uint COUNTER_TERMINATED_MAX_DEPTH = 8u;
static const uint COUNTER_TERMINATED_MISS = 9u;
static const uint COUNTER_TERMINATED_ABSORBED = 10u;
static const uint COUNTER_VISIBILITY_RAYS = 11u;
static const uint COUNTER_TOTAL_INTERNAL_REFLECTIONS = 12u;
static const uint COUNTER_LIGHT_SAMPLES = 13u;
static const uint COUNTER_REFLECTION_RAYS = 14u;

// Radiance payload carries hit information only (32 bytes = kRadiancePayloadBytes). Shading is
// done in ray generation so the pipeline needs MaxTraceRecursionDepth = 1.
struct RadiancePayload
{
    float2 barycentrics;
    float hit_t;
    uint instance_index;
    uint primitive_index;
    uint flags;
    uint rng_seed;
    uint padding;
};

// Visibility payload (8 bytes = kVisibilityPayloadBytes).
struct VisibilityPayload
{
    uint visible;
    uint rng_seed;
};

uint3 fetch_triangle_indices(RtGeometryData geometry, uint primitive_index)
{
    return g_buffers[NonUniformResourceIndex(geometry.index_buffer_descriptor)].Load3(primitive_index * 12u);
}

float3 fetch_position(RtGeometryData geometry, uint vertex)
{
    return asfloat(g_buffers[NonUniformResourceIndex(geometry.vertex_buffer_descriptor)].Load3(vertex * geometry.vertex_stride));
}

float3 fetch_normal(RtGeometryData geometry, uint vertex)
{
    return asfloat(g_buffers[NonUniformResourceIndex(geometry.vertex_buffer_descriptor)].Load3(vertex * geometry.vertex_stride + 12u));
}

float4 fetch_tangent(RtGeometryData geometry, uint vertex)
{
    return asfloat(g_buffers[NonUniformResourceIndex(geometry.vertex_buffer_descriptor)].Load4(vertex * geometry.vertex_stride + 24u));
}

float4 fetch_texcoords(RtGeometryData geometry, uint vertex)
{
    return asfloat(g_buffers[NonUniformResourceIndex(geometry.vertex_buffer_descriptor)].Load4(vertex * geometry.vertex_stride + 40u));
}

float4 fetch_color(RtGeometryData geometry, uint vertex)
{
    return asfloat(g_buffers[NonUniformResourceIndex(geometry.vertex_buffer_descriptor)].Load4(vertex * geometry.vertex_stride + 56u));
}

float3 transform_point_rows(float4 rows[3], float3 p)
{
    return float3(dot(rows[0], float4(p, 1.0)), dot(rows[1], float4(p, 1.0)), dot(rows[2], float4(p, 1.0)));
}

float3 transform_vector_rows(float4 rows[3], float3 v)
{
    return float3(dot(rows[0].xyz, v), dot(rows[1].xyz, v), dot(rows[2].xyz, v));
}

float4 sample_material_texture(uint texture_index, uint sampler_index, float2 uv)
{
    // One mip is uploaded and raster samplers clamp LOD to zero, so level 0 matches raster exactly.
    return g_textures[NonUniformResourceIndex(texture_index)].SampleLevel(g_samplers[NonUniformResourceIndex(sampler_index)], uv, 0.0);
}

float2 select_texcoord(RtMaterialData material, uint slot, float4 uv01)
{
    return ((material.texcoord_sets >> slot) & 1u) != 0u ? uv01.zw : uv01.xy;
}

// Alpha evaluation shared by every any-hit shader. MASK uses exactly the raster rule
// (discard if alpha < cutoff). BLEND is evaluated as stochastic coverage: the hit is accepted with
// probability alpha (a declared, physically interpretable coverage model, not refraction).
bool alpha_accepts_hit(uint instance_index, uint primitive_index, float2 barycentrics, uint rng_seed)
{
    const RtInstanceData instance = g_instances[instance_index];
    const RtMaterialData material = g_materials[instance.material_index];
    const uint alpha_mode = material.flags & MATERIAL_ALPHA_MODE_MASK;
    if (alpha_mode == 0u)
        return true;
    const RtGeometryData geometry = g_geometries[instance.geometry_index];
    const uint3 indices = fetch_triangle_indices(geometry, primitive_index);
    const float3 weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    const float4 uv01 = fetch_texcoords(geometry, indices.x) * weights.x + fetch_texcoords(geometry, indices.y) * weights.y +
                        fetch_texcoords(geometry, indices.z) * weights.z;
    float alpha = material.base_color_factor.a;
    if ((geometry.flags & GEOMETRY_HAS_COLORS) != 0u)
        alpha *= fetch_color(geometry, indices.x).a * weights.x + fetch_color(geometry, indices.y).a * weights.y +
                 fetch_color(geometry, indices.z).a * weights.z;
    if ((material.flags & MATERIAL_BASE_COLOR_TEXTURE) != 0u)
        alpha *= sample_material_texture(material.base_color_texture, material.base_color_sampler, select_texcoord(material, 0u, uv01)).a;
    if (alpha_mode == 1u)
        return alpha >= material.roughness_normal_ao_cutoff.w;
    const uint hashed = pcg_hash(rng_seed ^ pcg_hash(instance_index * 0x9E3779B9u ^ primitive_index));
    return float(hashed >> 8u) * (1.0 / 16777216.0) < saturate(alpha);
}

[shader("closesthit")]
void RadianceClosestHit(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    payload.barycentrics = attributes.barycentrics;
    payload.hit_t = RayTCurrent();
    payload.instance_index = InstanceID();
    payload.primitive_index = PrimitiveIndex();
    payload.flags = PAYLOAD_HIT | (HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE ? PAYLOAD_FRONT_FACE : 0u);
}

[shader("anyhit")]
void RadianceAnyHit(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    if (!alpha_accepts_hit(InstanceID(), PrimitiveIndex(), attributes.barycentrics, payload.rng_seed))
        IgnoreHit();
}

[shader("anyhit")]
void VisibilityAnyHit(inout VisibilityPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    if (!alpha_accepts_hit(InstanceID(), PrimitiveIndex(), attributes.barycentrics, payload.rng_seed))
        IgnoreHit();
}

[shader("miss")]
void RadianceMiss(inout RadiancePayload payload)
{
    payload.flags = 0u;
    payload.hit_t = -1.0;
}

[shader("miss")]
void VisibilityMiss(inout VisibilityPayload payload)
{
    payload.visible = 1u;
}

// Wächter & Binder, "A Fast and Robust Method for Avoiding Self-Intersection" (Ray Tracing Gems,
// ch. 6): offsets in integer ULP space proportional to the position magnitude.
float3 offset_ray_origin(float3 position, float3 geometric_normal)
{
    const float origin = 1.0 / 32.0;
    const float float_scale = 1.0 / 65536.0;
    const float int_scale = 256.0;
    const int3 offset = int3(int_scale * geometric_normal);
    float3 result;
    result.x = abs(position.x) < origin ? position.x + float_scale * geometric_normal.x
                                         : asfloat(asint(position.x) + (position.x < 0.0 ? -offset.x : offset.x));
    result.y = abs(position.y) < origin ? position.y + float_scale * geometric_normal.y
                                         : asfloat(asint(position.y) + (position.y < 0.0 ? -offset.y : offset.y));
    result.z = abs(position.z) < origin ? position.z + float_scale * geometric_normal.z
                                         : asfloat(asint(position.z) + (position.z < 0.0 ? -offset.z : offset.z));
    return result;
}

struct SurfaceHit
{
    float3 position;
    float3 object_position;
    float3 geometric_normal;   // world, unit, oriented toward the incoming ray side
    float3 shading_normal;     // world, unit, after normal mapping, same hemisphere convention
    float3 tangent;
    float tangent_sign;
    bool tangent_valid;
    bool front_face;
    float2 uv0;
    float2 uv1;
    float4 vertex_color;
    float triangle_area;       // world-space area
    uint instance_index;
    uint material_index;
};

struct MaterialSample
{
    float3 base_color;
    float alpha;
    float metallic;
    float roughness;
    float3 emissive;
    float occlusion;
    float transmission;
    float ior;
    float thickness;
    float attenuation_distance;
    float3 attenuation_color;
    uint alpha_mode;
    bool double_sided;
    uint diagnostic_id;
};

// Reconstructs the world-space surface for a TLAS hit with the CURRENT instance transform (the same
// transform the TLAS was built with this frame). Normals use the inverse-transpose rows; tangent
// handedness is multiplied by the determinant sign, mirroring the raster contract.
SurfaceHit reconstruct_surface(uint instance_index, uint primitive_index, float2 barycentrics, bool front_face)
{
    SurfaceHit hit;
    const RtInstanceData instance = g_instances[instance_index];
    const RtGeometryData geometry = g_geometries[instance.geometry_index];
    const uint3 indices = fetch_triangle_indices(geometry, primitive_index);
    const float3 weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    const float3 p0 = fetch_position(geometry, indices.x);
    const float3 p1 = fetch_position(geometry, indices.y);
    const float3 p2 = fetch_position(geometry, indices.z);
    hit.object_position = p0 * weights.x + p1 * weights.y + p2 * weights.z;
    hit.position = transform_point_rows(instance.object_to_world, hit.object_position);
    const float3 w0 = transform_point_rows(instance.object_to_world, p0);
    const float3 w1 = transform_point_rows(instance.object_to_world, p1);
    const float3 w2 = transform_point_rows(instance.object_to_world, p2);
    hit.triangle_area = 0.5 * length(cross(w1 - w0, w2 - w0));

    // Outward (front-face) geometric normal: inverse-transpose of the object-space winding normal.
    float3 geometric = transform_vector_rows(instance.normal_to_world, cross(p1 - p0, p2 - p0));
    const float geometric_length_squared = dot(geometric, geometric);
    geometric = geometric_length_squared > 1.0e-30 ? geometric * rsqrt(geometric_length_squared) : float3(0.0, 0.0, 1.0);

    float3 normal = transform_vector_rows(instance.normal_to_world,
        fetch_normal(geometry, indices.x) * weights.x + fetch_normal(geometry, indices.y) * weights.y + fetch_normal(geometry, indices.z) * weights.z);
    const float normal_length_squared = dot(normal, normal);
    normal = normal_length_squared > 1.0e-20 ? normal * rsqrt(normal_length_squared) : geometric;

    const float4 tangent_object = fetch_tangent(geometry, indices.x) * weights.x + fetch_tangent(geometry, indices.y) * weights.y +
                                  fetch_tangent(geometry, indices.z) * weights.z;
    const float determinant_sign = (instance.flags & INSTANCE_NEGATIVE_DETERMINANT) != 0u ? -1.0 : 1.0;
    // Back-face hits (double-sided materials) reverse N and handedness, exactly like the raster
    // SV_IsFrontFace path; single-sided back faces are culled by the ray flags.
    const float face_sign = front_face ? 1.0 : -1.0;
    hit.front_face = front_face;
    hit.geometric_normal = geometric * face_sign;
    normal *= face_sign;
    float3 tangent = transform_vector_rows(instance.object_to_world, tangent_object.xyz);
    tangent -= normal * dot(normal, tangent);
    const float tangent_length_squared = dot(tangent, tangent);
    hit.tangent_valid = (geometry.flags & GEOMETRY_HAS_TANGENTS) != 0u && tangent_length_squared > 1.0e-20;
    hit.tangent = hit.tangent_valid ? tangent * rsqrt(tangent_length_squared) : float3(1.0, 0.0, 0.0);
    hit.tangent_sign = (tangent_object.w >= 0.0 ? 1.0 : -1.0) * determinant_sign * face_sign;
    hit.shading_normal = normal;
    const float4 uv01 = fetch_texcoords(geometry, indices.x) * weights.x + fetch_texcoords(geometry, indices.y) * weights.y +
                        fetch_texcoords(geometry, indices.z) * weights.z;
    hit.uv0 = uv01.xy;
    hit.uv1 = uv01.zw;
    hit.vertex_color = (geometry.flags & GEOMETRY_HAS_COLORS) != 0u
        ? fetch_color(geometry, indices.x) * weights.x + fetch_color(geometry, indices.y) * weights.y + fetch_color(geometry, indices.z) * weights.z
        : float4(1.0, 1.0, 1.0, 1.0);
    hit.instance_index = instance_index;
    hit.material_index = instance.material_index;
    return hit;
}

// Same material semantics as RasterPbr.hlsl: sRGB-decoded base/emissive, linear MR/normal/AO,
// roughness = G, metallic = B, occlusion = R, vertex colour multiplies base colour.
MaterialSample evaluate_material(inout SurfaceHit hit)
{
    const RtMaterialData material = g_materials[hit.material_index];
    const float4 uv01 = float4(hit.uv0, hit.uv1);
    MaterialSample result;
    float4 base = material.base_color_factor * hit.vertex_color;
    if ((material.flags & MATERIAL_BASE_COLOR_TEXTURE) != 0u)
        base *= sample_material_texture(material.base_color_texture, material.base_color_sampler, select_texcoord(material, 0u, uv01));
    result.base_color = max(base.rgb, 0.0);
    result.alpha = base.a;
    float4 metallic_roughness = 1.0.xxxx;
    if ((material.flags & MATERIAL_METALLIC_ROUGHNESS_TEXTURE) != 0u)
        metallic_roughness = sample_material_texture(material.metallic_roughness_texture, material.metallic_roughness_sampler,
                                                     select_texcoord(material, 1u, uv01));
    result.roughness = saturate(material.roughness_normal_ao_cutoff.x * metallic_roughness.g);
    result.metallic = saturate(material.emissive_metallic.w * metallic_roughness.b);
    float3 emissive = material.emissive_metallic.xyz * g_view.emissive_scale;
    if ((material.flags & MATERIAL_EMISSIVE_TEXTURE) != 0u)
        emissive *= sample_material_texture(material.emissive_texture, material.emissive_sampler, select_texcoord(material, 4u, uv01)).rgb;
    result.emissive = emissive;
    float occlusion_sample = 1.0;
    if ((material.flags & MATERIAL_OCCLUSION_TEXTURE) != 0u)
        occlusion_sample = sample_material_texture(material.occlusion_texture, material.occlusion_sampler, select_texcoord(material, 3u, uv01)).r;
    result.occlusion = lerp(1.0, occlusion_sample, saturate(material.roughness_normal_ao_cutoff.z));
    if ((material.flags & MATERIAL_NORMAL_TEXTURE) != 0u && hit.tangent_valid)
    {
        float3 tangent_normal = sample_material_texture(material.normal_texture, material.normal_sampler, select_texcoord(material, 2u, uv01)).xyz * 2.0 - 1.0;
        tangent_normal.xy *= material.roughness_normal_ao_cutoff.y;
        const float tangent_normal_length_squared = dot(tangent_normal, tangent_normal);
        if (tangent_normal_length_squared > 1.0e-20)
        {
            tangent_normal *= rsqrt(tangent_normal_length_squared);
            const float3 bitangent = normalize(cross(hit.shading_normal, hit.tangent)) * hit.tangent_sign;
            const float3 mapped = hit.tangent * tangent_normal.x + bitangent * tangent_normal.y + hit.shading_normal * tangent_normal.z;
            const float mapped_length_squared = dot(mapped, mapped);
            if (mapped_length_squared > 1.0e-20)
                hit.shading_normal = mapped * rsqrt(mapped_length_squared);
        }
    }
    result.transmission = (material.flags & MATERIAL_TRANSMISSION) != 0u ? saturate(material.transmission_ior.x) : 0.0;
    result.ior = max(material.transmission_ior.y, 1.0);
    result.thickness = (material.flags & MATERIAL_VOLUME) != 0u ? material.transmission_ior.z : 0.0;
    result.attenuation_distance = material.transmission_ior.w;
    result.attenuation_color = material.attenuation_color.rgb;
    result.alpha_mode = material.flags & MATERIAL_ALPHA_MODE_MASK;
    result.double_sided = (material.flags & MATERIAL_DOUBLE_SIDED) != 0u;
    result.diagnostic_id = material.diagnostic_id;
    return result;
}

// Punctual light evaluation identical to RasterPbr.hlsl (glTF KHR_lights_punctual semantics).
bool evaluate_punctual_light(uint index, float3 position, out float3 light_direction, out float light_distance, out float3 radiance)
{
    const GpuLight light = g_lights[index];
    light_direction = float3(0.0, 1.0, 0.0);
    light_distance = g_pass.ray_extent;
    radiance = 0.0;
    float attenuation = 1.0;
    if (light.type == 0u)
    {
        float3 direction = -light.direction_outer_cos.xyz;
        const float len2 = dot(direction, direction);
        if (len2 <= 1.0e-20)
            return false;
        light_direction = direction * rsqrt(len2);
    }
    else
    {
        const float3 to_light = light.position_range.xyz - position;
        const float distance_squared = dot(to_light, to_light);
        if (distance_squared <= 1.0e-20)
            return false;
        light_distance = sqrt(distance_squared);
        light_direction = to_light / light_distance;
        attenuation = distance_attenuation(light_distance, light.position_range.w);
        if (light.type == 2u)
        {
            const float3 emitted_direction = normalize(light.direction_outer_cos.xyz);
            attenuation *= spot_attenuation(dot(emitted_direction, -light_direction), light.inner_cone_cos, light.direction_outer_cos.w);
        }
    }
    if (attenuation <= 0.0)
        return false;
    radiance = light.color_intensity.rgb * (light.color_intensity.w * attenuation);
    return any(radiance > 0.0);
}

// Visibility between `origin` (already offset) and a point at `distance` along `direction`.
// `mask` selects which instances occlude; BLEND instances are excluded by hybrid shadows to match
// the raster shadow-caster contract and included (stochastically) by the path tracer.
bool trace_visibility(float3 origin, float3 direction, float max_distance, uint mask, uint rng_seed)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = max(max_distance, 0.0);
    VisibilityPayload payload;
    payload.visible = 0u;
    payload.rng_seed = rng_seed;
    TraceRay(g_scene,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
             mask, RAY_TYPE_VISIBILITY, 0, RAY_TYPE_VISIBILITY, ray, payload);
    return payload.visible != 0u;
}

RadiancePayload trace_radiance(float3 origin, float3 direction, float t_max, uint mask, uint rng_seed)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = t_max;
    RadiancePayload payload;
    payload.barycentrics = 0.0;
    payload.hit_t = -1.0;
    payload.instance_index = 0u;
    payload.primitive_index = 0u;
    payload.flags = 0u;
    payload.rng_seed = rng_seed;
    payload.padding = 0u;
    TraceRay(g_scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, mask, RAY_TYPE_RADIANCE, 0, RAY_TYPE_RADIANCE, ray, payload);
    return payload;
}

float3 environment_radiance(float3 direction)
{
    return procedural_environment_radiance(direction) * max(g_view.environment_intensity, 0.0);
}

float3 material_id_color(uint identifier)
{
    if (identifier == 0u)
        return float3(1.0, 0.0, 1.0);
    uint hash = identifier * 747796405u + 2891336453u;
    hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
    hash = (hash >> 22u) ^ hash;
    return float3(0.2 + 0.8 * float((hash >> 0u) & 255u) / 255.0,
                  0.2 + 0.8 * float((hash >> 8u) & 255u) / 255.0,
                  0.2 + 0.8 * float((hash >> 16u) & 255u) / 255.0);
}

#endif
