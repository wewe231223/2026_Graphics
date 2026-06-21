#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "DirectXTK12/SimpleMath.h"

#include "RenderContract/Environment/EnvironmentDrawRecord.h"
#include "RenderContract/Environment/EnvironmentInstanceContext.h"
#include "RenderContract/Environment/EnvironmentSegmentContext.h"
#include "RenderContract/Frame/FrameGlobals.h"
#include "RenderContract/Frame/ShadowMappingParameter.h"
#include "RenderContract/Future/Future.h"
#include "RenderContract/Geometry/BoundingBoxContext.h"
#include "RenderContract/Geometry/DebugGeometryContext.h"
#include "RenderContract/Model/DrawRecord.h"
#include "RenderContract/Model/ModelContext.h"
#include "RenderContract/Shadow/ShadowRenderContext.h"
#include "RenderContract/Terrain/TerrainPatchContext.h"

namespace RenderContract {
    class RenderGatherResult final {
    public:
        RenderGatherResult() = default;
        ~RenderGatherResult() = default;
        RenderGatherResult(const RenderGatherResult& Other) = default;
        RenderGatherResult& operator=(const RenderGatherResult& Other) = default;
        RenderGatherResult(RenderGatherResult&& Other) noexcept = default;
        RenderGatherResult& operator=(RenderGatherResult&& Other) noexcept = default;

    public:
        void Clear();
        bool Empty() const;

        void Append(const RenderGatherResult& Other);
        void Append(RenderGatherResult&& Other);

        std::vector<ModelContext>& GetModelContexts();
        const std::vector<ModelContext>& GetModelContexts() const;

        std::vector<BoundingBoxContext>& GetBoundingBoxContexts();
        const std::vector<BoundingBoxContext>& GetBoundingBoxContexts() const;

        std::vector<DebugGeometryContext>& GetDebugGeometryContexts();
        const std::vector<DebugGeometryContext>& GetDebugGeometryContexts() const;

        std::vector<TerrainPatchContext>& GetTerrainPatchContexts();
        const std::vector<TerrainPatchContext>& GetTerrainPatchContexts() const;

        bool HasTerrainUploadFuture() const;
        void SetTerrainUploadFuture(const Future& TerrainUploadFuture);
        const Future& GetTerrainUploadFuture() const;

        std::vector<DrawRecord>& GetDrawRecords();
        const std::vector<DrawRecord>& GetDrawRecords() const;

        std::vector<DirectX::SimpleMath::Matrix>& GetBonePalette();
        const std::vector<DirectX::SimpleMath::Matrix>& GetBonePalette() const;

        std::vector<EnvironmentInstanceContext>& GetEnvironmentInstanceContexts();
        const std::vector<EnvironmentInstanceContext>& GetEnvironmentInstanceContexts() const;

        std::vector<EnvironmentSegmentContext>& GetEnvironmentSegmentContexts();
        const std::vector<EnvironmentSegmentContext>& GetEnvironmentSegmentContexts() const;

        std::vector<EnvironmentDrawRecord>& GetEnvironmentDrawRecords();
        const std::vector<EnvironmentDrawRecord>& GetEnvironmentDrawRecords() const;

        std::array<ShadowRenderContext, ShadowCascadeMaxCount>& GetShadowRenderContexts();
        const std::array<ShadowRenderContext, ShadowCascadeMaxCount>& GetShadowRenderContexts() const;

    private:
        static std::uint32_t AddIndexOffset(std::uint32_t Index, std::size_t Offset);
        static ModelContext BuildAdjustedModelContext(ModelContext ModelContextValue, std::size_t ModelContextOffset, std::size_t BonePaletteOffset);
        static DrawRecord BuildAdjustedDrawRecord(DrawRecord DrawRecordValue, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset);
        static EnvironmentDrawRecord BuildAdjustedEnvironmentDrawRecord(EnvironmentDrawRecord DrawRecordValue, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset);
        static bool AreShadowRenderContextsEmpty(const std::array<ShadowRenderContext, ShadowCascadeMaxCount>& ShadowRenderContexts);

        static void AppendModelContexts(const std::vector<ModelContext>& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<ModelContext>& OutModelContexts);
        static void AppendModelContexts(std::vector<ModelContext>&& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<ModelContext>& OutModelContexts);
        static void AppendDrawRecords(const std::vector<DrawRecord>& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<DrawRecord>& OutDrawRecords);
        static void AppendDrawRecords(std::vector<DrawRecord>&& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<DrawRecord>& OutDrawRecords);
        static void AppendEnvironmentDrawRecords(const std::vector<EnvironmentDrawRecord>& SourceDrawRecords, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::vector<EnvironmentDrawRecord>& OutDrawRecords);
        static void AppendEnvironmentDrawRecords(std::vector<EnvironmentDrawRecord>&& SourceDrawRecords, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::vector<EnvironmentDrawRecord>& OutDrawRecords);

        void AppendShadowRenderContexts(const std::array<ShadowRenderContext, ShadowCascadeMaxCount>& OtherShadowRenderContexts, std::size_t BonePaletteOffset, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset);
        void AppendShadowRenderContexts(std::array<ShadowRenderContext, ShadowCascadeMaxCount>&& OtherShadowRenderContexts, std::size_t BonePaletteOffset, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset);

    private:
        std::vector<ModelContext> mModelContexts{};
        std::vector<BoundingBoxContext> mBoundingBoxContexts{};
        std::vector<DebugGeometryContext> mDebugGeometryContexts{};
        std::vector<TerrainPatchContext> mTerrainPatchContexts{};
        Future mTerrainUploadFuture{};
        bool mHasTerrainUploadFuture{};
        std::vector<DrawRecord> mDrawRecords{};
        std::vector<DirectX::SimpleMath::Matrix> mBonePalette{};
        std::vector<EnvironmentInstanceContext> mEnvironmentInstanceContexts{};
        std::vector<EnvironmentSegmentContext> mEnvironmentSegmentContexts{};
        std::vector<EnvironmentDrawRecord> mEnvironmentDrawRecords{};
        std::array<ShadowRenderContext, ShadowCascadeMaxCount> mShadowRenderContexts{};
    };
}
