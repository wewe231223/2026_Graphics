#include "RegisterDefaultPipelineSystems.h"
#include <memory>
#include "Game/Scene/Pipeline/PipelineSystemRegistry.h"
#include "Game/Scene/Pipeline/Systems/PipelineAnimateSystem.h"
#include "Game/Scene/Pipeline/Systems/PipelineAnimationGraphSystem.h"
#include "Game/Scene/Pipeline/Systems/PipelineFootIKSystem.h"
#include "Game/Scene/Pipeline/Systems/PipelineSkinnedRenderSystem.h"
#include "Game/Scene/Pipeline/Systems/PipelineStaticRenderSystem.h"
#include "Game/Scene/Pipeline/Systems/PipelineTerrainRenderSystem.h"
#include "Game/Scene/Pipeline/Systems/PipelineTransformWorldSystem.h"

namespace Game {
    namespace Pipeline {
        void RegisterDefaultPipelineSystems(PipelineSystemRegistry& Registry) {
            Registry.RegisterFactory("AnimationGraphSystem", []() { return std::make_unique<PipelineAnimationGraphSystem>(); });
            Registry.RegisterFactory("AnimateSystem", []() { return std::make_unique<PipelineAnimateSystem>(); });
            Registry.RegisterFactory("FootIKSystem", []() { return std::make_unique<PipelineFootIKSystem>(); });
            Registry.RegisterFactory("TransformWorldSystem", []() { return std::make_unique<PipelineTransformWorldSystem>(); });
            Registry.RegisterFactory("StaticRenderSystem", []() { return std::make_unique<PipelineStaticRenderSystem>(); });
            Registry.RegisterFactory("TerrainRenderSystem", []() { return std::make_unique<PipelineTerrainRenderSystem>(); });
            Registry.RegisterFactory("SkinnedRenderSystem", []() { return std::make_unique<PipelineSkinnedRenderSystem>(); });
        }
    }
}
