#include "Game/Scene/Base/ScenePhysicsRuntimeCoordinator.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Arche/World.h"
#include "Core/Config.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/Tags.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Base/SceneTerrainBindings.h"
#include "Game/Scene/Base/SceneWorldSnapshot.h"
#include "Game/Scene/Base/SynchronousSystem.h"
#include "Terrain/TerrainManager.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "PhysicsLib/Actors/PhysicsStaticActor.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"
#include "PhysicsLib/Runtime/PhysicsRuntime.h"
#include "PhysicsLib/Runtime/PhysicsRuntimeTypes.h"
#include "Utility/MathValidation.h"
#include "Utility/Time.hpp"

namespace {
    constexpr std::string_view ObjectTagText{ "ObjectTag" };
    constexpr std::string_view PlayerTagText{ "PlayerTag" };
    constexpr double DefaultRenderPhysicsDelaySeconds{ 1.0 / 60.0 };
    constexpr std::uint64_t InvalidPhysicsSynchronizationStructureVersion{ std::numeric_limits<std::uint64_t>::max() };

    struct PendingPhysicsActorBinding final {
        Arche::EntityID mEntityId{ Arche::NullEntityID };
        PhysicsActorBase* mActorPointer{};
        std::uint32_t mActorIndex{};
        ActorId mActorId{ InvalidActorId };
        PhysicsActorBase::PhysicsActorType mActorType{ PhysicsActorBase::PhysicsActorType::Dynamic };
    };

    struct ScenePhysicsActorDesc final {
        Arche::EntityID mEntityId{ Arche::NullEntityID };
        std::uint32_t mActorIndex{};
        ActorId mActorId{ InvalidActorId };
        PhysicsActorBase::ActorDesc mActorDesc{};
        PhysicsTerrainActor::ActorDesc mTerrainActorDesc{};
        DirectX::SimpleMath::Quaternion mInitialOrientation{ DirectX::SimpleMath::Quaternion::Identity };
        bool mIsTerrainActor{};
    };

    std::string ResolvePhysicsActorName(const Arche::World& World, Arche::EntityID EntityId, const Game::Tag* TagComponent) {
        const Game::Name* NameComponent{ World.GetComponent<Game::Name>(EntityId) };
        if (NameComponent != nullptr && Game::GetNameTextView(*NameComponent).empty() == false) {
            return std::string{ Game::GetNameTextView(*NameComponent) };
        }

        if (TagComponent != nullptr && Game::GetTagTextView(*TagComponent).empty() == false) {
            return std::string{ Game::GetTagTextView(*TagComponent) };
        }

        return "PhysicsActor";
    }

    PhysicsActorBase::PhysicsActorFlags BuildDefaultPhysicsActorFlags() {
        PhysicsActorBase::PhysicsActorFlags ActorFlags{ PhysicsActorBase::PhysicsActorFlags::None };
        return ActorFlags;
    }

    PhysicsActorBase::ActorDesc BuildPhysicsActorDesc(const Arche::World& World, Arche::EntityID EntityId, const Game::Tag* TagComponent, const Game::BoundingBox& BoundingBoxComponent, const Game::Transform& TransformComponent, const Game::PhysicsActorSettings& SettingsComponent) {
        PhysicsActorBase::ActorDesc Desc{};
        const std::string_view SettingsName{ Game::GetPhysicsActorSettingsNameTextView(SettingsComponent) };
        Desc.Name = SettingsName.empty() == false ? std::string{ SettingsName } : ResolvePhysicsActorName(World, EntityId, TagComponent);
        Desc.IsActive = SettingsComponent.mIsActive;
        Desc.Mass = SettingsComponent.mMass;
        Desc.Flags = SettingsComponent.mFlags;
        Desc.ActorType = SettingsComponent.mActorType;
        Desc.LocalBoundingBox = BoundingBoxComponent.GetObb();
        Desc.Position = TransformComponent.position;
        Desc.Rotation = TransformComponent.rotationEuler;
        Desc.Scale = TransformComponent.scale;
        Desc.Velocity = DirectX::SimpleMath::Vector3{};
        Desc.Acceleration = DirectX::SimpleMath::Vector3{};
        Desc.Friction = SettingsComponent.mFriction;
        Desc.Restitution = SettingsComponent.mRestitution;
        return Desc;
    }

    PhysicsActorBase::ActorDesc BuildLegacyPhysicsActorDesc(const Arche::World& World, Arche::EntityID EntityId, const Game::Tag& TagComponent, const Game::BoundingBox& BoundingBoxComponent, const Game::Transform& TransformComponent, PhysicsActorBase::PhysicsActorType ActorType) {
        Game::PhysicsActorSettings SettingsComponent{};
        SettingsComponent.mActorType = ActorType;
        SettingsComponent.mFlags = BuildDefaultPhysicsActorFlags();
        return BuildPhysicsActorDesc(World, EntityId, &TagComponent, BoundingBoxComponent, TransformComponent, SettingsComponent);
    }

    PhysicsActorBase* CreatePhysicsActor(PhysicsWorld& PhysicsWorldInstance, const PhysicsActorBase::ActorDesc& Desc, std::uint32_t& OutActorIndex) {
        OutActorIndex = static_cast<std::uint32_t>(PhysicsWorldInstance.GetActorCount());
        if (Desc.ActorType == PhysicsActorBase::PhysicsActorType::Kinematic) {
            return PhysicsWorldInstance.CreateKinematicActor(Desc);
        }

        if (Desc.ActorType == PhysicsActorBase::PhysicsActorType::Static) {
            std::unique_ptr<PhysicsActorBase> NewActor{ std::make_unique<PhysicsStaticActor>(Desc) };
            PhysicsActorBase* CreatedActor{ NewActor.get() };
            PhysicsWorldInstance.AddActor(std::move(NewActor));
            return CreatedActor;
        }

        return PhysicsWorldInstance.CreateDynamicActor(Desc);
    }

    PhysicsTerrainActor::ActorDesc BuildPhysicsTerrainActorDesc(const PhysicsTerrainActor::ActorDesc& SourceDesc, const Game::Transform& TransformComponent) {
        PhysicsTerrainActor::ActorDesc Desc{ SourceDesc };
        Desc.Position = TransformComponent.position;
        Desc.Rotation = TransformComponent.rotationEuler;
        Desc.Scale = TransformComponent.scale;
        const std::uint32_t TerrainId{ Desc.mTerrainWorldData != nullptr ? Desc.mTerrainWorldData->mTerrainId : 0U };
        Desc.mTerrainWorldData = std::make_shared<const Terrain::TerrainWorldData>(PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(Desc, TerrainId));
        return Desc;
    }

    void RebuildTerrainActorSnapshot(PhysicsTerrainActor::ActorDesc& Desc, std::uint32_t TerrainId) {
        Desc.mTerrainWorldData = std::make_shared<const Terrain::TerrainWorldData>(PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(Desc, TerrainId));
    }

    void RegisterTerrainActorSnapshot(const PhysicsTerrainActor::ActorDesc& Desc, Terrain::TerrainManager& TerrainManagerInstance) {
        if (Desc.mTerrainWorldData == nullptr) {
            return;
        }

        static_cast<void>(TerrainManagerInstance.UpsertTerrainData(*Desc.mTerrainWorldData));
    }

    PhysicsTerrainActor::ActorDesc BuildCurrentPhysicsTerrainActorDesc(const PhysicsTerrainActor::ActorDesc& SourceDesc, const Game::Transform& TransformComponent, std::uint32_t TerrainId, const PhysicsTerrainActor* TerrainActorPointer, Terrain::TerrainManager& TerrainManagerInstance) {
        PhysicsTerrainActor::ActorDesc Desc{ TerrainActorPointer != nullptr ? TerrainActorPointer->GetActorDesc() : SourceDesc };
        Desc.Position = TransformComponent.position;
        Desc.Rotation = TransformComponent.rotationEuler;
        Desc.Scale = TransformComponent.scale;
        RebuildTerrainActorSnapshot(Desc, TerrainId);
        RegisterTerrainActorSnapshot(Desc, TerrainManagerInstance);
        return Desc;
    }

    void AssignScenePhysicsActorIndex(ScenePhysicsActorDesc& ActorDesc, std::size_t ActorIndex) {
        ActorDesc.mActorIndex = static_cast<std::uint32_t>(ActorIndex);
        ActorDesc.mActorId = static_cast<ActorId>(ActorDesc.mActorIndex);
    }

    ScenePhysicsActorDesc BuildScenePhysicsActorDesc(Arche::EntityID EntityId, const PhysicsActorBase::ActorDesc& ActorDesc, const DirectX::SimpleMath::Quaternion& InitialOrientation, std::size_t ActorIndex) {
        ScenePhysicsActorDesc SceneActorDesc{};
        SceneActorDesc.mEntityId = EntityId;
        SceneActorDesc.mActorDesc = ActorDesc;
        SceneActorDesc.mInitialOrientation = InitialOrientation;
        AssignScenePhysicsActorIndex(SceneActorDesc, ActorIndex);
        return SceneActorDesc;
    }

    ScenePhysicsActorDesc BuildSceneTerrainActorDesc(Arche::EntityID EntityId, const PhysicsTerrainActor::ActorDesc& TerrainActorDesc, std::size_t ActorIndex, Terrain::TerrainManager& TerrainManagerInstance) {
        ScenePhysicsActorDesc SceneActorDesc{};
        SceneActorDesc.mEntityId = EntityId;
        SceneActorDesc.mTerrainActorDesc = TerrainActorDesc;
        SceneActorDesc.mActorDesc.Name = "TerrainActor";
        SceneActorDesc.mActorDesc.IsActive = true;
        SceneActorDesc.mActorDesc.ActorType = PhysicsActorBase::PhysicsActorType::Static;
        SceneActorDesc.mActorDesc.Position = TerrainActorDesc.Position;
        SceneActorDesc.mActorDesc.Rotation = TerrainActorDesc.Rotation;
        SceneActorDesc.mActorDesc.Scale = TerrainActorDesc.Scale;
        SceneActorDesc.mInitialOrientation = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(TerrainActorDesc.Rotation.y, TerrainActorDesc.Rotation.x, TerrainActorDesc.Rotation.z);
        SceneActorDesc.mIsTerrainActor = true;
        AssignScenePhysicsActorIndex(SceneActorDesc, ActorIndex);
        RebuildTerrainActorSnapshot(SceneActorDesc.mTerrainActorDesc, SceneActorDesc.mActorId);
        RegisterTerrainActorSnapshot(SceneActorDesc.mTerrainActorDesc, TerrainManagerInstance);
        return SceneActorDesc;
    }

    bool IsValidTerrainActorDesc(const PhysicsTerrainActor::ActorDesc& TerrainActorDesc) {
        if (TerrainActorDesc.mTerrainWorldData != nullptr) {
            const std::size_t ExpectedHeightValueCount{ static_cast<std::size_t>(TerrainActorDesc.mTerrainWorldData->mHeightFieldWidth) * static_cast<std::size_t>(TerrainActorDesc.mTerrainWorldData->mHeightFieldHeight) };
            return TerrainActorDesc.mTerrainWorldData->mHeightFieldWidth > 1u && TerrainActorDesc.mTerrainWorldData->mHeightFieldHeight > 1u && TerrainActorDesc.mTerrainWorldData->mHeightFieldCellSizeX > 0.0f && TerrainActorDesc.mTerrainWorldData->mHeightFieldCellSizeZ > 0.0f && TerrainActorDesc.mTerrainWorldData->mHeightFieldMaxHeight > 0.0f && TerrainActorDesc.mTerrainWorldData->mHeightFieldValues != nullptr && TerrainActorDesc.mTerrainWorldData->mHeightFieldValues->size() == ExpectedHeightValueCount;
        }

        const std::size_t ExpectedHeightValueCount{ static_cast<std::size_t>(TerrainActorDesc.HeightFieldWidth) * static_cast<std::size_t>(TerrainActorDesc.HeightFieldHeight) };
        return TerrainActorDesc.HeightFieldWidth > 1u && TerrainActorDesc.HeightFieldHeight > 1u && TerrainActorDesc.HeightFieldCellSizeX > 0.0f && TerrainActorDesc.HeightFieldCellSizeZ > 0.0f && TerrainActorDesc.HeightFieldMaxHeight > 0.0f && TerrainActorDesc.HeightFieldValues != nullptr && TerrainActorDesc.HeightFieldValues->size() == ExpectedHeightValueCount;
    }

    bool AreTerrainWorldDataEquivalent(const Terrain::TerrainWorldData& Left, const Terrain::TerrainWorldData& Right) {
        return Left.mPosition == Right.mPosition && Left.mRotation == Right.mRotation && Left.mOrientation == Right.mOrientation && Left.mScale == Right.mScale && Left.mHalfExtentX == Right.mHalfExtentX && Left.mHalfExtentZ == Right.mHalfExtentZ && Left.mHeightFieldWidth == Right.mHeightFieldWidth && Left.mHeightFieldHeight == Right.mHeightFieldHeight && Left.mHeightFieldCellSizeX == Right.mHeightFieldCellSizeX && Left.mHeightFieldCellSizeZ == Right.mHeightFieldCellSizeZ && Left.mHeightFieldMaxHeight == Right.mHeightFieldMaxHeight && Left.mHeightFieldCenterOrigin == Right.mHeightFieldCenterOrigin && Left.mHeightFieldValues == Right.mHeightFieldValues;
    }

    void ClearPhysicsActorPendingCommands(Game::PhysicsActor& PhysicsActorComponent) {
        PhysicsCommand PendingCommand{};
        while (Game::TryConsumePhysicsActorPendingCommand(PhysicsActorComponent, PendingCommand) == true) {
            PendingCommand = PhysicsCommand{};
        }
    }

    double ResolveRenderPhysicsTimeSeconds(const Game::ScenePhysicsRuntimeContext& Context) {
        const double RenderPhysicsDelaySeconds{ Game::ScenePhysicsRuntimeCoordinator::ResolveRenderPhysicsDelaySeconds() };
        if (Context.mPhysicsTime != nullptr) {
            const double PhysicsTimeSeconds{ Context.mPhysicsTime->GetTimeSinceStarted<double>() };
            return std::max(0.0, PhysicsTimeSeconds - RenderPhysicsDelaySeconds);
        }

        const double LatestSimulationTimeSeconds{ Context.mPhysicsRuntime.LatestSimulationTimeSeconds() };
        return std::max(0.0, LatestSimulationTimeSeconds - RenderPhysicsDelaySeconds);
    }

    bool TryResolveLegacyPhysicsActorType(std::string_view TagText, PhysicsActorBase::PhysicsActorType& OutActorType) {
        OutActorType = PhysicsActorBase::PhysicsActorType::Dynamic;

        if (TagText == ObjectTagText) {
            OutActorType = PhysicsActorBase::PhysicsActorType::Dynamic;
            return true;
        }

        if (TagText == PlayerTagText) {
            OutActorType = PhysicsActorBase::PhysicsActorType::Kinematic;
            return true;
        }

        return false;
    }

    std::vector<ScenePhysicsActorDesc> CollectScenePhysicsActorDescs(Arche::World& World, const std::vector<Game::TerrainActorDescBinding>& TerrainActorDescBindings, Terrain::TerrainManager& TerrainManagerInstance) {
        std::vector<ScenePhysicsActorDesc> ActorDescs{};
        std::unordered_set<Arche::EntityID> ExplicitPhysicsActorEntities{};

        for (auto [PhysicsSettingsComponent, BoundingBoxComponent, TransformComponent, EntityHierarchyComponent] : World.Query<Game::PhysicsActorSettings, Game::BoundingBox, Game::Transform, Game::EntityHierarchy>()) {
            const Game::Tag* TagComponent{ std::as_const(World).GetComponent<Game::Tag>(EntityHierarchyComponent.self) };
            PhysicsActorBase::ActorDesc ActorDesc{ BuildPhysicsActorDesc(World, EntityHierarchyComponent.self, TagComponent, BoundingBoxComponent, TransformComponent, PhysicsSettingsComponent) };
            ActorDescs.push_back(BuildScenePhysicsActorDesc(EntityHierarchyComponent.self, ActorDesc, TransformComponent.rotation, ActorDescs.size()));
            ExplicitPhysicsActorEntities.insert(EntityHierarchyComponent.self);
        }

        for (auto [TagComponent, BoundingBoxComponent, TransformComponent, EntityHierarchyComponent] : World.Query<Game::Tag, Game::BoundingBox, Game::Transform, Game::EntityHierarchy>()) {
            if (ExplicitPhysicsActorEntities.contains(EntityHierarchyComponent.self) == true) {
                continue;
            }

            PhysicsActorBase::PhysicsActorType ActorType{ PhysicsActorBase::PhysicsActorType::Dynamic };
            const bool IsLegacyPhysicsActor{ TryResolveLegacyPhysicsActorType(Game::GetTagTextView(TagComponent), ActorType) };
            if (IsLegacyPhysicsActor == false) {
                continue;
            }

            PhysicsActorBase::ActorDesc ActorDesc{ BuildLegacyPhysicsActorDesc(World, EntityHierarchyComponent.self, TagComponent, BoundingBoxComponent, TransformComponent, ActorType) };
            ActorDescs.push_back(BuildScenePhysicsActorDesc(EntityHierarchyComponent.self, ActorDesc, TransformComponent.rotation, ActorDescs.size()));
        }

        for (const Game::TerrainActorDescBinding& BindingSource : TerrainActorDescBindings) {
            if (BindingSource.mEntityId == Arche::NullEntityID || IsValidTerrainActorDesc(BindingSource.mTerrainActorDesc) == false) {
                continue;
            }

            const Game::Transform* TransformComponent{ std::as_const(World).GetComponent<Game::Transform>(BindingSource.mEntityId) };
            if (TransformComponent == nullptr) {
                continue;
            }

            PhysicsTerrainActor::ActorDesc TerrainActorDesc{ BuildPhysicsTerrainActorDesc(BindingSource.mTerrainActorDesc, *TransformComponent) };
            ActorDescs.push_back(BuildSceneTerrainActorDesc(BindingSource.mEntityId, TerrainActorDesc, ActorDescs.size(), TerrainManagerInstance));
        }

        return ActorDescs;
    }

    PhysicsActorSpawnInfo BuildPhysicsActorSpawnInfo(const ScenePhysicsActorDesc& ActorDesc) {
        PhysicsActorSpawnInfo SpawnInfo{};
        SpawnInfo.mActorId = ActorDesc.mActorId;
        SpawnInfo.mName = ActorDesc.mActorDesc.Name;
        SpawnInfo.mIsActive = ActorDesc.mActorDesc.IsActive;
        SpawnInfo.mActorType = ActorDesc.mActorDesc.ActorType;
        SpawnInfo.mIsTerrainActor = ActorDesc.mIsTerrainActor;

        if (ActorDesc.mIsTerrainActor == true) {
            SpawnInfo.mTerrainActorDesc = ActorDesc.mTerrainActorDesc;
            if (SpawnInfo.mTerrainActorDesc.mTerrainWorldData == nullptr) {
                SpawnInfo.mTerrainActorDesc.mTerrainWorldData = std::make_shared<const Terrain::TerrainWorldData>(PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(SpawnInfo.mTerrainActorDesc, ActorDesc.mActorId));
            }
            return SpawnInfo;
        }

        SpawnInfo.mDynamicActorDesc = ActorDesc.mActorDesc;
        return SpawnInfo;
    }

    void BuildPhysicsRuntimeSceneFromActorDescs(const std::vector<ScenePhysicsActorDesc>& ActorDescs, PhysicsRuntimeScene& OutRuntimeScene) {
        OutRuntimeScene.mActorSpawnInfos.clear();
        OutRuntimeScene.mActorSpawnInfos.reserve(ActorDescs.size());

        for (const ScenePhysicsActorDesc& ActorDesc : ActorDescs) {
            OutRuntimeScene.mActorSpawnInfos.push_back(BuildPhysicsActorSpawnInfo(ActorDesc));
        }
    }

    PhysicsActorBase* CreatePhysicsWorldActor(PhysicsWorld& PhysicsWorldInstance, const ScenePhysicsActorDesc& ActorDesc, std::uint32_t& OutActorIndex) {
        OutActorIndex = static_cast<std::uint32_t>(PhysicsWorldInstance.GetActorCount());

        if (ActorDesc.mIsTerrainActor == true) {
            PhysicsTerrainActor* CreatedActor{ PhysicsWorldInstance.CreateTerrainActor(ActorDesc.mTerrainActorDesc) };
            if (CreatedActor != nullptr) {
                CreatedActor->SetName(ActorDesc.mActorDesc.Name);
                CreatedActor->SetIsActive(ActorDesc.mActorDesc.IsActive);
            }

            return CreatedActor;
        }

        PhysicsActorBase* CreatedActor{ CreatePhysicsActor(PhysicsWorldInstance, ActorDesc.mActorDesc, OutActorIndex) };
        if (CreatedActor == nullptr) {
            return nullptr;
        }

        CreatedActor->SetOrientation(ActorDesc.mInitialOrientation);
        CreatedActor->SetLocalBoundingBox(ActorDesc.mActorDesc.LocalBoundingBox);
        return CreatedActor;
    }

    std::vector<PendingPhysicsActorBinding> CreatePhysicsWorldActors(PhysicsWorld& PhysicsWorldInstance, const std::vector<ScenePhysicsActorDesc>& ActorDescs) {
        std::vector<PendingPhysicsActorBinding> PendingBindings{};
        PendingBindings.reserve(ActorDescs.size());

        for (const ScenePhysicsActorDesc& ActorDesc : ActorDescs) {
            std::uint32_t ActorIndex{};
            PhysicsActorBase* CreatedActor{ CreatePhysicsWorldActor(PhysicsWorldInstance, ActorDesc, ActorIndex) };
            if (CreatedActor == nullptr) {
                continue;
            }

            PendingPhysicsActorBinding Binding{};
            Binding.mEntityId = ActorDesc.mEntityId;
            Binding.mActorPointer = CreatedActor;
            Binding.mActorIndex = ActorIndex;
            Binding.mActorId = static_cast<ActorId>(ActorIndex);
            Binding.mActorType = ActorDesc.mActorDesc.ActorType;
            PendingBindings.push_back(Binding);
        }

        return PendingBindings;
    }

    std::uint32_t AdvancePhysicsWorldVersion(std::uint32_t CurrentVersion) {
        std::uint32_t NextVersion{ CurrentVersion + 1U };
        if (NextVersion == 0U) {
            NextVersion = 1U;
        }

        return NextVersion;
    }

    void AssignPhysicsActorBinding(Game::PhysicsActor& PhysicsActorComponent, const PendingPhysicsActorBinding& Binding, bool IsPhysicsRuntimeModeEnabled) {
        const bool ShouldBindSceneActorPointer{ IsPhysicsRuntimeModeEnabled == false || Binding.mActorType == PhysicsActorBase::PhysicsActorType::Kinematic };
        PhysicsActorComponent.mActorPointer = ShouldBindSceneActorPointer == true ? Binding.mActorPointer : nullptr;
        PhysicsActorComponent.mActorIndex = Binding.mActorIndex;
        PhysicsActorComponent.mActorId = Binding.mActorId;
        PhysicsActorComponent.mActorType = Binding.mActorType;

        if (Binding.mActorPointer == nullptr) {
            return;
        }

        Game::UpdatePhysicsActorCachedSnapshot(PhysicsActorComponent, Binding.mActorPointer->GetPosition(), Binding.mActorPointer->GetOrientation(), Binding.mActorPointer->GetScale(), Binding.mActorPointer->GetVelocity(), Binding.mActorPointer->GetWorldBoundingBox());
    }

    void AttachPhysicsActorComponent(Arche::World& World, const PendingPhysicsActorBinding& Binding, bool IsPhysicsRuntimeModeEnabled) {
        Game::PhysicsActor* ExistingPhysicsActorComponent{ World.GetComponent<Game::PhysicsActor>(Binding.mEntityId) };
        if (ExistingPhysicsActorComponent != nullptr) {
            AssignPhysicsActorBinding(*ExistingPhysicsActorComponent, Binding, IsPhysicsRuntimeModeEnabled);
            return;
        }

        Game::PhysicsActor NewPhysicsActorComponent{};
        AssignPhysicsActorBinding(NewPhysicsActorComponent, Binding, IsPhysicsRuntimeModeEnabled);
        World.AddComponent(Binding.mEntityId, NewPhysicsActorComponent);
    }

    void SetActorTransformFromComponent(PhysicsActorBase& Actor, const Game::Transform& TransformComponent) {
        if (Actor.GetPosition() != TransformComponent.position) {
            Actor.SetPosition(TransformComponent.position);
        }

        if (Actor.GetOrientation() != TransformComponent.rotation) {
            Actor.SetOrientation(TransformComponent.rotation);
        }

        if (Actor.GetScale() != TransformComponent.scale) {
            Actor.SetScale(TransformComponent.scale);
        }
    }

    void ApplyTerrainActorTransformIfChanged(PhysicsTerrainActor& TerrainActor, const Game::Transform& TransformComponent) {
        if (TerrainActor.GetPosition() != TransformComponent.position) {
            TerrainActor.SetPosition(TransformComponent.position);
        }

        if (TerrainActor.GetRotation() != TransformComponent.rotationEuler) {
            TerrainActor.SetRotation(TransformComponent.rotationEuler);
        }

        if (TerrainActor.GetScale() != TransformComponent.scale) {
            TerrainActor.SetScale(TransformComponent.scale);
        }
    }

    float ResolveTerrainRaycastDistance(const IPhysicsWorld& PhysicsWorldInstance, const DirectX::SimpleMath::Vector3& RayStartPoint, const DirectX::SimpleMath::Vector3& RayDirection, const float RayLength) {
        if (MathUtility::IsFiniteVector3(RayStartPoint) == false || MathUtility::IsFiniteVector3(RayDirection) == false || MathUtility::IsFiniteFloat(RayLength) == false || RayLength <= 0.0f) {
            return -1.0f;
        }

        bool IsHit{};
        float NearestHitDistance{ RayLength };
        std::vector<const PhysicsTerrainActor*> TerrainActors{ PhysicsWorldInstance.CollectTerrainActors() };
        const DirectX::SimpleMath::Ray Ray{ RayStartPoint, RayDirection };
        for (const PhysicsTerrainActor* TerrainActorPointer : TerrainActors) {
            if (TerrainActorPointer == nullptr) {
                continue;
            }

            DirectX::SimpleMath::Vector3 HitPosition{};
            DirectX::SimpleMath::Vector3 HitNormal{ DirectX::SimpleMath::Vector3::Up };
            float HitDistance{};
            const bool IsTerrainHit{ TerrainActorPointer->TryRaycast(Ray, RayLength, HitPosition, HitNormal, HitDistance) };
            if (IsTerrainHit == false || MathUtility::IsFiniteFloat(HitDistance) == false || HitDistance < 0.0f || HitDistance > RayLength) {
                continue;
            }

            if (IsHit == false || HitDistance < NearestHitDistance) {
                IsHit = true;
                NearestHitDistance = HitDistance;
            }
        }

        return IsHit == true ? NearestHitDistance : -1.0f;
    }
}

namespace Game {
    PhysicsWorld::WorldSettings ScenePhysicsRuntimeCoordinator::BuildDefaultPhysicsWorldSettings() {
        PhysicsWorld::WorldSettings Settings{};
        Settings.FixedTimeStep = 1.0f / 60.0f;
        Settings.Gravity = DirectX::SimpleMath::Vector3{ 0.0f, -9.8f, 0.0f };
        return Settings;
    }

    bool ScenePhysicsRuntimeCoordinator::ResolvePhysicsRuntimeModeEnabled() {
        const IConfig* ConfigInstance{ Config::Query() };
        if (ConfigInstance == nullptr) {
            return false;
        }

        return ConfigInstance->Get<bool>("PhysicsRuntime_Enabled");
    }

    double ScenePhysicsRuntimeCoordinator::ResolveRenderPhysicsDelaySeconds() {
        const IConfig* ConfigInstance{ Config::Query() };
        if (ConfigInstance == nullptr) {
            return DefaultRenderPhysicsDelaySeconds;
        }

        const double DelaySeconds{ ConfigInstance->Get<double>("RenderPhysicsDelaySeconds") };
        return std::max(0.0, DelaySeconds);
    }

    void ScenePhysicsRuntimeCoordinator::InitializePhysicsWorld(ScenePhysicsRuntimeContext Context) {
        PhysicsWorld::WorldSettings Settings{ BuildDefaultPhysicsWorldSettings() };
        Context.mPhysicsWorld.Initialize(Settings);
        Context.mFrameContext.PhysicsWorldResource = &Context.mPhysicsWorld;
        Context.mFrameContext.PhysicsRuntimeResource = &Context.mPhysicsRuntime;
        Context.mFrameContext.PhysicsSnapshotResource = nullptr;
        Context.mFrameContext.PhysicsSynchronizationEntityIds = &Context.mPhysicsSynchronizationEntityIds;
        Context.mFrameContext.TerrainManagerResource = &Context.mTerrainManager;
        Context.mFrameContext.TerrainQueryResource = &Context.mTerrainManager;
        Context.mFrameContext.IsPhysicsRuntimeModeEnabled = Context.mIsPhysicsRuntimeModeEnabled;
        Context.mFrameContext.mPhysicsWorldVersion = Context.mPhysicsWorldVersion;
        Context.mPhysicsSynchronizationEntityIds.clear();
        Context.mPhysicsSynchronizationStructureVersion = InvalidPhysicsSynchronizationStructureVersion;
        Context.mTerrainManager.Clear();

        for (TerrainActorDescBinding& Binding : Context.mTerrainActorDescBindings) {
            Binding.mIsTerrainActorDescApplied = false;
        }
    }

    void ScenePhysicsRuntimeCoordinator::InitializePhysicsRuntime(ScenePhysicsRuntimeContext Context) {
        ShutdownPhysicsRuntime(Context);
        Context.mFrameContext.IsPhysicsRuntimeModeEnabled = Context.mIsPhysicsRuntimeModeEnabled;
        Context.mFrameContext.PhysicsSnapshotResource = nullptr;
        Context.mFrameContext.PhysicsRuntimeResource = &Context.mPhysicsRuntime;
        Context.mFrameContext.TerrainManagerResource = &Context.mTerrainManager;
        Context.mFrameContext.TerrainQueryResource = &Context.mTerrainManager;

        if (Context.mIsPhysicsRuntimeModeEnabled == false) {
            Context.mFrameContext.PhysicsWorldResource = &Context.mPhysicsWorld;
            PublishPhysicsRuntimeStatus(Context, nullptr);
            return;
        }

        Context.mFrameContext.PhysicsWorldResource = &Context.mPhysicsWorld;

        PhysicsRuntime::RuntimeSettings RuntimeSettings{};
        RuntimeSettings.mWorldSettings = BuildDefaultPhysicsWorldSettings();
        RuntimeSettings.mMaxSubSteps = 4U;

        const bool IsRuntimeInitialized{ Context.mPhysicsRuntime.Initialize(Context.mPhysicsRuntimeScene, RuntimeSettings, Context.mPhysicsWorldVersion) };
        if (IsRuntimeInitialized == false) {
            Context.mIsPhysicsRuntimeModeEnabled = false;
            Context.mFrameContext.IsPhysicsRuntimeModeEnabled = false;
            Context.mFrameContext.PhysicsWorldResource = &Context.mPhysicsWorld;
            PublishPhysicsRuntimeStatus(Context, nullptr);
            return;
        }

        if (Context.mPhysicsTime != nullptr) {
            Context.mPhysicsTime->Reset();
        }

        PublishPhysicsRuntimeStatus(Context, nullptr);
    }

    void ScenePhysicsRuntimeCoordinator::ShutdownPhysicsRuntime(ScenePhysicsRuntimeContext Context) {
        Context.mPhysicsRuntime.Shutdown();
        Context.mFrameContext.PhysicsSnapshotResource = nullptr;
    }

    void ScenePhysicsRuntimeCoordinator::SubmitPhysicsRuntimeCommands(ScenePhysicsRuntimeContext Context) {
        if (Context.mIsPhysicsRuntimeModeEnabled == false || Context.mPhysicsRuntime.IsRunning() == false) {
            return;
        }

        for (Arche::EntityID EntityId : Context.mPhysicsSynchronizationEntityIds) {
            PhysicsActor* PhysicsActorComponent{ Context.mWorld.GetComponent<PhysicsActor>(EntityId) };
            if (PhysicsActorComponent == nullptr || PhysicsActorComponent->mActorType != PhysicsActorBase::PhysicsActorType::Dynamic) {
                continue;
            }

            PhysicsCommand PendingCommand{};
            while (TryConsumePhysicsActorPendingCommand(*PhysicsActorComponent, PendingCommand) == true) {
                if (PendingCommand.mActorId == InvalidActorId) {
                    continue;
                }

                static_cast<void>(Context.mPhysicsRuntime.EnqueueCommand(PendingCommand));
            }
        }

        for (TerrainActorDescBinding& BindingSource : Context.mTerrainActorDescBindings) {
            PhysicsActor* PhysicsActorComponent{ Context.mWorld.GetComponent<PhysicsActor>(BindingSource.mEntityId) };
            Transform* TransformComponent{ Context.mWorld.GetComponent<Transform>(BindingSource.mEntityId) };
            if (PhysicsActorComponent == nullptr || TransformComponent == nullptr || IsValidTerrainActorDesc(BindingSource.mTerrainActorDesc) == false) {
                continue;
            }

            const std::uint32_t PhysicsActorId{ ResolvePhysicsActorId(*PhysicsActorComponent) };
            if (PhysicsActorId == InvalidPhysicsActorId) {
                continue;
            }

            const PhysicsTerrainActor* TerrainActorPointer{ Context.mPhysicsWorld.GetTerrainActor(PhysicsActorId) };
            const bool IsTransformChanged{ PhysicsActorComponent->mHasCachedSnapshot == false || std::make_tuple(PhysicsActorComponent->mCachedPosition, PhysicsActorComponent->mCachedOrientation, PhysicsActorComponent->mCachedScale) != std::make_tuple(TransformComponent->position, TransformComponent->rotation, TransformComponent->scale) };
            const bool IsTerrainDataChanged{ TerrainActorPointer != nullptr && (TerrainActorPointer->GetTerrainWorldData() == nullptr || BindingSource.mTerrainActorDesc.mTerrainWorldData == nullptr || AreTerrainWorldDataEquivalent(*TerrainActorPointer->GetTerrainWorldData(), *BindingSource.mTerrainActorDesc.mTerrainWorldData) == false) };
            if (BindingSource.mIsTerrainActorDescApplied == true && IsTransformChanged == false && IsTerrainDataChanged == false) {
                continue;
            }

            PhysicsTerrainActor::ActorDesc Desc{ BuildCurrentPhysicsTerrainActorDesc(BindingSource.mTerrainActorDesc, *TransformComponent, PhysicsActorId, TerrainActorPointer, Context.mTerrainManager) };
            const std::shared_ptr<const PhysicsTerrainActor::ActorDesc> RuntimeTerrainActorDesc{ std::make_shared<const PhysicsTerrainActor::ActorDesc>(Desc) };
            PhysicsCommand Command{};
            Command.mType = PhysicsCommandType::SetTerrainActorDesc;
            Command.mActorId = static_cast<ActorId>(PhysicsActorId);
            Command.mTerrainActorDesc = RuntimeTerrainActorDesc;
            const bool IsCommandSubmitted{ Context.mPhysicsRuntime.EnqueueCommand(Command) };
            if (IsCommandSubmitted == true) {
                BindingSource.mTerrainActorDesc = std::move(Desc);
                BindingSource.mIsTerrainActorDescApplied = true;
            }
        }
    }

    void ScenePhysicsRuntimeCoordinator::PublishPhysicsRuntimeKinematicStates(ScenePhysicsRuntimeContext Context) {
        Context.mKinematicRuntimeStates.clear();

        if (Context.mIsPhysicsRuntimeModeEnabled == false || Context.mPhysicsRuntime.IsRunning() == false) {
            return;
        }

        for (Arche::EntityID EntityId : Context.mPhysicsSynchronizationEntityIds) {
            PhysicsActor* PhysicsActorComponent{ Context.mWorld.GetComponent<PhysicsActor>(EntityId) };
            Transform* TransformComponent{ Context.mWorld.GetComponent<Transform>(EntityId) };
            if (PhysicsActorComponent == nullptr || TransformComponent == nullptr || PhysicsActorComponent->mActorType != PhysicsActorBase::PhysicsActorType::Kinematic) {
                continue;
            }

            const std::uint32_t PhysicsActorId{ ResolvePhysicsActorId(*PhysicsActorComponent) };
            if (PhysicsActorId == InvalidPhysicsActorId) {
                continue;
            }

            PhysicsKinematicRuntimeState KinematicState{};
            KinematicState.mActorId = static_cast<ActorId>(PhysicsActorId);
            KinematicState.mPosition = TransformComponent->position;
            KinematicState.mOrientation = TransformComponent->rotation;
            KinematicState.mScale = TransformComponent->scale;
            KinematicState.mVelocity = PhysicsActorComponent->mCachedVelocity;
            KinematicState.mIsActive = true;

            PhysicsActorBase* ActorPointer{ PhysicsActorComponent->mActorPointer };
            if (ActorPointer == nullptr) {
                ActorPointer = Context.mPhysicsWorld.GetActor(static_cast<std::size_t>(PhysicsActorId));
            }

            if (ActorPointer != nullptr && ActorPointer->GetActorType() == PhysicsActorBase::PhysicsActorType::Kinematic) {
                KinematicState.mVelocity = ActorPointer->GetVelocity();
                KinematicState.mIsActive = ActorPointer->GetIsActive();
            }

            Context.mKinematicRuntimeStates.push_back(KinematicState);
        }

        Context.mPhysicsRuntime.PublishKinematicStates(Context.mKinematicRuntimeStates);
    }

    void ScenePhysicsRuntimeCoordinator::RefreshPhysicsRuntimeSnapshot(ScenePhysicsRuntimeContext Context) {
        Context.mFrameContext.PhysicsSnapshotResource = nullptr;
        if (Context.mIsPhysicsRuntimeModeEnabled == false || Context.mPhysicsRuntime.IsRunning() == false || Context.mPhysicsRuntime.PublishedSnapshotCount() == 0U) {
            PublishPhysicsRuntimeStatus(Context, nullptr);
            return;
        }

        const double RenderPhysicsTimeSeconds{ ResolveRenderPhysicsTimeSeconds(Context) };

        const bool HasSnapshot{ Context.mPhysicsRuntime.CopyInterpolatedSnapshotForTime(RenderPhysicsTimeSeconds, Context.mPhysicsRuntimeSnapshot) };

        if (HasSnapshot == false || Context.mPhysicsRuntimeSnapshot.mWorldVersion != Context.mPhysicsWorldVersion) {
            PublishPhysicsRuntimeStatus(Context, nullptr);
            return;
        }

        Context.mFrameContext.PhysicsSnapshotResource = &Context.mPhysicsRuntimeSnapshot;
        PublishPhysicsRuntimeStatus(Context, &Context.mPhysicsRuntimeSnapshot);
    }

    void ScenePhysicsRuntimeCoordinator::PublishPhysicsRuntimeStatus(ScenePhysicsRuntimeContext Context, const PhysicsSnapshot* Snapshot) {
        PhysicsRuntimeStatus Status{};
        Status.mIsRuntimeModeEnabled = Context.mIsPhysicsRuntimeModeEnabled;
        Status.mIsRunning = Context.mIsPhysicsRuntimeModeEnabled == true && Context.mPhysicsRuntime.IsRunning() == true;

        if (Context.mIsPhysicsRuntimeModeEnabled == true) {
            Status.mLatestStepIndex = Context.mPhysicsRuntime.LatestStepIndex();
        }

        if (Snapshot != nullptr) {
            Status.mSnapshotStepIndex = Snapshot->mStepIndex;
            Status.mActorCount = Snapshot->mTotalActorCount;
            Status.mLastUpdateStepCount = Snapshot->mLastUpdateStepCount;
            Status.mLastUpdateStepElapsedMilliseconds = Snapshot->mLastUpdateStepElapsedMilliseconds;
            Status.mLastStepElapsedMilliseconds = Snapshot->mLastStepElapsedMilliseconds;
            const double LatestSimulationTimeSeconds{ Context.mPhysicsRuntime.LatestSimulationTimeSeconds() };
            Status.mSnapshotAgeMilliseconds = std::max(0.0, LatestSimulationTimeSeconds - Snapshot->mSimulationTimeSeconds) * 1000.0;
        }
        else if (Context.mIsPhysicsRuntimeModeEnabled == false) {
            Status.mActorCount = Context.mPhysicsWorld.GetActorCount();
        }

        Context.mFrameContext.PhysicsRuntimeStatus = Status;
        Context.mWorldSnapshot.SetPhysicsRuntimeStatus(Status);
    }

    void ScenePhysicsRuntimeCoordinator::UpdateTerrainManager(ScenePhysicsRuntimeContext Context) {
        Context.mFrameContext.TerrainManagerResource = &Context.mTerrainManager;
        Context.mFrameContext.TerrainQueryResource = &Context.mTerrainManager;
    }

    void ScenePhysicsRuntimeCoordinator::UpdatePhysicsSynchronizationEntityIds(ScenePhysicsRuntimeContext Context) {
        const std::uint64_t CurrentStructureVersion{ Context.mWorld.GetStructureVersion() };
        if (Context.mPhysicsSynchronizationStructureVersion == CurrentStructureVersion) {
            return;
        }

        Context.mPhysicsSynchronizationEntityIds.clear();
        for (const auto [PhysicsActorComponent, TransformComponent, EntityHierarchyComponent] : Context.mWorld.Query<PhysicsActor, Transform, EntityHierarchy>()) {
            (void)TransformComponent;
            if (PhysicsActorComponent.mActorType == PhysicsActorBase::PhysicsActorType::Static) {
                continue;
            }

            Context.mPhysicsSynchronizationEntityIds.push_back(EntityHierarchyComponent.self);
        }

        Context.mPhysicsSynchronizationStructureVersion = CurrentStructureVersion;
    }

    void ScenePhysicsRuntimeCoordinator::UpdateSceneKinematicActors(ScenePhysicsRuntimeContext Context, float Dt) {
        (void)Dt;

        for (Arche::EntityID EntityId : Context.mPhysicsSynchronizationEntityIds) {
            PhysicsActor* PhysicsActorComponent{ Context.mWorld.GetComponent<PhysicsActor>(EntityId) };
            Transform* TransformComponent{ Context.mWorld.GetComponent<Transform>(EntityId) };
            BoundingBox* BoundingBoxComponent{ Context.mWorld.GetComponent<BoundingBox>(EntityId) };
            if (PhysicsActorComponent == nullptr || TransformComponent == nullptr || BoundingBoxComponent == nullptr) {
                continue;
            }

            if (PhysicsActorComponent->mActorType != PhysicsActorBase::PhysicsActorType::Kinematic) {
                continue;
            }

            PhysicsActorBase* ActorPointer{ PhysicsActorComponent->mActorPointer };
            const std::uint32_t PhysicsActorId{ ResolvePhysicsActorId(*PhysicsActorComponent) };
            if (ActorPointer == nullptr && PhysicsActorId != InvalidPhysicsActorId) {
                ActorPointer = Context.mPhysicsWorld.GetActor(static_cast<std::size_t>(PhysicsActorId));
                if (ActorPointer != nullptr && ActorPointer->GetActorType() == PhysicsActorBase::PhysicsActorType::Kinematic) {
                    PhysicsActorComponent->mActorPointer = ActorPointer;
                }
            }

            if (ActorPointer == nullptr || ActorPointer->GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
                continue;
            }

            SetActorTransformFromComponent(*ActorPointer, *TransformComponent);

            const DirectX::BoundingOrientedBox& ComponentLocalBoundingBox{ BoundingBoxComponent->GetObb() };
            const DirectX::BoundingOrientedBox& ActorLocalBoundingBox{ ActorPointer->GetLocalBoundingBox() };
            const std::tuple<DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Quaternion> ActorLocalBoundingBoxValue{ DirectX::SimpleMath::Vector3{ ActorLocalBoundingBox.Center }, DirectX::SimpleMath::Vector3{ ActorLocalBoundingBox.Extents }, DirectX::SimpleMath::Quaternion{ ActorLocalBoundingBox.Orientation } };
            const std::tuple<DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Quaternion> ComponentLocalBoundingBoxValue{ DirectX::SimpleMath::Vector3{ ComponentLocalBoundingBox.Center }, DirectX::SimpleMath::Vector3{ ComponentLocalBoundingBox.Extents }, DirectX::SimpleMath::Quaternion{ ComponentLocalBoundingBox.Orientation } };
            if (ActorLocalBoundingBoxValue != ComponentLocalBoundingBoxValue) {
                ActorPointer->SetLocalBoundingBox(ComponentLocalBoundingBox);
            }

            static_cast<void>(Context.mPhysicsWorld.ResolveKinematicTerrainContact(*ActorPointer));
            TransformComponent->position = ActorPointer->GetPosition();
            BoundingBoxComponent->SetWorldObb(ActorPointer->GetWorldBoundingBox());
            UpdatePhysicsActorCachedSnapshot(*PhysicsActorComponent, ActorPointer->GetPosition(), ActorPointer->GetOrientation(), ActorPointer->GetScale(), ActorPointer->GetVelocity(), ActorPointer->GetWorldBoundingBox());
        }
    }

    void ScenePhysicsRuntimeCoordinator::RebuildPhysicsActors(ScenePhysicsRuntimeContext Context) {
        ShutdownPhysicsRuntime(Context);
        InitializePhysicsWorld(Context);

        const std::vector<ScenePhysicsActorDesc> ActorDescs{ CollectScenePhysicsActorDescs(Context.mWorld, Context.mTerrainActorDescBindings, Context.mTerrainManager) };
        BuildPhysicsRuntimeSceneFromActorDescs(ActorDescs, Context.mPhysicsRuntimeScene);
        std::vector<PendingPhysicsActorBinding> PendingBindings{ CreatePhysicsWorldActors(Context.mPhysicsWorld, ActorDescs) };

        for (const PendingPhysicsActorBinding& Binding : PendingBindings) {
            AttachPhysicsActorComponent(Context.mWorld, Binding, Context.mIsPhysicsRuntimeModeEnabled);
        }

        UpdateTerrainManager(Context);
        Context.mPhysicsSynchronizationEntityIds.clear();
        Context.mPhysicsSynchronizationStructureVersion = InvalidPhysicsSynchronizationStructureVersion;
        Context.mPhysicsWorldVersion = AdvancePhysicsWorldVersion(Context.mPhysicsWorldVersion);
        Context.mFrameContext.mPhysicsWorldVersion = Context.mPhysicsWorldVersion;
        InitializePhysicsRuntime(Context);
    }

    void ScenePhysicsRuntimeCoordinator::UpdatePhysics(ScenePhysicsRuntimeContext Context, float Dt) {
        Context.mFrameContext.PhysicsWorldResource = &Context.mPhysicsWorld;
        Context.mFrameContext.PhysicsRuntimeResource = &Context.mPhysicsRuntime;
        Context.mFrameContext.PhysicsSynchronizationEntityIds = &Context.mPhysicsSynchronizationEntityIds;
        Context.mFrameContext.TerrainManagerResource = &Context.mTerrainManager;
        Context.mFrameContext.TerrainQueryResource = &Context.mTerrainManager;
        Context.mFrameContext.mPhysicsWorldVersion = Context.mPhysicsWorldVersion;
        UpdatePhysicsSynchronizationEntityIds(Context);
        UpdateTerrainManager(Context);
        UpdateSceneKinematicActors(Context, Dt);

        if (Context.mIsPhysicsRuntimeModeEnabled == true) {
            SubmitPhysicsRuntimeCommands(Context);
            PublishPhysicsRuntimeKinematicStates(Context);
            RefreshPhysicsRuntimeSnapshot(Context);
            return;
        }

        Context.mFrameContext.PhysicsSnapshotResource = nullptr;
        Context.mFrameContext.IsPhysicsRuntimeModeEnabled = false;

        for (TerrainActorDescBinding& BindingSource : Context.mTerrainActorDescBindings) {
            PhysicsActor* PhysicsActorComponent{ Context.mWorld.GetComponent<PhysicsActor>(BindingSource.mEntityId) };
            Transform* TransformComponent{ Context.mWorld.GetComponent<Transform>(BindingSource.mEntityId) };
            if (PhysicsActorComponent == nullptr || TransformComponent == nullptr || IsValidTerrainActorDesc(BindingSource.mTerrainActorDesc) == false) {
                continue;
            }

            PhysicsTerrainActor* TerrainActorPointer{ Context.mPhysicsWorld.GetTerrainActor(ResolvePhysicsActorId(*PhysicsActorComponent)) };
            if (TerrainActorPointer == nullptr) {
                continue;
            }

            if (BindingSource.mIsTerrainActorDescApplied == false) {
                const std::uint32_t PhysicsActorId{ ResolvePhysicsActorId(*PhysicsActorComponent) };
                PhysicsTerrainActor::ActorDesc Desc{ BuildCurrentPhysicsTerrainActorDesc(BindingSource.mTerrainActorDesc, *TransformComponent, PhysicsActorId, TerrainActorPointer, Context.mTerrainManager) };
                TerrainActorPointer->SetActorDesc(Desc);
                BindingSource.mTerrainActorDesc = std::move(Desc);
                BindingSource.mIsTerrainActorDescApplied = true;
                continue;
            }

            const bool IsTransformChanged{ std::make_tuple(TerrainActorPointer->GetPosition(), TerrainActorPointer->GetRotation(), TerrainActorPointer->GetScale()) != std::make_tuple(TransformComponent->position, TransformComponent->rotationEuler, TransformComponent->scale) };
            const bool IsTerrainDataChanged{ TerrainActorPointer->GetTerrainWorldData() == nullptr || BindingSource.mTerrainActorDesc.mTerrainWorldData == nullptr || AreTerrainWorldDataEquivalent(*TerrainActorPointer->GetTerrainWorldData(), *BindingSource.mTerrainActorDesc.mTerrainWorldData) == false };
            if (IsTransformChanged == true || IsTerrainDataChanged == true) {
                const std::uint32_t PhysicsActorId{ ResolvePhysicsActorId(*PhysicsActorComponent) };
                PhysicsTerrainActor::ActorDesc Desc{ BuildCurrentPhysicsTerrainActorDesc(BindingSource.mTerrainActorDesc, *TransformComponent, PhysicsActorId, TerrainActorPointer, Context.mTerrainManager) };
                TerrainActorPointer->SetActorDesc(Desc);
                BindingSource.mTerrainActorDesc = std::move(Desc);
            }
        }

        for (Arche::EntityID EntityId : Context.mPhysicsSynchronizationEntityIds) {
            PhysicsActor* PhysicsActorComponent{ Context.mWorld.GetComponent<PhysicsActor>(EntityId) };
            if (PhysicsActorComponent == nullptr) {
                continue;
            }

            ClearPhysicsActorPendingCommands(*PhysicsActorComponent);
        }

        Context.mPhysicsWorld.TickKinematicActors(Dt);
        Context.mPhysicsWorld.Update(Dt);
        PublishPhysicsRuntimeStatus(Context, nullptr);
    }

    void ScenePhysicsRuntimeCoordinator::AddTerrainActorDesc(ScenePhysicsRuntimeContext Context, Arche::EntityID EntityId, const PhysicsTerrainActor::ActorDesc& TerrainActorDesc) {
        PhysicsTerrainActor::ActorDesc StoredTerrainActorDesc{ TerrainActorDesc };
        StoredTerrainActorDesc.mTerrainWorldData = std::make_shared<const Terrain::TerrainWorldData>(PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(StoredTerrainActorDesc, 0U));
        for (TerrainActorDescBinding& Binding : Context.mTerrainActorDescBindings) {
            if (Binding.mEntityId != EntityId) {
                continue;
            }

            const bool IsTerrainActorDescChanged{ Binding.mTerrainActorDesc.mTerrainWorldData == nullptr || StoredTerrainActorDesc.mTerrainWorldData == nullptr || AreTerrainWorldDataEquivalent(*Binding.mTerrainActorDesc.mTerrainWorldData, *StoredTerrainActorDesc.mTerrainWorldData) == false };
            Binding.mTerrainActorDesc = StoredTerrainActorDesc;
            if (IsTerrainActorDescChanged == true) {
                Binding.mIsTerrainActorDescApplied = false;
            }

            return;
        }

        TerrainActorDescBinding NewBinding{};
        NewBinding.mEntityId = EntityId;
        NewBinding.mTerrainActorDesc = StoredTerrainActorDesc;
        Context.mTerrainActorDescBindings.push_back(NewBinding);
    }

    void ScenePhysicsRuntimeCoordinator::ClearTerrainActorDescs(ScenePhysicsRuntimeContext Context) {
        Context.mTerrainActorDescBindings.clear();
    }
}
