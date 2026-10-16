// Campaign E progressive reference path tracer (DXIL lib_6_3).
//
// Architecture: iterative path state in the ray-generation shader; TraceRay returns hit
// information only (MaxTraceRecursionDepth = 1). Every vertex performs next-event estimation with
// one light sample chosen among {punctual (delta), emissive triangles, analytic environment} by
// the CPU-provided category probabilities, combined with BSDF sampling via the power heuristic
// (delta punctual lights take the NEE contribution only). BSDF-sampled rays that hit emissive
// triangles or escape to the environment are MIS-weighted against the same light-selection pdfs,
// so no light is double counted. Russian roulette starts at min_russian_roulette_depth with
// throughput-based survival probability and 1/q correction.
//
// RNG: make_path_rng(pixel, sample_base + s, seed): identical per (pixel, global sample index,
// seed) irrespective of frame scheduling or frame slots.
//
// Outputs (u-registers): u0 accumulated radiance (rgb sum, a = samples), u1 moments
// (sum lum, sum lum^2, sum path length, sum discarded nonfinite samples), u2 accumulated direct,
// u3 display/debug output, u4..u10 denoiser inputs (demodulated noisy signal, albedo, primary
// emission, G-buffer), u11 counters.

#include "raytracing/PathTracingBsdf.hlsli"

RWTexture2D<float4> g_accumulation : register(u0);
RWTexture2D<float4> g_moments : register(u1);
RWTexture2D<float4> g_direct_accumulation : register(u2);
RWTexture2D<float4> g_output : register(u3);
RWTexture2D<float4> g_noisy_demodulated : register(u4);
RWTexture2D<float4> g_albedo : register(u5);
RWTexture2D<float4> g_primary_emission : register(u6);
RWTexture2D<float4> g_normal_roughness : register(u7);
RWTexture2D<float4> g_position_depth : register(u8);
RWTexture2D<uint4> g_identifiers : register(u9);
RWTexture2D<float4> g_motion : register(u10);
RWByteAddressBuffer g_counters : register(u11);

static const uint DEBUG_NONE = 0u;
static const uint DEBUG_SAMPLE_COUNT = 1u;
static const uint DEBUG_VARIANCE = 2u;
static const uint DEBUG_DIRECT = 3u;
static const uint DEBUG_INDIRECT = 4u;
static const uint DEBUG_PATH_LENGTH = 5u;
static const uint DEBUG_MATERIAL_ID = 6u;
static const uint DEBUG_NONFINITE = 7u;

struct PathCounters
{
    uint segments;
    uint visibility_rays;
    uint light_samples;
    uint nonfinite_throughput;
    uint invalid_pdf;
    uint terminated_russian_roulette;
    uint terminated_max_depth;
    uint terminated_miss;
    uint terminated_absorbed;
    uint total_internal_reflections;
};

struct PrimarySurface
{
    bool hit;
    float3 position;
    float3 shading_normal;
    float3 geometric_normal;
    float roughness;
    float3 albedo;
    float3 emission;
    float view_depth;
    float2 motion;
    float previous_view_depth;
    uint instance_index;
    uint material_index;
    uint diagnostic_id;
};

struct PathResult
{
    float3 radiance;
    float3 direct;
    uint segment_count;
};

void count_wave(uint counter, uint value)
{
    const uint total = WaveActiveSum(value);
    if (WaveIsFirstLane() && total != 0u)
        g_counters.InterlockedAdd(counter * 4u, total);
}

float3 sun_axis()
{
    return normalize(DAEDALUS_SUN_DIRECTION_UNNORMALIZED);
}

float environment_light_pdf(float3 direction)
{
    if ((g_pass.flags & PASS_ENVIRONMENT_NEE) == 0u || g_pass.probability_environment <= 0.0)
        return 0.0;
    return g_pass.probability_environment *
           environment_direction_pdf(sun_axis(), DAEDALUS_SUN_EXPONENT, g_pass.environment_sun_probability, direction);
}

float2 project_to_pixel(column_major float4x4 view_projection, float3 world)
{
    return clip_to_pixel(mul(view_projection, float4(world, 1.0)), g_view.render_size.xy);
}

// Solid-angle pdf with which emissive NEE would have sampled `light_point` on (instance, primitive).
float emissive_light_pdf(uint instance_index, uint primitive_index, float triangle_area, float distance_squared, float cos_light)
{
    if ((g_pass.flags & PASS_EMISSIVE_NEE) == 0u || g_pass.probability_emissive <= 0.0 || g_pass.emissive_triangle_count == 0u)
        return 0.0;
    const RtInstanceData instance = g_instances[instance_index];
    if (instance.emissive_triangle_base == 0xffffffffu || triangle_area <= 0.0 || cos_light <= 0.0)
        return 0.0;
    const RtEmissiveTriangle entry = g_emissive_triangles[instance.emissive_triangle_base + primitive_index];
    return g_pass.probability_emissive * entry.pmf * distance_squared / (triangle_area * cos_light);
}

float3 next_event_estimation(SurfaceHit hit, BsdfContext context, inout PathRng rng, inout PathCounters counters)
{
    const float category = rng_next_float(rng);
    const float3 u = float3(rng_next_float(rng), rng_next_float(rng), rng_next_float(rng));
    const uint visibility_seed = rng_next_uint(rng);
    const float3 origin_up = offset_ray_origin(hit.position, hit.geometric_normal);
    const float3 origin_down = offset_ray_origin(hit.position, -hit.geometric_normal);
    float cumulative = g_pass.probability_punctual;
    if (category < cumulative && (g_pass.flags & PASS_PUNCTUAL_NEE) != 0u && g_pass.light_count > 0u)
    {
        const uint index = min(uint(u.x * float(g_pass.light_count)), g_pass.light_count - 1u);
        float3 direction;
        float light_distance;
        float3 radiance;
        if (!evaluate_punctual_light(index, hit.position, direction, light_distance, radiance))
            return 0.0;
        float bsdf_pdf;
        const float3 f = bsdf_evaluate(context, direction, bsdf_pdf);
        if (!any(f > 0.0))
            return 0.0;
        ++counters.light_samples;
        ++counters.visibility_rays;
        const float3 origin = dot(direction, hit.geometric_normal) >= 0.0 ? origin_up : origin_down;
        const float max_distance = g_lights[index].type == 0u ? g_pass.ray_extent : light_distance * (1.0 - 1.0e-4);
        if (!trace_visibility(origin, direction, max_distance, MASK_ALL, visibility_seed))
            return 0.0;
        const float selection_pdf = g_pass.probability_punctual / float(g_pass.light_count);
        return f * radiance / selection_pdf; // delta light: no MIS
    }
    cumulative += g_pass.probability_emissive;
    if (category < cumulative && (g_pass.flags & PASS_EMISSIVE_NEE) != 0u && g_pass.emissive_triangle_count > 0u)
    {
        const uint bucket = min(uint(u.x * float(g_pass.emissive_triangle_count)), g_pass.emissive_triangle_count - 1u);
        const RtEmissiveTriangle candidate = g_emissive_triangles[bucket];
        const uint selected = u.y < candidate.alias_threshold ? bucket : candidate.alias;
        const RtEmissiveTriangle entry = g_emissive_triangles[selected];
        const float3 barycentric = sample_triangle_barycentrics(float2(u.z, rng_next_float(rng)));
        SurfaceHit light_point = reconstruct_surface(entry.instance_index, entry.triangle_index, barycentric.yz, true);
        const float3 to_light = light_point.position - hit.position;
        const float distance_squared = dot(to_light, to_light);
        if (distance_squared <= 1.0e-12 || light_point.triangle_area <= 0.0)
            return 0.0;
        const float light_distance = sqrt(distance_squared);
        const float3 direction = to_light / light_distance;
        const bool light_double_sided = (g_instances[entry.instance_index].flags & INSTANCE_DOUBLE_SIDED) != 0u;
        float cos_light = dot(light_point.geometric_normal, -direction);
        if (light_double_sided)
            cos_light = abs(cos_light);
        if (cos_light <= 1.0e-6)
            return 0.0;
        const float3 emitted = evaluate_material(light_point).emissive;
        if (!any(emitted > 0.0))
            return 0.0;
        float bsdf_pdf;
        const float3 f = bsdf_evaluate(context, direction, bsdf_pdf);
        if (!any(f > 0.0))
            return 0.0;
        const float light_pdf = g_pass.probability_emissive * entry.pmf * distance_squared / (light_point.triangle_area * cos_light);
        if (!(light_pdf > 0.0) || isinf(light_pdf))
            return 0.0;
        ++counters.light_samples;
        ++counters.visibility_rays;
        const float3 origin = dot(direction, hit.geometric_normal) >= 0.0 ? origin_up : origin_down;
        if (!trace_visibility(origin, direction, light_distance * (1.0 - 1.0e-4), MASK_ALL, visibility_seed))
            return 0.0;
        return f * emitted * power_heuristic(light_pdf, bsdf_pdf) / light_pdf;
    }
    cumulative += g_pass.probability_environment;
    if (category < cumulative && (g_pass.flags & PASS_ENVIRONMENT_NEE) != 0u)
    {
        const float3 direction = sample_environment_direction(sun_axis(), DAEDALUS_SUN_EXPONENT, g_pass.environment_sun_probability, u);
        const float light_pdf = environment_light_pdf(direction);
        if (!(light_pdf > 0.0))
            return 0.0;
        float bsdf_pdf;
        const float3 f = bsdf_evaluate(context, direction, bsdf_pdf);
        if (!any(f > 0.0))
            return 0.0;
        ++counters.light_samples;
        ++counters.visibility_rays;
        const float3 origin = dot(direction, hit.geometric_normal) >= 0.0 ? origin_up : origin_down;
        if (!trace_visibility(origin, direction, g_pass.ray_extent, MASK_ALL, visibility_seed))
            return 0.0;
        return f * environment_radiance(direction) * power_heuristic(light_pdf, bsdf_pdf) / light_pdf;
    }
    return 0.0;
}

PathResult trace_path(float3 origin, float3 direction, inout PathRng rng, bool record_primary, inout PrimarySurface primary,
                      inout PathCounters counters)
{
    PathResult result;
    result.radiance = 0.0;
    result.direct = 0.0;
    result.segment_count = 0u;
    float3 throughput = 1.0;
    float previous_bsdf_pdf = 0.0;
    bool previous_from_camera = true;
    bool inside_medium = false;
    float3 medium_sigma = 0.0;

    [loop]
    for (uint depth = 0u; depth < g_pass.max_depth; ++depth)
    {
        const RadiancePayload payload = trace_radiance(origin, direction, g_pass.ray_extent, MASK_ALL, rng_next_uint(rng));
        ++counters.segments;
        result.segment_count = depth + 1u;
        if ((payload.flags & PAYLOAD_HIT) == 0u)
        {
            const float3 environment = environment_radiance(direction);
            const float weight = previous_from_camera ? 1.0 : power_heuristic(previous_bsdf_pdf, environment_light_pdf(direction));
            const float3 contribution = throughput * environment * weight;
            result.radiance += contribution;
            if (depth <= 1u)
                result.direct += contribution;
            if (record_primary && depth == 0u)
            {
                primary.emission = environment;
                primary.motion = project_to_pixel(g_view.previous_view_projection_unjittered, g_view.previous_camera_position_far.xyz + direction * g_pass.ray_extent) -
                                 project_to_pixel(g_view.view_projection_unjittered, g_view.camera_position_near.xyz + direction * g_pass.ray_extent);
            }
            ++counters.terminated_miss;
            break;
        }

        const bool front_face = (payload.flags & PAYLOAD_FRONT_FACE) != 0u;
        SurfaceHit hit = reconstruct_surface(payload.instance_index, payload.primitive_index, payload.barycentrics, front_face);
        if (inside_medium)
            throughput *= exp(-medium_sigma * payload.hit_t);
        const MaterialSample material = evaluate_material(hit);

        if (any(material.emissive > 0.0))
        {
            float weight = 1.0;
            if (!previous_from_camera)
            {
                const float distance_squared = payload.hit_t * payload.hit_t;
                const float cos_light = abs(dot(hit.geometric_normal, -direction));
                weight = power_heuristic(previous_bsdf_pdf,
                                         emissive_light_pdf(payload.instance_index, payload.primitive_index, hit.triangle_area,
                                                            distance_squared, cos_light));
            }
            const float3 contribution = throughput * material.emissive * weight;
            result.radiance += contribution;
            if (depth <= 1u)
                result.direct += contribution;
        }

        const float3 view = -direction;
        BsdfContext context = make_bsdf_context(hit, material, view, front_face);
        if (record_primary && depth == 0u)
        {
            primary.hit = true;
            primary.position = hit.position;
            primary.shading_normal = context.normal;
            primary.geometric_normal = hit.geometric_normal;
            primary.roughness = material.roughness;
            primary.albedo = max(material.base_color * (1.0 - material.metallic) +
                                 lerp(DAEDALUS_DIELECTRIC_F0.xxx, material.base_color, material.metallic), 0.01);
            primary.emission = material.emissive;
            primary.view_depth = -mul(g_view.view, float4(hit.position, 1.0)).z;
            const RtInstanceData instance = g_instances[payload.instance_index];
            const float3 previous_position = transform_point_rows(instance.previous_object_to_world, hit.object_position);
            primary.motion = project_to_pixel(g_view.previous_view_projection_unjittered, previous_position) -
                             project_to_pixel(g_view.view_projection_unjittered, hit.position);
            primary.previous_view_depth = mul(g_view.previous_view_projection_unjittered, float4(previous_position, 1.0)).w;
            primary.instance_index = payload.instance_index;
            primary.material_index = hit.material_index;
            primary.diagnostic_id = material.diagnostic_id;
        }

        const float3 nee = throughput * next_event_estimation(hit, context, rng, counters);
        result.radiance += nee;
        if (depth == 0u)
            result.direct += nee;

        if (depth + 1u >= g_pass.max_depth)
        {
            ++counters.terminated_max_depth;
            break;
        }
        const BsdfSample bsdf = bsdf_sample(context, rng);
        if (bsdf.total_internal_reflection)
            ++counters.total_internal_reflections;
        if (!bsdf.valid)
        {
            if (bsdf.pdf <= 0.0 && any(bsdf.weight != 0.0))
                ++counters.invalid_pdf;
            ++counters.terminated_absorbed;
            break;
        }
        throughput *= bsdf.weight;
        if (!all_finite(throughput))
        {
            ++counters.nonfinite_throughput;
            break;
        }
        previous_bsdf_pdf = bsdf.pdf;
        previous_from_camera = false;
        if (bsdf.transmitted && context.volume)
        {
            inside_medium = front_face;
            if (inside_medium && material.attenuation_distance > 0.0)
                medium_sigma = -log(max(material.attenuation_color, 1.0e-6)) / material.attenuation_distance;
            else
                medium_sigma = 0.0;
        }
        origin = offset_ray_origin(hit.position, bsdf.transmitted ? -hit.geometric_normal : hit.geometric_normal);
        direction = bsdf.direction;

        if (depth + 1u >= g_pass.min_russian_roulette_depth)
        {
            const float survival = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 1.0);
            if (rng_next_float(rng) >= survival)
            {
                ++counters.terminated_russian_roulette;
                break;
            }
            throughput /= survival;
        }
    }
    return result;
}

[shader("raygeneration")]
void PathTraceRayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 size = DispatchRaysDimensions().xy;
    const bool denoiser_outputs = (g_pass.flags & PASS_DENOISER_OUTPUTS) != 0u;
    PathCounters counters = (PathCounters)0;
    PrimarySurface primary = (PrimarySurface)0;
    primary.albedo = 1.0;
    uint nonfinite_radiance = 0u;
    uint negative_radiance = 0u;
    uint clamped = 0u;

    float3 frame_sum = 0.0;
    float3 frame_direct = 0.0;
    float luminance_sum = 0.0;
    float luminance_squared_sum = 0.0;
    float length_sum = 0.0;
    float discarded = 0.0;
    const uint samples = max(g_pass.samples_per_dispatch, 1u);
    [loop]
    for (uint s = 0u; s < samples; ++s)
    {
        PathRng rng = make_path_rng(pixel, g_pass.sample_base + s, g_pass.seed);
        // Reference mode integrates the pixel box filter; denoiser mode traces pixel centres so the
        // primary G-buffer is stable for reprojection (declared: no sub-pixel AA in that mode).
        const float2 jitter = denoiser_outputs ? float2(0.5, 0.5) : rng_next_float2(rng);
        const float2 ndc = float2((float(pixel.x) + jitter.x) / float(size.x) * 2.0 - 1.0,
                                  1.0 - (float(pixel.y) + jitter.y) / float(size.y) * 2.0);
        const float4 far_point = mul(g_view.inverse_view_projection_unjittered, float4(ndc, 1.0, 1.0));
        const float3 camera = g_view.camera_position_near.xyz;
        const float3 direction = normalize(far_point.xyz / far_point.w - camera);
        PathResult path = trace_path(camera, direction, rng, s == 0u, primary, counters);

        float3 radiance = path.radiance;
        if (!all_finite(radiance))
        {
            // Numerical guard: the sample is discarded (contributes zero) and counted; outputs with
            // any discarded sample are reported as not certifiable reference results.
            ++nonfinite_radiance;
            discarded += 1.0;
            radiance = 0.0;
            path.direct = 0.0;
        }
        if (any(radiance < 0.0))
        {
            ++negative_radiance;
            radiance = max(radiance, 0.0);
            path.direct = max(path.direct, 0.0);
        }
        if (g_pass.firefly_clamp > 0.0)
        {
            const float sample_luminance = luminance(radiance);
            if (sample_luminance > g_pass.firefly_clamp)
            {
                const float scale = g_pass.firefly_clamp / sample_luminance;
                radiance *= scale;
                path.direct *= scale;
                ++clamped;
            }
        }
        const float sample_luminance = luminance(radiance);
        frame_sum += radiance;
        frame_direct += path.direct;
        luminance_sum += sample_luminance;
        luminance_squared_sum += sample_luminance * sample_luminance;
        length_sum += float(path.segment_count);
    }

    float4 accumulation = float4(frame_sum, float(samples));
    float4 moments = float4(luminance_sum, luminance_squared_sum, length_sum, discarded);
    float4 direct = float4(frame_direct, 0.0);
    if ((g_pass.flags & PASS_ACCUMULATE) != 0u)
    {
        // Progressive accumulation of the linear radiance SUM (never of tone-mapped values); the
        // mean is sum / sample count. A reset overwrites instead of adding.
        if (g_pass.accumulation_reset == 0u)
        {
            accumulation += g_accumulation[pixel];
            moments += g_moments[pixel];
            direct += g_direct_accumulation[pixel];
        }
        g_accumulation[pixel] = accumulation;
        g_moments[pixel] = moments;
        g_direct_accumulation[pixel] = direct;
    }

    const float count = max(accumulation.a, 1.0);
    const float3 mean = accumulation.rgb / count;
    float4 display = float4(mean, 1.0);
    const uint debug_view = g_pass.debug_view;
    if (debug_view == DEBUG_SAMPLE_COUNT)
        display = float4(saturate(count / float(max(g_pass.display_sample_target, 1u))).xxx, 1.0);
    else if (debug_view == DEBUG_VARIANCE)
    {
        const float mean_luminance = moments.x / count;
        const float variance_of_mean = max(moments.y / count - mean_luminance * mean_luminance, 0.0) / count;
        display = float4(saturate(sqrt(variance_of_mean) / max(mean_luminance, 1.0e-3)).xxx, 1.0);
    }
    else if (debug_view == DEBUG_DIRECT)
        display = float4(direct.rgb / count, 1.0);
    else if (debug_view == DEBUG_INDIRECT)
        display = float4(max(mean - direct.rgb / count, 0.0), 1.0);
    else if (debug_view == DEBUG_PATH_LENGTH)
        display = float4(saturate(moments.z / count / float(max(g_pass.max_depth, 1u))).xxx, 1.0);
    else if (debug_view == DEBUG_MATERIAL_ID)
        display = float4(primary.hit ? material_id_color(primary.diagnostic_id) : float3(0.0, 0.0, 0.0), 1.0);
    else if (debug_view == DEBUG_NONFINITE)
        display = moments.w > 0.0 ? float4(1.0, 0.0, 0.0, 1.0) : float4(saturate(luminance(mean)).xxx * 0.5, 1.0);
    g_output[pixel] = display;

    if (denoiser_outputs)
    {
        const float3 frame_mean = frame_sum / float(samples);
        const float3 emission = primary.emission;
        const float3 albedo = primary.hit ? primary.albedo : 1.0.xxx;
        const float3 demodulated = primary.hit ? max(frame_mean - emission, 0.0) / albedo : 0.0.xxx;
        g_noisy_demodulated[pixel] = float4(demodulated, 1.0);
        g_albedo[pixel] = float4(albedo, 1.0);
        g_primary_emission[pixel] = float4(emission, 1.0);
        g_normal_roughness[pixel] = float4(primary.hit ? primary.shading_normal : float3(0.0, 0.0, 0.0), primary.roughness);
        g_position_depth[pixel] = float4(primary.position, primary.hit ? max(primary.view_depth, 1.0e-6) : 0.0);
        g_identifiers[pixel] = primary.hit
            ? uint4(primary.instance_index + 1u, primary.material_index + 1u, encode_octahedral_normal(primary.geometric_normal),
                    (f32tof16(1.0) & 0xffffu))
            : uint4(0u, 0u, 0u, 0u);
        g_motion[pixel] = float4(primary.motion, primary.hit ? max(primary.previous_view_depth, 1.0e-6) : 0.0, 1.0);
    }

    count_wave(COUNTER_PRIMARY_SAMPLES, samples);
    count_wave(COUNTER_PATH_SEGMENTS, counters.segments);
    count_wave(COUNTER_NONFINITE_RADIANCE, nonfinite_radiance);
    count_wave(COUNTER_NONFINITE_THROUGHPUT, counters.nonfinite_throughput);
    count_wave(COUNTER_INVALID_PDF, counters.invalid_pdf);
    count_wave(COUNTER_NEGATIVE_RADIANCE, negative_radiance);
    count_wave(COUNTER_CLAMPED_SAMPLES, clamped);
    count_wave(COUNTER_TERMINATED_RUSSIAN_ROULETTE, counters.terminated_russian_roulette);
    count_wave(COUNTER_TERMINATED_MAX_DEPTH, counters.terminated_max_depth);
    count_wave(COUNTER_TERMINATED_MISS, counters.terminated_miss);
    count_wave(COUNTER_TERMINATED_ABSORBED, counters.terminated_absorbed);
    count_wave(COUNTER_VISIBILITY_RAYS, counters.visibility_rays);
    count_wave(COUNTER_TOTAL_INTERNAL_REFLECTIONS, counters.total_internal_reflections);
    count_wave(COUNTER_LIGHT_SAMPLES, counters.light_samples);
}
