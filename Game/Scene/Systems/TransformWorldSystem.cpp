#include "TransformWorldSystem.h"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Base/Context.h"

namespace Game {
    namespace Pipeline {
        namespace {
            struct ResolvedWorldMatrix final {
            public:
                SimpleMath::Matrix mMatrix{ SimpleMath::Matrix::Identity };
                bool mChanged{};
            };

            bool AreVector3Equal(const SimpleMath::Vector3& Left, const SimpleMath::Vector3& Right) {
                return Left.x == Right.x && Left.y == Right.y && Left.z == Right.z;
            }

            bool AreQuaternionEqual(const SimpleMath::Quaternion& Left, const SimpleMath::Quaternion& Right) {
                return Left.x == Right.x && Left.y == Right.y && Left.z == Right.z && Left.w == Right.w;
            }

            bool AreMatricesEqual(const SimpleMath::Matrix& Left, const SimpleMath::Matrix& Right) {
                return Left._11 == Right._11 && Left._12 == Right._12 && Left._13 == Right._13 && Left._14 == Right._14 && Left._21 == Right._21 && Left._22 == Right._22 && Left._23 == Right._23 && Left._24 == Right._24 && Left._31 == Right._31 && Left._32 == Right._32 && Left._33 == Right._33 && Left._34 == Right._34 && Left._41 == Right._41 && Left._42 == Right._42 && Left._43 == Right._43 && Left._44 == Right._44;
            }

            bool IsLocalTransformCacheCurrent(const Transform& TransformComponent, Arche::EntityID ParentEntityId) {
                return TransformComponent.mWorldMatrixCacheValid == true && TransformComponent.mCachedParentEntityId == ParentEntityId && AreVector3Equal(TransformComponent.position, TransformComponent.mCachedPosition) == true && AreQuaternionEqual(TransformComponent.rotation, TransformComponent.mCachedRotation) == true && AreVector3Equal(TransformComponent.scale, TransformComponent.mCachedScale) == true && AreMatricesEqual(TransformComponent.nodeToParent, TransformComponent.mCachedNodeToParent) == true;
            }

            void StoreLocalTransformCache(Transform& TransformComponent, Arche::EntityID ParentEntityId) {
                TransformComponent.mCachedPosition = TransformComponent.position;
                TransformComponent.mCachedRotation = TransformComponent.rotation;
                TransformComponent.mCachedScale = TransformComponent.scale;
                TransformComponent.mCachedNodeToParent = TransformComponent.nodeToParent;
                TransformComponent.mCachedParentEntityId = ParentEntityId;
                TransformComponent.mWorldMatrixCacheValid = true;
            }

            SimpleMath::Matrix BuildLocalWorldMatrix(const Transform& TransformComponent) {
                const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
                return TransformComponent.nodeToParent * TrsMatrix;
            }

            ResolvedWorldMatrix ResolveWorldMatrix(Transform& TransformComponent, Arche::EntityID ParentEntityId, const ResolvedWorldMatrix& ParentWorldMatrix) {
                if (IsLocalTransformCacheCurrent(TransformComponent, ParentEntityId) == true && ParentWorldMatrix.mChanged == false) {
                    TransformComponent.mWorldMatrixChanged = false;
                    return ResolvedWorldMatrix{ TransformComponent.worldMatrix, false };
                }

                const SimpleMath::Matrix LocalWorldMatrix{ BuildLocalWorldMatrix(TransformComponent) };
                const SimpleMath::Matrix CurrentWorldMatrix{ LocalWorldMatrix * ParentWorldMatrix.mMatrix };
                const bool IsWorldMatrixChanged{ TransformComponent.mWorldMatrixCacheValid == false || AreMatricesEqual(TransformComponent.worldMatrix, CurrentWorldMatrix) == false };
                if (IsWorldMatrixChanged == true) {
                    TransformComponent.worldMatrix = CurrentWorldMatrix;
                }

                StoreLocalTransformCache(TransformComponent, ParentEntityId);
                TransformComponent.mWorldMatrixChanged = IsWorldMatrixChanged;
                return ResolvedWorldMatrix{ TransformComponent.worldMatrix, IsWorldMatrixChanged };
            }

            bool TryResolveWorldMatrix(PipelineContext& Ctx, Arche::EntityID EntityId, std::unordered_map<Arche::EntityID, ResolvedWorldMatrix>& InOutWorldMatrices, ResolvedWorldMatrix& OutWorldMatrix) {
                const std::unordered_map<Arche::EntityID, ResolvedWorldMatrix>::const_iterator CachedWorldMatrixIter{ InOutWorldMatrices.find(EntityId) };
                if (CachedWorldMatrixIter != InOutWorldMatrices.end()) {
                    OutWorldMatrix = CachedWorldMatrixIter->second;
                    return true;
                }

                std::vector<Arche::EntityID> EntityPath{};
                Arche::EntityID CurrentEntityId{ EntityId };
                ResolvedWorldMatrix ParentWorldMatrix{};

                while (CurrentEntityId != Arche::NullEntityID) {
                    const std::unordered_map<Arche::EntityID, ResolvedWorldMatrix>::const_iterator CurrentCachedWorldMatrixIter{ InOutWorldMatrices.find(CurrentEntityId) };
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
                    Transform* TransformComponent{ Ctx.WriteComponent<Transform>(CurrentPathEntityId) };
                    const EntityHierarchy* HierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(CurrentPathEntityId) };
                    if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                        return false;
                    }

                    const ResolvedWorldMatrix CurrentWorldMatrix{ ResolveWorldMatrix(*TransformComponent, HierarchyComponent->parent, ParentWorldMatrix) };
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

            std::unordered_map<Arche::EntityID, ResolvedWorldMatrix> WorldMatrices{};
            Ctx.ForEach<Transform, EntityHierarchy>([&](Arche::EntityID EntityId, Transform& TransformComponent, EntityHierarchy& HierarchyComponent) {
                (void)TransformComponent;
                (void)HierarchyComponent;

                ResolvedWorldMatrix WorldMatrix{};
                const bool IsWorldMatrixResolved{ TryResolveWorldMatrix(Ctx, EntityId, WorldMatrices, WorldMatrix) };
                if (IsWorldMatrixResolved == false) {
                    return;
                }
            });
        }
    }
}
