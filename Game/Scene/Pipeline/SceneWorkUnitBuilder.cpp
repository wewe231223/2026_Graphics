#include "SceneWorkUnitBuilder.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Pipeline/PipelineScene.h"

namespace Game {
    namespace Pipeline {
        namespace {
            std::string ToEntityIdText(Arche::EntityID EntityId);
            void AddFailure(SceneWorkUnitBuildResult& BuildResult, const std::string& Message);
            bool ValidateAssignment(const Scene& TargetScene, const UnitPipelineAssignment& Assignment, SceneWorkUnitBuildResult& BuildResult);
            bool TryCollectHierarchyEntityIds(const Arche::World& World, Arche::EntityID UnitEntityId, Arche::EntityID CurrentEntityId, const std::unordered_set<Arche::EntityID>& AssignedUnitEntityIds, std::unordered_set<Arche::EntityID>& LocalVisitedEntityIds, std::vector<Arche::EntityID>& OutEntityIds, SceneWorkUnitBuildResult& BuildResult);
            bool TryAddWorkUnitEntityOwners(const SceneWorkUnit& WorkUnit, std::unordered_map<Arche::EntityID, Arche::EntityID>& WorkUnitOwnerByEntityId, SceneWorkUnitBuildResult& BuildResult);
            SceneWorkUnit CreateSceneWorkUnit(const UnitPipelineAssignment& Assignment, std::vector<Arche::EntityID>&& EntityIds);

            std::string ToEntityIdText(Arche::EntityID EntityId) {
                return std::to_string(EntityId.index) + ":" + std::to_string(EntityId.generation);
            }

            void AddFailure(SceneWorkUnitBuildResult& BuildResult, const std::string& Message) {
                BuildResult.IsSuccess = false;
                BuildResult.UndecidedItems.push_back(Message);
            }

            bool ValidateAssignment(const Scene& TargetScene, const UnitPipelineAssignment& Assignment, SceneWorkUnitBuildResult& BuildResult) {
                bool IsValid{ true };
                if (Assignment.mUnitEntityId == Arche::NullEntityID) {
                    AddFailure(BuildResult, "Unit EntityId is NullEntityID.");
                    IsValid = false;
                }

                if (Assignment.mPipelineId == InvalidPipelineId) {
                    AddFailure(BuildResult, std::string{ "PipelineId is invalid: " } + ToEntityIdText(Assignment.mUnitEntityId));
                    IsValid = false;
                }

                const PipelineDefinition* PipelineDefinitionValue{ TargetScene.FindPipelineDefinition(Assignment.mPipelineName) };
                if (PipelineDefinitionValue == nullptr) {
                    AddFailure(BuildResult, std::string{ "Pipeline definition is missing: " } + Assignment.mPipelineName);
                    IsValid = false;
                }
                else if (PipelineDefinitionValue->GetPipelineId() != Assignment.mPipelineId) {
                    AddFailure(BuildResult, std::string{ "PipelineId does not match Pipeline definition: " } + Assignment.mPipelineName);
                    IsValid = false;
                }

                if (Assignment.mUnitEntityId != Arche::NullEntityID) {
                    const EntityHierarchy* HierarchyComponent{ TargetScene.GetWorld().GetComponent<EntityHierarchy>(Assignment.mUnitEntityId) };
                    if (HierarchyComponent == nullptr) {
                        AddFailure(BuildResult, std::string{ "Unit EntityHierarchy is missing: " } + ToEntityIdText(Assignment.mUnitEntityId));
                        IsValid = false;
                    }
                }

                return IsValid;
            }

            bool TryCollectHierarchyEntityIds(const Arche::World& World, Arche::EntityID UnitEntityId, Arche::EntityID CurrentEntityId, const std::unordered_set<Arche::EntityID>& AssignedUnitEntityIds, std::unordered_set<Arche::EntityID>& LocalVisitedEntityIds, std::vector<Arche::EntityID>& OutEntityIds, SceneWorkUnitBuildResult& BuildResult) {
                if (CurrentEntityId == Arche::NullEntityID) {
                    return true;
                }

                const bool IsVisitedInserted{ LocalVisitedEntityIds.insert(CurrentEntityId).second };
                if (IsVisitedInserted == false) {
                    AddFailure(BuildResult, std::string{ "Entity hierarchy cycle detected: " } + ToEntityIdText(CurrentEntityId));
                    return false;
                }

                if (CurrentEntityId != UnitEntityId && AssignedUnitEntityIds.find(CurrentEntityId) != AssignedUnitEntityIds.end()) {
                    AddFailure(BuildResult, std::string{ "Child Entity has its own Unit Pipeline assignment: " } + ToEntityIdText(CurrentEntityId));
                    return false;
                }

                const EntityHierarchy* HierarchyComponent{ World.GetComponent<EntityHierarchy>(CurrentEntityId) };
                if (HierarchyComponent == nullptr) {
                    AddFailure(BuildResult, std::string{ "EntityHierarchy is missing: " } + ToEntityIdText(CurrentEntityId));
                    return false;
                }

                OutEntityIds.push_back(CurrentEntityId);
                Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
                while (ChildEntityId != Arche::NullEntityID) {
                    const EntityHierarchy* ChildHierarchyComponent{ World.GetComponent<EntityHierarchy>(ChildEntityId) };
                    if (ChildHierarchyComponent == nullptr) {
                        AddFailure(BuildResult, std::string{ "Child EntityHierarchy is missing: " } + ToEntityIdText(ChildEntityId));
                        return false;
                    }

                    if (TryCollectHierarchyEntityIds(World, UnitEntityId, ChildEntityId, AssignedUnitEntityIds, LocalVisitedEntityIds, OutEntityIds, BuildResult) == false) {
                        return false;
                    }

                    ChildEntityId = ChildHierarchyComponent->nextSibling;
                }

                return true;
            }

            bool TryAddWorkUnitEntityOwners(const SceneWorkUnit& WorkUnit, std::unordered_map<Arche::EntityID, Arche::EntityID>& WorkUnitOwnerByEntityId, SceneWorkUnitBuildResult& BuildResult) {
                bool IsValid{ true };
                for (Arche::EntityID EntityId : WorkUnit.GetEntityIds()) {
                    const std::unordered_map<Arche::EntityID, Arche::EntityID>::const_iterator OwnerIter{ WorkUnitOwnerByEntityId.find(EntityId) };
                    if (OwnerIter != WorkUnitOwnerByEntityId.end()) {
                        AddFailure(BuildResult, std::string{ "Entity belongs to multiple WorkUnits: " } + ToEntityIdText(EntityId));
                        IsValid = false;
                        continue;
                    }
                }

                if (IsValid == false) {
                    return false;
                }

                for (Arche::EntityID EntityId : WorkUnit.GetEntityIds()) {
                    WorkUnitOwnerByEntityId.emplace(EntityId, WorkUnit.GetUnitEntityId());
                }

                return true;
            }

            SceneWorkUnit CreateSceneWorkUnit(const UnitPipelineAssignment& Assignment, std::vector<Arche::EntityID>&& EntityIds) {
                SceneWorkUnit WorkUnit{};
                WorkUnit.SetUnitEntityId(Assignment.mUnitEntityId);
                WorkUnit.SetPipelineId(Assignment.mPipelineId);
                WorkUnit.GetEntityIds() = std::move(EntityIds);
                WorkUnit.GetPipelineSystems().clear();
                WorkUnit.GetRenderGatherResult().Clear();
                return WorkUnit;
            }
        }

        SceneWorkUnitBuildResult::SceneWorkUnitBuildResult() = default;
        SceneWorkUnitBuildResult::~SceneWorkUnitBuildResult() = default;
        SceneWorkUnitBuildResult::SceneWorkUnitBuildResult(const SceneWorkUnitBuildResult& Other) = default;
        SceneWorkUnitBuildResult& SceneWorkUnitBuildResult::operator=(const SceneWorkUnitBuildResult& Other) = default;
        SceneWorkUnitBuildResult::SceneWorkUnitBuildResult(SceneWorkUnitBuildResult&& Other) noexcept = default;
        SceneWorkUnitBuildResult& SceneWorkUnitBuildResult::operator=(SceneWorkUnitBuildResult&& Other) noexcept = default;

        SceneWorkUnitBuilder::SceneWorkUnitBuilder() = default;
        SceneWorkUnitBuilder::~SceneWorkUnitBuilder() = default;
        SceneWorkUnitBuilder::SceneWorkUnitBuilder(const SceneWorkUnitBuilder& Other) = default;
        SceneWorkUnitBuilder& SceneWorkUnitBuilder::operator=(const SceneWorkUnitBuilder& Other) = default;
        SceneWorkUnitBuilder::SceneWorkUnitBuilder(SceneWorkUnitBuilder&& Other) noexcept = default;
        SceneWorkUnitBuilder& SceneWorkUnitBuilder::operator=(SceneWorkUnitBuilder&& Other) noexcept = default;

        SceneWorkUnitBuildResult SceneWorkUnitBuilder::Build(const Scene& TargetScene, std::vector<SceneWorkUnit>& OutWorkUnits) const {
            SceneWorkUnitBuildResult BuildResult{};
            std::vector<SceneWorkUnit> NewWorkUnits{};
            std::unordered_set<Arche::EntityID> AssignedUnitEntityIds{};
            std::unordered_map<Arche::EntityID, Arche::EntityID> WorkUnitOwnerByEntityId{};

            for (const UnitPipelineAssignment& Assignment : TargetScene.GetUnitPipelineAssignments()) {
                if (Assignment.mUnitEntityId == Arche::NullEntityID) {
                    AddFailure(BuildResult, "Unit EntityId is NullEntityID.");
                    continue;
                }

                const bool IsUnitEntityIdInserted{ AssignedUnitEntityIds.insert(Assignment.mUnitEntityId).second };
                if (IsUnitEntityIdInserted == false) {
                    AddFailure(BuildResult, std::string{ "Duplicate Unit Entity pipeline assignment: " } + ToEntityIdText(Assignment.mUnitEntityId));
                }
            }

            for (const UnitPipelineAssignment& Assignment : TargetScene.GetUnitPipelineAssignments()) {
                if (ValidateAssignment(TargetScene, Assignment, BuildResult) == false) {
                    continue;
                }

                std::vector<Arche::EntityID> EntityIds{};
                std::unordered_set<Arche::EntityID> LocalVisitedEntityIds{};
                if (TryCollectHierarchyEntityIds(TargetScene.GetWorld(), Assignment.mUnitEntityId, Assignment.mUnitEntityId, AssignedUnitEntityIds, LocalVisitedEntityIds, EntityIds, BuildResult) == false) {
                    continue;
                }

                SceneWorkUnit WorkUnit{ CreateSceneWorkUnit(Assignment, std::move(EntityIds)) };
                if (TryAddWorkUnitEntityOwners(WorkUnit, WorkUnitOwnerByEntityId, BuildResult) == false) {
                    continue;
                }

                NewWorkUnits.push_back(std::move(WorkUnit));
            }

            if (BuildResult.IsSuccess == true) {
                OutWorkUnits = std::move(NewWorkUnits);
            }

            return BuildResult;
        }
    }
}
