#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include "Environment/EnvironmentObjectTypes.h"
#include "RenderContract/Gather/RenderGatherResult.h"

namespace Game {
    struct EnvironmentObjectRenderPacketLod final {
    public:
        std::vector<RenderContract::EnvironmentSegmentContext> mSegmentContexts{};
        std::vector<RenderContract::EnvironmentDrawRecord> mDrawRecords{};
    };

    struct EnvironmentObjectRenderPacket final {
    public:
        EnvironmentObjectCellKey mCellKey{};
        DirectX::BoundingOrientedBox mWorldBoundingBox{};
        std::vector<RenderContract::EnvironmentInstanceContext> mInstanceContexts{};
        std::vector<std::uint32_t> mInstancePrototypeIndices{};
        std::vector<std::uint32_t> mPrototypeIndices{};
        std::vector<EnvironmentObjectRenderPacketLod> mLods{};
        std::uint64_t mGenerationVersion{};
        std::uint64_t mLastTouchedFrame{};
        bool mHasWorldBoundingBox{};
    };

    struct EnvironmentObjectRenderBuildOptions final {
    public:
        std::uint32_t mLodLevel{};
        std::uint32_t mShadowCascadeMask{ 0xffffffffu };
        bool mEnableMainPass{ true };
        bool mEnableShadowPass{ true };
    };

    struct EnvironmentObjectRenderViewPacket final {
    public:
        const EnvironmentObjectRenderPacket* mPacket{};
        const std::uint32_t* mPrototypeLodLevels{};
        std::size_t mPrototypeLodLevelCount{};
        std::uint32_t mVisibilityMask{};
    };

    std::uint32_t GetEnvironmentObjectMainVisibilityMaskBit();
    std::uint32_t BuildEnvironmentObjectShadowVisibilityMaskBit(std::uint32_t CascadeIndex);
    std::uint32_t BuildEnvironmentObjectVisibilityMask(bool IsMainVisible, std::uint32_t ShadowCascadeMask);

    EnvironmentObjectRenderPacket BuildEnvironmentObjectRenderPacket(const EnvironmentObjectCell& Cell);
    bool EmptyEnvironmentObjectRenderPacket(const EnvironmentObjectRenderPacket& Packet);
    void AppendEnvironmentObjectRenderViewData(std::span<const EnvironmentObjectRenderViewPacket> ViewPackets, RenderContract::RenderGatherResult& OutRenderGatherResult);
    void AppendEnvironmentObjectRenderPacketDataLegacy(const EnvironmentObjectRenderPacket& Packet, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult);
    void AppendEnvironmentObjectCellRenderDataLegacy(const EnvironmentObjectCell& Cell, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult);
    void AppendEnvironmentObjectRenderPacketData(const EnvironmentObjectRenderPacket& Packet, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult);
    void AppendEnvironmentObjectCellRenderData(const EnvironmentObjectCell& Cell, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult);
}
