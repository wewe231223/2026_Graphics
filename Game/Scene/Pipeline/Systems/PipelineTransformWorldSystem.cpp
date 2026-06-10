#include "PipelineTransformWorldSystem.h"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Pipeline/PipelineContext.h"

namespace Game {
    namespace Pipeline {
        namespace {
            SimpleMath::Matrix BuildLocalWorldMatrix(const Transform& TransformComponent) {
                const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
                return TransformComponent.nodeToParent * TrsMatrix;
            }

            bool TryResolveWorldMatrix(PipelineContext& Ctx, Arche::EntityID EntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, SimpleMath::Matrix& OutWorldMatrix) {
                const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CachedWorldMatrixIter{ InOutWorldMatrices.find(EntityId) };
                if (CachedWorldMatrixIter != InOutWorldMatrices.end()) {
                    OutWorldMatrix = CachedWorldMatrixIter->second;
                    return true;
                }

                std::vector<Arche::EntityID> EntityPath{};
                Arche::EntityID CurrentEntityId{ EntityId };
                SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };

                while (CurrentEntityId != Arche::NullEntityID && Ctx.ContainsEntity(CurrentEntityId) == true) {
                    const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CurrentCachedWorldMatrixIter{ InOutWorldMatrices.find(CurrentEntityId) };
                    if (CurrentCachedWorldMatrixIter != InOutWorldMatrices.end()) {
                        ParentWorldMatrix = CurrentCachedWorldMatrixIter->second;
                        break;
                    }

                    const Transform* TransformComponent{ Ctx.ReadComponent<Transform>(CurrentEntityId) };
                    const EntityHierarchy* HierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(CurrentEntityId) };
                    if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                        return false;
                    }

                    EntityPath.push_back(CurrentEntityId);
                    CurrentEntityId = HierarchyComponent->parent;
                }

                for (std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
                    const Arche::EntityID CurrentPathEntityId{ *EntityPathIter };
                    const Transform* TransformComponent{ Ctx.ReadComponent<Transform>(CurrentPathEntityId) };
                    if (TransformComponent == nullptr) {
                        return false;
                    }

                    const SimpleMath::Matrix LocalWorldMatrix{ BuildLocalWorldMatrix(*TransformComponent) };
                    const SimpleMath::Matrix CurrentWorldMatrix{ LocalWorldMatrix * ParentWorldMatrix };
                    InOutWorldMatrices[CurrentPathEntityId] = CurrentWorldMatrix;
                    ParentWorldMatrix = CurrentWorldMatrix;
                }

                OutWorldMatrix = ParentWorldMatrix;
                return true;
            }
        }

        PipelineTransformWorldSystem::PipelineTransformWorldSystem() {
        }

        PipelineTransformWorldSystem::~PipelineTransformWorldSystem() {
        }

        PipelineTransformWorldSystem::PipelineTransformWorldSystem(const PipelineTransformWorldSystem& Other) {
            (void)Other;
        }

        PipelineTransformWorldSystem& PipelineTransformWorldSystem::operator=(const PipelineTransformWorldSystem& Other) {
            (void)Other;
            return *this;
        }

        PipelineTransformWorldSystem::PipelineTransformWorldSystem(PipelineTransformWorldSystem&& Other) noexcept {
            (void)Other;
        }

        PipelineTransformWorldSystem& PipelineTransformWorldSystem::operator=(PipelineTransformWorldSystem&& Other) noexcept {
            (void)Other;
            return *this;
        }

        const std::string& PipelineTransformWorldSystem::Name() const {
            static const std::string NameText{ "TransformWorldSystem" };
            return NameText;
        }

        void PipelineTransformWorldSystem::Execute(PipelineContext& Ctx, float Dt) {
            (void)Dt;

            std::unordered_map<Arche::EntityID, SimpleMath::Matrix> WorldMatrices{};
            Ctx.ForEach<Transform, EntityHierarchy>([&](Arche::EntityID EntityId, Transform& TransformComponent, EntityHierarchy& HierarchyComponent) {
                (void)HierarchyComponent;

                SimpleMath::Matrix WorldMatrix{};
                const bool IsWorldMatrixResolved{ TryResolveWorldMatrix(Ctx, EntityId, WorldMatrices, WorldMatrix) };
                if (IsWorldMatrixResolved == false) {
                    return;
                }

                TransformComponent.worldMatrix = WorldMatrix;
            });
        }
    }
}
