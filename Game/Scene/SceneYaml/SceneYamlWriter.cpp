#include "SceneYamlInternal.h"

namespace Game::SceneYaml {
    SceneYamlSaveResult SceneYamlWriter::Serialize(const SceneWorldSnapshot& TargetSnapshot, std::string& OutYamlText) const {
        SceneYamlSaveResult SaveResult{};
        std::ostringstream Stream{};
        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ TargetSnapshot.GetReadOnlyWorld() };
        const AssetRegistry* AssetRegistryInstance{ TargetSnapshot.GetAssetRegistry() };

        if (ReadOnlyWorld == nullptr) {
            SaveResult.IsSuccess = false;
            SaveResult.UndecidedItems.push_back("Scene Snapshot 에 ReadOnlyWorld 가 바인딩되어 있지 않습니다.");
            OutYamlText.clear();
            return SaveResult;
        }

        if (AssetRegistryInstance == nullptr) {
            SaveResult.IsSuccess = false;
            SaveResult.UndecidedItems.push_back("Scene Snapshot 에 AssetRegistry 가 바인딩되어 있지 않습니다.");
            OutYamlText.clear();
            return SaveResult;
        }

        AppendLine(Stream, 0, std::string{ "SceneName: " } + ToYamlText(TargetSnapshot.GetSceneName()));
        if (TargetSnapshot.GetSystemNames().empty()) {
            AppendLine(Stream, 0, "Systems: []");
        }
        else {
            AppendLine(Stream, 0, "Systems:");

            for (const std::string& SystemName : TargetSnapshot.GetSystemNames()) {
                AppendLine(Stream, 1, std::string{ "- Type: " } + ToYamlText(SystemName));
            }
        }

        std::unordered_map<std::uint64_t, PrefabDescriptor> PrefabDescriptors{};
        for (const SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot : TargetSnapshot.GetEntities()) {
            const Arche::EntityID EntityId{ EntitySnapshot.mEntityId };
            if (EntityId.IsDerivedEntity()) {
                continue;
            }

            const PrefabInstance* PrefabInstanceComponent{ ReadOnlyWorld->GetComponent<PrefabInstance>(EntityId) };
            if (PrefabInstanceComponent == nullptr || PrefabInstanceComponent->PrefabId == 0ull) {
                continue;
            }

            PrefabDescriptor Descriptor{};
            Descriptor.mPrefabId = PrefabInstanceComponent->PrefabId;

            const StaticMeshRenderer* ResolvedRenderer{ nullptr };
            if (TryFindRendererInHierarchy(ReadOnlyWorld, EntityId, ResolvedRenderer) == true && ResolvedRenderer != nullptr) {
                const std::string ModelSelector{ AssetRegistryInstance->FindModelSelectorByPointer(ResolvedRenderer->model) };
                Descriptor.mModelSelector = MakeSceneRelativeResourcePath(TargetSnapshot.GetSceneName(), ModelSelector);
                Descriptor.mActive = ResolvedRenderer->active;
            }

            std::uint32_t ResolvedMaterialGroupIndex{ 0 };
            if (TryResolveMaterialGroupIndexInHierarchy(ReadOnlyWorld, EntityId, ResolvedMaterialGroupIndex) == true) {
                const std::string MaterialPath{ AssetRegistryInstance->FindMaterialGroupSourcePathByIndex(ResolvedMaterialGroupIndex) };
                if (IsDefaultMaterialPath(MaterialPath) == false) {
                    Descriptor.mMaterialPath = MakeSceneRelativeResourcePath(TargetSnapshot.GetSceneName(), MaterialPath);
                }
            }

            PrefabDescriptors[Descriptor.mPrefabId] = Descriptor;
        }

        if (PrefabDescriptors.empty() == false) {
            AppendLine(Stream, 0, "Prefabs:");
            for (const std::pair<const std::uint64_t, PrefabDescriptor>& PrefabPair : PrefabDescriptors) {
                const PrefabDescriptor& Descriptor{ PrefabPair.second };
                AppendLine(Stream, 1, std::string{ "- &Prefab" } + std::to_string(Descriptor.mPrefabId));
                AppendLine(Stream, 2, std::string{ "prefabId: " } + std::to_string(Descriptor.mPrefabId));
                if (Descriptor.mModelSelector.empty()) {
                    SaveResult.IsSuccess = false;
                    SaveResult.UndecidedItems.push_back(std::string{ "PrefabId 에 대응되는 modelPath 를 찾지 못했습니다: " } + std::to_string(Descriptor.mPrefabId));
                }

                AppendLine(Stream, 2, std::string{ "modelPath: " } + ToYamlText(Descriptor.mModelSelector));
                AppendLine(Stream, 2, std::string{ "materialPath: " } + ToYamlText(Descriptor.mMaterialPath));
                AppendLine(Stream, 2, std::string{ "active: " } + ToYamlBooleanText(Descriptor.mActive));
            }
        }

        AppendLine(Stream, 0, "Entities:");

        std::unordered_map<Arche::EntityID, std::uint32_t> SerializedEntityIds{};
        std::uint32_t NextSerializedEntityId{ 0 };
        for (const SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot : TargetSnapshot.GetEntities()) {
            if (EntitySnapshot.mEntityId.IsDerivedEntity()) {
                continue;
            }

            SerializedEntityIds[EntitySnapshot.mEntityId] = NextSerializedEntityId;
            NextSerializedEntityId += 1;
        }

        for (const SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot : TargetSnapshot.GetEntities()) {
            const Arche::EntityID EntityId{ EntitySnapshot.mEntityId };
            if (EntityId.IsDerivedEntity()) {
                continue;
            }

            const std::unordered_map<Arche::EntityID, std::uint32_t>::const_iterator SerializedEntityIter{ SerializedEntityIds.find(EntityId) };
            if (SerializedEntityIter == SerializedEntityIds.end()) {
                continue;
            }

            AppendLine(Stream, 1, std::string{ "- EntityId: " } + std::to_string(SerializedEntityIter->second));
            const std::unordered_map<Arche::EntityID, std::uint32_t>::const_iterator SerializedParentIter{ SerializedEntityIds.find(EntitySnapshot.mParentId) };
            if (SerializedParentIter == SerializedEntityIds.end()) {
                AppendLine(Stream, 2, "ParentEntityId: -1");
            }
            else {
                AppendLine(Stream, 2, std::string{ "ParentEntityId: " } + std::to_string(SerializedParentIter->second));
            }

            const Name* NameComponent{ ReadOnlyWorld->GetComponent<Game::Name>(EntityId) };
            const Transform* TransformComponent{ ReadOnlyWorld->GetComponent<Game::Transform>(EntityId) };
            const DirectionalLight* DirectionalLightComponent{ ReadOnlyWorld->GetComponent<Game::DirectionalLight>(EntityId) };
            const BoneSkinReference* BoneSkinReferenceComponent{ ReadOnlyWorld->GetComponent<BoneSkinReference>(EntityId) };
            const FootIKRig* FootIKRigComponent{ ReadOnlyWorld->GetComponent<FootIKRig>(EntityId) };
            const Material* MaterialComponent{ ReadOnlyWorld->GetComponent<Material>(EntityId) };
            const StaticMeshRenderer* StaticMeshRendererComponent{ ReadOnlyWorld->GetComponent<StaticMeshRenderer>(EntityId) };
            const TerrainRenderer* TerrainRendererComponent{ ReadOnlyWorld->GetComponent<TerrainRenderer>(EntityId) };
            const Culling* CullingComponent{ ReadOnlyWorld->GetComponent<Culling>(EntityId) };
            const Camera* CameraComponent{ ReadOnlyWorld->GetComponent<Camera>(EntityId) };
            const SkySphere* SkySphereComponent{ ReadOnlyWorld->GetComponent<SkySphere>(EntityId) };
            const Tag* TagComponent{ ReadOnlyWorld->GetComponent<Tag>(EntityId) };
            const PhysicsActorSettings* PhysicsActorSettingsComponent{ ReadOnlyWorld->GetComponent<PhysicsActorSettings>(EntityId) };
            const Animator* AnimatorComponent{ nullptr };
            Arche::EntityID AnimatorEntityId{ Arche::NullEntityID };
            const bool IsAnimatorFound{ TryFindAnimatorForSerializationInHierarchy(ReadOnlyWorld, EntityId, AnimatorComponent, AnimatorEntityId) };
            static_cast<void>(IsAnimatorFound);

            if (ShouldSkipEntityInSceneExport(StaticMeshRendererComponent)) {
                continue;
            }

            AppendLine(Stream, 2, "Components:");

            if (TagComponent != nullptr) {
                AppendLine(Stream, 3, std::string{ TagTypeName } + std::string{ ":" });
                AppendLine(Stream, 4, std::string{ "text: " } + ToYamlText(Game::GetTagTextView(*TagComponent)));
            }

            if (NameComponent != nullptr) {
                AppendLine(Stream, 3, std::string{ NameTypeName } + std::string{ ":" });
                AppendLine(Stream, 4, std::string{ "text: " } + ToYamlText(Game::GetNameText(*NameComponent)));
            }

            if (TransformComponent != nullptr) {
                AppendLine(Stream, 3, std::string{ TransformTypeName } + std::string{ ":" });
                AppendVector3(Stream, 4, "position", TransformComponent->position);
                AppendVector3(Stream, 4, "rotationEuler", TransformComponent->rotationEuler);
                AppendQuaternion(Stream, 4, "rotation", TransformComponent->rotation);
                AppendVector3(Stream, 4, "scale", TransformComponent->scale);
            }

            if (DirectionalLightComponent != nullptr) {
                AppendLine(Stream, 3, std::string{ DirectionalLightTypeName } + std::string{ ":" });
                AppendLine(Stream, 4, std::string{ "isActive: " } + ToYamlBooleanText(DirectionalLightComponent->mIsActive));
                AppendLine(Stream, 4, std::string{ "castsShadow: " } + ToYamlBooleanText(DirectionalLightComponent->mCastsShadow));
                AppendLine(Stream, 4, std::string{ "useTransformDirection: " } + ToYamlBooleanText(DirectionalLightComponent->mUseTransformDirection));
                AppendVector3(Stream, 4, "direction", DirectionalLightComponent->mDirection);
                AppendVector3(Stream, 4, "color", DirectionalLightComponent->mColor);
                AppendLine(Stream, 4, std::string{ "intensity: " } + std::to_string(DirectionalLightComponent->mIntensity));
                AppendLine(Stream, 4, std::string{ "ambientIntensity: " } + std::to_string(DirectionalLightComponent->mAmbientIntensity));
            }

            if (BoneSkinReferenceComponent != nullptr) {
                AppendLine(Stream, 3, std::string{ BoneSkinReferenceTypeName } + std::string{ ":" });
                const std::unordered_map<Arche::EntityID, std::uint32_t>::const_iterator BoneRootSerializedIter{ SerializedEntityIds.find(BoneSkinReferenceComponent->boneRootEntityId) };
                const std::int32_t BoneRootSerializedId{ BoneRootSerializedIter == SerializedEntityIds.end() ? -1 : static_cast<std::int32_t>(BoneRootSerializedIter->second) };
                AppendLine(Stream, 4, std::string{ "boneRootEntityId: " } + std::to_string(BoneRootSerializedId));
            }

            if (FootIKRigComponent != nullptr) {
                AppendLine(Stream, 3, std::string{ FootIKRigTypeName } + std::string{ ":" });
                AppendLine(Stream, 4, std::string{ "enabled: " } + ToYamlBooleanText(FootIKRigComponent->mEnabled));
                AppendLine(Stream, 4, std::string{ "leftFootBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mLeftFootBoneName)));
                AppendLine(Stream, 4, std::string{ "rightFootBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mRightFootBoneName)));
                AppendLine(Stream, 4, std::string{ "leftToeBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mLeftToeBoneName)));
                AppendLine(Stream, 4, std::string{ "rightToeBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mRightToeBoneName)));
                AppendLine(Stream, 4, std::string{ "leftShinBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mLeftShinBoneName)));
                AppendLine(Stream, 4, std::string{ "rightShinBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mRightShinBoneName)));
                AppendLine(Stream, 4, std::string{ "leftThighBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mLeftThighBoneName)));
                AppendLine(Stream, 4, std::string{ "rightThighBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mRightThighBoneName)));
                AppendLine(Stream, 4, std::string{ "pelvisBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mPelvisBoneName)));
                AppendLine(Stream, 4, std::string{ "blendSpeed: " } + std::to_string(FootIKRigComponent->mBlendSpeed));
                AppendLine(Stream, 4, std::string{ "maxLift: " } + std::to_string(FootIKRigComponent->mMaxLift));
                AppendLine(Stream, 4, std::string{ "maxDrop: " } + std::to_string(FootIKRigComponent->mMaxDrop));
            }

            if (MaterialComponent != nullptr) {
                AppendLine(Stream, 3, std::string{ MaterialTypeName } + std::string{ ":" });
                const std::string MaterialPath{ AssetRegistryInstance->FindMaterialGroupSourcePathByIndex(MaterialComponent->MaterialGroupIndex) };
                if (MaterialPath.empty()) {
                    SaveResult.IsSuccess = false;
                    SaveResult.UndecidedItems.push_back(std::string{ "MaterialGroupIndex 에 대응되는 materialPath 를 찾지 못했습니다: " } + std::to_string(MaterialComponent->MaterialGroupIndex));
                }

                const std::string MaterialPathForYaml{ IsDefaultMaterialPath(MaterialPath) ? std::string{} : MakeSceneRelativeResourcePath(TargetSnapshot.GetSceneName(), MaterialPath) };
                AppendLine(Stream, 4, std::string{ "materialPath: " } + ToYamlText(MaterialPathForYaml));
            }

            AppendLine(Stream, 3, std::string{ CullingTypeName } + std::string{ ":" });
            const bool IsFrustumCullingEnabled{ CullingComponent == nullptr ? true : CullingComponent->frustumCulling };
            AppendLine(Stream, 4, std::string{ "frustumCulling: " } + ToYamlBooleanText(IsFrustumCullingEnabled));

            if (AnimatorComponent != nullptr) {
                AppendLine(Stream, 3, std::string{ AnimationTypeName } + std::string{ ":" });
                const std::string AnimationSelector{ AssetRegistryInstance->FindAnimationSelectorByPointer(AnimatorComponent->animation) };
                const std::string AnimationSelectorForYaml{ MakeSceneRelativeResourcePath(TargetSnapshot.GetSceneName(), AnimationSelector) };
                AppendLine(Stream, 4, std::string{ "text: " } + ToYamlText(AnimationSelectorForYaml));
                AppendLine(Stream, 4, std::string{ "initclip: " } + std::to_string(AnimatorComponent->FallbackClipIndex));
                const std::string AnimationGraphSelector{ AssetRegistryInstance->FindAnimationGraphSelectorByPointer(AnimatorComponent->GraphAsset) };
                const std::string AnimationGraphSelectorForYaml{ MakeSceneRelativeResourcePath(TargetSnapshot.GetSceneName(), AnimationGraphSelector) };
                if (AnimationGraphSelectorForYaml.empty() == false) {
                    AppendLine(Stream, 4, std::string{ "AnimationGraph: " } + ToYamlText(AnimationGraphSelectorForYaml));
                }
            }

            const BoundingBox* BoundingBoxComponent{ ReadOnlyWorld->GetComponent<BoundingBox>(EntityId) };
            if (PhysicsActorSettingsComponent != nullptr) {
                AppendPhysicsActorSettings(Stream, 3, *PhysicsActorSettingsComponent, BoundingBoxComponent);
            }
            else if (BoundingBoxComponent != nullptr) {
                AppendBoundingBox(Stream, 3, BoundingBoxComponent->GetObb());
            }

            const PrefabInstance* PrefabInstanceComponent{ ReadOnlyWorld->GetComponent<PrefabInstance>(EntityId) };
            if (PrefabInstanceComponent != nullptr && PrefabInstanceComponent->PrefabId != 0ull) {
                AppendLine(Stream, 3, std::string{ PrefabInstanceTypeName } + std::string{ ":" });
                AppendLine(Stream, 4, std::string{ "<<: *Prefab" } + std::to_string(PrefabInstanceComponent->PrefabId));
                AppendLine(Stream, 4, std::string{ "prefabId: " } + std::to_string(PrefabInstanceComponent->PrefabId));
            }

            if (StaticMeshRendererComponent != nullptr && PrefabInstanceComponent == nullptr) {
                const std::string ModelSelector{ AssetRegistryInstance->FindModelSelectorByPointer(StaticMeshRendererComponent->model) };
                if (ModelSelector.empty()) {
                    SaveResult.IsSuccess = false;
                    SaveResult.UndecidedItems.push_back("StaticMeshRenderer model 포인터에 대응되는 selector 를 찾지 못했습니다.");
                }
                else {
                    TerrainBuildDesc TerrainDesc{};
                    const bool IsTerrainModel{ TryParseTerrainModelSelector(ModelSelector, TerrainDesc) };
                    if (IsTerrainModel == true) {
                        AppendTerrainBuildDesc(Stream, 3, TargetSnapshot.GetSceneName(), TerrainDesc);
                        AppendLine(Stream, 4, std::string{ "active: " } + ToYamlBooleanText(StaticMeshRendererComponent->active));
                    }
                    else {
                        AppendLine(Stream, 3, std::string{ StaticMeshRendererTypeName } + std::string{ ":" });
                        const std::string ModelSelectorForYaml{ MakeSceneRelativeResourcePath(TargetSnapshot.GetSceneName(), ModelSelector) };
                        AppendLine(Stream, 4, std::string{ "modelPath: " } + ToYamlText(ModelSelectorForYaml));
                        AppendLine(Stream, 4, std::string{ "active: " } + ToYamlBooleanText(StaticMeshRendererComponent->active));
                    }
                }
            }

            if (TerrainRendererComponent != nullptr && PrefabInstanceComponent == nullptr) {
                const std::string TerrainSelector{ AssetRegistryInstance->FindTerrainRenderResourceSelectorByPointer(TerrainRendererComponent->mResource) };
                if (TerrainSelector.empty()) {
                    SaveResult.IsSuccess = false;
                    SaveResult.UndecidedItems.push_back("TerrainRenderer resource 포인터에 대응되는 selector 를 찾지 못했습니다.");
                }
                else {
                    TerrainBuildDesc TerrainDesc{};
                    const bool IsTerrainResource{ TryParseTerrainModelSelector(TerrainSelector, TerrainDesc) };
                    if (IsTerrainResource == true) {
                        AppendTerrainBuildDesc(Stream, 3, TargetSnapshot.GetSceneName(), TerrainDesc);
                        AppendLine(Stream, 4, std::string{ "active: " } + ToYamlBooleanText(TerrainRendererComponent->mActive));
                    }
                    else {
                        SaveResult.IsSuccess = false;
                        SaveResult.UndecidedItems.push_back("TerrainRenderer selector 해석 실패");
                    }
                }
            }

            if (CameraComponent != nullptr) {
                AppendLine(Stream, 3, std::string{ CameraTypeName } + std::string{ ":" });
                AppendLine(Stream, 4, std::string{ "fov: " } + std::to_string(CameraComponent->fov));
                AppendLine(Stream, 4, std::string{ "aspectRatio: " } + std::to_string(CameraComponent->aspectRatio));
                AppendLine(Stream, 4, std::string{ "nearPlane: " } + std::to_string(CameraComponent->nearPlane));
                AppendLine(Stream, 4, std::string{ "farPlane: " } + std::to_string(CameraComponent->farPlane));
                AppendLine(Stream, 4, std::string{ "isActive: " } + ToYamlBooleanText(CameraComponent->isActive));
                AppendLine(Stream, 4, std::string{ "isOrthographic: " } + ToYamlBooleanText(CameraComponent->isOrthographic));
                AppendLine(Stream, 4, std::string{ "orthoSize: " } + std::to_string(CameraComponent->orthoSize));
                AppendColor4(Stream, 4, "clearColor", CameraComponent->clearColor.data());
                const std::unordered_map<Arche::EntityID, std::uint32_t>::const_iterator ThirdPersonFollowTargetSerializedIter{ SerializedEntityIds.find(CameraComponent->thirdPersonFollowTarget) };
                const std::int32_t ThirdPersonFollowTargetSerializedId{ ThirdPersonFollowTargetSerializedIter == SerializedEntityIds.end() ? -1 : static_cast<std::int32_t>(ThirdPersonFollowTargetSerializedIter->second) };
                AppendLine(Stream, 4, std::string{ "thirdPersonFollowTargetEntityId: " } + std::to_string(ThirdPersonFollowTargetSerializedId));
                AppendLine(Stream, 4, std::string{ "thirdPersonDistance: " } + std::to_string(CameraComponent->thirdPersonDistance));
                AppendLine(Stream, 4, std::string{ "thirdPersonMinDistance: " } + std::to_string(CameraComponent->thirdPersonMinDistance));
                AppendLine(Stream, 4, std::string{ "thirdPersonMaxDistance: " } + std::to_string(CameraComponent->thirdPersonMaxDistance));
                AppendLine(Stream, 4, std::string{ "thirdPersonHeightOffset: " } + std::to_string(CameraComponent->thirdPersonHeightOffset));
                AppendLine(Stream, 4, std::string{ "thirdPersonOrbitYaw: " } + std::to_string(CameraComponent->thirdPersonOrbitYaw));
                AppendLine(Stream, 4, std::string{ "thirdPersonOrbitPitch: " } + std::to_string(CameraComponent->thirdPersonOrbitPitch));
                AppendLine(Stream, 4, std::string{ "thirdPersonPositionLerpSpeed: " } + std::to_string(CameraComponent->thirdPersonPositionLerpSpeed));
                AppendLine(Stream, 4, std::string{ "thirdPersonZoomSpeed: " } + std::to_string(CameraComponent->thirdPersonZoomSpeed));
                if (SkySphereComponent != nullptr) {
                    const std::unordered_map<Arche::EntityID, std::uint32_t>::const_iterator SkySphereSerializedIter{ SerializedEntityIds.find(SkySphereComponent->SkySphereEntityId) };
                    const std::int32_t SkySphereSerializedId{ SkySphereSerializedIter == SerializedEntityIds.end() ? -1 : static_cast<std::int32_t>(SkySphereSerializedIter->second) };
                    AppendLine(Stream, 4, std::string{ "skySphereEntityId: " } + std::to_string(SkySphereSerializedId));
                }

                AppendLine(Stream, 4, std::string{ "startMode: " } + ResolveCameraModeText(CameraComponent->cameraFlags));
            }

        }

        OutYamlText = Stream.str();
        return SaveResult;
    }
}
