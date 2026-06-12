#include "SceneYamlBasicComponents.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/DirectionalLight.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/PrefabInstance.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Tags.h"
#include "Game/Scene/SceneYaml/SceneYamlReadUtils.h"
#include "Game/Scene/SceneYaml/SceneYamlTerrain.h"
#include "Game/Scene/SceneYaml/SceneYamlWriteUtils.h"

namespace Game::SceneYaml {
    const char* SceneYamlTagComponentReader::TypeName() {
        return TagTypeName;
    }

    void SceneYamlTagComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        const c4::yml::ConstNodeRef TagNode{ ComponentsNode[TypeName()] };
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
        LoadContext.mScene.GetWorld().AddComponent(Entity, NewTag);
    }

    const char* SceneYamlTagComponentWriter::TypeName() {
        return TagTypeName;
    }

    void SceneYamlTagComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const Tag* TagComponent{ WriteContext.mReadOnlyWorld.GetComponent<Tag>(EntityId) };
        if (TagComponent == nullptr) {
            return;
        }

        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        AppendLine(WriteContext.mStream, 4, std::string{ "text: " } + ToYamlText(Game::GetTagTextView(*TagComponent)));
    }

    const char* SceneYamlDirectionalLightComponentReader::TypeName() {
        return DirectionalLightTypeName;
    }

    void SceneYamlDirectionalLightComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        DirectionalLight NewDirectionalLight{};
        const c4::yml::ConstNodeRef DirectionalLightNode{ ComponentsNode[TypeName()] };
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

        LoadContext.mScene.GetWorld().AddComponent(Entity, NewDirectionalLight);
    }

    const char* SceneYamlDirectionalLightComponentWriter::TypeName() {
        return DirectionalLightTypeName;
    }

    void SceneYamlDirectionalLightComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const DirectionalLight* DirectionalLightComponent{ WriteContext.mReadOnlyWorld.GetComponent<Game::DirectionalLight>(EntityId) };
        if (DirectionalLightComponent == nullptr) {
            return;
        }

        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        AppendLine(WriteContext.mStream, 4, std::string{ "isActive: " } + ToYamlBooleanText(DirectionalLightComponent->mIsActive));
        AppendLine(WriteContext.mStream, 4, std::string{ "castsShadow: " } + ToYamlBooleanText(DirectionalLightComponent->mCastsShadow));
        AppendLine(WriteContext.mStream, 4, std::string{ "useTransformDirection: " } + ToYamlBooleanText(DirectionalLightComponent->mUseTransformDirection));
        AppendVector3(WriteContext.mStream, 4, "direction", DirectionalLightComponent->mDirection);
        AppendVector3(WriteContext.mStream, 4, "color", DirectionalLightComponent->mColor);
        AppendLine(WriteContext.mStream, 4, std::string{ "intensity: " } + std::to_string(DirectionalLightComponent->mIntensity));
        AppendLine(WriteContext.mStream, 4, std::string{ "ambientIntensity: " } + std::to_string(DirectionalLightComponent->mAmbientIntensity));
    }

    const char* SceneYamlBoundingBoxComponentReader::TypeName() {
        return BoundingBoxTypeName;
    }

    void SceneYamlBoundingBoxComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        PendingBoundingBoxBinding NewPendingBinding{};
        if (TryReadBoundingBoxBinding(ComponentsNode[TypeName()], Entity, NewPendingBinding) == true) {
            LoadContext.mPendingBoundingBoxBindings.push_back(NewPendingBinding);
        }
    }

    const char* SceneYamlBoundingBoxComponentWriter::TypeName() {
        return BoundingBoxTypeName;
    }

    void SceneYamlBoundingBoxComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const PhysicsActorSettings* PhysicsActorSettingsComponent{ WriteContext.mReadOnlyWorld.GetComponent<PhysicsActorSettings>(EntityId) };
        if (PhysicsActorSettingsComponent != nullptr) {
            return;
        }

        const BoundingBox* BoundingBoxComponent{ WriteContext.mReadOnlyWorld.GetComponent<BoundingBox>(EntityId) };
        if (BoundingBoxComponent == nullptr) {
            return;
        }

        AppendBoundingBox(WriteContext.mStream, 3, BoundingBoxComponent->GetObb());
    }

    const char* SceneYamlPrefabInstanceComponentReader::TypeName() {
        return PrefabInstanceTypeName;
    }

    void SceneYamlPrefabInstanceComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        PrefabInstance NewPrefabInstance{};
        const c4::yml::ConstNodeRef PrefabNode{ ComponentsNode[TypeName()] };
        if (PrefabNode.has_child("prefabId")) {
            PrefabNode["prefabId"] >> NewPrefabInstance.PrefabId;
        }

        if (NewPrefabInstance.PrefabId == 0ull) {
            return;
        }

        LoadContext.mScene.GetWorld().AddComponent(Entity, NewPrefabInstance);
        const std::unordered_map<std::uint64_t, PrefabDescriptor>::const_iterator PrefabIter{ LoadContext.mPrefabDescriptors.find(NewPrefabInstance.PrefabId) };
        if (PrefabIter == LoadContext.mPrefabDescriptors.end()) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "Prefab descriptor 를 찾을 수 없습니다. prefabId: " } + std::to_string(NewPrefabInstance.PrefabId));
            return;
        }

        ReadState.mHasPrefabInstance = true;
        ReadState.mPrefabModelSelector = PrefabIter->second.mModelSelector;
        ReadState.mPrefabIsActive = PrefabIter->second.mActive;

        if (PrefabIter->second.mMaterialPath.empty() == false) {
            const std::string ResolvedPrefabMaterialPath{ ResolveSceneResourcePath(LoadContext.mSceneName, PrefabIter->second.mMaterialPath) };
            const bool IsLoaded{ LoadContext.mScene.GetAssetRegistry().LoadMaterialGroups(ResolvedPrefabMaterialPath) };
            if (IsLoaded == false) {
                LoadContext.mLoadResult.IsSuccess = false;
                LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "Prefab Material 파일 로드 실패: " } + ResolvedPrefabMaterialPath);
            }
            else {
                const std::uint32_t MaterialGroupIndex{ LoadContext.mScene.GetAssetRegistry().FindMaterialGroupIndexBySourcePath(ResolvedPrefabMaterialPath) };
                if (MaterialGroupIndex == static_cast<std::uint32_t>(-1)) {
                    LoadContext.mLoadResult.IsSuccess = false;
                    LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "Prefab MaterialGroupIndex 해석 실패: " } + ResolvedPrefabMaterialPath);
                }
                else {
                    ReadState.mMaterialGroupIndexForModel = MaterialGroupIndex;
                }
            }
        }
        else {
            ReadState.mMaterialGroupIndexForModel = 0;
        }
    }

    const char* SceneYamlPrefabInstanceComponentWriter::TypeName() {
        return PrefabInstanceTypeName;
    }

    void SceneYamlPrefabInstanceComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const PrefabInstance* PrefabInstanceComponent{ WriteContext.mReadOnlyWorld.GetComponent<PrefabInstance>(EntityId) };
        if (PrefabInstanceComponent == nullptr || PrefabInstanceComponent->PrefabId == 0ull) {
            return;
        }

        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        AppendLine(WriteContext.mStream, 4, std::string{ "<<: *Prefab" } + std::to_string(PrefabInstanceComponent->PrefabId));
        AppendLine(WriteContext.mStream, 4, std::string{ "prefabId: " } + std::to_string(PrefabInstanceComponent->PrefabId));
    }

    const char* SceneYamlBoneSkinReferenceComponentReader::TypeName() {
        return BoneSkinReferenceTypeName;
    }

    void SceneYamlBoneSkinReferenceComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        BoneSkinReference NewBoneSkinReference{};
        const c4::yml::ConstNodeRef BoneSkinReferenceNode{ ComponentsNode[TypeName()] };
        LoadContext.mScene.GetWorld().AddComponent(Entity, NewBoneSkinReference);

        if (BoneSkinReferenceNode.has_child("boneRootEntityId")) {
            std::int64_t SerializedBoneRootEntityId{ -1 };
            BoneSkinReferenceNode["boneRootEntityId"] >> SerializedBoneRootEntityId;
            LoadContext.mDeferredBoneSkinReferenceEntities.push_back(std::pair<Arche::EntityID, std::int64_t>{ Entity, SerializedBoneRootEntityId });
        }
    }

    const char* SceneYamlBoneSkinReferenceComponentWriter::TypeName() {
        return BoneSkinReferenceTypeName;
    }

    void SceneYamlBoneSkinReferenceComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const BoneSkinReference* BoneSkinReferenceComponent{ WriteContext.mReadOnlyWorld.GetComponent<BoneSkinReference>(EntityId) };
        if (BoneSkinReferenceComponent == nullptr) {
            return;
        }

        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        const std::unordered_map<Arche::EntityID, std::uint32_t>::const_iterator BoneRootSerializedIter{ WriteContext.mSerializedEntityIds.find(BoneSkinReferenceComponent->boneRootEntityId) };
        const std::int32_t BoneRootSerializedId{ BoneRootSerializedIter == WriteContext.mSerializedEntityIds.end() ? -1 : static_cast<std::int32_t>(BoneRootSerializedIter->second) };
        AppendLine(WriteContext.mStream, 4, std::string{ "boneRootEntityId: " } + std::to_string(BoneRootSerializedId));
    }

    const char* SceneYamlFootIKRigComponentReader::TypeName() {
        return FootIKRigTypeName;
    }

    void SceneYamlFootIKRigComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        FootIKRig NewFootIKRig{};
        const c4::yml::ConstNodeRef FootIKRigNode{ ComponentsNode[TypeName()] };
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

        LoadContext.mScene.GetWorld().AddComponent(Entity, NewFootIKRig);
    }

    const char* SceneYamlFootIKRigComponentWriter::TypeName() {
        return FootIKRigTypeName;
    }

    void SceneYamlFootIKRigComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const FootIKRig* FootIKRigComponent{ WriteContext.mReadOnlyWorld.GetComponent<FootIKRig>(EntityId) };
        if (FootIKRigComponent == nullptr) {
            return;
        }

        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        AppendLine(WriteContext.mStream, 4, std::string{ "enabled: " } + ToYamlBooleanText(FootIKRigComponent->mEnabled));
        AppendLine(WriteContext.mStream, 4, std::string{ "leftFootBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mLeftFootBoneName)));
        AppendLine(WriteContext.mStream, 4, std::string{ "rightFootBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mRightFootBoneName)));
        AppendLine(WriteContext.mStream, 4, std::string{ "leftToeBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mLeftToeBoneName)));
        AppendLine(WriteContext.mStream, 4, std::string{ "rightToeBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mRightToeBoneName)));
        AppendLine(WriteContext.mStream, 4, std::string{ "leftShinBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mLeftShinBoneName)));
        AppendLine(WriteContext.mStream, 4, std::string{ "rightShinBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mRightShinBoneName)));
        AppendLine(WriteContext.mStream, 4, std::string{ "leftThighBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mLeftThighBoneName)));
        AppendLine(WriteContext.mStream, 4, std::string{ "rightThighBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mRightThighBoneName)));
        AppendLine(WriteContext.mStream, 4, std::string{ "pelvisBoneName: " } + ToYamlText(GetFootIKRigBoneNameText(FootIKRigComponent->mPelvisBoneName)));
        AppendLine(WriteContext.mStream, 4, std::string{ "blendSpeed: " } + std::to_string(FootIKRigComponent->mBlendSpeed));
        AppendLine(WriteContext.mStream, 4, std::string{ "maxLift: " } + std::to_string(FootIKRigComponent->mMaxLift));
        AppendLine(WriteContext.mStream, 4, std::string{ "maxDrop: " } + std::to_string(FootIKRigComponent->mMaxDrop));
    }

    const char* SceneYamlMaterialComponentReader::TypeName() {
        return MaterialTypeName;
    }

    void SceneYamlMaterialComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        Material NewMaterial{};
        const c4::yml::ConstNodeRef MaterialNode{ ComponentsNode[TypeName()] };
        std::string MaterialPath{};

        if (MaterialNode.has_child("materialPath")) {
            MaterialNode["materialPath"] >> MaterialPath;
        }

        if (MaterialPath.empty()) {
            NewMaterial.MaterialGroupIndex = 0;
        }
        else {
            const std::string ResolvedMaterialPath{ ResolveSceneResourcePath(LoadContext.mSceneName, MaterialPath) };
            const bool IsLoaded{ LoadContext.mScene.GetAssetRegistry().LoadMaterialGroups(ResolvedMaterialPath) };
            if (IsLoaded == false) {
                LoadContext.mLoadResult.IsSuccess = false;
                LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "Material 파일 로드 실패, DefaultMaterialGroup 을 사용합니다. " } + ResolvedMaterialPath);
                NewMaterial.MaterialGroupIndex = 0;
            }
            else {
                const std::uint32_t MaterialGroupIndex{ LoadContext.mScene.GetAssetRegistry().FindMaterialGroupIndexBySourcePath(ResolvedMaterialPath) };
                if (MaterialGroupIndex == static_cast<std::uint32_t>(-1)) {
                    LoadContext.mLoadResult.IsSuccess = false;
                    LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "Material 파일에서 MaterialGroupIndex 를 해석할 수 없어 DefaultMaterialGroup 을 사용합니다. " } + ResolvedMaterialPath);
                    NewMaterial.MaterialGroupIndex = 0;
                }
                else {
                    NewMaterial.MaterialGroupIndex = MaterialGroupIndex;
                }
            }
        }

        ReadState.mMaterialGroupIndexForModel = NewMaterial.MaterialGroupIndex;
        LoadContext.mScene.GetWorld().AddComponent(Entity, NewMaterial);
    }

    const char* SceneYamlMaterialComponentWriter::TypeName() {
        return MaterialTypeName;
    }

    void SceneYamlMaterialComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const Material* MaterialComponent{ WriteContext.mReadOnlyWorld.GetComponent<Material>(EntityId) };
        if (MaterialComponent == nullptr) {
            return;
        }

        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        const std::string MaterialPath{ WriteContext.mAssetRegistry.FindMaterialGroupSourcePathByIndex(MaterialComponent->MaterialGroupIndex) };
        if (MaterialPath.empty()) {
            WriteContext.mSaveResult.IsSuccess = false;
            WriteContext.mSaveResult.UndecidedItems.push_back(std::string{ "MaterialGroupIndex 에 대응되는 materialPath 를 찾지 못했습니다: " } + std::to_string(MaterialComponent->MaterialGroupIndex));
        }

        const std::string MaterialPathForYaml{ IsDefaultMaterialPath(MaterialPath) ? std::string{} : MakeSceneRelativeResourcePath(WriteContext.mTargetSnapshot.GetSceneName(), MaterialPath) };
        AppendLine(WriteContext.mStream, 4, std::string{ "materialPath: " } + ToYamlText(MaterialPathForYaml));
    }

    const char* SceneYamlCullingComponentReader::TypeName() {
        return CullingTypeName;
    }

    void SceneYamlCullingComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        const c4::yml::ConstNodeRef CullingNode{ ComponentsNode[TypeName()] };
        Culling NewCulling{};
        if (CullingNode.has_child("frustumCulling")) {
            CullingNode["frustumCulling"] >> NewCulling.frustumCulling;
        }

        ReadState.mFrustumCullingEnabled = NewCulling.frustumCulling;
        LoadContext.mScene.GetWorld().AddComponent(Entity, NewCulling);
    }

    const char* SceneYamlCullingComponentWriter::TypeName() {
        return CullingTypeName;
    }

    void SceneYamlCullingComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const Culling* CullingComponent{ WriteContext.mReadOnlyWorld.GetComponent<Culling>(EntityId) };
        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        const bool IsFrustumCullingEnabled{ CullingComponent == nullptr ? true : CullingComponent->frustumCulling };
        AppendLine(WriteContext.mStream, 4, std::string{ "frustumCulling: " } + ToYamlBooleanText(IsFrustumCullingEnabled));
    }

    const char* SceneYamlStaticMeshRendererComponentReader::TypeName() {
        return StaticMeshRendererTypeName;
    }

    void SceneYamlStaticMeshRendererComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        if (ComponentsNode.has_child(TypeName()) == false || ComponentsNode.has_child(TerrainTypeName) == true || ReadState.mHasInstantiatedPrefabModel == true) {
            return;
        }

        const c4::yml::ConstNodeRef StaticMeshRendererNode{ ComponentsNode[TypeName()] };
        if (StaticMeshRendererNode.has_child("modelPath") == false && StaticMeshRendererNode.has_child("modelPrimitive") == false) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "StaticMeshRenderer 에 modelPath/modelPrimitive 없음" });
            return;
        }

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
            ResolvedModelPath = ResolveSceneResourcePath(LoadContext.mSceneName, ModelPath);
            ModelSelector = ResolvedModelPath;
        }

        if (ModelSelector.empty()) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back("StaticMeshRenderer 의 modelPath/modelPrimitive 해석 실패");
            return;
        }

        const std::shared_ptr<Model> ModelData{ LoadContext.mScene.GetAssetRegistry().GetModel(ModelSelector) };
        if (ModelData == nullptr) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "modelPath 로 Model 로드 실패: " } + ResolvedModelPath);
            return;
        }

        ModelHierarchySpawnRequest SpawnRequest{};
        SpawnRequest.ModelData = ModelData;
        SpawnRequest.RootEntityId = Entity;
        SpawnRequest.MaterialGroupIndex = ReadState.mMaterialGroupIndexForModel;
        SpawnRequest.FrustumCullingEnabled = ReadState.mFrustumCullingEnabled;
        SpawnRequest.IsActive = IsActive;
        const bool IsSpawned{ LoadContext.mEntityFactory.SpawnModelHierarchy(SpawnRequest) };
        if (IsSpawned == false) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "Model RootNode 를 찾을 수 없습니다: " } + ModelSelector);
        }
    }

    const char* SceneYamlStaticMeshRendererComponentWriter::TypeName() {
        return StaticMeshRendererTypeName;
    }

    void SceneYamlStaticMeshRendererComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const StaticMeshRenderer* StaticMeshRendererComponent{ WriteContext.mReadOnlyWorld.GetComponent<StaticMeshRenderer>(EntityId) };
        const PrefabInstance* PrefabInstanceComponent{ WriteContext.mReadOnlyWorld.GetComponent<PrefabInstance>(EntityId) };
        if (StaticMeshRendererComponent == nullptr || PrefabInstanceComponent != nullptr) {
            return;
        }

        const std::string ModelSelector{ WriteContext.mAssetRegistry.FindModelSelectorByPointer(StaticMeshRendererComponent->model) };
        if (ModelSelector.empty()) {
            WriteContext.mSaveResult.IsSuccess = false;
            WriteContext.mSaveResult.UndecidedItems.push_back("StaticMeshRenderer model 포인터에 대응되는 selector 를 찾지 못했습니다.");
            return;
        }

        TerrainBuildDesc TerrainDesc{};
        const bool IsTerrainModel{ TryParseTerrainModelSelector(ModelSelector, TerrainDesc) };
        if (IsTerrainModel == true) {
            AppendTerrainBuildDesc(WriteContext.mStream, 3, WriteContext.mTargetSnapshot.GetSceneName(), TerrainDesc);
            AppendLine(WriteContext.mStream, 4, std::string{ "active: " } + ToYamlBooleanText(StaticMeshRendererComponent->active));
            return;
        }

        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        const std::string ModelSelectorForYaml{ MakeSceneRelativeResourcePath(WriteContext.mTargetSnapshot.GetSceneName(), ModelSelector) };
        AppendLine(WriteContext.mStream, 4, std::string{ "modelPath: " } + ToYamlText(ModelSelectorForYaml));
        AppendLine(WriteContext.mStream, 4, std::string{ "active: " } + ToYamlBooleanText(StaticMeshRendererComponent->active));
    }

    const char* SceneYamlScriptComponentReader::TypeName() {
        return ScriptTypeName;
    }

    void SceneYamlScriptComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        const bool HasScriptNode{ ComponentsNode.has_child(ScriptTypeName) || ComponentsNode.has_child(ScriptComponentTypeName) || ComponentsNode.has_child(BehaviorInstanceComponentTypeName) };
        if (HasScriptNode == false) {
            return;
        }

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

        if (ScriptPath.empty() == true) {
            return;
        }

        const std::string ResolvedScriptPath{ ResolveSceneResourcePath(LoadContext.mSceneName, ScriptPath) };
        const Script::LuaBehaviorFramework::BehaviorOperationResult AttachResult{ LoadContext.mScene.GetLuaScriptFramework().AttachBehaviorFromFile(Entity, ResolvedScriptPath) };
        if (!AttachResult) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "Script 부착 실패: " } + ResolvedScriptPath + std::string{ " / " } + AttachResult.mError.mMessage);
        }
    }
}
