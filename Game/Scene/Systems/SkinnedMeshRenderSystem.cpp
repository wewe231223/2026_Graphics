#include "SkinnedMeshRenderSystem.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Game/Base/Common.h"
#include "Game/Model/Model.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }

    bool TryResolveWorldMatrix(Arche::World& World, Arche::EntityID EntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, SimpleMath::Matrix& OutWorldMatrix) {
        const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CachedWorldMatrixIter{ InOutWorldMatrices.find(EntityId) };
        if (CachedWorldMatrixIter != InOutWorldMatrices.end()) {
            OutWorldMatrix = CachedWorldMatrixIter->second;
            return true;
        }

        std::vector<Arche::EntityID> EntityPath{};
        Arche::EntityID CurrentEntityId{ EntityId };
        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };

        while (CurrentEntityId != Arche::NullEntityID) {
            const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CurrentCachedWorldMatrixIter{ InOutWorldMatrices.find(CurrentEntityId) };
            if (CurrentCachedWorldMatrixIter != InOutWorldMatrices.end()) {
                ParentWorldMatrix = CurrentCachedWorldMatrixIter->second;
                break;
            }

            const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(CurrentEntityId) };
            const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                return false;
            }

            EntityPath.push_back(CurrentEntityId);
            CurrentEntityId = HierarchyComponent->parent;
        }

        for (std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
            const Arche::EntityID CurrentPathEntityId{ *EntityPathIter };
            const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(CurrentPathEntityId) };
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

    std::uint32_t ResolveBoneMatrixCount(const Game::Model& ModelData) {
        std::uint32_t BoneMatrixCount{ 0 };
        const std::vector<Game::ModelNode>& Nodes{ ModelData.GetNodes() };

        for (const Game::ModelNode& Node : Nodes) {
            const std::vector<Game::ModelBoneInfo>& BoneInfos{ Node.GetBoneInfos() };
            for (const Game::ModelBoneInfo& BoneInfo : BoneInfos) {
                const std::uint32_t RequiredCount{ BoneInfo.JointArrayIndex + 1u };
                if (RequiredCount > BoneMatrixCount) {
                    BoneMatrixCount = RequiredCount;
                }
            }
        }

        return BoneMatrixCount;
    }

    void GatherBoneMatricesRecursive(Arche::World& World, Arche::EntityID EntityId, Game::Model* ModelData, const SimpleMath::Matrix& MeshWorldInverseMatrix, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, std::vector<SimpleMath::Matrix>& InOutBoneMatrices) {
        if (EntityId == Arche::NullEntityID || ModelData == nullptr) {
            return;
        }

        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr) {
            return;
        }

        const Game::Bone* BoneComponent{ World.GetComponent<Game::Bone>(EntityId) };
        if (BoneComponent != nullptr && BoneComponent->model == ModelData) {
            SimpleMath::Matrix BoneWorldMatrix{};
            const bool IsBoneWorldMatrixResolved{ TryResolveWorldMatrix(World, EntityId, InOutWorldMatrices, BoneWorldMatrix) };
            if (IsBoneWorldMatrixResolved == true) {
                const std::vector<Game::ModelNode>& Nodes{ ModelData->GetNodes() };
                if (BoneComponent->nodeIndex < Nodes.size()) {
                    const Game::ModelNode& Node{ Nodes[BoneComponent->nodeIndex] };
                    const std::vector<Game::ModelBoneInfo>& BoneInfos{ Node.GetBoneInfos() };

                    for (const Game::ModelBoneInfo& BoneInfo : BoneInfos) {
                        if (BoneInfo.JointArrayIndex >= InOutBoneMatrices.size()) {
                            continue;
                        }

                        InOutBoneMatrices[BoneInfo.JointArrayIndex] = BoneInfo.InverseBindMatrix * BoneWorldMatrix * MeshWorldInverseMatrix;
                    }
                }
            }
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            const Game::EntityHierarchy* ChildHierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            GatherBoneMatricesRecursive(World, ChildEntityId, ModelData, MeshWorldInverseMatrix, InOutWorldMatrices, InOutBoneMatrices);
            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }
    }
}

namespace Game {
    const std::string& SkinnedMeshRenderSystem::Name() const {
        return mName;
    }

    Phase SkinnedMeshRenderSystem::GetPhase() const {
        return Phase::Render;
    }

    std::span<const ComponentAccess> SkinnedMeshRenderSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 4> Accesses{ { { typeid(BoneSkinReference), Access::Read }, { typeid(SkinnedMeshRenderer), Access::Read }, { typeid(Transform), Access::Read }, { typeid(EntityHierarchy), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> SkinnedMeshRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 1> Accesses{ { { typeid(RFD::RenderFrameData), Access::Write } } };
        return Accesses;
    }

    void SkinnedMeshRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        for (auto [BoneSkinReferenceComponent, SkinnedMeshRendererComponent, TransformComponent, EntityHierarchyComponent] : World.Query<BoneSkinReference, SkinnedMeshRenderer, Transform, EntityHierarchy>()) {
            (void)TransformComponent;
            (void)EntityHierarchyComponent;

            if (SkinnedMeshRendererComponent.active == false || SkinnedMeshRendererComponent.model == nullptr) {
                continue;
            }

            if (BoneSkinReferenceComponent.boneRootEntityId == Arche::NullEntityID) {
                continue;
            }

            const Arche::EntityID EntityId{ EntityHierarchyComponent.self };
            SimpleMath::Matrix MeshWorldMatrix{};
            const bool IsMeshWorldMatrixResolved{ TryResolveWorldMatrix(World, EntityId, Ctx.WorldMatrices, MeshWorldMatrix) };
            if (IsMeshWorldMatrixResolved == false) {
                continue;
            }

            SimpleMath::Matrix MeshWorldInverseMatrix{ MeshWorldMatrix };
            MeshWorldInverseMatrix = MeshWorldInverseMatrix.Invert();

            const std::uint32_t BoneMatrixCount{ ResolveBoneMatrixCount(*SkinnedMeshRendererComponent.model) };
            if (BoneMatrixCount == 0) {
                continue;
            }

            std::vector<SimpleMath::Matrix> BoneMatrices{};
            BoneMatrices.resize(static_cast<std::size_t>(BoneMatrixCount), SimpleMath::Matrix::Identity);

            GatherBoneMatricesRecursive(World, BoneSkinReferenceComponent.boneRootEntityId, SkinnedMeshRendererComponent.model, MeshWorldInverseMatrix, Ctx.WorldMatrices, BoneMatrices);
        }
    }
}
