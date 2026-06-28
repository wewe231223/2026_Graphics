#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d12.h>

#include "RenderContract/Frame/ShadowMappingParameter.h"
#include "RenderContract/Future/Future.h"

namespace RenderContract {
    class IPipeline;
    class IModelNode;

    struct EnvironmentGpuDrivenDrawBatch final {
    public:
        const IPipeline* mPipeline{};
        const IModelNode* mMesh{};
        std::uint32_t mDrawRecordOffset{};
        std::uint32_t mDrawRecordCount{};
        std::uint32_t mShadowCascadeMask{ 0xffffffffu };
        bool mCastsShadow{ true };
        bool mBillboard{};
    };

    struct EnvironmentGpuDrivenFrameData final {
    public:
        Future mGpuDispatchFuture{};
        ID3D12Resource* mInstanceContextResource{};
        ID3D12Resource* mSegmentContextResource{};
        ID3D12Resource* mDrawRecordResource{};
        ID3D12Resource* mVisibleInstanceIndexResource{};
        ID3D12Resource* mIndirectArgumentResource{};
        std::uint32_t mInstanceContextSrvIndex{ 0xffffffffu };
        std::uint32_t mSegmentContextSrvIndex{ 0xffffffffu };
        std::uint32_t mDrawRecordSrvIndex{ 0xffffffffu };
        std::uint32_t mVisibleInstanceIndexSrvIndex{ 0xffffffffu };
        std::uint32_t mDrawRecordCount{};
        std::uint32_t mInstanceContextCount{};
        std::vector<EnvironmentGpuDrivenDrawBatch> mGBufferDrawBatches{};
        std::array<std::vector<EnvironmentGpuDrivenDrawBatch>, ShadowCascadeMaxCount> mShadowDrawBatches{};
        bool mEnabled{};
    };
}
