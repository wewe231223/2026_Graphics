#include "Game/Scene/Systems/FootIKSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/FootIKRuntime.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/TerrainCollidee.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    bool IsFiniteFloat(const float Value) {
        return ::std::isfinite(Value) != 0;
    }

    bool IsFiniteVector3(const SimpleMath::Vector3& Value) {
        return IsFiniteFloat(Value.x) && IsFiniteFloat(Value.y) && IsFiniteFloat(Value.z);
    }

    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }

    bool TryResolveWorldMatrix(Arche::World& World, const Arche::EntityID EntityId, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, SimpleMath::Matrix& OutWorldMatrix) {
        const ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CachedWorldMatrixIter{ InOutWorldMatrices.find(EntityId) };
        if (CachedWorldMatrixIter != InOutWorldMatrices.end()) {
            OutWorldMatrix = CachedWorldMatrixIter->second;
            return true;
        }

        ::std::vector<Arche::EntityID> EntityPath{};
        Arche::EntityID CurrentEntityId{ EntityId };
        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };
        while (CurrentEntityId != Arche::NullEntityID) {
            const ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CurrentCachedWorldMatrixIter{ InOutWorldMatrices.find(CurrentEntityId) };
            if (CurrentCachedWorldMatrixIter != InOutWorldMatrices.end()) {
                ParentWorldMatrix = CurrentCachedWorldMatrixIter->second;
                break;
            }

            const Game::Transform* TransformComponent{ ::std::as_const(World).GetComponent<Game::Transform>(CurrentEntityId) };
            const Game::EntityHierarchy* HierarchyComponent{ ::std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                return false;
            }

            EntityPath.push_back(CurrentEntityId);
            CurrentEntityId = HierarchyComponent->parent;
        }

        for (::std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
            const Arche::EntityID CurrentPathEntityId{ *EntityPathIter };
            const Game::Transform* TransformComponent{ ::std::as_const(World).GetComponent<Game::Transform>(CurrentPathEntityId) };
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

    Arche::EntityID FindBoneEntityByNameInHierarchy(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const Arche::EntityID RootEntityId, const char* TargetNameText) {
        if (RootEntityId == Arche::NullEntityID || TargetNameText == nullptr || TargetNameText[0] == '\0') {
            return Arche::NullEntityID;
        }

        ::std::vector<Arche::EntityID> Stack{};
        Stack.reserve(256);
        Stack.push_back(RootEntityId);
        while (Stack.empty() == false) {
            const Arche::EntityID CurrentEntityId{ Stack.back() };
            Stack.pop_back();

            const Game::Bone* BoneComponent{ ReadOnlyWorld.GetComponent<Game::Bone>(CurrentEntityId) };
            const Game::Name* NameComponent{ ReadOnlyWorld.GetComponent<Game::Name>(CurrentEntityId) };
            if (BoneComponent != nullptr && NameComponent != nullptr) {
                const char* CurrentNameText{ Game::GetNameText(*NameComponent) };
                if (CurrentNameText != nullptr && ::std::strcmp(CurrentNameText, TargetNameText) == 0) {
                    return CurrentEntityId;
                }
            }

            const Game::EntityHierarchy* HierarchyComponent{ ReadOnlyWorld.GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (HierarchyComponent == nullptr) {
                continue;
            }

            Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
            while (ChildEntityId != Arche::NullEntityID) {
                Stack.push_back(ChildEntityId);

                const Game::EntityHierarchy* ChildHierarchyComponent{ ReadOnlyWorld.GetComponent<Game::EntityHierarchy>(ChildEntityId) };
                ChildEntityId = ChildHierarchyComponent == nullptr ? Arche::NullEntityID : ChildHierarchyComponent->nextSibling;
            }
        }

        return Arche::NullEntityID;
    }

    bool TryResolveTerrainGroundY(Arche::World& World, const SimpleMath::Vector3& Position, float& OutGroundY) {
        bool IsResolved{};
        float HighestGroundY{};
        for (const auto [TerrainCollideeComponent] : World.Query<Game::TerrainCollidee>()) {
            Game::TerrainHeightResolver* TerrainHeightResolverPointer{ TerrainCollideeComponent.mTerrainHeightResolver };
            if (TerrainHeightResolverPointer == nullptr) {
                continue;
            }

            SimpleMath::Vector3 CandidatePosition{ Position };
            if (TerrainHeightResolverPointer->TryResolvePositionY(CandidatePosition) == false || IsFiniteVector3(CandidatePosition) == false) {
                continue;
            }

            if (IsResolved == false || CandidatePosition.y > HighestGroundY) {
                HighestGroundY = CandidatePosition.y;
                IsResolved = true;
            }
        }

        if (IsResolved == false || IsFiniteFloat(HighestGroundY) == false) {
            return false;
        }

        OutGroundY = HighestGroundY;
        return true;
    }

    bool IsCachedFootEntityValid(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const Arche::EntityID EntityId, const char* ExpectedBoneNameText) {
        if (ExpectedBoneNameText == nullptr || ExpectedBoneNameText[0] == '\0') {
            return EntityId == Arche::NullEntityID;
        }

        if (EntityId == Arche::NullEntityID) {
            return false;
        }

        const Game::Bone* BoneComponent{ ReadOnlyWorld.GetComponent<Game::Bone>(EntityId) };
        const Game::Name* NameComponent{ ReadOnlyWorld.GetComponent<Game::Name>(EntityId) };
        if (BoneComponent == nullptr || NameComponent == nullptr) {
            return false;
        }

        const char* CurrentNameText{ Game::GetNameText(*NameComponent) };
        if (CurrentNameText == nullptr) {
            return false;
        }

        return ::std::strcmp(CurrentNameText, ExpectedBoneNameText) == 0;
    }

    void ResolveFootBoneEntities(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const Game::FootIKRig& FootIKRigComponent, const Arche::EntityID BoneRootEntityId, Game::FootIKRuntime& InOutFootIKRuntimeComponent) {
        InOutFootIKRuntimeComponent.mLeftFootEntityId = Arche::NullEntityID;
        InOutFootIKRuntimeComponent.mRightFootEntityId = Arche::NullEntityID;
        InOutFootIKRuntimeComponent.mResolved = false;
        if (BoneRootEntityId == Arche::NullEntityID) {
            return;
        }

        const char* LeftFootBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mLeftFootBoneName) };
        const char* RightFootBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mRightFootBoneName) };

        InOutFootIKRuntimeComponent.mLeftFootEntityId = FindBoneEntityByNameInHierarchy(ReadOnlyWorld, BoneRootEntityId, LeftFootBoneNameText);
        InOutFootIKRuntimeComponent.mRightFootEntityId = FindBoneEntityByNameInHierarchy(ReadOnlyWorld, BoneRootEntityId, RightFootBoneNameText);
        InOutFootIKRuntimeComponent.mResolved = true;
    }

    bool TryResolveFootPenetration(Arche::World& World, const Arche::EntityID FootEntityId, const float FootSoleOffset, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, float& OutPenetration) {
        if (FootEntityId == Arche::NullEntityID) {
            return false;
        }

        SimpleMath::Matrix FootWorldMatrix{};
        if (TryResolveWorldMatrix(World, FootEntityId, InOutWorldMatrices, FootWorldMatrix) == false) {
            return false;
        }

        const SimpleMath::Vector3 FootWorldPosition{ FootWorldMatrix._41, FootWorldMatrix._42, FootWorldMatrix._43 };
        if (IsFiniteVector3(FootWorldPosition) == false) {
            return false;
        }

        float GroundY{};
        if (TryResolveTerrainGroundY(World, FootWorldPosition, GroundY) == false) {
            return false;
        }

        const float Penetration{ (GroundY + FootSoleOffset) - FootWorldPosition.y };
        if (IsFiniteFloat(Penetration) == false) {
            return false;
        }

        OutPenetration = ::std::max(Penetration, 0.0f);
        return true;
    }

    bool TryApplyAbsoluteFootLiftToBoneTransform(Arche::World& World, const Arche::EntityID FootEntityId, const float AbsoluteLift, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (FootEntityId == Arche::NullEntityID || IsFiniteFloat(AbsoluteLift) == false || AbsoluteLift <= 0.0f) {
            return false;
        }

        Game::Transform* FootTransformComponent{ World.GetComponent<Game::Transform>(FootEntityId) };
        const Game::EntityHierarchy* FootHierarchyComponent{ ::std::as_const(World).GetComponent<Game::EntityHierarchy>(FootEntityId) };
        if (FootTransformComponent == nullptr || FootHierarchyComponent == nullptr) {
            return false;
        }

        SimpleMath::Matrix FootWorldMatrix{};
        if (TryResolveWorldMatrix(World, FootEntityId, InOutWorldMatrices, FootWorldMatrix) == false) {
            return false;
        }

        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };
        if (FootHierarchyComponent->parent != Arche::NullEntityID) {
            if (TryResolveWorldMatrix(World, FootHierarchyComponent->parent, InOutWorldMatrices, ParentWorldMatrix) == false) {
                return false;
            }
        }

        SimpleMath::Matrix DesiredFootWorldMatrix{ FootWorldMatrix };
        DesiredFootWorldMatrix._42 += AbsoluteLift;

        SimpleMath::Matrix ParentWorldInverseMatrix{ ParentWorldMatrix };
        ParentWorldInverseMatrix = ParentWorldInverseMatrix.Invert();
        if (IsFiniteFloat(ParentWorldInverseMatrix._11) == false) {
            return false;
        }

        const SimpleMath::Matrix DesiredFootLocalWorldMatrix{ DesiredFootWorldMatrix * ParentWorldInverseMatrix };
        SimpleMath::Matrix NodeToParentInverseMatrix{ FootTransformComponent->nodeToParent };
        NodeToParentInverseMatrix = NodeToParentInverseMatrix.Invert();
        if (IsFiniteFloat(NodeToParentInverseMatrix._11) == false) {
            return false;
        }

        SimpleMath::Matrix DesiredTrsMatrix{ NodeToParentInverseMatrix * DesiredFootLocalWorldMatrix };
        SimpleMath::Vector3 DecomposedScale{};
        SimpleMath::Quaternion DecomposedRotation{};
        SimpleMath::Vector3 DecomposedPosition{};
        const bool IsDecomposeSucceeded{ DesiredTrsMatrix.Decompose(DecomposedScale, DecomposedRotation, DecomposedPosition) };
        if (IsDecomposeSucceeded == false) {
            return false;
        }

        if (IsFiniteVector3(DecomposedPosition) == false) {
            return false;
        }

        FootTransformComponent->position = DecomposedPosition;
        InOutWorldMatrices[FootEntityId] = DesiredFootWorldMatrix;
        return true;
    }

    float ResolveSmoothedLift(const float CurrentLift, const float TargetLift, const float BlendSpeed, const float Dt) {
        const float SafeCurrentLift{ IsFiniteFloat(CurrentLift) ? CurrentLift : 0.0f };
        const float SafeTargetLift{ IsFiniteFloat(TargetLift) ? TargetLift : 0.0f };
        const float SafeBlendSpeed{ ::std::max(BlendSpeed, 0.0f) };
        const float SafeDt{ ::std::max(Dt, 0.0f) };
        if (SafeBlendSpeed <= 0.0f || SafeDt <= 0.0f) {
            return SafeTargetLift;
        }

        const float BlendAlpha{ ::std::clamp(SafeBlendSpeed * SafeDt, 0.0f, 1.0f) };
        return ::std::lerp(SafeCurrentLift, SafeTargetLift, BlendAlpha);
    }
}

namespace Game {
    FootIKSystem::FootIKSystem()
        : mName{ "FootIKSystem" } {
    }

    FootIKSystem::~FootIKSystem() {
    }

    FootIKSystem::FootIKSystem(const FootIKSystem& Other)
        : mName{ Other.mName } {
    }

    FootIKSystem& FootIKSystem::operator=(const FootIKSystem& Other) {
        if (this == &Other) {
            return *this;
        }

        mName = Other.mName;
        return *this;
    }

    FootIKSystem::FootIKSystem(FootIKSystem&& Other) noexcept
        : mName{ ::std::move(Other.mName) } {
    }

    FootIKSystem& FootIKSystem::operator=(FootIKSystem&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mName = ::std::move(Other.mName);
        return *this;
    }

    const std::string& FootIKSystem::Name() const {
        return mName;
    }

    Phase FootIKSystem::GetPhase() const {
        return Phase::RenderPrepare;
    }

    std::span<const ComponentAccess> FootIKSystem::ComponentAccesses() const {
        static ::std::array<ComponentAccess, 9> Accesses{ {
            { typeid(Animator), Access::Read },
            { typeid(FootIKRig), Access::Read },
            { typeid(BoneSkinReference), Access::Read },
            { typeid(Bone), Access::Read },
            { typeid(Game::Name), Access::Read },
            { typeid(EntityHierarchy), Access::Read },
            { typeid(TerrainCollidee), Access::Read },
            { typeid(Transform), Access::Write },
            { typeid(FootIKRuntime), Access::Write }
        } };
        return Accesses;
    }

    std::span<const ResourceAccess> FootIKSystem::ResourceAccesses() const {
        static ::std::array<ResourceAccess, 0> Accesses{};
        return Accesses;
    }

    void FootIKSystem::Execute(Arche::World& World, FrameContext& Ctx, const float Dt) {
        (void)Ctx;

        const Arche::World::WorldReadOnlyView& ReadOnlyWorld{ ::std::as_const(World).GetReadOnlyView() };
        ::std::vector<Arche::EntityID> RuntimeMissingEntityIds{};
        RuntimeMissingEntityIds.reserve(32);
        for (auto [AnimatorComponent, FootIKRigComponent, BoneSkinReferenceComponent, TransformComponent, EntityHierarchyComponent] : World.Query<Animator, FootIKRig, BoneSkinReference, Transform, EntityHierarchy>()) {
            (void)AnimatorComponent;
            (void)FootIKRigComponent;
            (void)BoneSkinReferenceComponent;
            (void)TransformComponent;
            if (ReadOnlyWorld.GetComponent<FootIKRuntime>(EntityHierarchyComponent.self) == nullptr) {
                RuntimeMissingEntityIds.push_back(EntityHierarchyComponent.self);
            }
        }

        for (const Arche::EntityID RuntimeMissingEntityId : RuntimeMissingEntityIds) {
            if (World.GetComponent<FootIKRuntime>(RuntimeMissingEntityId) != nullptr) {
                continue;
            }

            FootIKRuntime NewFootIKRuntime{};
            World.AddComponent(RuntimeMissingEntityId, NewFootIKRuntime);
        }

        ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix> WorldMatrices{};
        WorldMatrices.reserve(1024);

        for (auto [AnimatorComponent, FootIKRigComponent, BoneSkinReferenceComponent, TransformComponent, FootIKRuntimeComponent, EntityHierarchyComponent] : World.Query<Animator, FootIKRig, BoneSkinReference, Transform, FootIKRuntime, EntityHierarchy>()) {
            (void)AnimatorComponent;
            (void)TransformComponent;
            (void)EntityHierarchyComponent;

            const char* LeftFootBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mLeftFootBoneName) };
            const char* RightFootBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mRightFootBoneName) };
            const bool IsLeftCachedEntityValid{ IsCachedFootEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mLeftFootEntityId, LeftFootBoneNameText) };
            const bool IsRightCachedEntityValid{ IsCachedFootEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mRightFootEntityId, RightFootBoneNameText) };
            if (IsLeftCachedEntityValid == false || IsRightCachedEntityValid == false) {
                FootIKRuntimeComponent.mResolved = false;
            }

            if (FootIKRuntimeComponent.mResolved == false) {
                ResolveFootBoneEntities(ReadOnlyWorld, FootIKRigComponent, BoneSkinReferenceComponent.boneRootEntityId, FootIKRuntimeComponent);
            }

            float LeftTargetLift{};
            float RightTargetLift{};
            if (FootIKRigComponent.mEnabled == true && FootIKRuntimeComponent.mResolved == true) {
                float LeftPenetration{};
                const bool IsLeftPenetrationResolved{ TryResolveFootPenetration(World, FootIKRuntimeComponent.mLeftFootEntityId, FootIKRigComponent.mFootSoleOffset, WorldMatrices, LeftPenetration) };
                if (IsLeftPenetrationResolved == true) {
                    LeftTargetLift = LeftPenetration;
                }

                float RightPenetration{};
                const bool IsRightPenetrationResolved{ TryResolveFootPenetration(World, FootIKRuntimeComponent.mRightFootEntityId, FootIKRigComponent.mFootSoleOffset, WorldMatrices, RightPenetration) };
                if (IsRightPenetrationResolved == true) {
                    RightTargetLift = RightPenetration;
                }
            }

            const float MaxLift{ ::std::max(FootIKRigComponent.mMaxLift, 0.0f) };
            LeftTargetLift = ::std::clamp(LeftTargetLift, 0.0f, MaxLift);
            RightTargetLift = ::std::clamp(RightTargetLift, 0.0f, MaxLift);

            const float PreviousLeftLift{ IsFiniteFloat(FootIKRuntimeComponent.mLeftCurrentLift) ? FootIKRuntimeComponent.mLeftCurrentLift : 0.0f };
            const float PreviousRightLift{ IsFiniteFloat(FootIKRuntimeComponent.mRightCurrentLift) ? FootIKRuntimeComponent.mRightCurrentLift : 0.0f };
            const float SmoothedLeftLift{ ResolveSmoothedLift(PreviousLeftLift, LeftTargetLift, FootIKRigComponent.mBlendSpeed, Dt) };
            const float SmoothedRightLift{ ResolveSmoothedLift(PreviousRightLift, RightTargetLift, FootIKRigComponent.mBlendSpeed, Dt) };
            FootIKRuntimeComponent.mLeftCurrentLift = IsFiniteFloat(SmoothedLeftLift) ? SmoothedLeftLift : 0.0f;
            FootIKRuntimeComponent.mRightCurrentLift = IsFiniteFloat(SmoothedRightLift) ? SmoothedRightLift : 0.0f;
            FootIKRuntimeComponent.mCurrentLift = ::std::max(FootIKRuntimeComponent.mLeftCurrentLift, FootIKRuntimeComponent.mRightCurrentLift);

            TryApplyAbsoluteFootLiftToBoneTransform(World, FootIKRuntimeComponent.mLeftFootEntityId, FootIKRuntimeComponent.mLeftCurrentLift, WorldMatrices);
            TryApplyAbsoluteFootLiftToBoneTransform(World, FootIKRuntimeComponent.mRightFootEntityId, FootIKRuntimeComponent.mRightCurrentLift, WorldMatrices);
        }
    }
}
