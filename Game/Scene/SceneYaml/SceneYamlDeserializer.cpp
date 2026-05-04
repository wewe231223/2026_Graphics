#include "SceneYamlInternal.h"

namespace Game::SceneYaml {
    SceneYamlLoadResult SceneYamlDeserializer::Deserialize(const std::string& YamlText, Scene& OutScene) const {
        SceneYamlLoadResult LoadResult{};
        c4::yml::Tree Tree{ c4::yml::parse_in_arena(c4::to_csubstr(YamlText)) };
        Tree.resolve();
        c4::yml::ConstNodeRef RootNode{ Tree.rootref() };
        std::string SceneName{};

        if (RootNode.has_child("SceneName")) {
            RootNode["SceneName"] >> SceneName;
            OutScene.SetName(SceneName);
        }

        if (RootNode.has_child("Systems")) {
            const c4::yml::ConstNodeRef SystemsNode{ RootNode["Systems"] };
            for (const c4::yml::ConstNodeRef SystemNode : SystemsNode.children()) {
                std::string SystemName{};
                const bool IsSystemNameRead{ TryReadSystemName(SystemNode, SystemName) };
                if (IsSystemNameRead == false) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back("System Type 을 읽을 수 없습니다.");
                    continue;
                }

                std::unique_ptr<ISystem> NewSystem{ CreateSystemByName(SystemName) };
                if (NewSystem == nullptr) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back(std::string{ "알 수 없는 System Type: " } + SystemName);
                    continue;
                }

                OutScene.AddSystem(std::move(NewSystem));
            }
        }

        std::unordered_map<std::uint64_t, PrefabDescriptor> PrefabDescriptors{};
        if (RootNode.has_child("Prefabs")) {
            const c4::yml::ConstNodeRef PrefabsNode{ RootNode["Prefabs"] };
            for (const c4::yml::ConstNodeRef PrefabNode : PrefabsNode.children()) {
                PrefabDescriptor Descriptor{};

                if (PrefabNode.has_child("prefabId")) {
                    PrefabNode["prefabId"] >> Descriptor.mPrefabId;
                }

                if (PrefabNode.has_child("modelPath")) {
                    std::string ModelPath{};
                    PrefabNode["modelPath"] >> ModelPath;
                    Descriptor.mModelSelector = ResolveSceneResourcePath(SceneName, ModelPath);
                }

                if (PrefabNode.has_child("materialPath")) {
                    PrefabNode["materialPath"] >> Descriptor.mMaterialPath;
                }

                if (PrefabNode.has_child("active")) {
                    PrefabNode["active"] >> Descriptor.mActive;
                }

                if (Descriptor.mPrefabId != 0ull) {
                    PrefabDescriptors[Descriptor.mPrefabId] = Descriptor;
                }
            }
        }

        if (RootNode.has_child("Entities") == false) {
            OutScene.RebuildPhysicsActors();
            OutScene.BuildSystemExecutionPlan();
            return LoadResult;
        }

        OutScene.ClearTerrainActorDescs();
        SceneEntityFactory EntityFactory{ OutScene };
        std::unordered_map<std::int64_t, Arche::EntityID> EntityBySerializedId{};
        std::vector<std::pair<Arche::EntityID, std::int64_t>> DeferredParents{};
        std::vector<std::pair<Arche::EntityID, std::int64_t>> DeferredBoneSkinReferenceEntities{};
        std::vector<std::pair<Arche::EntityID, std::int64_t>> DeferredThirdPersonFollowTargetEntities{};
        std::vector<std::pair<Arche::EntityID, std::int64_t>> DeferredSkySphereEntities{};
        std::vector<PendingAnimatorBinding> PendingAnimatorBindings{};
        std::vector<PendingBoundingBoxBinding> PendingBoundingBoxBindings{};
        std::vector<PendingTerrainSnapBinding> PendingTerrainSnapBindings{};
        std::vector<TerrainSurfaceBinding> TerrainSurfaceBindings{};
        const c4::yml::ConstNodeRef EntitiesNode{ RootNode["Entities"] };
        for (const c4::yml::ConstNodeRef EntityNode : EntitiesNode.children()) {
            const Arche::EntityID Entity{ EntityFactory.CreateEntity(false) };

            std::int64_t SerializedEntityId{ -1 };
            if (EntityNode.has_child("EntityId")) {
                EntityNode["EntityId"] >> SerializedEntityId;
                EntityBySerializedId[SerializedEntityId] = Entity;
            }

            if (EntityNode.has_child("ParentEntityId")) {
                std::int64_t SerializedParentId{ -1 };
                EntityNode["ParentEntityId"] >> SerializedParentId;
                DeferredParents.push_back(std::pair<Arche::EntityID, std::int64_t>{ Entity, SerializedParentId });
            }

            const c4::yml::ConstNodeRef ComponentsNode{ EntityNode.has_child("Components") ? EntityNode["Components"] : EntityNode };
            if (ComponentsNode.readable() == false || ComponentsNode.is_map() == false) {
                continue;
            }

            if (ComponentsNode.has_child(NameTypeName)) {
                const c4::yml::ConstNodeRef NameNode{ ComponentsNode[NameTypeName] };
                if (NameNode.has_child("text")) {
                    std::string NameText{};
                    NameNode["text"] >> NameText;
                    const Name NewName{ Game::CreateNameComponent(NameText) };
                    OutScene.GetWorld().AddComponent(Entity, NewName);
                }
            }

            if (ComponentsNode.has_child(TransformTypeName)) {
                Transform NewTransform{};
                const c4::yml::ConstNodeRef TransformNode{ ComponentsNode[TransformTypeName] };
                if (TransformNode.has_child("position")) {
                    ReadVector3(TransformNode["position"], NewTransform.position);
                }

                if (TransformNode.has_child("rotationEuler")) {
                    ReadVector3(TransformNode["rotationEuler"], NewTransform.rotationEuler);
                }

                if (TransformNode.has_child("rotation")) {
                    ReadQuaternion(TransformNode["rotation"], NewTransform.rotation);
                }

                if (TransformNode.has_child("scale")) {
                    ReadVector3(TransformNode["scale"], NewTransform.scale);
                }

                OutScene.GetWorld().AddComponent(Entity, NewTransform);

                bool SnapToTerrainOnSpawn{};
                if (TryReadBoolChild(TransformNode, { "snapToTerrainOnSpawn", "SnapToTerrainOnSpawn" }, SnapToTerrainOnSpawn) == true && SnapToTerrainOnSpawn == true) {
                    PendingTerrainSnapBinding NewPendingTerrainSnapBinding{};
                    NewPendingTerrainSnapBinding.mEntityId = Entity;
                    TryReadFloatChild(TransformNode, { "terrainOffsetY", "TerrainOffsetY" }, NewPendingTerrainSnapBinding.mOffsetY);
                    PendingTerrainSnapBindings.push_back(NewPendingTerrainSnapBinding);
                }
            }

            if (ComponentsNode.has_child(DirectionalLightTypeName)) {
                DirectionalLight NewDirectionalLight{};
                const c4::yml::ConstNodeRef DirectionalLightNode{ ComponentsNode[DirectionalLightTypeName] };
                if (DirectionalLightNode.has_child("isActive")) {
                    DirectionalLightNode["isActive"] >> NewDirectionalLight.mIsActive;
                }

                if (DirectionalLightNode.has_child("castsShadow")) {
                    DirectionalLightNode["castsShadow"] >> NewDirectionalLight.mCastsShadow;
                }

                if (DirectionalLightNode.has_child("useTransformDirection")) {
                    DirectionalLightNode["useTransformDirection"] >> NewDirectionalLight.mUseTransformDirection;
                }

                if (DirectionalLightNode.has_child("direction")) {
                    ReadVector3(DirectionalLightNode["direction"], NewDirectionalLight.mDirection);
                }

                if (DirectionalLightNode.has_child("color")) {
                    ReadVector3(DirectionalLightNode["color"], NewDirectionalLight.mColor);
                }

                if (DirectionalLightNode.has_child("intensity")) {
                    DirectionalLightNode["intensity"] >> NewDirectionalLight.mIntensity;
                }

                if (DirectionalLightNode.has_child("ambientIntensity")) {
                    DirectionalLightNode["ambientIntensity"] >> NewDirectionalLight.mAmbientIntensity;
                }

                OutScene.GetWorld().AddComponent(Entity, NewDirectionalLight);
            }

            if (ComponentsNode.has_child(BoundingBoxTypeName)) {
                PendingBoundingBoxBinding NewPendingBinding{};
                if (TryReadBoundingBoxBinding(ComponentsNode[BoundingBoxTypeName], Entity, NewPendingBinding) == true) {
                    PendingBoundingBoxBindings.push_back(NewPendingBinding);
                }
            }

            if (ComponentsNode.has_child(PhysicsTypeName)) {
                const c4::yml::ConstNodeRef PhysicsNode{ ComponentsNode[PhysicsTypeName] };
                PhysicsActorSettings NewPhysicsSettings{};
                std::string PhysicsErrorText{};
                if (TryReadPhysicsActorSettings(PhysicsNode, NewPhysicsSettings, PhysicsErrorText) == false) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back(PhysicsErrorText);
                }
                else {
                    OutScene.GetWorld().AddComponent(Entity, NewPhysicsSettings);
                }

                if (PhysicsNode.readable() == true && PhysicsNode.is_map() == true && PhysicsNode.has_child(BoundingBoxTypeName)) {
                    PendingBoundingBoxBinding NewPendingBinding{};
                    if (TryReadBoundingBoxBinding(PhysicsNode[BoundingBoxTypeName], Entity, NewPendingBinding) == true) {
                        PendingBoundingBoxBindings.push_back(NewPendingBinding);
                    }
                }
            }

            std::uint32_t MaterialGroupIndexForModel{ 0 };
            bool HasPrefabInstance{ false };
            std::string PrefabModelSelector{};
            bool PrefabIsActive{ true };

            if (ComponentsNode.has_child(PrefabInstanceTypeName)) {
                PrefabInstance NewPrefabInstance{};
                const c4::yml::ConstNodeRef PrefabNode{ ComponentsNode[PrefabInstanceTypeName] };
                if (PrefabNode.has_child("prefabId")) {
                    PrefabNode["prefabId"] >> NewPrefabInstance.PrefabId;
                }

                if (NewPrefabInstance.PrefabId != 0ull) {
                    OutScene.GetWorld().AddComponent(Entity, NewPrefabInstance);
                    const std::unordered_map<std::uint64_t, PrefabDescriptor>::const_iterator PrefabIter{ PrefabDescriptors.find(NewPrefabInstance.PrefabId) };
                    if (PrefabIter == PrefabDescriptors.end()) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Prefab descriptor 를 찾을 수 없습니다. prefabId: " } + std::to_string(NewPrefabInstance.PrefabId));
                    }
                    else {
                        HasPrefabInstance = true;
                        PrefabModelSelector = PrefabIter->second.mModelSelector;
                        PrefabIsActive = PrefabIter->second.mActive;

                        if (PrefabIter->second.mMaterialPath.empty() == false) {
                            const std::string ResolvedPrefabMaterialPath{ ResolveSceneResourcePath(SceneName, PrefabIter->second.mMaterialPath) };
                            const bool IsLoaded{ OutScene.GetAssetRegistry().LoadMaterialGroups(ResolvedPrefabMaterialPath) };
                            if (IsLoaded == false) {
                                LoadResult.IsSuccess = false;
                                LoadResult.UndecidedItems.push_back(std::string{ "Prefab Material 파일 로드 실패: " } + ResolvedPrefabMaterialPath);
                            }
                            else {
                                const std::uint32_t MaterialGroupIndex{ OutScene.GetAssetRegistry().FindMaterialGroupIndexBySourcePath(ResolvedPrefabMaterialPath) };
                                if (MaterialGroupIndex == static_cast<std::uint32_t>(-1)) {
                                    LoadResult.IsSuccess = false;
                                    LoadResult.UndecidedItems.push_back(std::string{ "Prefab MaterialGroupIndex 해석 실패: " } + ResolvedPrefabMaterialPath);
                                }
                                else {
                                    MaterialGroupIndexForModel = MaterialGroupIndex;
                                }
                            }
                        }
                        else {
                            MaterialGroupIndexForModel = 0;
                        }
                    }
                }
            }

            if (ComponentsNode.has_child(BoneSkinReferenceTypeName)) {
                BoneSkinReference NewBoneSkinReference{};
                const c4::yml::ConstNodeRef BoneSkinReferenceNode{ ComponentsNode[BoneSkinReferenceTypeName] };
                OutScene.GetWorld().AddComponent(Entity, NewBoneSkinReference);

                if (BoneSkinReferenceNode.has_child("boneRootEntityId")) {
                    std::int64_t SerializedBoneRootEntityId{ -1 };
                    BoneSkinReferenceNode["boneRootEntityId"] >> SerializedBoneRootEntityId;
                    DeferredBoneSkinReferenceEntities.push_back(std::pair<Arche::EntityID, std::int64_t>{ Entity, SerializedBoneRootEntityId });
                }
            }

            if (ComponentsNode.has_child(FootIKRigTypeName)) {
                FootIKRig NewFootIKRig{};
                const c4::yml::ConstNodeRef FootIKRigNode{ ComponentsNode[FootIKRigTypeName] };
                if (FootIKRigNode.has_child("enabled")) {
                    FootIKRigNode["enabled"] >> NewFootIKRig.mEnabled;
                }

                if (FootIKRigNode.has_child("leftFootBoneName")) {
                    std::string LeftFootBoneNameText{};
                    FootIKRigNode["leftFootBoneName"] >> LeftFootBoneNameText;
                    SetFootIKRigBoneName(NewFootIKRig.mLeftFootBoneName, LeftFootBoneNameText);
                }

                if (FootIKRigNode.has_child("rightFootBoneName")) {
                    std::string RightFootBoneNameText{};
                    FootIKRigNode["rightFootBoneName"] >> RightFootBoneNameText;
                    SetFootIKRigBoneName(NewFootIKRig.mRightFootBoneName, RightFootBoneNameText);
                }

                if (FootIKRigNode.has_child("leftToeBoneName")) {
                    std::string LeftToeBoneNameText{};
                    FootIKRigNode["leftToeBoneName"] >> LeftToeBoneNameText;
                    SetFootIKRigBoneName(NewFootIKRig.mLeftToeBoneName, LeftToeBoneNameText);
                }

                if (FootIKRigNode.has_child("rightToeBoneName")) {
                    std::string RightToeBoneNameText{};
                    FootIKRigNode["rightToeBoneName"] >> RightToeBoneNameText;
                    SetFootIKRigBoneName(NewFootIKRig.mRightToeBoneName, RightToeBoneNameText);
                }

                if (FootIKRigNode.has_child("leftShinBoneName")) {
                    std::string LeftShinBoneNameText{};
                    FootIKRigNode["leftShinBoneName"] >> LeftShinBoneNameText;
                    SetFootIKRigBoneName(NewFootIKRig.mLeftShinBoneName, LeftShinBoneNameText);
                }

                if (FootIKRigNode.has_child("rightShinBoneName")) {
                    std::string RightShinBoneNameText{};
                    FootIKRigNode["rightShinBoneName"] >> RightShinBoneNameText;
                    SetFootIKRigBoneName(NewFootIKRig.mRightShinBoneName, RightShinBoneNameText);
                }

                if (FootIKRigNode.has_child("leftThighBoneName")) {
                    std::string LeftThighBoneNameText{};
                    FootIKRigNode["leftThighBoneName"] >> LeftThighBoneNameText;
                    SetFootIKRigBoneName(NewFootIKRig.mLeftThighBoneName, LeftThighBoneNameText);
                }

                if (FootIKRigNode.has_child("rightThighBoneName")) {
                    std::string RightThighBoneNameText{};
                    FootIKRigNode["rightThighBoneName"] >> RightThighBoneNameText;
                    SetFootIKRigBoneName(NewFootIKRig.mRightThighBoneName, RightThighBoneNameText);
                }

                if (FootIKRigNode.has_child("pelvisBoneName")) {
                    std::string PelvisBoneNameText{};
                    FootIKRigNode["pelvisBoneName"] >> PelvisBoneNameText;
                    SetFootIKRigBoneName(NewFootIKRig.mPelvisBoneName, PelvisBoneNameText);
                }

                if (FootIKRigNode.has_child("blendSpeed")) {
                    FootIKRigNode["blendSpeed"] >> NewFootIKRig.mBlendSpeed;
                }

                if (FootIKRigNode.has_child("maxLift")) {
                    FootIKRigNode["maxLift"] >> NewFootIKRig.mMaxLift;
                }

                if (FootIKRigNode.has_child("maxDrop")) {
                    FootIKRigNode["maxDrop"] >> NewFootIKRig.mMaxDrop;
                }

                OutScene.GetWorld().AddComponent(Entity, NewFootIKRig);
            }

            if (ComponentsNode.has_child(MaterialTypeName)) {
                Material NewMaterial{};
                const c4::yml::ConstNodeRef MaterialNode{ ComponentsNode[MaterialTypeName] };
                std::string MaterialPath{};

                if (MaterialNode.has_child("materialPath")) {
                    MaterialNode["materialPath"] >> MaterialPath;
                }

                if (MaterialPath.empty()) {
                    NewMaterial.MaterialGroupIndex = 0;
                }
                else {
                    const std::string ResolvedMaterialPath{ ResolveSceneResourcePath(SceneName, MaterialPath) };
                    const bool IsLoaded{ OutScene.GetAssetRegistry().LoadMaterialGroups(ResolvedMaterialPath) };
                    if (IsLoaded == false) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Material 파일 로드 실패, DefaultMaterialGroup 을 사용합니다: " } + ResolvedMaterialPath);
                        NewMaterial.MaterialGroupIndex = 0;
                    }
                    else {
                        const std::uint32_t MaterialGroupIndex{ OutScene.GetAssetRegistry().FindMaterialGroupIndexBySourcePath(ResolvedMaterialPath) };
                        if (MaterialGroupIndex == static_cast<std::uint32_t>(-1)) {
                            LoadResult.IsSuccess = false;
                            LoadResult.UndecidedItems.push_back(std::string{ "Material 파일에서 MaterialGroupIndex 를 해석할 수 없어 DefaultMaterialGroup 을 사용합니다: " } + ResolvedMaterialPath);
                            NewMaterial.MaterialGroupIndex = 0;
                        }
                        else {
                            NewMaterial.MaterialGroupIndex = MaterialGroupIndex;
                        }
                    }
                }

                MaterialGroupIndexForModel = NewMaterial.MaterialGroupIndex;
                OutScene.GetWorld().AddComponent(Entity, NewMaterial);
            }

            bool HasInstantiatedPrefabModel{ false };
            bool FrustumCullingEnabled{ true };
            if (ComponentsNode.has_child(CullingTypeName)) {
                const c4::yml::ConstNodeRef CullingNode{ ComponentsNode[CullingTypeName] };
                Culling NewCulling{};
                if (CullingNode.has_child("frustumCulling")) {
                    CullingNode["frustumCulling"] >> NewCulling.frustumCulling;
                }

                FrustumCullingEnabled = NewCulling.frustumCulling;
                OutScene.GetWorld().AddComponent(Entity, NewCulling);
            }

            if (HasPrefabInstance == true && PrefabModelSelector.empty() == false) {
                const std::shared_ptr<Model> ModelData{ OutScene.GetAssetRegistry().GetModel(PrefabModelSelector) };
                if (ModelData == nullptr) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back(std::string{ "Prefab modelPath 로 Model 로드 실패: " } + PrefabModelSelector);
                }
                else {
                    ModelHierarchySpawnRequest SpawnRequest{};
                    SpawnRequest.ModelData = ModelData;
                    SpawnRequest.RootEntityId = Entity;
                    SpawnRequest.MaterialGroupIndex = MaterialGroupIndexForModel;
                    SpawnRequest.FrustumCullingEnabled = FrustumCullingEnabled;
                    SpawnRequest.IsActive = PrefabIsActive;
                    HasInstantiatedPrefabModel = EntityFactory.SpawnModelHierarchy(SpawnRequest);
                    if (HasInstantiatedPrefabModel == false) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Model RootNode 를 찾을 수 없습니다: " } + PrefabModelSelector);
                    }
                }
            }

            if (ComponentsNode.has_child(TerrainTypeName) && HasInstantiatedPrefabModel == false) {
                const c4::yml::ConstNodeRef TerrainNode{ ComponentsNode[TerrainTypeName] };
                TerrainBuildDesc Desc{};
                const bool IsTerrainDescRead{ TryReadTerrainBuildDesc(TerrainNode, SceneName, Desc) };
                if (IsTerrainDescRead == false) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back("Terrain Component 데이터 해석 실패");
                }
                else {
                    const bool IsInitialStreamingTerrainDescPrepared{ TryPrepareInitialStreamingTerrainBuildDesc(OutScene.GetWorld(), Desc) };
                    if (IsInitialStreamingTerrainDescPrepared == false) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back("Terrain Streaming 초기 데이터 해석 실패");
                        continue;
                    }

                    const std::shared_ptr<TerrainRenderResource> TerrainResource{ OutScene.GetAssetRegistry().GetTerrainRenderResource(Desc) };
                    if (TerrainResource == nullptr) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Terrain 생성 실패: " } + (Desc.mHeightSourceType == TerrainHeightSourceType::Procedural ? std::string{ "Procedural" } : Desc.HeightMapPath));
                    }
                    else {
                        bool IsActive{ true };
                        if (TerrainNode.has_child("active")) {
                            TerrainNode["active"] >> IsActive;
                        }

                        Transform* TerrainTransformComponent{ OutScene.GetWorld().GetComponent<Transform>(Entity) };
                        ApplyInitialStreamingTerrainTransform(*TerrainResource, TerrainTransformComponent);

                        PhysicsTerrainActor::ActorDesc TerrainActorDesc{};
                        const bool IsTerrainActorDescBuilt{ TryBuildTerrainActorDescFromRenderResource(*TerrainResource, TerrainActorDesc) };
                        if (IsTerrainActorDescBuilt == false) {
                            LoadResult.IsSuccess = false;
                            LoadResult.UndecidedItems.push_back("Terrain ActorDesc 생성 실패");
                            continue;
                        }

                        TerrainRenderer TerrainRendererComponent{};
                        TerrainRendererComponent.mResource = TerrainResource.get();
                        TerrainRendererComponent.mTileQuadCount = Desc.TileQuadCount;
                        TerrainRendererComponent.mActive = IsActive;
                        OutScene.GetWorld().AddComponent(Entity, TerrainRendererComponent);

                        BoundingBox TerrainBoundingBox{};
                        TerrainBoundingBox.SetObb(TerrainResource->GetLocalBoundingBox());
                        BoundingBox* ExistingTerrainBoundingBox{ OutScene.GetWorld().GetComponent<BoundingBox>(Entity) };
                        if (ExistingTerrainBoundingBox == nullptr) {
                            OutScene.GetWorld().AddComponent(Entity, TerrainBoundingBox);
                        }
                        else {
                            ExistingTerrainBoundingBox->SetObb(TerrainResource->GetLocalBoundingBox());
                        }

                        Culling TerrainCulling{};
                        TerrainCulling.frustumCulling = FrustumCullingEnabled;
                        Culling* ExistingTerrainCulling{ OutScene.GetWorld().GetComponent<Culling>(Entity) };
                        if (ExistingTerrainCulling == nullptr) {
                            OutScene.GetWorld().AddComponent(Entity, TerrainCulling);
                        }
                        else {
                            ExistingTerrainCulling->frustumCulling = FrustumCullingEnabled;
                        }

                        const Material* ParentTerrainMaterial{ OutScene.GetWorld().GetComponent<Material>(Entity) };
                        const std::vector<TerrainTileMetadata>& TileMetadataItems{ TerrainResource->GetTileMetadata() };
                        for (std::size_t TileMetadataIndex{ 0 }; TileMetadataIndex < TileMetadataItems.size(); ++TileMetadataIndex) {
                            const TerrainTileMetadata& TileMetadata{ TileMetadataItems[TileMetadataIndex] };
                            const Arche::EntityID TileEntity{ EntityFactory.CreateEntity(true) };
                            EntityFactory.AttachChildEntity(Entity, TileEntity);

                            const Name TileName{ CreateNameComponent(std::format("Terrain_Tile_{}_{}", TileMetadata.mTileIndexX, TileMetadata.mTileIndexZ)) };
                            OutScene.GetWorld().AddComponent(TileEntity, TileName);

                            TerrainRenderer TileTerrainRenderer{};
                            TileTerrainRenderer.mResource = TerrainResource.get();
                            TileTerrainRenderer.mTileQuadCount = Desc.TileQuadCount;
                            TileTerrainRenderer.mTileMetadataIndex = static_cast<std::uint32_t>(TileMetadataIndex);
                            TileTerrainRenderer.mActive = IsActive;
                            OutScene.GetWorld().AddComponent(TileEntity, TileTerrainRenderer);

                            Material TileMaterial{};
                            if (ParentTerrainMaterial == nullptr) {
                                TileMaterial.MaterialGroupIndex = MaterialGroupIndexForModel;
                            }
                            else {
                                TileMaterial = *ParentTerrainMaterial;
                            }
                            OutScene.GetWorld().AddComponent(TileEntity, TileMaterial);

                            Culling TileCulling{};
                            TileCulling.frustumCulling = FrustumCullingEnabled;
                            OutScene.GetWorld().AddComponent(TileEntity, TileCulling);

                            BoundingBox TileBoundingBox{};
                            TileBoundingBox.SetObb(TileMetadata.mLocalBoundingBox);
                            OutScene.GetWorld().AddComponent(TileEntity, TileBoundingBox);
                        }

                        OutScene.AddTerrainActorDesc(Entity, TerrainActorDesc);
                        TerrainSurfaceBinding NewTerrainSurfaceBinding{};
                        NewTerrainSurfaceBinding.mEntityId = Entity;
                        NewTerrainSurfaceBinding.mTerrainActorDesc = TerrainActorDesc;
                        TerrainSurfaceBindings.push_back(std::move(NewTerrainSurfaceBinding));
                    }
                }
            }

            if (ComponentsNode.has_child(StaticMeshRendererTypeName) && ComponentsNode.has_child(TerrainTypeName) == false && HasInstantiatedPrefabModel == false) {
                const c4::yml::ConstNodeRef StaticMeshRendererNode{ ComponentsNode[StaticMeshRendererTypeName] };
                if (StaticMeshRendererNode.has_child("modelPath") || StaticMeshRendererNode.has_child("modelPrimitive")) {
                    std::string ModelSelector{};
                    std::string ResolvedModelPath{};
                    bool IsActive{ true };

                    if (StaticMeshRendererNode.has_child("active")) {
                        StaticMeshRendererNode["active"] >> IsActive;
                    }

                    if (StaticMeshRendererNode.has_child("modelPrimitive")) {
                        StaticMeshRendererNode["modelPrimitive"] >> ModelSelector;
                        float PrimitiveSize{ 1.0f };
                        if (StaticMeshRendererNode.has_child("modelPrimitiveSize")) {
                            StaticMeshRendererNode["modelPrimitiveSize"] >> PrimitiveSize;
                        }

                        std::array<float, 4> PrimitiveColor{ { 1.0f, 1.0f, 1.0f, 1.0f } };
                        if (StaticMeshRendererNode.has_child("modelPrimitiveColor")) {
                            ReadColor4(StaticMeshRendererNode["modelPrimitiveColor"], PrimitiveColor.data());
                        }

                        const bool IsPrimitiveSelectorPrefixed{ StartsWith(ModelSelector, "primitive:") };
                        if (IsPrimitiveSelectorPrefixed == false) {
                            ModelSelector = std::string{ "primitive:" } + ModelSelector;
                        }

                        ModelSelector = BuildPrimitiveSelector(ModelSelector, PrimitiveSize, PrimitiveColor);
                        ResolvedModelPath = ModelSelector;
                    }

                    if (ModelSelector.empty() && StaticMeshRendererNode.has_child("modelPath")) {
                        std::string ModelPath{};
                        StaticMeshRendererNode["modelPath"] >> ModelPath;
                        ResolvedModelPath = ResolveSceneResourcePath(SceneName, ModelPath);
                        ModelSelector = ResolvedModelPath;
                    }

                    if (ModelSelector.empty()) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back("StaticMeshRenderer 의 modelPath/modelPrimitive 해석 실패");
                    }
                    else {
                        const std::shared_ptr<Model> ModelData{ OutScene.GetAssetRegistry().GetModel(ModelSelector) };
                        if (ModelData == nullptr) {
                            LoadResult.IsSuccess = false;
                            LoadResult.UndecidedItems.push_back(std::string{ "modelPath 로 Model 로드 실패: " } + ResolvedModelPath);
                        }
                        else {
                            ModelHierarchySpawnRequest SpawnRequest{};
                            SpawnRequest.ModelData = ModelData;
                            SpawnRequest.RootEntityId = Entity;
                            SpawnRequest.MaterialGroupIndex = MaterialGroupIndexForModel;
                            SpawnRequest.FrustumCullingEnabled = FrustumCullingEnabled;
                            SpawnRequest.IsActive = IsActive;
                            const bool IsSpawned{ EntityFactory.SpawnModelHierarchy(SpawnRequest) };
                            if (IsSpawned == false) {
                                LoadResult.IsSuccess = false;
                                LoadResult.UndecidedItems.push_back(std::string{ "Model RootNode 를 찾을 수 없습니다: " } + ModelSelector);
                            }
                        }
                    }
                }
                else {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back(std::string{ "StaticMeshRenderer 의 modelPath/modelPrimitive 없음" });
                }
            }

            if (ComponentsNode.has_child(AnimationTypeName)) {
                const c4::yml::ConstNodeRef AnimationNode{ ComponentsNode[AnimationTypeName] };
                std::string AnimationPath{};
                PendingAnimatorBinding NewBinding{};
                NewBinding.mSourceEntityId = Entity;

                if (AnimationNode.has_child("text")) {
                    AnimationNode["text"] >> AnimationPath;
                }

                if (AnimationNode.has_child("initclip")) {
                    AnimationNode["initclip"] >> NewBinding.mClipIndex;
                    NewBinding.mFallbackClipIndex = NewBinding.mClipIndex;
                }

                if (AnimationNode.has_child("AnimationGraph")) {
                    std::string AnimationGraphPath{};
                    AnimationNode["AnimationGraph"] >> AnimationGraphPath;
                    const std::string ResolvedAnimationGraphPath{ ResolveSceneResourcePath(SceneName, AnimationGraphPath) };
                    const std::shared_ptr<AnimationGraphAsset> AnimationGraphData{ OutScene.GetAssetRegistry().GetAnimationGraph(ResolvedAnimationGraphPath) };
                    if (AnimationGraphData == nullptr) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "AnimationGraph 파일 로드 실패: " } + ResolvedAnimationGraphPath);
                    }
                    else {
                        NewBinding.mAnimationGraphData = AnimationGraphData.get();
                    }
                }

                if (AnimationNode.has_child("node")) {
                    AnimationNode["node"] >> NewBinding.mTargetNodeName;
                }

                if (ComponentsNode.has_child(RuntimeVariablesTypeName)) {
                    const c4::yml::ConstNodeRef RuntimeVariablesNode{ ComponentsNode[RuntimeVariablesTypeName] };
                    for (const c4::yml::ConstNodeRef RuntimeVariableNode : RuntimeVariablesNode.children()) {
                        PendingAnimatorBinding::PendingRuntimeVariableInitialization NewInitialization{};
                        std::string TypeText{};
                        RuntimeVariableNode["Name"] >> NewInitialization.mParameterName;
                        RuntimeVariableNode["Type"] >> TypeText;

                        if (TypeText == "Bool") {
                            NewInitialization.mType = PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Bool;
                            RuntimeVariableNode["Value"] >> NewInitialization.mBoolValue;
                        }
                        else if (TypeText == "Int") {
                            NewInitialization.mType = PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Int;
                            RuntimeVariableNode["Value"] >> NewInitialization.mIntValue;
                        }
                        else if (TypeText == "Float") {
                            NewInitialization.mType = PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Float;
                            RuntimeVariableNode["Value"] >> NewInitialization.mFloatValue;
                        }
                        else {
                            LoadResult.IsSuccess = false;
                            LoadResult.UndecidedItems.push_back(std::string{ "RuntimeVariables Type 값 오류: " } + TypeText);
                            continue;
                        }

                        NewBinding.mRuntimeVariableInitializations.push_back(std::move(NewInitialization));
                    }
                }

                if (AnimationPath.empty() == false) {
                    const std::string ResolvedAnimationPath{ ResolveSceneResourcePath(SceneName, AnimationPath) };
                    const std::shared_ptr<asset::Animation> AnimationData{ OutScene.GetAssetRegistry().GetAnimation(ResolvedAnimationPath) };
                    if (AnimationData == nullptr) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Animation 파일 로드 실패: " } + ResolvedAnimationPath);
                    }
                    else {
                        NewBinding.mAnimationData = AnimationData.get();
                    }
                }

                PendingAnimatorBindings.push_back(std::move(NewBinding));
            }

            if (ComponentsNode.has_child(CameraTypeName)) {
                Camera NewCamera{};
                Frustum NewFrustum{};
                const c4::yml::ConstNodeRef CameraNode{ ComponentsNode[CameraTypeName] };
                if (CameraNode.has_child("fov")) {
                    CameraNode["fov"] >> NewCamera.fov;
                }

                if (CameraNode.has_child("aspectRatio")) {
                    CameraNode["aspectRatio"] >> NewCamera.aspectRatio;
                }

                if (CameraNode.has_child("nearPlane")) {
                    CameraNode["nearPlane"] >> NewCamera.nearPlane;
                }

                if (CameraNode.has_child("farPlane")) {
                    CameraNode["farPlane"] >> NewCamera.farPlane;
                }

                if (CameraNode.has_child("isActive")) {
                    CameraNode["isActive"] >> NewCamera.isActive;
                }

                if (CameraNode.has_child("isOrthographic")) {
                    CameraNode["isOrthographic"] >> NewCamera.isOrthographic;
                }

                if (CameraNode.has_child("orthoSize")) {
                    CameraNode["orthoSize"] >> NewCamera.orthoSize;
                }

                if (CameraNode.has_child("clearColor")) {
                    ReadColor4(CameraNode["clearColor"], NewCamera.clearColor.data());
                }

                if (CameraNode.has_child("startMode")) {
                    std::string CameraModeText{};
                    CameraNode["startMode"] >> CameraModeText;
                    TryParseCameraModeText(CameraModeText, NewCamera.cameraFlags);
                }
                else if (CameraNode.has_child("cameraFlags")) {
                    CameraNode["cameraFlags"] >> NewCamera.cameraFlags;
                }

                if (CameraNode.has_child("thirdPersonFollowTargetEntityId")) {
                    CameraNode["thirdPersonFollowTargetEntityId"] >> NewCamera.thirdPersonFollowTargetSerializedId;
                    DeferredThirdPersonFollowTargetEntities.push_back(std::pair<Arche::EntityID, std::int64_t>{ Entity, NewCamera.thirdPersonFollowTargetSerializedId });
                }

                if (CameraNode.has_child("thirdPersonDistance")) {
                    CameraNode["thirdPersonDistance"] >> NewCamera.thirdPersonDistance;
                }

                if (CameraNode.has_child("thirdPersonMinDistance")) {
                    CameraNode["thirdPersonMinDistance"] >> NewCamera.thirdPersonMinDistance;
                }

                if (CameraNode.has_child("thirdPersonMaxDistance")) {
                    CameraNode["thirdPersonMaxDistance"] >> NewCamera.thirdPersonMaxDistance;
                }

                if (CameraNode.has_child("thirdPersonHeightOffset")) {
                    CameraNode["thirdPersonHeightOffset"] >> NewCamera.thirdPersonHeightOffset;
                }

                if (CameraNode.has_child("thirdPersonOrbitYaw")) {
                    CameraNode["thirdPersonOrbitYaw"] >> NewCamera.thirdPersonOrbitYaw;
                }

                if (CameraNode.has_child("thirdPersonOrbitPitch")) {
                    CameraNode["thirdPersonOrbitPitch"] >> NewCamera.thirdPersonOrbitPitch;
                }

                if (CameraNode.has_child("thirdPersonPositionLerpSpeed")) {
                    CameraNode["thirdPersonPositionLerpSpeed"] >> NewCamera.thirdPersonPositionLerpSpeed;
                }

                if (CameraNode.has_child("thirdPersonZoomSpeed")) {
                    CameraNode["thirdPersonZoomSpeed"] >> NewCamera.thirdPersonZoomSpeed;
                }

                SkySphere NewSkySphere{};
                const bool HasSkySphereNode{ CameraNode.has_child("skySphereEntityId") };
                if (HasSkySphereNode) {
                    CameraNode["skySphereEntityId"] >> NewSkySphere.SkySphereSerializedEntityId;
                    DeferredSkySphereEntities.push_back(std::pair<Arche::EntityID, std::int64_t>{ Entity, NewSkySphere.SkySphereSerializedEntityId });
                }

                OutScene.GetWorld().AddComponent(Entity, NewCamera);
                OutScene.GetWorld().AddComponent(Entity, NewFrustum);
                if (HasSkySphereNode) {
                    OutScene.GetWorld().AddComponent(Entity, NewSkySphere);
                }
            }

            if (ComponentsNode.has_child(TagTypeName)) {
                const c4::yml::ConstNodeRef TagNode{ ComponentsNode[TagTypeName] };
                std::string TagText{};
                if (TagNode.is_val() || TagNode.is_keyval()) {
                    TagNode >> TagText;
                }
                else if (TagNode.has_child("text")) {
                    TagNode["text"] >> TagText;
                }
                else if (TagNode.has_child("Text")) {
                    TagNode["Text"] >> TagText;
                }
                else if (TagNode.has_child("mText")) {
                    TagNode["mText"] >> TagText;
                }

                const Tag NewTag{ Game::CreateTagComponent(TagText) };
                OutScene.GetWorld().AddComponent(Entity, NewTag);
            }

            const bool HasScriptNode{ ComponentsNode.has_child(ScriptTypeName) || ComponentsNode.has_child(ScriptComponentTypeName) || ComponentsNode.has_child(BehaviorInstanceComponentTypeName) };
            if (HasScriptNode) {
                c4::yml::ConstNodeRef ScriptNode{ ComponentsNode };
                if (ComponentsNode.has_child(ScriptTypeName)) {
                    ScriptNode = ComponentsNode[ScriptTypeName];
                }
                else if (ComponentsNode.has_child(ScriptComponentTypeName)) {
                    ScriptNode = ComponentsNode[ScriptComponentTypeName];
                }
                else {
                    ScriptNode = ComponentsNode[BehaviorInstanceComponentTypeName];
                }

                std::string ScriptPath{};
                if (ScriptNode.is_val() || ScriptNode.is_keyval()) {
                    ScriptNode >> ScriptPath;
                }
                else {
                    if (ScriptNode.has_child("path")) {
                        ScriptNode["path"] >> ScriptPath;
                    }
                    else if (ScriptNode.has_child("scriptPath")) {
                        ScriptNode["scriptPath"] >> ScriptPath;
                    }
                    else if (ScriptNode.has_child("text")) {
                        ScriptNode["text"] >> ScriptPath;
                    }
                }

                if (ScriptPath.empty() == false) {
                    const std::string ResolvedScriptPath{ ResolveSceneResourcePath(SceneName, ScriptPath) };
                    const Script::LuaBehaviorFramework::BehaviorOperationResult AttachResult{ OutScene.GetLuaScriptFramework().AttachBehaviorFromFile(Entity, ResolvedScriptPath) };
                    if (!AttachResult) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Script 부착 실패: " } + ResolvedScriptPath + std::string{ " / " } + AttachResult.mError.mMessage);
                    }
                }
            }
        }

        for (const std::pair<Arche::EntityID, std::int64_t>& DeferredParent : DeferredParents) {
            const std::unordered_map<std::int64_t, Arche::EntityID>::const_iterator ParentIter{ EntityBySerializedId.find(DeferredParent.second) };
            if (ParentIter == EntityBySerializedId.end()) {
                continue;
            }

            Game::EntityHierarchy* ChildHierarchy{ OutScene.GetWorld().GetComponent<Game::EntityHierarchy>(DeferredParent.first) };
            Game::EntityHierarchy* ParentHierarchy{ OutScene.GetWorld().GetComponent<Game::EntityHierarchy>(ParentIter->second) };
            if (ChildHierarchy == nullptr || ParentHierarchy == nullptr) {
                continue;
            }

            ChildHierarchy->parent = Arche::NullEntityID;
            ChildHierarchy->nextSibling = Arche::NullEntityID;
            EntityFactory.AttachChildEntity(ParentIter->second, DeferredParent.first);
        }

        for (const PendingBoundingBoxBinding& PendingBoundingBoxBindingItem : PendingBoundingBoxBindings) {
            if (PendingBoundingBoxBindingItem.mEntityId == Arche::NullEntityID) {
                continue;
            }

            DirectX::BoundingOrientedBox LocalBoundingBox{};
            LocalBoundingBox.Center = DirectX::XMFLOAT3{ PendingBoundingBoxBindingItem.mCenter.x, PendingBoundingBoxBindingItem.mCenter.y, PendingBoundingBoxBindingItem.mCenter.z };
            LocalBoundingBox.Extents = DirectX::XMFLOAT3{ PendingBoundingBoxBindingItem.mExtents.x, PendingBoundingBoxBindingItem.mExtents.y, PendingBoundingBoxBindingItem.mExtents.z };
            LocalBoundingBox.Orientation = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };

            BoundingBox* ExistingBoundingBox{ OutScene.GetWorld().GetComponent<BoundingBox>(PendingBoundingBoxBindingItem.mEntityId) };
            if (ExistingBoundingBox == nullptr) {
                BoundingBox NewBoundingBox{};
                NewBoundingBox.SetObb(LocalBoundingBox);
                const Transform* TransformComponent{ std::as_const(OutScene.GetWorld()).GetComponent<Transform>(PendingBoundingBoxBindingItem.mEntityId) };
                if (TransformComponent != nullptr) {
                    const SimpleMath::Matrix TransformOnlyWorldMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent->scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent->rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent->position) };
                    DirectX::BoundingOrientedBox WorldBoundingBox{};
                    LocalBoundingBox.Transform(WorldBoundingBox, TransformOnlyWorldMatrix);
                    NewBoundingBox.SetWorldObb(WorldBoundingBox);
                }
                OutScene.GetWorld().AddComponent(PendingBoundingBoxBindingItem.mEntityId, NewBoundingBox);
            }
            else {
                ExistingBoundingBox->SetObb(LocalBoundingBox);
                const Transform* TransformComponent{ std::as_const(OutScene.GetWorld()).GetComponent<Transform>(PendingBoundingBoxBindingItem.mEntityId) };
                if (TransformComponent != nullptr) {
                    const SimpleMath::Matrix TransformOnlyWorldMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent->scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent->rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent->position) };
                    DirectX::BoundingOrientedBox WorldBoundingBox{};
                    LocalBoundingBox.Transform(WorldBoundingBox, TransformOnlyWorldMatrix);
                    ExistingBoundingBox->SetWorldObb(WorldBoundingBox);
                }
                else {
                    ExistingBoundingBox->InvalidateWorldObb();
                }
            }
        }

        ApplyPendingTerrainSnapBindings(OutScene.GetWorld(), TerrainSurfaceBindings, PendingTerrainSnapBindings);

        for (const std::pair<Arche::EntityID, std::int64_t>& DeferredBoneSkinReferenceEntity : DeferredBoneSkinReferenceEntities) {
            const std::unordered_map<std::int64_t, Arche::EntityID>::const_iterator BoneRootIter{ EntityBySerializedId.find(DeferredBoneSkinReferenceEntity.second) };
            if (BoneRootIter == EntityBySerializedId.end()) {
                continue;
            }

            OutScene.GetWorld().WriteComponent<BoneSkinReference>(DeferredBoneSkinReferenceEntity.first, [ResolvedEntityId = BoneRootIter->second](BoneSkinReference& TargetComponent) {
                TargetComponent.boneRootEntityId = ResolvedEntityId;
            });
        }

        for (const std::pair<Arche::EntityID, std::int64_t>& DeferredThirdPersonFollowTargetEntity : DeferredThirdPersonFollowTargetEntities) {
            const std::unordered_map<std::int64_t, Arche::EntityID>::const_iterator FollowTargetIter{ EntityBySerializedId.find(DeferredThirdPersonFollowTargetEntity.second) };
            if (FollowTargetIter == EntityBySerializedId.end()) {
                continue;
            }

            OutScene.GetWorld().WriteComponent<Camera>(DeferredThirdPersonFollowTargetEntity.first, [ResolvedEntityId = FollowTargetIter->second](Camera& TargetComponent) {
                TargetComponent.thirdPersonFollowTarget = ResolvedEntityId;
            });
        }

        for (const std::pair<Arche::EntityID, std::int64_t>& DeferredSkySphereEntity : DeferredSkySphereEntities) {
            const std::unordered_map<std::int64_t, Arche::EntityID>::const_iterator SkySphereIter{ EntityBySerializedId.find(DeferredSkySphereEntity.second) };
            if (SkySphereIter == EntityBySerializedId.end()) {
                continue;
            }

            OutScene.GetWorld().WriteComponent<SkySphere>(DeferredSkySphereEntity.first, [ResolvedEntityId = SkySphereIter->second](SkySphere& TargetComponent) {
                TargetComponent.SkySphereEntityId = ResolvedEntityId;
            });
        }

        for (const PendingAnimatorBinding& Binding : PendingAnimatorBindings) {
            if (Binding.mAnimationData == nullptr) {
                StdOutput::WriteWarningLine(std::format("[SceneYamlSerializer] Animation data is null. source={}:{} node={}", Binding.mSourceEntityId.index, Binding.mSourceEntityId.generation, Binding.mTargetNodeName));
                continue;
            }

            Arche::EntityID TargetEntityId{ Binding.mSourceEntityId };
            if (Binding.mTargetNodeName.empty() == false) {
                const bool IsFound{ TryFindEntityByNameInHierarchy(&OutScene.GetWorld(), Binding.mSourceEntityId, Binding.mTargetNodeName, TargetEntityId, false) };
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

            Animator* ExistingAnimator{ OutScene.GetWorld().GetComponent<Animator>(TargetEntityId) };
            if (ExistingAnimator == nullptr) {
                OutScene.GetWorld().AddComponent(TargetEntityId, NewAnimator);
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

                AnimatorGraphPlayer* ExistingPlayer{ OutScene.GetWorld().GetComponent<AnimatorGraphPlayer>(TargetEntityId) };
                if (ExistingPlayer == nullptr) {
                    OutScene.GetWorld().AddComponent(TargetEntityId, Player);
                }
                else {
                    *ExistingPlayer = Player;
                }

                RuntimeVariableTable* ExistingVariableTable{ OutScene.GetWorld().GetComponent<RuntimeVariableTable>(TargetEntityId) };
                if (ExistingVariableTable == nullptr) {
                    OutScene.GetWorld().AddComponent(TargetEntityId, VariableTable);
                }
                else {
                    *ExistingVariableTable = VariableTable;
                }
            }
        }

        OutScene.RebuildPhysicsActors();
        OutScene.BuildSystemExecutionPlan();
        return LoadResult;
    }
}
