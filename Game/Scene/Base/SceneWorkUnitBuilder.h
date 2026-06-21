#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Arche/World.h"
#include "Game/Scene/Base/SceneWorkUnit.h"

namespace Game {
    namespace Pipeline {
        class Scene;
        struct UnitPipelineAssignment;

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

        private:
            std::string ToEntityIdText(Arche::EntityID EntityId) const;
            void AddFailure(SceneWorkUnitBuildResult& BuildResult, const std::string& Message) const;
            void AppendRuntimePipelineAssignments(const Scene& TargetScene, std::vector<UnitPipelineAssignment>& InOutAssignments, SceneWorkUnitBuildResult& BuildResult) const;
            bool ValidateAssignment(const Scene& TargetScene, const UnitPipelineAssignment& Assignment, SceneWorkUnitBuildResult& BuildResult) const;
            bool TryCollectHierarchyEntityIds(const Arche::World& World, Arche::EntityID UnitEntityId, Arche::EntityID CurrentEntityId, const std::unordered_set<Arche::EntityID>& AssignedUnitEntityIds, std::unordered_set<Arche::EntityID>& LocalVisitedEntityIds, std::vector<Arche::EntityID>& OutEntityIds, SceneWorkUnitBuildResult& BuildResult) const;
            bool TryAddWorkUnitEntityOwners(const SceneWorkUnit& WorkUnit, std::unordered_map<Arche::EntityID, Arche::EntityID>& WorkUnitOwnerByEntityId, SceneWorkUnitBuildResult& BuildResult) const;
            SceneWorkUnit CreateSceneWorkUnit(const UnitPipelineAssignment& Assignment, std::vector<Arche::EntityID>&& EntityIds) const;
        };
    }
}
