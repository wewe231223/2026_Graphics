#pragma once
#include <array>
#include <cstddef>
#include <vector>
#include "Game/Base/RenderFrameData.h"

namespace Game {
    namespace Pipeline {
        class RenderGatherResult final {
        public:
            RenderGatherResult();
            ~RenderGatherResult();

            RenderGatherResult(const RenderGatherResult& Other);
            RenderGatherResult& operator=(const RenderGatherResult& Other);

            RenderGatherResult(RenderGatherResult&& Other) noexcept;
            RenderGatherResult& operator=(RenderGatherResult&& Other) noexcept;

        public:
            void Clear();
            bool Empty() const;

            void Append(const RenderGatherResult& Other);
            void Append(RenderGatherResult&& Other);

            std::vector<RFD::ModelContext>& GetModelContexts();
            const std::vector<RFD::ModelContext>& GetModelContexts() const;

            std::vector<RFD::BoundingBoxContext>& GetBoundingBoxContexts();
            const std::vector<RFD::BoundingBoxContext>& GetBoundingBoxContexts() const;

            std::vector<RFD::DebugGeometryContext>& GetDebugGeometryContexts();
            const std::vector<RFD::DebugGeometryContext>& GetDebugGeometryContexts() const;

            std::vector<RFD::TerrainPatchContext>& GetTerrainPatchContexts();
            const std::vector<RFD::TerrainPatchContext>& GetTerrainPatchContexts() const;

            bool HasTerrainUploadFuture() const;
            void SetTerrainUploadFuture(const Interface::Future& TerrainUploadFuture);
            const Interface::Future& GetTerrainUploadFuture() const;

            std::vector<RFD::DrawRecord>& GetDrawRecords();
            const std::vector<RFD::DrawRecord>& GetDrawRecords() const;

            std::vector<SimpleMath::Matrix>& GetBonePalette();
            const std::vector<SimpleMath::Matrix>& GetBonePalette() const;

            std::vector<RFD::EnvironmentInstanceContext>& GetEnvironmentInstanceContexts();
            const std::vector<RFD::EnvironmentInstanceContext>& GetEnvironmentInstanceContexts() const;

            std::vector<RFD::EnvironmentSegmentContext>& GetEnvironmentSegmentContexts();
            const std::vector<RFD::EnvironmentSegmentContext>& GetEnvironmentSegmentContexts() const;

            std::vector<RFD::EnvironmentDrawRecord>& GetEnvironmentDrawRecords();
            const std::vector<RFD::EnvironmentDrawRecord>& GetEnvironmentDrawRecords() const;

            std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& GetShadowRenderContexts();
            const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& GetShadowRenderContexts() const;

        private:
            std::uint32_t AddIndexOffset(std::uint32_t Index, std::size_t Offset) const;
            RFD::ModelContext BuildAdjustedModelContext(RFD::ModelContext ModelContext, std::size_t ModelContextOffset, std::size_t BonePaletteOffset) const;
            RFD::DrawRecord BuildAdjustedDrawRecord(RFD::DrawRecord DrawRecord, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset) const;
            RFD::EnvironmentDrawRecord BuildAdjustedEnvironmentDrawRecord(RFD::EnvironmentDrawRecord DrawRecord, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset) const;
            bool AreShadowRenderContextsEmpty(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& ShadowRenderContexts) const;

            void AppendModelContexts(const std::vector<RFD::ModelContext>& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts) const;
            void AppendModelContexts(std::vector<RFD::ModelContext>&& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts) const;
            void AppendDrawRecords(const std::vector<RFD::DrawRecord>& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords) const;
            void AppendDrawRecords(std::vector<RFD::DrawRecord>&& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords) const;
            void AppendEnvironmentDrawRecords(const std::vector<RFD::EnvironmentDrawRecord>& SourceDrawRecords, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::vector<RFD::EnvironmentDrawRecord>& OutDrawRecords) const;
            void AppendEnvironmentDrawRecords(std::vector<RFD::EnvironmentDrawRecord>&& SourceDrawRecords, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::vector<RFD::EnvironmentDrawRecord>& OutDrawRecords) const;

            void AppendShadowRenderContexts(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& OtherShadowRenderContexts, std::size_t BonePaletteOffset, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset);
            void AppendShadowRenderContexts(std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>&& OtherShadowRenderContexts, std::size_t BonePaletteOffset, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset);

        private:
            std::vector<RFD::ModelContext> mModelContexts{};
            std::vector<RFD::BoundingBoxContext> mBoundingBoxContexts{};
            std::vector<RFD::DebugGeometryContext> mDebugGeometryContexts{};
            std::vector<RFD::TerrainPatchContext> mTerrainPatchContexts{};
            Interface::Future mTerrainUploadFuture{};
            bool mHasTerrainUploadFuture{};
            std::vector<RFD::DrawRecord> mDrawRecords{};
            std::vector<SimpleMath::Matrix> mBonePalette{};
            std::vector<RFD::EnvironmentInstanceContext> mEnvironmentInstanceContexts{};
            std::vector<RFD::EnvironmentSegmentContext> mEnvironmentSegmentContexts{};
            std::vector<RFD::EnvironmentDrawRecord> mEnvironmentDrawRecords{};
            std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount> mShadowRenderContexts{};
        };
    }
}
