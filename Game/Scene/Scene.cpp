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

    struct PendingPhysicsActorBinding final {
        Arche::EntityID mEntityId{ Arche::NullEntityID };
        PhysicsActorBase* mActorPointer{};
        std::uint32_t mActorIndex{};
        PhysicsActorBase::PhysicsActorType mActorType{ PhysicsActorBase::PhysicsActorType::Dynamic };
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
        return Desc;
    }

    bool IsValidTerrainActorDesc(const PhysicsTerrainActor::ActorDesc& TerrainActorDesc) {
        const std::size_t ExpectedHeightValueCount{ static_cast<std::size_t>(TerrainActorDesc.HeightFieldWidth) * static_cast<std::size_t>(TerrainActorDesc.HeightFieldHeight) };
        return TerrainActorDesc.HeightFieldWidth > 1u && TerrainActorDesc.HeightFieldHeight > 1u && TerrainActorDesc.HeightFieldCellSizeX > 0.0f && TerrainActorDesc.HeightFieldCellSizeZ > 0.0f && TerrainActorDesc.HeightFieldMaxHeight > 0.0f && TerrainActorDesc.HeightFieldValues.size() == ExpectedHeightValueCount;
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

    void AttachPhysicsActorComponent(Arche::World& World, const PendingPhysicsActorBinding& Binding) {
        Game::PhysicsActor* ExistingPhysicsActorComponent{ World.GetComponent<Game::PhysicsActor>(Binding.mEntityId) };
        if (ExistingPhysicsActorComponent != nullptr) {
            ExistingPhysicsActorComponent->mActorPointer = Binding.mActorPointer;
            ExistingPhysicsActorComponent->mActorIndex = Binding.mActorIndex;
            ExistingPhysicsActorComponent->mActorType = Binding.mActorType;
            return;
        }

        Game::PhysicsActor NewPhysicsActorComponent{};
        NewPhysicsActorComponent.mActorPointer = Binding.mActorPointer;
        NewPhysicsActorComponent.mActorIndex = Binding.mActorIndex;
        NewPhysicsActorComponent.mActorType = Binding.mActorType;
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
        mIsBoundingBoxDrawEnabled{} {
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

    void Scene::InitializeAssetRegistry(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Core::DX::DescriptorHeap* SrvHeap) {
        mAssetRegistry.Initialize(Device, CopyQueue, Allocator);
        mAssetRegistry.SetSrvHeap(SrvHeap);
        mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
        mFrameContext.AssetRegistryResource = &mAssetRegistry;
        mFrameContext.RenderData.materials = mAssetRegistry.GetPackedMaterials();
        mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();

    }

    void Scene::InitializePhysicsWorld() {
        PhysicsWorld::WorldSettings Settings{};
        Settings.FixedTimeStep = 1.0f / 60.0f;
        Settings.Gravity = DirectX::SimpleMath::Vector3{ 0.0f, -9.8f, 0.0f };
        mPhysicsWorld.Initialize(Settings);
        mFrameContext.PhysicsWorldResource = &mPhysicsWorld;

        for (TerrainActorDescBinding& Binding : mTerrainActorDescBindings) {
            Binding.mIsTerrainActorDescApplied = false;
        }
    }

    void Scene::RebuildPhysicsActors() {
        InitializePhysicsWorld();

        std::vector<PendingPhysicsActorBinding> PendingBindings{};
        std::unordered_set<Arche::EntityID> ExplicitPhysicsActorEntities{};
        for (auto [PhysicsSettingsComponent, BoundingBoxComponent, TransformComponent, EntityHierarchyComponent] : mWorld.Query<PhysicsActorSettings, BoundingBox, Transform, EntityHierarchy>()) {
            std::uint32_t ActorIndex{};
            const Tag* TagComponent{ std::as_const(mWorld).GetComponent<Tag>(EntityHierarchyComponent.self) };
            PhysicsActorBase::ActorDesc Desc{ BuildPhysicsActorDesc(mWorld, EntityHierarchyComponent.self, TagComponent, BoundingBoxComponent, TransformComponent, PhysicsSettingsComponent) };
            PhysicsActorBase* CreatedActor{ CreatePhysicsActor(mPhysicsWorld, Desc, ActorIndex) };
            if (CreatedActor == nullptr) {
                continue;
            }

            CreatedActor->SetOrientation(TransformComponent.rotation);
            CreatedActor->SetLocalBoundingBox(BoundingBoxComponent.GetObb());
            PendingPhysicsActorBinding Binding{};
            Binding.mEntityId = EntityHierarchyComponent.self;
            Binding.mActorPointer = CreatedActor;
            Binding.mActorIndex = ActorIndex;
            Binding.mActorType = Desc.ActorType;
            PendingBindings.push_back(Binding);
            ExplicitPhysicsActorEntities.insert(EntityHierarchyComponent.self);
        }

        for (auto [TagComponent, BoundingBoxComponent, TransformComponent, EntityHierarchyComponent] : mWorld.Query<Tag, BoundingBox, Transform, EntityHierarchy>()) {
            if (ExplicitPhysicsActorEntities.contains(EntityHierarchyComponent.self) == true) {
                continue;
            }

            const std::string_view TagText{ GetTagTextView(TagComponent) };
            PhysicsActorBase::PhysicsActorType ActorType{ PhysicsActorBase::PhysicsActorType::Dynamic };

            if (TagText == ObjectTagText) {
                ActorType = PhysicsActorBase::PhysicsActorType::Dynamic;
            }
            else if (TagText == PlayerTagText) {
                ActorType = PhysicsActorBase::PhysicsActorType::Kinematic;
            }
            else {
                continue;
            }

            std::uint32_t ActorIndex{};
            PhysicsActorBase::ActorDesc Desc{ BuildLegacyPhysicsActorDesc(mWorld, EntityHierarchyComponent.self, TagComponent, BoundingBoxComponent, TransformComponent, ActorType) };
            PhysicsActorBase* CreatedActor{ CreatePhysicsActor(mPhysicsWorld, Desc, ActorIndex) };
            if (CreatedActor == nullptr) {
                continue;
            }

            CreatedActor->SetOrientation(TransformComponent.rotation);
            CreatedActor->SetLocalBoundingBox(BoundingBoxComponent.GetObb());
            PendingPhysicsActorBinding Binding{};
            Binding.mEntityId = EntityHierarchyComponent.self;
            Binding.mActorPointer = CreatedActor;
            Binding.mActorIndex = ActorIndex;
            Binding.mActorType = ActorType;
            PendingBindings.push_back(Binding);
        }

        for (const TerrainActorDescBinding& BindingSource : mTerrainActorDescBindings) {
            if (BindingSource.mEntityId == Arche::NullEntityID || IsValidTerrainActorDesc(BindingSource.mTerrainActorDesc) == false) {
                continue;
            }

            const Transform* TransformComponent{ std::as_const(mWorld).GetComponent<Transform>(BindingSource.mEntityId) };
            if (TransformComponent == nullptr) {
                continue;
            }

            const std::uint32_t ActorIndex{ static_cast<std::uint32_t>(mPhysicsWorld.GetActorCount()) };
            PhysicsTerrainActor::ActorDesc Desc{ BuildPhysicsTerrainActorDesc(BindingSource.mTerrainActorDesc, *TransformComponent) };
            PhysicsTerrainActor* CreatedActor{ mPhysicsWorld.CreateTerrainActor(Desc) };
            if (CreatedActor == nullptr) {
                continue;
            }

            CreatedActor->SetName("TerrainActor");
            PendingPhysicsActorBinding Binding{};
            Binding.mEntityId = BindingSource.mEntityId;
            Binding.mActorPointer = CreatedActor;
            Binding.mActorIndex = ActorIndex;
            Binding.mActorType = PhysicsActorBase::PhysicsActorType::Static;
            PendingBindings.push_back(Binding);
        }

        for (const PendingPhysicsActorBinding& Binding : PendingBindings) {
            AttachPhysicsActorComponent(mWorld, Binding);
        }
    }

    void Scene::UpdatePhysics(float Dt) {
        for (auto [PhysicsActorComponent, TransformComponent, BoundingBoxComponent] : mWorld.Query<PhysicsActor, Transform, BoundingBox>()) {
            PhysicsActorBase* ActorPointer{ PhysicsActorComponent.mActorPointer };
            if (ActorPointer == nullptr) {
                continue;
            }

            if (ActorPointer->GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
                continue;
            }

            const bool IsActorTransformChanged{ std::make_tuple(ActorPointer->GetPosition(), ActorPointer->GetOrientation(), ActorPointer->GetScale()) != std::make_tuple(TransformComponent.position, TransformComponent.rotation, TransformComponent.scale) };
            if (IsActorTransformChanged == true) {
                SetActorTransformFromComponent(*ActorPointer, TransformComponent);
            }

            const DirectX::BoundingOrientedBox& ComponentLocalBoundingBox{ BoundingBoxComponent.GetObb() };
            const DirectX::BoundingOrientedBox& ActorLocalBoundingBox{ ActorPointer->GetLocalBoundingBox() };
            const std::tuple<DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Quaternion> ActorLocalBoundingBoxValue{ DirectX::SimpleMath::Vector3{ ActorLocalBoundingBox.Center }, DirectX::SimpleMath::Vector3{ ActorLocalBoundingBox.Extents }, DirectX::SimpleMath::Quaternion{ ActorLocalBoundingBox.Orientation } };
            const std::tuple<DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Quaternion> ComponentLocalBoundingBoxValue{ DirectX::SimpleMath::Vector3{ ComponentLocalBoundingBox.Center }, DirectX::SimpleMath::Vector3{ ComponentLocalBoundingBox.Extents }, DirectX::SimpleMath::Quaternion{ ComponentLocalBoundingBox.Orientation } };
            if (ActorLocalBoundingBoxValue != ComponentLocalBoundingBoxValue) {
                ActorPointer->SetLocalBoundingBox(ComponentLocalBoundingBox);
            }
        }

        for (TerrainActorDescBinding& BindingSource : mTerrainActorDescBindings) {
            PhysicsActor* PhysicsActorComponent{ mWorld.GetComponent<PhysicsActor>(BindingSource.mEntityId) };
            Transform* TransformComponent{ mWorld.GetComponent<Transform>(BindingSource.mEntityId) };
            if (PhysicsActorComponent == nullptr || TransformComponent == nullptr || IsValidTerrainActorDesc(BindingSource.mTerrainActorDesc) == false) {
                continue;
            }

            PhysicsTerrainActor* TerrainActorPointer{ mPhysicsWorld.GetTerrainActor(PhysicsActorComponent->mActorIndex) };
            if (TerrainActorPointer == nullptr) {
                continue;
            }

            if (BindingSource.mIsTerrainActorDescApplied == false) {
                PhysicsTerrainActor::ActorDesc Desc{ BuildPhysicsTerrainActorDesc(BindingSource.mTerrainActorDesc, *TransformComponent) };
                TerrainActorPointer->SetActorDesc(Desc);
                BindingSource.mIsTerrainActorDescApplied = true;
                continue;
            }

            ApplyTerrainActorTransformIfChanged(*TerrainActorPointer, *TransformComponent);
        }

        mPhysicsWorld.TickKinematicActors(Dt);
        mPhysicsWorld.Update(Dt);
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
            return ResolveTerrainRaycastDistance(mPhysicsWorld, RayStartPoint, RayDirection, RayLength);
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
        for (TerrainActorDescBinding& Binding : mTerrainActorDescBindings) {
            if (Binding.mEntityId != EntityId) {
                continue;
            }

            const bool IsTerrainActorDescChanged{ std::tie(Binding.mTerrainActorDesc.HalfExtentX, Binding.mTerrainActorDesc.HalfExtentZ, Binding.mTerrainActorDesc.HeightFieldWidth, Binding.mTerrainActorDesc.HeightFieldHeight, Binding.mTerrainActorDesc.HeightFieldCellSizeX, Binding.mTerrainActorDesc.HeightFieldCellSizeZ, Binding.mTerrainActorDesc.HeightFieldMaxHeight, Binding.mTerrainActorDesc.HeightFieldCenterOrigin, Binding.mTerrainActorDesc.HeightFieldValues) != std::tie(TerrainActorDesc.HalfExtentX, TerrainActorDesc.HalfExtentZ, TerrainActorDesc.HeightFieldWidth, TerrainActorDesc.HeightFieldHeight, TerrainActorDesc.HeightFieldCellSizeX, TerrainActorDesc.HeightFieldCellSizeZ, TerrainActorDesc.HeightFieldMaxHeight, TerrainActorDesc.HeightFieldCenterOrigin, TerrainActorDesc.HeightFieldValues) };
            Binding.mTerrainActorDesc = TerrainActorDesc;
            if (IsTerrainActorDescChanged == true) {
                Binding.mIsTerrainActorDescApplied = false;
            }

            return;
        }

        TerrainActorDescBinding NewBinding{};
        NewBinding.mEntityId = EntityId;
        NewBinding.mTerrainActorDesc = TerrainActorDesc;
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
