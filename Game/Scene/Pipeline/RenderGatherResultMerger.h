#pragma once
#include <span>
#include "Game/Base/RenderFrameData.h"
#include "Game/Scene/Pipeline/SceneWorkUnit.h"

namespace Game {
    namespace Pipeline {
        class RenderGatherResultMerger final {
        public:
            RenderGatherResultMerger();
            ~RenderGatherResultMerger();

            RenderGatherResultMerger(const RenderGatherResultMerger& Other);
            RenderGatherResultMerger& operator=(const RenderGatherResultMerger& Other);

            RenderGatherResultMerger(RenderGatherResultMerger&& Other) noexcept;
            RenderGatherResultMerger& operator=(RenderGatherResultMerger&& Other) noexcept;

        public:
            static void Merge(std::span<const SceneWorkUnit> WorkUnits, RFD::RenderFrameData& OutRenderData);
        };
    }
}
