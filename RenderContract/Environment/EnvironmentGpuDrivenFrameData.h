#pragma once

#include <cstdint>

#include <d3d12.h>

#include "RenderContract/Future/Future.h"

namespace RenderContract {
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
        bool mEnabled{};
    };
}
