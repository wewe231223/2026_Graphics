#pragma once

#include <cstdint>

#include <d3d12.h>

#include "RenderContract/Common.h"
#include "RenderContract/Future/Future.h"

namespace RenderContract {
    struct RenderFrameData;
    struct ShadowRenderContext;

    class IEnvironmentRenderPipelineProvider abstract {
    public:
        virtual ~IEnvironmentRenderPipelineProvider() = default;

    public:
        virtual const IPipeline* ResolveEnvironmentObjectPipeline() = 0;
        virtual const IPipeline* ResolveEnvironmentObjectDepthPipeline() = 0;
        virtual const IPipeline* ResolveEnvironmentBillboardDepthPipeline() = 0;
    };

    struct EnvironmentGBufferRenderCommandContext final {
    public:
        ID3D12GraphicsCommandList* mCommandList{};
        IEnvironmentRenderPipelineProvider* mPipelineProvider{};
        const RenderFrameData* mRenderFrameData{};
        std::uint32_t mFrameGlobalsSrvIndex{ 0xffffffffu };
        std::uint32_t mEnvironmentInstanceContextSrvIndex{ 0xffffffffu };
        std::uint32_t mEnvironmentSegmentContextSrvIndex{ 0xffffffffu };
        std::uint32_t mEnvironmentDrawRecordSrvIndex{ 0xffffffffu };
        std::uint32_t mMaterialSrvIndex{ 0xffffffffu };
        std::uint32_t mMaterialTextureTableSrvIndex{ 0xffffffffu };
    };

    struct EnvironmentShadowDepthRenderCommandContext final {
    public:
        ID3D12GraphicsCommandList* mCommandList{};
        ID3D12GraphicsCommandList9* mDynamicDepthBiasCommandList{};
        IEnvironmentRenderPipelineProvider* mPipelineProvider{};
        const ShadowRenderContext* mShadowRenderContext{};
        std::uint32_t mShadowFrameGlobalsIndex{};
        std::uint32_t mFrameGlobalsSrvIndex{ 0xffffffffu };
        std::uint32_t mEnvironmentInstanceContextSrvIndex{ 0xffffffffu };
        std::uint32_t mEnvironmentSegmentContextSrvIndex{ 0xffffffffu };
        std::uint32_t mEnvironmentDrawRecordSrvIndex{ 0xffffffffu };
        std::uint32_t mMaterialSrvIndex{ 0xffffffffu };
        std::uint32_t mMaterialTextureTableSrvIndex{ 0xffffffffu };
        float mRasterDepthBias{};
        float mRasterDepthBiasClamp{};
        float mRasterSlopeScaledDepthBias{};
    };

    class IEnvironmentRenderRuntime abstract {
    public:
        virtual ~IEnvironmentRenderRuntime() = default;

    public:
        virtual void RecordGBuffer(const EnvironmentGBufferRenderCommandContext& Context) = 0;
        virtual void RecordShadowDepth(const EnvironmentShadowDepthRenderCommandContext& Context) = 0;
        virtual Future GetEnvironmentGpuFuture() const = 0;
    };
}
