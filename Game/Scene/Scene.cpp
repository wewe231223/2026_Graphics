#include "Scene.h"
#include <utility>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <memory>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>
#include "Imgui/imgui.h"
#include "Core/Config.h"
#include "Asset/Common.h"

#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/FootIKRuntime.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PrefabInstance.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Components/ComponentLuaTypeDefinitions.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/DirectionalLight.h"
#include "Game/Scene/Components/Frustum.h"
#include "Game/Scene/Components/SkySphere.h"
#include "Game/Scene/Components/RuntimeVariableTable.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/AnimatorGraphPlayer.h"
#include "Game/Scene/Components/ScriptComponent.h"
#include "Game/Scene/Components/Tags.h"
#include "Game/Base/Input.h"
#include "PhysicsLib/Actors/PhysicsStaticActor.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

#include "Game/Scene/Events/SelectionEvent.h"
#include "Core/Event/EventQueue.h"
#include "Core/Event/FileDropEvent.h"
#include "Utility/MathValidation.h"
#include "Utility/StringUtils.h"
#include "SceneEntityFactory.h"

namespace {
    constexpr std::string_view ObjectTagText{ "ObjectTag" };
    constexpr std::string_view PlayerTagText{ "PlayerTag" };
    constexpr double DefaultRenderPhysicsDelaySeconds{ 1.0 / 60.0 };

    PhysicsWorld::WorldSettings BuildDefaultPhysicsWorldSettings() {
        PhysicsWorld::WorldSettings Settings{};
        Settings.FixedTimeStep = 1.0f / 60.0f;
        Settings.Gravity = DirectX::SimpleMath::Vector3{ 0.0f, -9.8f, 0.0f };
        return Settings;
    }

    bool ResolvePhysicsRuntimeModeEnabled() {
        const IConfig* ConfigInstance{ Config::Query() };
        if (ConfigInstance == nullptr) {
            return false;
        }

        return ConfigInstance->Get<bool>("PhysicsRuntime_Enabled");
    }

    double ResolveRenderPhysicsDelaySeconds() {
        const IConfig* ConfigInstance{ Config::Query() };
        if (ConfigInstance == nullptr) {
            return DefaultRenderPhysicsDelaySeconds;
        }

        const double DelaySeconds{ ConfigInstance->Get<double>("RenderPhysicsDelaySeconds") };
        return std::max(0.0, DelaySeconds);
    }

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

    std::uint64_t GenerateNextPrefabId(Game::Scene& TargetScene) {
        std::unordered_set<std::uint64_t> UsedPrefabIds{};
        for (const auto [PrefabComponent] : TargetScene.GetWorld().Query<Game::PrefabInstance>()) {
            if (PrefabComponent.PrefabId == 0ull) {
                continue;
            }

            UsedPrefabIds.insert(PrefabComponent.PrefabId);
        }

        std::uint64_t NextPrefabId{ 1001ull };
        while (UsedPrefabIds.contains(NextPrefabId)) {
            NextPrefabId += 1ull;
        }

        return NextPrefabId;
    }

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
        Desc.mTerrainWorldData = std::make_shared<const TerrainWorldData>(PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(Desc, TerrainId));
        return Desc;
    }

    PhysicsTerrainActor::ActorDesc BuildCurrentPhysicsTerrainActorDesc(const PhysicsTerrainActor::ActorDesc& SourceDesc, const Game::Transform& TransformComponent, std::uint32_t TerrainId, const PhysicsTerrainActor* TerrainActorPointer) {
        PhysicsTerrainActor::ActorDesc Desc{ TerrainActorPointer != nullptr ? TerrainActorPointer->GetActorDesc() : SourceDesc };
        Desc.Position = TransformComponent.position;
        Desc.Rotation = TransformComponent.rotationEuler;
        Desc.Scale = TransformComponent.scale;
        Desc.mTerrainWorldData = std::make_shared<const TerrainWorldData>(PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(Desc, TerrainId));
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

    ScenePhysicsActorDesc BuildSceneTerrainActorDesc(Arche::EntityID EntityId, const PhysicsTerrainActor::ActorDesc& TerrainActorDesc, std::size_t ActorIndex) {
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
        SceneActorDesc.mTerrainActorDesc.mTerrainWorldData = std::make_shared<const TerrainWorldData>(PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(SceneActorDesc.mTerrainActorDesc, SceneActorDesc.mActorId));
        return SceneActorDesc;
    }

    bool IsValidTerrainActorDesc(const PhysicsTerrainActor::ActorDesc& TerrainActorDesc) {
        if (TerrainActorDesc.mTerrainWorldData != nullptr) {
            return IsTerrainWorldDataValid(*TerrainActorDesc.mTerrainWorldData);
        }

        const std::size_t ExpectedHeightValueCount{ static_cast<std::size_t>(TerrainActorDesc.HeightFieldWidth) * static_cast<std::size_t>(TerrainActorDesc.HeightFieldHeight) };
        return TerrainActorDesc.HeightFieldWidth > 1u && TerrainActorDesc.HeightFieldHeight > 1u && TerrainActorDesc.HeightFieldCellSizeX > 0.0f && TerrainActorDesc.HeightFieldCellSizeZ > 0.0f && TerrainActorDesc.HeightFieldMaxHeight > 0.0f && TerrainActorDesc.HeightFieldValues.size() == ExpectedHeightValueCount;
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

    std::vector<ScenePhysicsActorDesc> CollectScenePhysicsActorDescs(Arche::World& World, const std::vector<Game::TerrainActorDescBinding>& TerrainActorDescBindings) {
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
            ActorDescs.push_back(BuildSceneTerrainActorDesc(BindingSource.mEntityId, TerrainActorDesc, ActorDescs.size()));
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
                SpawnInfo.mTerrainActorDesc.mTerrainWorldData = std::make_shared<const TerrainWorldData>(PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(SpawnInfo.mTerrainActorDesc, ActorDesc.mActorId));
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
}

namespace Game {
    Scene::Scene()
        : mName{},
        mWorld{},
        mPhysicsWorld{},
        mPhysicsRuntime{},
        mPhysicsRuntimeScene{},
        mPhysicsRuntimeScenes{},
        mPhysicsRuntimeSnapshot{},
        mKinematicSceneSimulator{},
        mTerrainDataRepository{},
        mKinematicRuntimeStates{},
        mPhysicsWorldVersion{ 1U },
        mRenderPhysicsDelaySeconds{ ResolveRenderPhysicsDelaySeconds() },
        mFrameContext{},
        mAssetRegistry{},
        mSystems{},
        mSystemSceduler{},
        mWorldSnapshot{},
        mWorldSnapshotVersion{},
        mHierarchyEntitySelectedSubscriptionId{},
        mFileDropSubscriptionId{},
        mIsDefaultCameraControlBehaviorAttached{},
        mIsDebugGeometryDrawEnabled{},
        mIsBoundingBoxDrawEnabled{},
        mIsPhysicsRuntimeModeEnabled{ ResolvePhysicsRuntimeModeEnabled() } {
        InitializePhysicsWorld();

        mWorldSnapshot.BindReadOnlyWorld(&mWorld.GetReadOnlyView());
        mWorldSnapshot.BindWorld(&mWorld);
        mWorldSnapshot.BindAssetRegistry(&mAssetRegistry);

		mLuaScriptFramework.Initialize(&mWorld);
        mLuaScriptFramework.SetFixedUpdateInterval(1.f); 
        mLuaScriptFramework.OpenDefaultLibraries(); 
        RegisterScriptTypes();
        mIsDebugGeometryDrawEnabled = Config::Query()->Get<bool>("Draw_DebugGeometry");

        mHierarchyEntitySelectedSubscriptionId = Core::Event::Subscribe<Game::HierarchyEntitySelectedEventTag>([this](const Core::Event::Event<Game::HierarchyEntitySelectedEventTag>& HierarchyEntitySelectedEvent) {
            const Game::HierarchyEntitySelectedPayload* Payload{ HierarchyEntitySelectedEvent.GetPayloadAs<Game::HierarchyEntitySelectedPayload>() };

            if (Payload == nullptr) {
                return;
            }

            mFrameContext.PickedEntityId = Payload->SelectedEntityId;

            Game::PickedEntityChangedPayload PickedEntityChangedPayload{};
            PickedEntityChangedPayload.PickedEntityId = Payload->SelectedEntityId;
            Core::Event::Enqueue<Game::PickedEntityChangedEventTag, Game::PickedEntityChangedPayload>(std::move(PickedEntityChangedPayload), true);
        });

        mFileDropSubscriptionId = Core::Event::Subscribe<Core::Event::FbxBinFileDroppedEventTag>([this](const Core::Event::Event<Core::Event::FbxBinFileDroppedEventTag>& DroppedFileEvent) {
            const Core::Event::FbxBinFileDroppedPayload* Payload{ DroppedFileEvent.GetPayloadAs<Core::Event::FbxBinFileDroppedPayload>() };

            if (Payload == nullptr) {
                return;
            }

            OnFileDropped(Payload->FilePath);
        });
    }

    Scene::~Scene() {
        ShutdownPhysicsRuntime();
    }

    Arche::World& Scene::GetWorld() {
        return mWorld;
    }

    const Arche::World& Scene::GetWorld() const {
        return mWorld;
    }

    FrameContext& Scene::GetFrameContext() {
        return mFrameContext;
    }

    const FrameContext& Scene::GetFrameContext() const {
        return mFrameContext;
    }

    RFD::RenderFrameData& Scene::GetRenderFrameData() {
        return mFrameContext.RenderData;
    }

    const RFD::RenderFrameData& Scene::GetRenderFrameData() const {
        return mFrameContext.RenderData;
    }

    void Scene::SetRenderFrameIndex(std::uint32_t RenderFrameIndex) {
        mFrameContext.RenderData.globals.frameIndex = RenderFrameIndex;
    }

    AssetRegistry& Scene::GetAssetRegistry() {
        return mAssetRegistry;
    }

    const AssetRegistry& Scene::GetAssetRegistry() const {
        return mAssetRegistry;
    }

    Script::LuaBehaviorFramework& Scene::GetLuaScriptFramework() {
        return mLuaScriptFramework;
    }

    const Script::LuaBehaviorFramework& Scene::GetLuaScriptFramework() const {
        return mLuaScriptFramework;
    }

    PhysicsWorld& Scene::GetPhysicsWorld() {
        return mPhysicsWorld;
    }

    const PhysicsWorld& Scene::GetPhysicsWorld() const {
        return mPhysicsWorld;
    }

    const PhysicsRuntime& Scene::GetPhysicsRuntime() const {
        return mPhysicsRuntime;
    }

    const PhysicsRuntimeScene& Scene::GetPhysicsRuntimeScene() const {
        return mPhysicsRuntimeScene;
    }

    std::uint32_t Scene::GetPhysicsWorldVersion() const {
        return mPhysicsWorldVersion;
    }

    bool Scene::IsPhysicsRuntimeModeEnabled() const {
        return mIsPhysicsRuntimeModeEnabled;
    }

    void Scene::InitializeAssetRegistry(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Core::DX::DescriptorHeap* SrvHeap) {
        mAssetRegistry.Initialize(Device, CopyQueue, Allocator);
        mAssetRegistry.SetSrvHeap(SrvHeap);
        mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
        mFrameContext.AssetRegistryResource = &mAssetRegistry;
        mFrameContext.RenderData.materials = mAssetRegistry.GetPackedMaterials();
        mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();

    }

    void Scene::InitializePhysicsWorld() {
        PhysicsWorld::WorldSettings Settings{ BuildDefaultPhysicsWorldSettings() };
        mPhysicsWorld.Initialize(Settings);
        mFrameContext.PhysicsWorldResource = &mPhysicsWorld;
        mFrameContext.PhysicsSnapshotResource = nullptr;
        mFrameContext.TerrainDataRepositoryResource = &mTerrainDataRepository;
        mFrameContext.IsPhysicsRuntimeModeEnabled = mIsPhysicsRuntimeModeEnabled;
        mTerrainDataRepository.Clear();

        for (TerrainActorDescBinding& Binding : mTerrainActorDescBindings) {
            Binding.mIsTerrainActorDescApplied = false;
        }
    }

    void Scene::InitializePhysicsRuntime() {
        ShutdownPhysicsRuntime();
        mFrameContext.IsPhysicsRuntimeModeEnabled = mIsPhysicsRuntimeModeEnabled;
        mFrameContext.PhysicsSnapshotResource = nullptr;
        mFrameContext.TerrainDataRepositoryResource = &mTerrainDataRepository;

        if (mIsPhysicsRuntimeModeEnabled == false) {
            mFrameContext.PhysicsWorldResource = &mPhysicsWorld;
            PublishPhysicsRuntimeStatus(nullptr);
            return;
        }

        mFrameContext.PhysicsWorldResource = &mPhysicsWorld;
        mPhysicsRuntimeScenes.clear();
        mPhysicsRuntimeScenes.push_back(mPhysicsRuntimeScene);

        PhysicsRuntime::RuntimeSettings RuntimeSettings{};
        RuntimeSettings.mWorldSettings = BuildDefaultPhysicsWorldSettings();
        RuntimeSettings.mMaxSubSteps = 4U;

        const bool IsRuntimeInitialized{ mPhysicsRuntime.Initialize(&mPhysicsRuntimeScenes, RuntimeSettings, 0U, mPhysicsWorldVersion) };
        if (IsRuntimeInitialized == false) {
            mIsPhysicsRuntimeModeEnabled = false;
            mFrameContext.IsPhysicsRuntimeModeEnabled = false;
            mFrameContext.PhysicsWorldResource = &mPhysicsWorld;
            PublishPhysicsRuntimeStatus(nullptr);
            return;
        }

        PublishPhysicsRuntimeStatus(nullptr);
    }

    void Scene::ShutdownPhysicsRuntime() {
        mPhysicsRuntime.Shutdown();
        mFrameContext.PhysicsSnapshotResource = nullptr;
    }

    void Scene::SubmitPhysicsRuntimeCommands() {
        if (mIsPhysicsRuntimeModeEnabled == false || mPhysicsRuntime.IsRunning() == false) {
            return;
        }

        for (auto [PhysicsActorComponent] : mWorld.Query<PhysicsActor>()) {
            PhysicsActorPendingCommands PendingCommands{};
            const bool HasPendingCommands{ TryConsumePhysicsActorPendingCommands(PhysicsActorComponent, PendingCommands) };
            if (HasPendingCommands == false || PendingCommands.mActorId == InvalidPhysicsActorId) {
                continue;
            }

            const ActorId RuntimeActorId{ static_cast<ActorId>(PendingCommands.mActorId) };
            if (PendingCommands.mHasSetVelocity == true && PhysicsActorComponent.mActorType == PhysicsActorBase::PhysicsActorType::Dynamic) {
                static_cast<void>(mPhysicsRuntime.EnqueueSetVelocity(RuntimeActorId, PendingCommands.mVelocity));
            }

            if (PendingCommands.mHasForce == true && PhysicsActorComponent.mActorType == PhysicsActorBase::PhysicsActorType::Dynamic) {
                static_cast<void>(mPhysicsRuntime.EnqueueAddForce(RuntimeActorId, PendingCommands.mForce));
            }

            if (PendingCommands.mHasImpulse == true && PhysicsActorComponent.mActorType == PhysicsActorBase::PhysicsActorType::Dynamic) {
                static_cast<void>(mPhysicsRuntime.EnqueueAddImpulse(RuntimeActorId, PendingCommands.mImpulse));
            }
        }

        for (TerrainActorDescBinding& BindingSource : mTerrainActorDescBindings) {
            PhysicsActor* PhysicsActorComponent{ mWorld.GetComponent<PhysicsActor>(BindingSource.mEntityId) };
            Transform* TransformComponent{ mWorld.GetComponent<Transform>(BindingSource.mEntityId) };
            if (PhysicsActorComponent == nullptr || TransformComponent == nullptr || IsValidTerrainActorDesc(BindingSource.mTerrainActorDesc) == false) {
                continue;
            }

            const std::uint32_t PhysicsActorId{ ResolvePhysicsActorId(*PhysicsActorComponent) };
            if (PhysicsActorId == InvalidPhysicsActorId) {
                continue;
            }

            const bool IsTransformChanged{ PhysicsActorComponent->mHasCachedSnapshot == false || std::make_tuple(PhysicsActorComponent->mCachedPosition, PhysicsActorComponent->mCachedOrientation, PhysicsActorComponent->mCachedScale) != std::make_tuple(TransformComponent->position, TransformComponent->rotation, TransformComponent->scale) };
            if (BindingSource.mIsTerrainActorDescApplied == true && IsTransformChanged == false) {
                continue;
            }

            const PhysicsTerrainActor* TerrainActorPointer{ mPhysicsWorld.GetTerrainActor(PhysicsActorId) };
            PhysicsTerrainActor::ActorDesc Desc{ BuildCurrentPhysicsTerrainActorDesc(BindingSource.mTerrainActorDesc, *TransformComponent, PhysicsActorId, TerrainActorPointer) };
            const std::shared_ptr<const PhysicsTerrainActor::ActorDesc> RuntimeTerrainActorDesc{ std::make_shared<const PhysicsTerrainActor::ActorDesc>(std::move(Desc)) };
            const bool IsCommandSubmitted{ mPhysicsRuntime.EnqueueSetTerrainActorDesc(static_cast<ActorId>(PhysicsActorId), RuntimeTerrainActorDesc) };
            if (IsCommandSubmitted == true) {
                BindingSource.mIsTerrainActorDescApplied = true;
            }
        }
    }

    void Scene::PublishPhysicsRuntimeKinematicStates() {
        mKinematicRuntimeStates.clear();

        if (mIsPhysicsRuntimeModeEnabled == false || mPhysicsRuntime.IsRunning() == false) {
            return;
        }

        for (auto [PhysicsActorComponent, TransformComponent] : mWorld.Query<PhysicsActor, Transform>()) {
            if (PhysicsActorComponent.mActorType != PhysicsActorBase::PhysicsActorType::Kinematic) {
                continue;
            }

            const std::uint32_t PhysicsActorId{ ResolvePhysicsActorId(PhysicsActorComponent) };
            if (PhysicsActorId == InvalidPhysicsActorId) {
                continue;
            }

            PhysicsKinematicRuntimeState KinematicState{};
            KinematicState.mActorId = static_cast<ActorId>(PhysicsActorId);
            KinematicState.mPosition = TransformComponent.position;
            KinematicState.mOrientation = TransformComponent.rotation;
            KinematicState.mScale = TransformComponent.scale;
            KinematicState.mVelocity = PhysicsActorComponent.mCachedVelocity;
            KinematicState.mIsActive = true;

            PhysicsActorBase* ActorPointer{ PhysicsActorComponent.mActorPointer };
            if (ActorPointer == nullptr) {
                ActorPointer = mPhysicsWorld.GetActor(static_cast<std::size_t>(PhysicsActorId));
            }

            if (ActorPointer != nullptr && ActorPointer->GetActorType() == PhysicsActorBase::PhysicsActorType::Kinematic) {
                KinematicState.mVelocity = ActorPointer->GetVelocity();
                KinematicState.mIsActive = ActorPointer->GetIsActive();
            }

            mKinematicRuntimeStates.push_back(KinematicState);
        }

        mPhysicsRuntime.PublishKinematicStates(mKinematicRuntimeStates);
    }

    void Scene::RefreshPhysicsRuntimeSnapshot() {
        mFrameContext.PhysicsSnapshotResource = nullptr;
        if (mIsPhysicsRuntimeModeEnabled == false || mPhysicsRuntime.IsRunning() == false || mPhysicsRuntime.PublishedSnapshotCount() == 0U) {
            PublishPhysicsRuntimeStatus(nullptr);
            return;
        }

        const double LatestSimulationTimeSeconds{ mPhysicsRuntime.LatestSimulationTimeSeconds() };
        const double RenderPhysicsTimeSeconds{ std::max(0.0, LatestSimulationTimeSeconds - mRenderPhysicsDelaySeconds) };

        PhysicsSnapshot PreviousSnapshot{};
        PhysicsSnapshot NextSnapshot{};
        float SnapshotAlpha{};
        bool HasSnapshot{ mPhysicsRuntime.TryGetSnapshotPairForTime(RenderPhysicsTimeSeconds, PreviousSnapshot, NextSnapshot, SnapshotAlpha) };
        if (HasSnapshot == true) {
            (void)NextSnapshot;
            (void)SnapshotAlpha;
            mPhysicsRuntimeSnapshot = std::move(PreviousSnapshot);
        }
        else {
            const std::uint32_t SnapshotIndex{ mPhysicsRuntime.GetReadableSnapshotIndex() };
            mPhysicsRuntimeSnapshot = mPhysicsRuntime.GetSnapshot(SnapshotIndex);
            HasSnapshot = mPhysicsRuntimeSnapshot.mPublishIndex != 0U;
        }

        if (HasSnapshot == false || mPhysicsRuntimeSnapshot.mWorldVersion != mPhysicsWorldVersion) {
            PublishPhysicsRuntimeStatus(nullptr);
            return;
        }

        mFrameContext.PhysicsSnapshotResource = &mPhysicsRuntimeSnapshot;
        PublishPhysicsRuntimeStatus(&mPhysicsRuntimeSnapshot);
    }

    void Scene::PublishPhysicsRuntimeStatus(const PhysicsSnapshot* Snapshot) {
        PhysicsRuntimeStatus Status{};
        Status.mIsRuntimeModeEnabled = mIsPhysicsRuntimeModeEnabled;
        Status.mIsRunning = mIsPhysicsRuntimeModeEnabled == true && mPhysicsRuntime.IsRunning() == true;

        if (mIsPhysicsRuntimeModeEnabled == true) {
            Status.mLatestStepIndex = mPhysicsRuntime.LatestStepIndex();
        }

        if (Snapshot != nullptr) {
            Status.mSnapshotStepIndex = Snapshot->mStepIndex;
            Status.mActorCount = Snapshot->mActorCount;
            const double LatestSimulationTimeSeconds{ mPhysicsRuntime.LatestSimulationTimeSeconds() };
            Status.mSnapshotAgeMilliseconds = std::max(0.0, LatestSimulationTimeSeconds - Snapshot->mSimulationTimeSeconds) * 1000.0;
        }
        else if (mIsPhysicsRuntimeModeEnabled == false) {
            Status.mActorCount = mPhysicsWorld.GetActorCount();
        }

        mFrameContext.PhysicsRuntimeStatus = Status;
        mWorldSnapshot.SetPhysicsRuntimeStatus(Status);
    }

    void Scene::UpdateTerrainDataRepository() {
        std::vector<TerrainWorldData> TerrainDataItems{};
        TerrainDataItems.reserve(mTerrainActorDescBindings.size());

        for (const TerrainActorDescBinding& BindingSource : mTerrainActorDescBindings) {
            const PhysicsActor* PhysicsActorComponent{ mWorld.GetComponent<PhysicsActor>(BindingSource.mEntityId) };
            const Transform* TransformComponent{ mWorld.GetComponent<Transform>(BindingSource.mEntityId) };
            if (PhysicsActorComponent == nullptr || TransformComponent == nullptr || IsValidTerrainActorDesc(BindingSource.mTerrainActorDesc) == false) {
                continue;
            }

            const std::uint32_t PhysicsActorId{ ResolvePhysicsActorId(*PhysicsActorComponent) };
            if (PhysicsActorId == InvalidPhysicsActorId) {
                continue;
            }

            const PhysicsTerrainActor* TerrainActorPointer{ mPhysicsWorld.GetTerrainActor(PhysicsActorId) };
            PhysicsTerrainActor::ActorDesc TerrainActorDesc{ BuildCurrentPhysicsTerrainActorDesc(BindingSource.mTerrainActorDesc, *TransformComponent, PhysicsActorId, TerrainActorPointer) };
            TerrainWorldData TerrainData{ PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(TerrainActorDesc, PhysicsActorId) };
            if (IsTerrainWorldDataValid(TerrainData) == false) {
                continue;
            }

            TerrainDataItems.push_back(std::move(TerrainData));
        }

        mTerrainDataRepository.PublishSnapshot(std::move(TerrainDataItems));
    }

    void Scene::UpdateSceneKinematicActors(float Dt) {
        for (auto [PhysicsActorComponent, TransformComponent, BoundingBoxComponent] : mWorld.Query<PhysicsActor, Transform, BoundingBox>()) {
            if (PhysicsActorComponent.mActorType != PhysicsActorBase::PhysicsActorType::Kinematic) {
                continue;
            }

            PhysicsActorBase* ActorPointer{ PhysicsActorComponent.mActorPointer };
            const std::uint32_t PhysicsActorId{ ResolvePhysicsActorId(PhysicsActorComponent) };
            if (ActorPointer == nullptr && PhysicsActorId != InvalidPhysicsActorId) {
                ActorPointer = mPhysicsWorld.GetActor(static_cast<std::size_t>(PhysicsActorId));
                if (ActorPointer != nullptr && ActorPointer->GetActorType() == PhysicsActorBase::PhysicsActorType::Kinematic) {
                    PhysicsActorComponent.mActorPointer = ActorPointer;
                }
            }

            if (ActorPointer == nullptr || ActorPointer->GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
                continue;
            }

            SetActorTransformFromComponent(*ActorPointer, TransformComponent);

            const DirectX::BoundingOrientedBox& ComponentLocalBoundingBox{ BoundingBoxComponent.GetObb() };
            const DirectX::BoundingOrientedBox& ActorLocalBoundingBox{ ActorPointer->GetLocalBoundingBox() };
            const std::tuple<DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Quaternion> ActorLocalBoundingBoxValue{ DirectX::SimpleMath::Vector3{ ActorLocalBoundingBox.Center }, DirectX::SimpleMath::Vector3{ ActorLocalBoundingBox.Extents }, DirectX::SimpleMath::Quaternion{ ActorLocalBoundingBox.Orientation } };
            const std::tuple<DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Quaternion> ComponentLocalBoundingBoxValue{ DirectX::SimpleMath::Vector3{ ComponentLocalBoundingBox.Center }, DirectX::SimpleMath::Vector3{ ComponentLocalBoundingBox.Extents }, DirectX::SimpleMath::Quaternion{ ComponentLocalBoundingBox.Orientation } };
            if (ActorLocalBoundingBoxValue != ComponentLocalBoundingBoxValue) {
                ActorPointer->SetLocalBoundingBox(ComponentLocalBoundingBox);
            }
        }

        mKinematicSceneSimulator.Tick(mPhysicsWorld.GetActorRepository(), mTerrainDataRepository, mPhysicsWorld.GetGravity(), Dt);

        for (auto [PhysicsActorComponent, TransformComponent, BoundingBoxComponent] : mWorld.Query<PhysicsActor, Transform, BoundingBox>()) {
            if (PhysicsActorComponent.mActorType != PhysicsActorBase::PhysicsActorType::Kinematic) {
                continue;
            }

            PhysicsActorBase* ActorPointer{ PhysicsActorComponent.mActorPointer };
            if (ActorPointer == nullptr || ActorPointer->GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
                continue;
            }

            TransformComponent.position = ActorPointer->GetPosition();
            TransformComponent.rotation = ActorPointer->GetOrientation();
            TransformComponent.scale = ActorPointer->GetScale();
            TransformComponent.UpdateEulerRadiansFromRotation();
            BoundingBoxComponent.SetWorldObb(ActorPointer->GetWorldBoundingBox());
            UpdatePhysicsActorCachedSnapshot(PhysicsActorComponent, TransformComponent.position, TransformComponent.rotation, TransformComponent.scale, ActorPointer->GetVelocity(), ActorPointer->GetWorldBoundingBox());
        }
    }

    void Scene::RebuildPhysicsActors() {
        ShutdownPhysicsRuntime();
        InitializePhysicsWorld();

        const std::vector<ScenePhysicsActorDesc> ActorDescs{ CollectScenePhysicsActorDescs(mWorld, mTerrainActorDescBindings) };
        BuildPhysicsRuntimeSceneFromActorDescs(ActorDescs, mPhysicsRuntimeScene);
        std::vector<PendingPhysicsActorBinding> PendingBindings{ CreatePhysicsWorldActors(mPhysicsWorld, ActorDescs) };

        for (const PendingPhysicsActorBinding& Binding : PendingBindings) {
            AttachPhysicsActorComponent(mWorld, Binding, mIsPhysicsRuntimeModeEnabled);
        }

        UpdateTerrainDataRepository();
        mPhysicsWorldVersion = AdvancePhysicsWorldVersion(mPhysicsWorldVersion);
        InitializePhysicsRuntime();
    }

    void Scene::UpdatePhysics(float Dt) {
        mFrameContext.PhysicsWorldResource = &mPhysicsWorld;
        mFrameContext.TerrainDataRepositoryResource = &mTerrainDataRepository;
        UpdateTerrainDataRepository();
        UpdateSceneKinematicActors(Dt);

        if (mIsPhysicsRuntimeModeEnabled == true) {
            SubmitPhysicsRuntimeCommands();
            PublishPhysicsRuntimeKinematicStates();
            RefreshPhysicsRuntimeSnapshot();
            return;
        }

        mFrameContext.PhysicsSnapshotResource = nullptr;
        mFrameContext.IsPhysicsRuntimeModeEnabled = false;

        for (TerrainActorDescBinding& BindingSource : mTerrainActorDescBindings) {
            PhysicsActor* PhysicsActorComponent{ mWorld.GetComponent<PhysicsActor>(BindingSource.mEntityId) };
            Transform* TransformComponent{ mWorld.GetComponent<Transform>(BindingSource.mEntityId) };
            if (PhysicsActorComponent == nullptr || TransformComponent == nullptr || IsValidTerrainActorDesc(BindingSource.mTerrainActorDesc) == false) {
                continue;
            }

            PhysicsTerrainActor* TerrainActorPointer{ mPhysicsWorld.GetTerrainActor(ResolvePhysicsActorId(*PhysicsActorComponent)) };
            if (TerrainActorPointer == nullptr) {
                continue;
            }

            if (BindingSource.mIsTerrainActorDescApplied == false) {
                const std::uint32_t PhysicsActorId{ ResolvePhysicsActorId(*PhysicsActorComponent) };
                PhysicsTerrainActor::ActorDesc Desc{ BuildCurrentPhysicsTerrainActorDesc(BindingSource.mTerrainActorDesc, *TransformComponent, PhysicsActorId, TerrainActorPointer) };
                TerrainActorPointer->SetActorDesc(Desc);
                BindingSource.mIsTerrainActorDescApplied = true;
                continue;
            }

            const bool IsTransformChanged{ std::make_tuple(TerrainActorPointer->GetPosition(), TerrainActorPointer->GetRotation(), TerrainActorPointer->GetScale()) != std::make_tuple(TransformComponent->position, TransformComponent->rotationEuler, TransformComponent->scale) };
            if (IsTransformChanged == true) {
                const std::uint32_t PhysicsActorId{ ResolvePhysicsActorId(*PhysicsActorComponent) };
                PhysicsTerrainActor::ActorDesc Desc{ BuildCurrentPhysicsTerrainActorDesc(BindingSource.mTerrainActorDesc, *TransformComponent, PhysicsActorId, TerrainActorPointer) };
                TerrainActorPointer->SetActorDesc(Desc);
            }
        }

        for (auto [PhysicsActorComponent] : mWorld.Query<PhysicsActor>()) {
            PhysicsActorPendingCommands PendingCommands{};
            static_cast<void>(TryConsumePhysicsActorPendingCommands(PhysicsActorComponent, PendingCommands));
        }

        mPhysicsWorld.TickKinematicActors(Dt);
        mPhysicsWorld.Update(Dt);
        PublishPhysicsRuntimeStatus(nullptr);
    }

    void Scene::SetName(const std::string& NewName) {
        mName = NewName;
        mWorldSnapshot.SetSceneName(mName);
    }

    const std::string& Scene::GetName() const {
        return mName;
    }

    void Scene::AddSystem(std::unique_ptr<ISystem> NewSystem) {
        if (NewSystem == nullptr) {
            return;
        }

        mSystems.push_back(std::move(NewSystem));
    }

    void Scene::BuildSystemExecutionPlan() {
        mSystemSceduler.BuildExecutionPlan(mSystems);
    }

    void Scene::PrepareRender() {
        mAssetRegistry.PrepareRenderTextures(mFrameContext.RenderData);
        mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();
    }

    void Scene::OnFileDropped(const std::filesystem::path& FilePath) {
        const std::wstring ExtensionTextWide{ FilePath.extension().wstring() };
        const std::string ExtensionText{ ConvertWstringToUtf8(ExtensionTextWide) };
        std::string LowerExtensionText{ ExtensionText };
        std::transform(LowerExtensionText.begin(), LowerExtensionText.end(), LowerExtensionText.begin(), [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });

        if (LowerExtensionText != ".bin") {
            return;
        }

        const std::string ModelPath{ FilePath.generic_string() };
        const std::string RootEntityName{ ConvertWstringToUtf8(FilePath.stem().wstring()) };
        const std::uint32_t MaterialGroupIndex{ 0 };
        SpawnModelAtOrigin(ModelPath, RootEntityName, MaterialGroupIndex, false);
    }

    void Scene::SpawnModelAtOrigin(const std::string& ModelSelector, const std::string& RootEntityName, std::uint32_t MaterialGroupIndex, bool IsDerivedEntity) {
        const std::shared_ptr<Model> ModelData{ mAssetRegistry.GetModel(ModelSelector) };
        if (ModelData == nullptr) {
            return;
        }

        SceneEntityFactory EntityFactory{ *this };
        ModelHierarchySpawnRequest SpawnRequest{};
        SpawnRequest.ModelData = ModelData;
        SpawnRequest.RootEntityName = RootEntityName;
        SpawnRequest.MaterialGroupIndex = MaterialGroupIndex;
        SpawnRequest.IsActive = true;
        SpawnRequest.IsDerivedEntity = IsDerivedEntity;

        std::vector<Arche::EntityID> SpawnedEntities{};
        const bool IsSpawned{ EntityFactory.SpawnModelHierarchy(SpawnRequest, &SpawnedEntities) };
        if (IsSpawned == false || SpawnedEntities.empty() == true) {
            return;
        }

        Arche::EntityID RootEntityId{ Arche::NullEntityID };
        for (Arche::EntityID SpawnedEntityId : SpawnedEntities) {
            const EntityHierarchy* Hierarchy{ std::as_const(mWorld).GetComponent<EntityHierarchy>(SpawnedEntityId) };
            if (Hierarchy != nullptr && Hierarchy->parent == Arche::NullEntityID) {
                RootEntityId = SpawnedEntityId;
                break;
            }
        }

        if (RootEntityId == Arche::NullEntityID) {
            return;
        }

        PrefabInstance RootPrefabInstance{};
        RootPrefabInstance.PrefabId = GenerateNextPrefabId(*this);
        mWorld.AddComponent(RootEntityId, RootPrefabInstance);
    }

    void Scene::RegisterScriptTypes() {
        mLuaScriptFramework.RegisterGlobalFunction("IsInputKeyDown", &Globals::IsInputKeyDown);
        mLuaScriptFramework.RegisterGlobalFunction("IsInputKeyPressed", &Globals::IsInputKeyPressed);
        mLuaScriptFramework.RegisterGlobalFunction("IsInputKeyReleased", &Globals::IsInputKeyReleased);
        mLuaScriptFramework.RegisterGlobalFunction("GetInputMousePositionX", &Globals::GetInputMousePositionX);
        mLuaScriptFramework.RegisterGlobalFunction("GetInputMousePositionY", &Globals::GetInputMousePositionY);
        mLuaScriptFramework.RegisterGlobalFunction("GetInputMouseDeltaX", &Globals::GetInputMouseDeltaX);
        mLuaScriptFramework.RegisterGlobalFunction("GetInputMouseDeltaY", &Globals::GetInputMouseDeltaY);
        mLuaScriptFramework.RegisterGlobalFunction("IsInputMouseLeftButtonDown", &Globals::IsInputMouseLeftButtonDown);
        mLuaScriptFramework.RegisterGlobalFunction("IsInputMouseRightButtonDown", &Globals::IsInputMouseRightButtonDown);
        mLuaScriptFramework.RegisterGlobalFunction("IsInputMouseMiddleButtonDown", &Globals::IsInputMouseMiddleButtonDown);
        mLuaScriptFramework.RegisterGlobalFunction("GetInputMouseWheelDelta", &Globals::GetInputMouseWheelDelta);
        mLuaScriptFramework.RegisterGlobalFunction("GetActiveCameraForwardDirection", [this]() -> DirectX::SimpleMath::Vector3 {
            for (auto [TransformComponent, CameraComponent] : mWorld.Query<Transform, Camera>()) {
                if (CameraComponent.isActive == false) {
                    continue;
                }

                return TransformComponent.GetForwardDirection();
            }

            return DirectX::SimpleMath::Vector3::Forward;
        });
        mLuaScriptFramework.RegisterGlobalFunction("GetActiveCameraRightDirection", [this]() -> DirectX::SimpleMath::Vector3 {
            for (auto [TransformComponent, CameraComponent] : mWorld.Query<Transform, Camera>()) {
                if (CameraComponent.isActive == false) {
                    continue;
                }

                return TransformComponent.TransformDirectionToWorld(DirectX::SimpleMath::Vector3::Right);
            }

            return DirectX::SimpleMath::Vector3::Right;
        });
        mLuaScriptFramework.RegisterGlobalFunction("GetActiveCameraFlags", [this]() -> std::uint32_t {
            for (auto [CameraComponent] : mWorld.Query<Camera>()) {
                if (CameraComponent.isActive == false) {
                    continue;
                }

                return CameraComponent.cameraFlags;
            }

            return CameraFlagNone;
        });
        mLuaScriptFramework.RegisterGlobalFunction("RaycastTerrainDistance", [this](const DirectX::SimpleMath::Vector3& RayStartPoint, const DirectX::SimpleMath::Vector3& RayDirection, const float RayLength) -> float {
            if (MathUtility::IsFiniteVector3(RayStartPoint) == false || MathUtility::IsFiniteVector3(RayDirection) == false || MathUtility::IsFiniteFloat(RayLength) == false || RayLength <= 0.0f) {
                return -1.0f;
            }

            const DirectX::SimpleMath::Ray Ray{ RayStartPoint, RayDirection };
            DirectX::SimpleMath::Vector3 HitPosition{};
            DirectX::SimpleMath::Vector3 HitNormal{ DirectX::SimpleMath::Vector3::Up };
            float HitDistance{};
            const bool HasHit{ mTerrainDataRepository.TryRaycast(Ray, RayLength, HitPosition, HitNormal, HitDistance) };
            return HasHit == true ? HitDistance : -1.0f;
        });

        mLuaScriptFramework.RegisterTypeByDefinition<Arche::EntityID>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Vector2>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Vector3>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Quaternion>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Matrix>();
        mLuaScriptFramework.RegisterTypeUsertype<Game::ComponentTextArray>("Text", sol::constructors<Game::ComponentTextArray()>(), sol::meta_function::index, [](const Game::ComponentTextArray& TargetArray, std::size_t LuaIndex) -> Game::ComponentTextArray::value_type { if (LuaIndex >= TargetArray.size()) { return '\0'; } return TargetArray[LuaIndex]; }, sol::meta_function::new_index, [](Game::ComponentTextArray& TargetArray, std::size_t LuaIndex, const Game::ComponentTextArray::value_type Value) { if (LuaIndex >= TargetArray.size()) { return; } TargetArray[LuaIndex] = Value; }, "Get", [](const Game::ComponentTextArray& TargetArray, const std::size_t Index) -> Game::ComponentTextArray::value_type { if (Index >= TargetArray.size()) { return '\0'; } return TargetArray[Index]; }, "Set", [](Game::ComponentTextArray& TargetArray, const std::size_t Index, const Game::ComponentTextArray::value_type Value) { if (Index >= TargetArray.size()) { return; } TargetArray[Index] = Value; }, "Size", [](const Game::ComponentTextArray& TargetArray) -> std::size_t { return TargetArray.size(); });
        mLuaScriptFramework.RegisterTypeByDefinition<Game::RuntimeVariableBoolArray>();
        mLuaScriptFramework.RegisterTypeByDefinition<Game::RuntimeVariableIntArray>();
        mLuaScriptFramework.RegisterTypeByDefinition<Game::RuntimeVariableFloatArray>();




        mLuaScriptFramework.RegisterComponentByDefinition<Game::Material>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Name>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Transform>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::EntityHierarchy>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::StaticMeshRenderer>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::TerrainRenderer>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::SkinnedMeshRenderer>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Culling>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::BoundingBox>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Bone>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::BoneSkinReference>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Camera>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::DirectionalLight>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Frustum>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::SkySphere>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::RuntimeVariableTable>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Animator>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::AnimatorGraphPlayer>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::FootIKRig>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::FootIKRuntime>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::PrefabInstance>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::PhysicsActorSettings>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::PhysicsActor>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Tag>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::BehaviorInstanceComponent>();
    }

    void Scene::AddTerrainActorDesc(Arche::EntityID EntityId, const PhysicsTerrainActor::ActorDesc& TerrainActorDesc) {
        PhysicsTerrainActor::ActorDesc StoredTerrainActorDesc{ TerrainActorDesc };
        StoredTerrainActorDesc.mTerrainWorldData = std::make_shared<const TerrainWorldData>(PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(StoredTerrainActorDesc, 0U));
        for (TerrainActorDescBinding& Binding : mTerrainActorDescBindings) {
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
        mTerrainActorDescBindings.push_back(NewBinding);
    }

    void Scene::ClearTerrainActorDescs() {
        mTerrainActorDescBindings.clear();
    }

    void Scene::InitializeWorldSnapshot() {
        mWorldSnapshot.BindReadOnlyWorld(&mWorld.GetReadOnlyView());
        mWorldSnapshot.BindWorld(&mWorld);
        mWorldSnapshot.BindAssetRegistry(&mAssetRegistry);
        RebuildWorldSnapshot();
        mWorldSnapshotVersion = mWorld.GetStructureVersion();
    }

    void Scene::UpdateWorldSnapshotIfNeeded() {
        const std::uint64_t CurrentStructureVersion{ mWorld.GetStructureVersion() };

        if (CurrentStructureVersion == mWorldSnapshotVersion) {
            return;
        }

        RebuildWorldSnapshot();
        mWorldSnapshotVersion = CurrentStructureVersion;
    }

    const SceneWorldSnapshot& Scene::GetWorldSnapshot() const {
        return mWorldSnapshot;
    }

    void Scene::RebuildWorldSnapshot() {
        mWorldSnapshot.Clear();
        mWorldSnapshot.SetSceneName(mName);

        for (const std::unique_ptr<ISystem>& SystemInstance : mSystems) {
            if (SystemInstance == nullptr) {
                continue;
            }

            mWorldSnapshot.AddSystemName(SystemInstance->Name());
        }

        for (const auto& [NameComponent, HierarchyComponent] : mWorld.Query<Name, EntityHierarchy>()) {
            if (GetNameText(NameComponent)[0] == '\0') {
                continue;
            }

            mWorldSnapshot.AddEntity(HierarchyComponent.self, HierarchyComponent.parent);
        }

        mWorldSnapshot.BuildHierarchy();
    }

    void Scene::UpdateCameraVirtualMouseState() {
        Globals::Input& InputInstance{ Globals::Input::Get() };
        const bool IsDebugGeometryToggleRequested{ InputInstance.IsKeyPressed(DirectX::Keyboard::Keys::F3) };
        const bool IsThirdPersonToggleRequested{ InputInstance.IsKeyPressed(DirectX::Keyboard::Keys::F8) };
        const bool IsBoundingBoxToggleRequested{ InputInstance.IsKeyPressed(DirectX::Keyboard::Keys::F9) };
        const DirectX::Mouse::ButtonStateTracker& MouseTracker{ InputInstance.GetMouseTracker() };
        const bool IsSelectionDragInput{ mFrameContext.PickedEntityId != Arche::NullEntityID && (MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::PRESSED || MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::HELD) };

        if (IsDebugGeometryToggleRequested) {
            mIsDebugGeometryDrawEnabled = (mIsDebugGeometryDrawEnabled == false);
        }

        if (IsBoundingBoxToggleRequested) {
            mIsBoundingBoxDrawEnabled = (mIsBoundingBoxDrawEnabled == false);
        }

        bool IsUiCapturingInput{ false };
        if (Config::Query()->Get<bool>("Block_ImGui") == false) {
            const ImGuiIO& ImGuiInputState{ ImGui::GetIO() };
            const bool IsUiCapturingMouseInput{ ImGuiInputState.WantCaptureMouse };
            const bool IsUiCapturingKeyboardInput{ ImGuiInputState.WantCaptureKeyboard };
            IsUiCapturingInput = IsUiCapturingMouseInput || IsUiCapturingKeyboardInput;
        }

        for (auto [CameraComponent] : mWorld.Query<Camera>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            if (IsThirdPersonToggleRequested) {
                const bool IsThirdPersonMode{ (CameraComponent.cameraFlags & CameraFlagThirdPerson) != 0u };
                if (IsThirdPersonMode) {
                    CameraComponent.cameraFlags &= ~CameraFlagThirdPerson;
                    CameraComponent.cameraFlags |= CameraFlagFreeLook;
                    InputInstance.SetRightButtonVirtualMouseEnabled(true);
                    InputInstance.SetVirtualMouse(false);
                }
                else {
                    CameraComponent.cameraFlags |= CameraFlagThirdPerson;
                    CameraComponent.cameraFlags &= ~CameraFlagFreeLook;
                    InputInstance.SetRightButtonVirtualMouseEnabled(false);
                    InputInstance.SetVirtualMouse(true);
                }
            }

            if (IsSelectionDragInput || IsUiCapturingInput) {
                return;
            }

            const bool IsThirdPersonMode{ (CameraComponent.cameraFlags & CameraFlagThirdPerson) != 0u };
            if (IsThirdPersonMode) {
                InputInstance.SetRightButtonVirtualMouseEnabled(false);
                InputInstance.SetVirtualMouse(true);
            }
            else {
                InputInstance.SetRightButtonVirtualMouseEnabled(true);
            }

            return;
        }
    }

    void Scene::AttachDefaultCameraControlBehavior() {
        if (mIsDefaultCameraControlBehaviorAttached) {
            return;
        }

        for (const auto [CameraComponent, HierarchyComponent] : mWorld.Query<Camera, EntityHierarchy>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            const Arche::EntityID TargetCameraEntity{ HierarchyComponent.self };
            const BehaviorInstanceComponent* ExistingBehaviorComponent{ std::as_const(mWorld).GetComponent<BehaviorInstanceComponent>(TargetCameraEntity) };
            if (ExistingBehaviorComponent != nullptr) {
                mIsDefaultCameraControlBehaviorAttached = true;
                return;
            }

            const Script::LuaBehaviorFramework::BehaviorOperationResult AttachResult{ mLuaScriptFramework.AttachBehaviorFromFile(TargetCameraEntity, "Script/Lua/CameraController.lua") };
            if (AttachResult) {
                mIsDefaultCameraControlBehaviorAttached = true;
            }

            return;
        }
    }

    void Scene::AppendDebugWorldAxes() {
        constexpr float AxisLength{ 100000.0f };
        constexpr float AxisThickness{ 0.0035f };
        const SimpleMath::Vector3 Origin{ 0.0f, 0.0f, 0.0f };
        mFrameContext.RenderData.debugGeometryContexts.push_back(RFD::DebugGeometryContext::CreateDirection(Origin, SimpleMath::Vector3{ 1.0f, 0.0f, 0.0f }, AxisLength, SimpleMath::Vector4{ 1.0f, 0.1f, 0.1f, 1.0f }, AxisThickness));
        mFrameContext.RenderData.debugGeometryContexts.push_back(RFD::DebugGeometryContext::CreateDirection(Origin, SimpleMath::Vector3{ 0.0f, 1.0f, 0.0f }, AxisLength, SimpleMath::Vector4{ 0.1f, 1.0f, 0.1f, 1.0f }, AxisThickness));
        mFrameContext.RenderData.debugGeometryContexts.push_back(RFD::DebugGeometryContext::CreateDirection(Origin, SimpleMath::Vector3{ 0.0f, 0.0f, 1.0f }, AxisLength, SimpleMath::Vector4{ 0.1f, 0.4f, 1.0f, 1.0f }, AxisThickness));
    }

    void Scene::ExecutePhase(Phase TargetPhase, float Dt) {
        switch (TargetPhase) {
            case Phase::PreUpdate:
                UpdateCameraVirtualMouseState();
                mFrameContext.RenderData.modelContexts.clear();
                mFrameContext.RenderData.boundingBoxContexts.clear();
                mFrameContext.RenderData.debugGeometryContexts.clear();
                mFrameContext.RenderData.TerrainPatchContexts.clear();
                mFrameContext.RenderData.drawRecords.clear();
                mFrameContext.RenderData.bonePalette.clear();
                mFrameContext.RenderData.mTerrainUploadFutures.clear();
                for (RFD::ShadowRenderContext& ShadowRenderContext : mFrameContext.RenderData.ShadowRenderContexts) {
                    ShadowRenderContext.ModelContexts.clear();
                    ShadowRenderContext.TerrainPatchContexts.clear();
                    ShadowRenderContext.DrawRecords.clear();
                }

                mFrameContext.RenderData.materials = mAssetRegistry.GetPackedMaterials();
                mFrameContext.RenderData.globals.flags = 0u;
                if (mIsBoundingBoxDrawEnabled) {
                    mFrameContext.RenderData.globals.flags |= RFD::FrameGlobalFlagDrawBoundingBoxes;
                }

                if (mIsDebugGeometryDrawEnabled) {
                    mFrameContext.RenderData.globals.flags |= RFD::FrameGlobalFlagDrawDebugGeometry;
                }

                AppendDebugWorldAxes();
                mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();
                mFrameContext.SkinnedMeshPreparedDataItems.clear();
                break;

            case Phase::Update:
                AttachDefaultCameraControlBehavior();
                mLuaScriptFramework.Update(Dt);
                break;

            default:
                break;
        }

        const SystemSceduler::PhaseBatchArray* PhaseBatches{ mSystemSceduler.GetPhaseBatches(TargetPhase) };
        if (PhaseBatches == nullptr) {
            switch (TargetPhase) {
                case Phase::Update:
                    mLuaScriptFramework.LateUpdate(Dt);
                    break;

                default:
                    break;
            }

            return;
        }

        for (const SystemSceduler::SystemBatch& Batch : *PhaseBatches) {
            for (ISystem* System : Batch) {
                System->Execute(mWorld, mFrameContext, Dt);
            }
        }

        switch (TargetPhase) {
            case Phase::Update:
                mLuaScriptFramework.LateUpdate(Dt);
                break;

            case Phase::PostUpdate:
                UpdatePhysics(Dt);
                break;

            default:
                break;
        }
    }
}
