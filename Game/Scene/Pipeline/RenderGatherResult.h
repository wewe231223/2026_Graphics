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

            std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& GetShadowRenderContexts();
            const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& GetShadowRenderContexts() const;

        private:
            void AppendShadowRenderContexts(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& OtherShadowRenderContexts, std::size_t BonePaletteOffset);
            void AppendShadowRenderContexts(std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>&& OtherShadowRenderContexts, std::size_t BonePaletteOffset);

        private:
            std::vector<RFD::ModelContext> mModelContexts{};
            std::vector<RFD::BoundingBoxContext> mBoundingBoxContexts{};
            std::vector<RFD::DebugGeometryContext> mDebugGeometryContexts{};
            std::vector<RFD::TerrainPatchContext> mTerrainPatchContexts{};
            Interface::Future mTerrainUploadFuture{};
            bool mHasTerrainUploadFuture{};
            std::vector<RFD::DrawRecord> mDrawRecords{};
            std::vector<SimpleMath::Matrix> mBonePalette{};
            std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount> mShadowRenderContexts{};
        };
    }
}
