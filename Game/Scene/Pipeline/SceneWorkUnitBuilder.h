#pragma once
#include <string>
#include <vector>
#include "Game/Scene/Pipeline/SceneWorkUnit.h"

namespace Game {
    namespace Pipeline {
        class Scene;

        struct SceneWorkUnitBuildResult final {
        public:
            SceneWorkUnitBuildResult();
            ~SceneWorkUnitBuildResult();

            SceneWorkUnitBuildResult(const SceneWorkUnitBuildResult& Other);
            SceneWorkUnitBuildResult& operator=(const SceneWorkUnitBuildResult& Other);

            SceneWorkUnitBuildResult(SceneWorkUnitBuildResult&& Other) noexcept;
            SceneWorkUnitBuildResult& operator=(SceneWorkUnitBuildResult&& Other) noexcept;

        public:
            bool IsSuccess{ true };
            std::vector<std::string> UndecidedItems{};
        };

        class SceneWorkUnitBuilder final {
        public:
            SceneWorkUnitBuilder();
            ~SceneWorkUnitBuilder();

            SceneWorkUnitBuilder(const SceneWorkUnitBuilder& Other);
            SceneWorkUnitBuilder& operator=(const SceneWorkUnitBuilder& Other);

            SceneWorkUnitBuilder(SceneWorkUnitBuilder&& Other) noexcept;
            SceneWorkUnitBuilder& operator=(SceneWorkUnitBuilder&& Other) noexcept;

        public:
            SceneWorkUnitBuildResult Build(const Scene& TargetScene, std::vector<SceneWorkUnit>& OutWorkUnits) const;
        };
    }
}
