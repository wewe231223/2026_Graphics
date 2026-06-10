#include "SceneYamlInternal.h"
#include <format>
#include <memory>
#include <utility>
#include <ryml_std.hpp>
#include "Game/Asset/AnimationGraphAsset.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/AnimatorGraphPlayer.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/FootIKRuntime.h"
#include "Game/Scene/Components/RuntimeVariableTable.h"
#include "Game/Scene/Components/SkySphere.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Pipeline/PipelineScene.h"
#include "Game/Scene/Systems/ProceduralFoliageSystem.h"
#include "Utility/StdOutput.h"

namespace Game::SceneYaml {
    namespace {
        c4::yml::Tree ParseSceneYaml(const std::string& YamlText);
        void ReadSceneMetadata(c4::yml::ConstNodeRef RootNode, SceneYamlLoadContext& LoadContext);
        void ReadSystems(c4::yml::ConstNodeRef RootNode, SceneYamlLoadContext& LoadContext);
        void ReadPrefabDescriptors(c4::yml::ConstNodeRef RootNode, SceneYamlLoadContext& LoadContext);
        void ReadEntityComponentsFromRegistry(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState, const std::vector<SceneYamlComponentReader>& ComponentReaders);
        void ReadEntityComponents(c4::yml::ConstNodeRef EntityNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext);
        void CreateSceneEntities(c4::yml::ConstNodeRef RootNode, SceneYamlLoadContext& LoadContext);
        void ResolveDeferredParentBindings(SceneYamlLoadContext& LoadContext);
        void ResolvePendingBoundingBoxBindings(SceneYamlLoadContext& LoadContext);
        void ResolveDeferredEntityReferenceBindings(SceneYamlLoadContext& LoadContext);
        void ResolvePendingAnimatorBindings(SceneYamlLoadContext& LoadContext);
        void ResolveDeferredBindings(SceneYamlLoadContext& LoadContext);
        void ApplyTerrainPostProcess(SceneYamlLoadContext& LoadContext);
        void EnsureFootIKRuntimeComponents(SceneYamlLoadContext& LoadContext);
        void FinalizeSceneBuild(SceneYamlLoadContext& LoadContext);
        SceneYamlLoadResult DeserializeSceneYaml(const std::string& YamlText, SceneYamlLoadContext& LoadContext, std::unordered_map<std::int64_t, Arche::EntityID>* OutEntityIdMap);
    }

    SceneYamlLoadTarget::SceneYamlLoadTarget(Scene& TargetScene)
        : mScene{ &TargetScene },
        mPipelineScene{} {
    }

    SceneYamlLoadTarget::SceneYamlLoadTarget(Pipeline::Scene& TargetScene)
        : mScene{},
        mPipelineScene{ &TargetScene } {
    }

    SceneYamlLoadTarget::~SceneYamlLoadTarget() = default;
    SceneYamlLoadTarget::SceneYamlLoadTarget(const SceneYamlLoadTarget& Other) = default;
    SceneYamlLoadTarget& SceneYamlLoadTarget::operator=(const SceneYamlLoadTarget& Other) = default;
    SceneYamlLoadTarget::SceneYamlLoadTarget(SceneYamlLoadTarget&& Other) noexcept = default;
    SceneYamlLoadTarget& SceneYamlLoadTarget::operator=(SceneYamlLoadTarget&& Other) noexcept = default;

    Arche::World& SceneYamlLoadTarget::GetWorld() {
        if (mScene != nullptr) {
            return mScene->GetWorld();
        }

        return mPipelineScene->GetWorld();
    }

    const Arche::World& SceneYamlLoadTarget::GetWorld() const {
        if (mScene != nullptr) {
            return mScene->GetWorld();
        }

        return mPipelineScene->GetWorld();
    }

    AssetRegistry& SceneYamlLoadTarget::GetAssetRegistry() {
        if (mScene != nullptr) {
            return mScene->GetAssetRegistry();
        }

        return mPipelineScene->GetAssetRegistry();
    }

    const AssetRegistry& SceneYamlLoadTarget::GetAssetRegistry() const {
        if (mScene != nullptr) {
            return mScene->GetAssetRegistry();
        }

        return mPipelineScene->GetAssetRegistry();
    }

    Script::LuaBehaviorFramework& SceneYamlLoadTarget::GetLuaScriptFramework() {
        if (mScene != nullptr) {
            return mScene->GetLuaScriptFramework();
        }

        return mPipelineScene->GetLuaScriptFramework();
    }

    const Script::LuaBehaviorFramework& SceneYamlLoadTarget::GetLuaScriptFramework() const {
        if (mScene != nullptr) {
            return mScene->GetLuaScriptFramework();
        }

        return mPipelineScene->GetLuaScriptFramework();
    }

    void SceneYamlLoadTarget::SetName(const std::string& NewName) {
        if (mScene != nullptr) {
            mScene->SetName(NewName);
            return;
        }

        mPipelineScene->GetWorldSnapshot().SetSceneName(NewName);
    }

    bool SceneYamlLoadTarget::ShouldReadSystems() const {
        return mScene != nullptr || mPipelineScene != nullptr;
    }

    void SceneYamlLoadTarget::AddSystem(std::unique_ptr<ISystem> NewSystem) {
        if (mScene != nullptr) {
            mScene->AddSystem(std::move(NewSystem));
            return;
        }

        mPipelineScene->AddSynchronousSystem(std::move(NewSystem));
    }

    void SceneYamlLoadTarget::BuildSystemExecutionPlan() {
        if (mScene == nullptr) {
            return;
        }

        mScene->BuildSystemExecutionPlan();
    }

    void SceneYamlLoadTarget::RebuildPhysicsActors() {
        if (mScene != nullptr) {
            mScene->RebuildPhysicsActors();
            return;
        }

        mPipelineScene->RebuildPhysicsActors();
    }

    void SceneYamlLoadTarget::AddTerrainActorDesc(Arche::EntityID EntityId, const PhysicsTerrainActor::ActorDesc& TerrainActorDesc) {
        if (mScene != nullptr) {
            mScene->AddTerrainActorDesc(EntityId, TerrainActorDesc);
            return;
        }

        mPipelineScene->AddTerrainActorDesc(EntityId, TerrainActorDesc);
    }

    void SceneYamlLoadTarget::ClearTerrainActorDescs() {
        if (mScene != nullptr) {
            mScene->ClearTerrainActorDescs();
            return;
        }

        mPipelineScene->ClearTerrainActorDescs();
    }

    SceneYamlLoadContext::SceneYamlLoadContext(Scene& TargetScene, SceneYamlLoadResult& TargetLoadResult)
        : mScene(TargetScene),
        mLoadResult(TargetLoadResult),
        mEntityFactory(mScene.GetWorld()) {
    }

    SceneYamlLoadContext::SceneYamlLoadContext(Pipeline::Scene& TargetScene, SceneYamlLoadResult& TargetLoadResult)
        : mScene(TargetScene),
        mLoadResult(TargetLoadResult),
        mEntityFactory(mScene.GetWorld()) {
    }

    namespace {
        c4::yml::Tree ParseSceneYaml(const std::string& YamlText) {
            c4::yml::Tree Tree{ c4::yml::parse_in_arena(c4::to_csubstr(YamlText)) };
            Tree.resolve();
            return Tree;
        }

        void ReadSceneMetadata(c4::yml::ConstNodeRef RootNode, SceneYamlLoadContext& LoadContext) {
            if (RootNode.has_child("SceneName")) {
                RootNode["SceneName"] >> LoadContext.mSceneName;
                LoadContext.mScene.SetName(LoadContext.mSceneName);
            }
        }

        void ReadSystems(c4::yml::ConstNodeRef RootNode, SceneYamlLoadContext& LoadContext) {
            if (LoadContext.mScene.ShouldReadSystems() == false) {
                return;
            }

            if (RootNode.has_child("Systems") == false) {
                return;
            }

            const c4::yml::ConstNodeRef SystemsNode{ RootNode["Systems"] };
            for (const c4::yml::ConstNodeRef SystemNode : SystemsNode.children()) {
                std::string SystemName{};
                const bool IsSystemNameRead{ TryReadSystemName(SystemNode, SystemName) };
                if (IsSystemNameRead == false) {
                    LoadContext.mLoadResult.IsSuccess = false;
                    LoadContext.mLoadResult.UndecidedItems.push_back("System Type 을 읽을 수 없습니다.");
                    continue;
                }

                std::unique_ptr<ISystem> NewSystem{ CreateSystemByName(SystemName) };
                if (NewSystem == nullptr) {
                    LoadContext.mLoadResult.IsSuccess = false;
                    LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "알 수 없는 System Type: " } + SystemName);
                    continue;
                }

                if (SystemName == "ProceduralFoliageSystem" && SystemNode.is_map() == true) {
                    std::string ConfigPath{};
                    if (TryReadStringChild(SystemNode, { "ConfigPath", "configPath" }, ConfigPath) == true) {
                        ProceduralFoliageSystem* FoliageSystem{ dynamic_cast<ProceduralFoliageSystem*>(NewSystem.get()) };
                        if (FoliageSystem != nullptr) {
                            FoliageSystem->SetConfigPath(ResolveSceneResourcePath(LoadContext.mSceneName, ConfigPath));
                        }
                    }
                }

                LoadContext.mScene.AddSystem(std::move(NewSystem));
            }
        }

        void ReadPrefabDescriptors(c4::yml::ConstNodeRef RootNode, SceneYamlLoadContext& LoadContext) {
            if (RootNode.has_child("Prefabs") == false) {
                return;
            }

            const c4::yml::ConstNodeRef PrefabsNode{ RootNode["Prefabs"] };
            for (const c4::yml::ConstNodeRef PrefabNode : PrefabsNode.children()) {
                PrefabDescriptor Descriptor{};

                if (PrefabNode.has_child("prefabId")) {
                    PrefabNode["prefabId"] >> Descriptor.mPrefabId;
                }

                if (PrefabNode.has_child("modelPath")) {
                    std::string ModelPath{};
                    PrefabNode["modelPath"] >> ModelPath;
                    Descriptor.mModelSelector = ResolveSceneResourcePath(LoadContext.mSceneName, ModelPath);
                }

                if (PrefabNode.has_child("materialPath")) {
                    PrefabNode["materialPath"] >> Descriptor.mMaterialPath;
                }

                if (PrefabNode.has_child("active")) {
                    PrefabNode["active"] >> Descriptor.mActive;
                }

                if (Descriptor.mPrefabId != 0ull) {
                    LoadContext.mPrefabDescriptors[Descriptor.mPrefabId] = Descriptor;
                }
            }
        }

        void ReadEntityComponentsFromRegistry(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState, const std::vector<SceneYamlComponentReader>& ComponentReaders) {
            for (const SceneYamlComponentReader& ComponentReader : ComponentReaders) {
                static_cast<void>(ComponentReader.mTypeName);
                ComponentReader.mRead(ComponentsNode, Entity, LoadContext, ReadState);
                if (ReadState.mShouldStopReadingEntity == true) {
                    return;
                }
            }
        }

        void ReadEntityComponents(c4::yml::ConstNodeRef EntityNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext) {
            const c4::yml::ConstNodeRef ComponentsNode{ EntityNode.has_child("Components") ? EntityNode["Components"] : EntityNode };
            if (ComponentsNode.readable() == false || ComponentsNode.is_map() == false) {
                return;
            }

            SceneYamlComponentReadState ReadState{};
            ReadEntityComponentsFromRegistry(ComponentsNode, Entity, LoadContext, ReadState, GetSceneYamlPreModelComponentReaders());
            if (ReadState.mShouldStopReadingEntity == true) {
                return;
            }

            SpawnPrefabModelIfNeeded(Entity, LoadContext, ReadState);
            ReadEntityComponentsFromRegistry(ComponentsNode, Entity, LoadContext, ReadState, GetSceneYamlModelComponentReaders());
            if (ReadState.mShouldStopReadingEntity == true) {
                return;
            }

            ReadEntityComponentsFromRegistry(ComponentsNode, Entity, LoadContext, ReadState, GetSceneYamlPostModelComponentReaders());
        }

        void CreateSceneEntities(c4::yml::ConstNodeRef RootNode, SceneYamlLoadContext& LoadContext) {
            if (RootNode.has_child("Entities") == false) {
                return;
            }

            LoadContext.mScene.ClearTerrainActorDescs();
            const c4::yml::ConstNodeRef EntitiesNode{ RootNode["Entities"] };
            for (const c4::yml::ConstNodeRef EntityNode : EntitiesNode.children()) {
                const Arche::EntityID Entity{ LoadContext.mEntityFactory.CreateEntity(false) };

                std::int64_t SerializedEntityId{ -1 };
                if (EntityNode.has_child("EntityId")) {
                    EntityNode["EntityId"] >> SerializedEntityId;
                    LoadContext.mEntityBySerializedId[SerializedEntityId] = Entity;
                }

                if (EntityNode.has_child("ParentEntityId")) {
                    std::int64_t SerializedParentId{ -1 };
                    EntityNode["ParentEntityId"] >> SerializedParentId;
                    LoadContext.mDeferredParents.push_back(std::pair<Arche::EntityID, std::int64_t>{ Entity, SerializedParentId });
                }

                ReadEntityComponents(EntityNode, Entity, LoadContext);
            }
        }

        void ResolveDeferredParentBindings(SceneYamlLoadContext& LoadContext) {
            for (const std::pair<Arche::EntityID, std::int64_t>& DeferredParent : LoadContext.mDeferredParents) {
                const std::unordered_map<std::int64_t, Arche::EntityID>::const_iterator ParentIter{ LoadContext.mEntityBySerializedId.find(DeferredParent.second) };
                if (ParentIter == LoadContext.mEntityBySerializedId.end()) {
                    continue;
                }

                Game::EntityHierarchy* ChildHierarchy{ LoadContext.mScene.GetWorld().GetComponent<Game::EntityHierarchy>(DeferredParent.first) };
                Game::EntityHierarchy* ParentHierarchy{ LoadContext.mScene.GetWorld().GetComponent<Game::EntityHierarchy>(ParentIter->second) };
                if (ChildHierarchy == nullptr || ParentHierarchy == nullptr) {
                    continue;
                }

                ChildHierarchy->parent = Arche::NullEntityID;
                ChildHierarchy->nextSibling = Arche::NullEntityID;
                LoadContext.mEntityFactory.AttachChildEntity(ParentIter->second, DeferredParent.first);
            }
        }

        void ResolvePendingBoundingBoxBindings(SceneYamlLoadContext& LoadContext) {
            for (const PendingBoundingBoxBinding& PendingBoundingBoxBindingItem : LoadContext.mPendingBoundingBoxBindings) {
                if (PendingBoundingBoxBindingItem.mEntityId == Arche::NullEntityID) {
                    continue;
                }

                DirectX::BoundingOrientedBox LocalBoundingBox{};
                LocalBoundingBox.Center = DirectX::XMFLOAT3{ PendingBoundingBoxBindingItem.mCenter.x, PendingBoundingBoxBindingItem.mCenter.y, PendingBoundingBoxBindingItem.mCenter.z };
                LocalBoundingBox.Extents = DirectX::XMFLOAT3{ PendingBoundingBoxBindingItem.mExtents.x, PendingBoundingBoxBindingItem.mExtents.y, PendingBoundingBoxBindingItem.mExtents.z };
                LocalBoundingBox.Orientation = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };

                BoundingBox* ExistingBoundingBox{ LoadContext.mScene.GetWorld().GetComponent<BoundingBox>(PendingBoundingBoxBindingItem.mEntityId) };
                if (ExistingBoundingBox == nullptr) {
                    BoundingBox NewBoundingBox{};
                    NewBoundingBox.SetObb(LocalBoundingBox);
                    const Transform* TransformComponent{ std::as_const(LoadContext.mScene.GetWorld()).GetComponent<Transform>(PendingBoundingBoxBindingItem.mEntityId) };
                    if (TransformComponent != nullptr) {
                        const DirectX::SimpleMath::Matrix TransformOnlyWorldMatrix{ DirectX::SimpleMath::Matrix::CreateScale(TransformComponent->scale) * DirectX::SimpleMath::Matrix::CreateFromQuaternion(TransformComponent->rotation) * DirectX::SimpleMath::Matrix::CreateTranslation(TransformComponent->position) };
                        DirectX::BoundingOrientedBox WorldBoundingBox{};
                        LocalBoundingBox.Transform(WorldBoundingBox, TransformOnlyWorldMatrix);
                        NewBoundingBox.SetWorldObb(WorldBoundingBox);
                    }
                    LoadContext.mScene.GetWorld().AddComponent(PendingBoundingBoxBindingItem.mEntityId, NewBoundingBox);
                }
                else {
                    ExistingBoundingBox->SetObb(LocalBoundingBox);
                    const Transform* TransformComponent{ std::as_const(LoadContext.mScene.GetWorld()).GetComponent<Transform>(PendingBoundingBoxBindingItem.mEntityId) };
                    if (TransformComponent != nullptr) {
                        const DirectX::SimpleMath::Matrix TransformOnlyWorldMatrix{ DirectX::SimpleMath::Matrix::CreateScale(TransformComponent->scale) * DirectX::SimpleMath::Matrix::CreateFromQuaternion(TransformComponent->rotation) * DirectX::SimpleMath::Matrix::CreateTranslation(TransformComponent->position) };
                        DirectX::BoundingOrientedBox WorldBoundingBox{};
                        LocalBoundingBox.Transform(WorldBoundingBox, TransformOnlyWorldMatrix);
                        ExistingBoundingBox->SetWorldObb(WorldBoundingBox);
                    }
                    else {
                        ExistingBoundingBox->InvalidateWorldObb();
                    }
                }
            }
        }

        void ResolveDeferredEntityReferenceBindings(SceneYamlLoadContext& LoadContext) {
            for (const std::pair<Arche::EntityID, std::int64_t>& DeferredBoneSkinReferenceEntity : LoadContext.mDeferredBoneSkinReferenceEntities) {
                const std::unordered_map<std::int64_t, Arche::EntityID>::const_iterator BoneRootIter{ LoadContext.mEntityBySerializedId.find(DeferredBoneSkinReferenceEntity.second) };
                if (BoneRootIter == LoadContext.mEntityBySerializedId.end()) {
                    continue;
                }

                LoadContext.mScene.GetWorld().WriteComponent<BoneSkinReference>(DeferredBoneSkinReferenceEntity.first, [ResolvedEntityId = BoneRootIter->second](BoneSkinReference& TargetComponent) {
                    TargetComponent.boneRootEntityId = ResolvedEntityId;
                });
            }

            for (const std::pair<Arche::EntityID, std::int64_t>& DeferredThirdPersonFollowTargetEntity : LoadContext.mDeferredThirdPersonFollowTargetEntities) {
                const std::unordered_map<std::int64_t, Arche::EntityID>::const_iterator FollowTargetIter{ LoadContext.mEntityBySerializedId.find(DeferredThirdPersonFollowTargetEntity.second) };
                if (FollowTargetIter == LoadContext.mEntityBySerializedId.end()) {
                    continue;
                }

                LoadContext.mScene.GetWorld().WriteComponent<Camera>(DeferredThirdPersonFollowTargetEntity.first, [ResolvedEntityId = FollowTargetIter->second](Camera& TargetComponent) {
                    TargetComponent.thirdPersonFollowTarget = ResolvedEntityId;
                });
            }

            for (const std::pair<Arche::EntityID, std::int64_t>& DeferredSkySphereEntity : LoadContext.mDeferredSkySphereEntities) {
                const std::unordered_map<std::int64_t, Arche::EntityID>::const_iterator SkySphereIter{ LoadContext.mEntityBySerializedId.find(DeferredSkySphereEntity.second) };
                if (SkySphereIter == LoadContext.mEntityBySerializedId.end()) {
                    continue;
                }

                LoadContext.mScene.GetWorld().WriteComponent<SkySphere>(DeferredSkySphereEntity.first, [ResolvedEntityId = SkySphereIter->second](SkySphere& TargetComponent) {
                    TargetComponent.SkySphereEntityId = ResolvedEntityId;
                });
            }
        }

        void ResolvePendingAnimatorBindings(SceneYamlLoadContext& LoadContext) {
            for (const PendingAnimatorBinding& Binding : LoadContext.mPendingAnimatorBindings) {
                if (Binding.mAnimationData == nullptr) {
                    StdOutput::WriteWarningLine(std::format("[SceneYamlSerializer] Animation data is null. source={}:{} node={}", Binding.mSourceEntityId.index, Binding.mSourceEntityId.generation, Binding.mTargetNodeName));
                    continue;
                }

                Arche::EntityID TargetEntityId{ Binding.mSourceEntityId };
                if (Binding.mTargetNodeName.empty() == false) {
                    const bool IsFound{ TryFindEntityByNameInHierarchy(&LoadContext.mScene.GetWorld(), Binding.mSourceEntityId, Binding.mTargetNodeName, TargetEntityId, false) };
                    if (IsFound == false) {
                        StdOutput::WriteWarningLine(std::format("[SceneYamlSerializer] Animation target node not found. source={}:{} node={}", Binding.mSourceEntityId.index, Binding.mSourceEntityId.generation, Binding.mTargetNodeName));
                        continue;
                    }
                }

                Animator NewAnimator{};
                NewAnimator.animation = Binding.mAnimationData;
                NewAnimator.clipIndex = Binding.mClipIndex;
                NewAnimator.FallbackClipIndex = Binding.mFallbackClipIndex;
                NewAnimator.GraphAsset = Binding.mAnimationGraphData;
                NewAnimator.IsGraphEnabled = Binding.mAnimationGraphData != nullptr;

                Animator* ExistingAnimator{ LoadContext.mScene.GetWorld().GetComponent<Animator>(TargetEntityId) };
                if (ExistingAnimator == nullptr) {
                    LoadContext.mScene.GetWorld().AddComponent(TargetEntityId, NewAnimator);
                }
                else {
                    *ExistingAnimator = NewAnimator;
                }

                if (NewAnimator.IsGraphEnabled) {
                    AnimatorGraphPlayer Player{};
                    RuntimeVariableTable VariableTable{};
                    const std::int32_t DefaultNodeIndex{ NewAnimator.GraphAsset->GetDefaultNodeIndex() };
                    Player.CurrentNodeIndex = DefaultNodeIndex;
                    if (DefaultNodeIndex >= 0 && static_cast<std::size_t>(DefaultNodeIndex) < NewAnimator.GraphAsset->GetNodes().size()) {
                        const AnimationGraphAsset::AnimationGraphNodeAsset& DefaultNode{ NewAnimator.GraphAsset->GetNodes()[DefaultNodeIndex] };
                        Player.SampleSourceClipIndex = DefaultNode.ClipIndex;
                        Player.SampleDestinationClipIndex = DefaultNode.ClipIndex;
                        Player.SamplePlaySpeed = DefaultNode.PlaySpeed;
                        Player.SampleIsLoop = DefaultNode.IsLoop;
                        NewAnimator.clipIndex = DefaultNode.ClipIndex;
                    }

                    const std::vector<RuntimeParameterDefinition>& Definitions{ NewAnimator.GraphAsset->GetParameterDefinitions() };
                    for (std::size_t ParameterIndex{ 0 }; ParameterIndex < Definitions.size() && ParameterIndex < RuntimeVariableTableMaxParameterCount; ++ParameterIndex) {
                        if (Definitions[ParameterIndex].ParameterTypeValue == RuntimeParameterDefinition::ParameterType::Int) {
                            VariableTable.IntValues[ParameterIndex] = std::get<std::int32_t>(Definitions[ParameterIndex].DefaultValue);
                        }
                        else if (Definitions[ParameterIndex].ParameterTypeValue == RuntimeParameterDefinition::ParameterType::Float) {
                            VariableTable.FloatValues[ParameterIndex] = std::get<float>(Definitions[ParameterIndex].DefaultValue);
                        }
                        else {
                            VariableTable.BoolValues[ParameterIndex] = std::get<bool>(Definitions[ParameterIndex].DefaultValue);
                        }
                    }

                    for (const PendingAnimatorBinding::PendingRuntimeVariableInitialization& Initialization : Binding.mRuntimeVariableInitializations) {
                        if (Initialization.mType == PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Bool) {
                            VariableTable.TrySetBoolParameter(Definitions, Initialization.mParameterName, Initialization.mBoolValue);
                        }
                        else if (Initialization.mType == PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Int) {
                            VariableTable.TrySetIntParameter(Definitions, Initialization.mParameterName, Initialization.mIntValue);
                        }
                        else {
                            VariableTable.TrySetFloatParameter(Definitions, Initialization.mParameterName, Initialization.mFloatValue);
                        }
                    }

                    AnimatorGraphPlayer* ExistingPlayer{ LoadContext.mScene.GetWorld().GetComponent<AnimatorGraphPlayer>(TargetEntityId) };
                    if (ExistingPlayer == nullptr) {
                        LoadContext.mScene.GetWorld().AddComponent(TargetEntityId, Player);
                    }
                    else {
                        *ExistingPlayer = Player;
                    }

                    RuntimeVariableTable* ExistingVariableTable{ LoadContext.mScene.GetWorld().GetComponent<RuntimeVariableTable>(TargetEntityId) };
                    if (ExistingVariableTable == nullptr) {
                        LoadContext.mScene.GetWorld().AddComponent(TargetEntityId, VariableTable);
                    }
                    else {
                        *ExistingVariableTable = VariableTable;
                    }
                }
            }
        }

        void ResolveDeferredBindings(SceneYamlLoadContext& LoadContext) {
            ResolveDeferredParentBindings(LoadContext);
            ResolvePendingBoundingBoxBindings(LoadContext);
            ResolveDeferredEntityReferenceBindings(LoadContext);
            ResolvePendingAnimatorBindings(LoadContext);
        }

        void ApplyTerrainPostProcess(SceneYamlLoadContext& LoadContext) {
            ApplyPendingTerrainSnapBindings(LoadContext.mScene.GetWorld(), LoadContext.mTerrainSurfaceBindings, LoadContext.mPendingTerrainSnapBindings);
        }

        void EnsureFootIKRuntimeComponents(SceneYamlLoadContext& LoadContext) {
            Arche::World& World{ LoadContext.mScene.GetWorld() };
            const Arche::World::WorldReadOnlyView& ReadOnlyWorld{ std::as_const(World).GetReadOnlyView() };
            std::vector<Arche::EntityID> RuntimeMissingEntityIds{};
            RuntimeMissingEntityIds.reserve(32);

            for (auto [FootIKRigComponent, HierarchyComponent] : World.Query<FootIKRig, EntityHierarchy>()) {
                (void)FootIKRigComponent;
                if (ReadOnlyWorld.GetComponent<FootIKRuntime>(HierarchyComponent.self) == nullptr) {
                    RuntimeMissingEntityIds.push_back(HierarchyComponent.self);
                }
            }

            for (const Arche::EntityID RuntimeMissingEntityId : RuntimeMissingEntityIds) {
                if (World.GetComponent<FootIKRuntime>(RuntimeMissingEntityId) != nullptr) {
                    continue;
                }

                FootIKRuntime NewFootIKRuntime{};
                World.AddComponent(RuntimeMissingEntityId, NewFootIKRuntime);
            }
        }

        void FinalizeSceneBuild(SceneYamlLoadContext& LoadContext) {
            EnsureFootIKRuntimeComponents(LoadContext);
            LoadContext.mScene.RebuildPhysicsActors();
            LoadContext.mScene.BuildSystemExecutionPlan();
        }

        SceneYamlLoadResult DeserializeSceneYaml(const std::string& YamlText, SceneYamlLoadContext& LoadContext, std::unordered_map<std::int64_t, Arche::EntityID>* OutEntityIdMap) {
            c4::yml::Tree Tree{ ParseSceneYaml(YamlText) };
            const c4::yml::ConstNodeRef RootNode{ Tree.rootref() };

            ReadSceneMetadata(RootNode, LoadContext);
            ReadSystems(RootNode, LoadContext);
            ReadPrefabDescriptors(RootNode, LoadContext);
            CreateSceneEntities(RootNode, LoadContext);
            ResolveDeferredBindings(LoadContext);
            ApplyTerrainPostProcess(LoadContext);
            FinalizeSceneBuild(LoadContext);

            if (OutEntityIdMap != nullptr) {
                *OutEntityIdMap = LoadContext.mEntityBySerializedId;
            }

            return LoadContext.mLoadResult;
        }
    }

    SceneYamlLoadResult SceneYamlDeserializer::Deserialize(const std::string& YamlText, Scene& OutScene) const {
        SceneYamlLoadResult LoadResult{};
        SceneYamlLoadContext LoadContext{ OutScene, LoadResult };
        return DeserializeSceneYaml(YamlText, LoadContext, nullptr);
    }

    SceneYamlLoadResult SceneYamlDeserializer::Deserialize(const std::string& YamlText, Pipeline::Scene& OutScene, std::unordered_map<std::int64_t, Arche::EntityID>& OutEntityIdMap) const {
        SceneYamlLoadResult LoadResult{};
        SceneYamlLoadContext LoadContext{ OutScene, LoadResult };
        return DeserializeSceneYaml(YamlText, LoadContext, &OutEntityIdMap);
    }
}
