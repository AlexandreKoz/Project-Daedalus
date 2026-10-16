#pragma once

#ifndef _WIN32
#error AccelerationStructureManager is available only on Windows.
#endif

#include "graphics/D3D12Context.h"
#include "graphics/GpuSceneResources.h"
#include "rendering/RayTracingPreparation.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace daedalus
{
struct AccelerationStructureStats
{
    std::uint64_t blas_count = 0;
    std::uint64_t blas_triangles = 0;
    std::uint64_t blas_bytes_before_compaction = 0;
    std::uint64_t blas_bytes = 0;
    std::uint64_t tlas_bytes = 0;
    std::uint64_t tlas_scratch_bytes = 0;
    std::uint64_t instance_count = 0;
    std::uint64_t tlas_full_builds = 0;
    std::uint64_t tlas_updates = 0;
    bool compaction_applied = false;

    [[nodiscard]] std::string to_json() const;
};

// Owns all bottom- and top-level acceleration structures for one GpuSceneResources instance.
//
// BLAS: one per canonical primitive (exactly one triangle geometry each), built once at creation
// with PREFER_FAST_TRACE | ALLOW_COMPACTION, then compacted through an explicit postbuild-size
// readback. Static BLAS are never rebuilt per frame. Geometry flags come from the material alpha
// mode (OPAQUE skips any-hit; MASK/BLEND use NO_DUPLICATE_ANYHIT_INVOCATION).
//
// TLAS: one instance per prepared raster draw (InstanceID == draw index). Built with ALLOW_UPDATE;
// rigid transform changes perform an in-place update, with a full rebuild every
// kUpdatesBeforeRebuild updates to bound BVH quality loss. Instance descriptors are written into a
// per-frame-slot upload buffer so the CPU never overwrites descriptors a queued build still reads.
//
// Lifetime: destroying this object requires GPU idle (owned by SceneRenderer, which is destroyed
// only after D3D12Context::wait_for_gpu on reload/mode change/shutdown).
class AccelerationStructureManager final
{
public:
    static constexpr std::uint64_t kUpdatesBeforeRebuild = 32U;

    AccelerationStructureManager(D3D12Context& context,
                                 GpuSceneResources& resources,
                                 const RtSceneTables& tables,
                                 std::span<const Mat4> initial_world,
                                 bool enable_compaction = true);
    ~AccelerationStructureManager();

    AccelerationStructureManager(const AccelerationStructureManager&) = delete;
    AccelerationStructureManager& operator=(const AccelerationStructureManager&) = delete;
    AccelerationStructureManager(AccelerationStructureManager&&) = delete;
    AccelerationStructureManager& operator=(AccelerationStructureManager&&) = delete;

    // Records a TLAS build (first use or forced) or update (transforms changed), followed by a UAV
    // barrier on the TLAS. Returns true when any TLAS work was recorded.
    bool record_top_level(ID3D12GraphicsCommandList4* command_list,
                          std::uint32_t frame_index,
                          std::span<const Mat4> world,
                          bool transforms_changed);
    void force_rebuild() noexcept { force_rebuild_ = true; }

    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS top_level_address() const noexcept;
    [[nodiscard]] const AccelerationStructureStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::uint64_t committed_allocation_bytes() const noexcept;

private:
    void build_bottom_level(const RtSceneTables& tables, bool enable_compaction);
    void create_top_level();
    void write_instance_descs(std::uint32_t frame_index, std::span<const Mat4> world);

    D3D12Context& context_;
    GpuSceneResources& resources_;
    ID3D12Device5* device_ = nullptr;
    std::vector<RtInstancePolicy> policies_;
    std::vector<std::uint32_t> instance_geometry_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> bottom_levels_;
    Microsoft::WRL::ComPtr<ID3D12Resource> top_level_;
    Microsoft::WRL::ComPtr<ID3D12Resource> top_level_scratch_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, D3D12Context::kFrameCount> instance_descs_{};
    std::array<D3D12_RAYTRACING_INSTANCE_DESC*, D3D12Context::kFrameCount> mapped_instance_descs_{};
    bool top_level_built_ = false;
    bool force_rebuild_ = false;
    std::uint64_t updates_since_rebuild_ = 0;
    AccelerationStructureStats stats_{};
};
}
