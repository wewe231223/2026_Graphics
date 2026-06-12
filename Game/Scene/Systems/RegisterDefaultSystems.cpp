#include "RegisterDefaultSystems.h"
#include <memory>
#include "Game/Scene/Base/SystemRegistry.h"
#include "Game/Scene/Systems/AnimateSystem.h"
#include "Game/Scene/Systems/AnimationGraphSystem.h"
#include "Game/Scene/Systems/FootIKSystem.h"
#include "Game/Scene/Systems/SkinnedRenderSystem.h"
#include "Game/Scene/Systems/StaticRenderSystem.h"
#include "Game/Scene/Systems/TerrainRenderSystem.h"
#include "Game/Scene/Systems/TransformWorldSystem.h"

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
