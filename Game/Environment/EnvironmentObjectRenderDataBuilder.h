#pragma once

#include <cstdint>
#include <vector>
#include "Game/Environment/EnvironmentObjectTypes.h"
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

    EnvironmentObjectRenderPacket BuildEnvironmentObjectRenderPacket(const EnvironmentObjectCell& Cell);
    bool EmptyEnvironmentObjectRenderPacket(const EnvironmentObjectRenderPacket& Packet);
    void AppendEnvironmentObjectRenderPacketData(const EnvironmentObjectRenderPacket& Packet, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult);
    void AppendEnvironmentObjectCellRenderData(const EnvironmentObjectCell& Cell, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult);
}
