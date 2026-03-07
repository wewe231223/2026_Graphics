#include "SceneYamlSerializer.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Intents/CameraIntent.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Tags.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Systems/CameraInputSystem.h"
#include "Game/Scene/Systems/IntentClentUpSystem.h"
#include "Game/Scene/Systems/StaticRenderSystem.h"
#include "Game/Scene/Systems/CameraRenderSystem.h"

namespace {
    constexpr const char* TransformTypeName{ "Transform" };
    constexpr const char* MaterialTypeName{ "Material" };
    constexpr const char* StaticMeshRendererTypeName{ "StaticMeshRenderer" };
    constexpr const char* CameraTypeName{ "Camera" };
    constexpr const char* CameraIntentTypeName{ "CameraIntent" };
    constexpr const char* LocalPlayerTagTypeName{ "LocalPlayerTag" };

    bool ReadVector3(c4::yml::ConstNodeRef TargetNode, SimpleMath::Vector3& OutValue) {
        if (TargetNode.is_seq() == false || TargetNode.num_children() != 3) {
            return false;
        }

        std::array<float, 3> Values{};
        std::size_t Index{ 0 };
        for (const c4::yml::ConstNodeRef Child : TargetNode.children()) {
            Child >> Values[Index];
            Index += 1;
        }

        OutValue = SimpleMath::Vector3{ Values[0], Values[1], Values[2] };
        return true;
    }

    bool ReadVector2(c4::yml::ConstNodeRef TargetNode, SimpleMath::Vector2& OutValue) {
        if (TargetNode.is_seq() == false || TargetNode.num_children() != 2) {
            return false;
        }

        std::array<float, 2> Values{};
        std::size_t Index{ 0 };
        for (const c4::yml::ConstNodeRef Child : TargetNode.children()) {
            Child >> Values[Index];
            Index += 1;
        }

        OutValue = SimpleMath::Vector2{ Values[0], Values[1] };
        return true;
    }

    bool ReadQuaternion(c4::yml::ConstNodeRef TargetNode, SimpleMath::Quaternion& OutValue) {
        if (TargetNode.is_seq() == false || TargetNode.num_children() != 4) {
            return false;
        }

        std::array<float, 4> Values{};
        std::size_t Index{ 0 };
        for (const c4::yml::ConstNodeRef Child : TargetNode.children()) {
            Child >> Values[Index];
            Index += 1;
        }

        OutValue = SimpleMath::Quaternion{ Values[0], Values[1], Values[2], Values[3] };
        return true;
    }

    bool ReadColor4(c4::yml::ConstNodeRef TargetNode, float* OutValue) {
        if (TargetNode.is_seq() == false || TargetNode.num_children() != 4) {
            return false;
        }

        std::array<float, 4> Values{};
        std::size_t Index{ 0 };
        for (const c4::yml::ConstNodeRef Child : TargetNode.children()) {
            Child >> Values[Index];
            Index += 1;
        }

        OutValue[0] = Values[0];
        OutValue[1] = Values[1];
        OutValue[2] = Values[2];
        OutValue[3] = Values[3];
        return true;
    }

    std::unique_ptr<Game::ISystem> CreateSystemByName(const std::string& SystemName) {
        if (SystemName == "StaticRenderSystem") {
            return std::make_unique<Game::StaticRenderSystem>();
        }

        if (SystemName == "CameraInputSystem") {
            return std::make_unique<Game::CameraInputSystem>();
        }

		if (SystemName == "CameraRenderSystem") {
			return std::make_unique<Game::CameraRenderSystem>();
		}

        if (SystemName == "CleanUpSystem" || SystemName == "CleanUpSystem<CameraIntent>") {
            return std::make_unique<Game::CleanUpSystem<Game::CameraIntent>>();
        }

        return nullptr;
    }

    std::string ResolveSceneResourcePath(const std::string& SceneName, const std::string& FileName) {
        if (SceneName.empty() || FileName.empty()) {
            return FileName;
        }

        const std::filesystem::path ResolvedPath{ std::filesystem::path{ "Resources" } / SceneName / FileName };
        return ResolvedPath.generic_string();
    }
}

namespace Game {
    SceneYamlLoadResult::SceneYamlLoadResult()
        : IsSuccess{ true },
        UndecidedItems{} {
    }

    SceneYamlLoadResult::~SceneYamlLoadResult() {
    }

    SceneYamlLoadResult::SceneYamlLoadResult(const SceneYamlLoadResult& Other)
        : IsSuccess{ Other.IsSuccess },
        UndecidedItems{ Other.UndecidedItems } {
    }

    SceneYamlLoadResult& SceneYamlLoadResult::operator=(const SceneYamlLoadResult& Other) {
        if (this == &Other) {
            return *this;
        }

        IsSuccess = Other.IsSuccess;
        UndecidedItems = Other.UndecidedItems;
        return *this;
    }

    SceneYamlLoadResult::SceneYamlLoadResult(SceneYamlLoadResult&& Other) noexcept
        : IsSuccess{ Other.IsSuccess },
        UndecidedItems{ std::move(Other.UndecidedItems) } {
    }

    SceneYamlLoadResult& SceneYamlLoadResult::operator=(SceneYamlLoadResult&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        IsSuccess = Other.IsSuccess;
        UndecidedItems = std::move(Other.UndecidedItems);
        return *this;
    }

    SceneYamlSerializer::SceneYamlSerializer() {
    }

    SceneYamlSerializer::~SceneYamlSerializer() {
    }

    SceneYamlSerializer::SceneYamlSerializer(const SceneYamlSerializer& Other) {
        (void)Other;
    }

    SceneYamlSerializer& SceneYamlSerializer::operator=(const SceneYamlSerializer& Other) {
        if (this == &Other) {
            return *this;
        }

        (void)Other;
        return *this;
    }

    SceneYamlSerializer::SceneYamlSerializer(SceneYamlSerializer&& Other) noexcept {
        (void)Other;
    }

    SceneYamlSerializer& SceneYamlSerializer::operator=(SceneYamlSerializer&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        (void)Other;
        return *this;
    }

    SceneYamlLoadResult SceneYamlSerializer::Deserialize(const std::string& YamlText, Scene& OutScene) const {
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
                SystemNode >> SystemName;
                std::unique_ptr<ISystem> NewSystem{ CreateSystemByName(SystemName) };
                if (NewSystem == nullptr) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back(std::string{ "알 수 없는 System Type: " } + SystemName);
                    continue;
                }

                OutScene.AddSystem(std::move(NewSystem));
            }
        }

        if (RootNode.has_child("Entities") == false) {
            OutScene.BuildSystemExecutionPlan();
            return LoadResult;
        }

        const c4::yml::ConstNodeRef EntitiesNode{ RootNode["Entities"] };
        for (const c4::yml::ConstNodeRef EntityNode : EntitiesNode.children()) {
            const Arche::EntityID Entity{ OutScene.GetWorld().CreateEntity() };
            if (EntityNode.has_child("Components") == false) {
                continue;
            }

            const c4::yml::ConstNodeRef ComponentsNode{ EntityNode["Components"] };

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
            }

            std::uint32_t MaterialGroupIndexForModel{ 0 };

            if (ComponentsNode.has_child(MaterialTypeName)) {
                Material NewMaterial{};
                const c4::yml::ConstNodeRef MaterialNode{ ComponentsNode[MaterialTypeName] };
                std::string MaterialPath{};

                if (MaterialNode.has_child("materialPath")) {
                    MaterialNode["materialPath"] >> MaterialPath;
                }

                if (MaterialPath.empty()) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back(std::string{ "Material 컴포넌트의 materialPath 가 비어 있습니다." });
                }
                else {
                    const std::string ResolvedMaterialPath{ ResolveSceneResourcePath(SceneName, MaterialPath) };
                    const bool IsLoaded{ OutScene.GetAssetRegistry().LoadMaterialGroups(ResolvedMaterialPath) };
                    if (IsLoaded == false) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Material 파일 로드 실패: " } + ResolvedMaterialPath);
                    }
                    else {
                        const std::uint32_t MaterialGroupIndex{ OutScene.GetAssetRegistry().FindMaterialGroupIndexBySourcePath(ResolvedMaterialPath) };
                        if (MaterialGroupIndex == static_cast<std::uint32_t>(-1)) {
                            LoadResult.IsSuccess = false;
                            LoadResult.UndecidedItems.push_back(std::string{ "Material 파일에서 MaterialGroupIndex 를 해석할 수 없습니다: " } + ResolvedMaterialPath);
                        }
                        else {
                            NewMaterial.MaterialGroupIndex = MaterialGroupIndex;
                        }
                    }
                }

                MaterialGroupIndexForModel = NewMaterial.MaterialGroupIndex;
                OutScene.GetWorld().AddComponent(Entity, NewMaterial);
            }

            if (ComponentsNode.has_child(StaticMeshRendererTypeName)) {
                const c4::yml::ConstNodeRef StaticMeshRendererNode{ ComponentsNode[StaticMeshRendererTypeName] };
                if (StaticMeshRendererNode.has_child("modelPath")) {
                    std::string ModelPath{};
                    StaticMeshRendererNode["modelPath"] >> ModelPath;
                    const std::string ResolvedModelPath{ ResolveSceneResourcePath(SceneName, ModelPath) };
                    const std::shared_ptr<Model> ModelData{ OutScene.GetAssetRegistry().GetModel(ResolvedModelPath) };
                    if (ModelData == nullptr) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "modelPath 로 Model 로드 실패: " } + ResolvedModelPath);
                    }
                    else {
                        const Model* SourceModel{ ModelData.get() };
                        const std::vector<ModelNode>& ModelNodes{ SourceModel->GetNodes() };
                        const ModelNode* RootNode{ SourceModel->GetRootNode() };
                        if (RootNode == nullptr || ModelNodes.empty()) {
                            LoadResult.IsSuccess = false;
                            LoadResult.UndecidedItems.push_back(std::string{ "Model RootNode 를 찾을 수 없습니다: " } + ResolvedModelPath);
                        }
                        else {
                            const std::size_t RootNodeIndex{ static_cast<std::size_t>(RootNode - ModelNodes.data()) };
                            std::vector<Arche::EntityID> NodeEntities(ModelNodes.size(), Arche::NullEntityID);
                            NodeEntities[RootNodeIndex] = Entity;

                            for (std::size_t NodeIndex{ 0 }; NodeIndex < ModelNodes.size(); ++NodeIndex) {
                                if (NodeIndex != RootNodeIndex) {
                                    NodeEntities[NodeIndex] = OutScene.GetWorld().CreateEntity();
                                }

                                Transform NodeTransform{};
                                NodeTransform.nodeToParent = ModelNodes[NodeIndex].GetNodeToParent();
                                NodeTransform.geometryToNode = ModelNodes[NodeIndex].GetGeometryToNode();

                                if (NodeIndex == RootNodeIndex) {
                                    Transform* ExistingTransform{ OutScene.GetWorld().GetComponent<Transform>(NodeEntities[NodeIndex]) };
                                    if (ExistingTransform == nullptr) {
                                        OutScene.GetWorld().AddComponent(NodeEntities[NodeIndex], NodeTransform);
                                    }
                                    else {
                                        ExistingTransform->nodeToParent = NodeTransform.nodeToParent;
                                        ExistingTransform->geometryToNode = NodeTransform.geometryToNode;
                                    }
                                }
                                else {
                                    OutScene.GetWorld().AddComponent(NodeEntities[NodeIndex], NodeTransform);
                                }

                                StaticMeshRenderer NodeRenderer{};
                                NodeRenderer.model = ModelData.get();
                                NodeRenderer.nodeIndex = static_cast<std::uint32_t>(NodeIndex);
                                NodeRenderer.materialGroupIndex = MaterialGroupIndexForModel;
                                OutScene.GetWorld().AddComponent(NodeEntities[NodeIndex], NodeRenderer);

                                EntityHierarchy Hierarchy{};
                                Hierarchy.self = NodeEntities[NodeIndex];
                                OutScene.GetWorld().AddComponent(NodeEntities[NodeIndex], Hierarchy);
                            }

                            for (std::size_t NodeIndex{ 0 }; NodeIndex < ModelNodes.size(); ++NodeIndex) {
                                EntityHierarchy* ParentHierarchy{ OutScene.GetWorld().GetComponent<EntityHierarchy>(NodeEntities[NodeIndex]) };
                                if (ParentHierarchy == nullptr) {
                                    continue;
                                }

                                const std::vector<std::uint32_t>& Children{ ModelNodes[NodeIndex].GetChildren() };
                                Arche::EntityID PreviousChild{ Arche::NullEntityID };

                                for (std::uint32_t ChildNodeIndex : Children) {
                                    if (ChildNodeIndex >= NodeEntities.size()) {
                                        continue;
                                    }

                                    EntityHierarchy* ChildHierarchy{ OutScene.GetWorld().GetComponent<EntityHierarchy>(NodeEntities[ChildNodeIndex]) };
                                    if (ChildHierarchy == nullptr) {
                                        continue;
                                    }

                                    ChildHierarchy->parent = NodeEntities[NodeIndex];
                                    if (ParentHierarchy->firstChild == Arche::NullEntityID) {
                                        ParentHierarchy->firstChild = NodeEntities[ChildNodeIndex];
                                    }

                                    if (PreviousChild != Arche::NullEntityID) {
                                        EntityHierarchy* PreviousHierarchy{ OutScene.GetWorld().GetComponent<EntityHierarchy>(PreviousChild) };
                                        if (PreviousHierarchy != nullptr) {
                                            PreviousHierarchy->nextSibling = NodeEntities[ChildNodeIndex];
                                        }
                                    }

                                    PreviousChild = NodeEntities[ChildNodeIndex];
                                }
                            }
                        }
                    }
                }
                else {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back(std::string{ "StaticMeshRenderer 의 modelPath 없음" });
                }
            }

            if (ComponentsNode.has_child(CameraTypeName)) {
                Camera NewCamera{};
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

                if (CameraNode.has_child("cullingMask")) {
                    CameraNode["cullingMask"] >> NewCamera.cullingMask;
                }

                if (CameraNode.has_child("clearColor")) {
                    ReadColor4(CameraNode["clearColor"], NewCamera.clearColor);
                }

                if (CameraNode.has_child("priority")) {
                    CameraNode["priority"] >> NewCamera.priority;
                }

                if (CameraNode.has_child("smoothingFactor")) {
                    CameraNode["smoothingFactor"] >> NewCamera.smoothingFactor;
                }

                if (CameraNode.has_child("currentFovBias")) {
                    CameraNode["currentFovBias"] >> NewCamera.currentFovBias;
                }

                if (CameraNode.has_child("currentShakeIntensity")) {
                    CameraNode["currentShakeIntensity"] >> NewCamera.currentShakeIntensity;
                }

                if (CameraNode.has_child("lockOnTargetIndex")) {
                    CameraNode["lockOnTargetIndex"] >> NewCamera.lockOnTarget.index;
                }

                if (CameraNode.has_child("lockOnTargetGeneration")) {
                    CameraNode["lockOnTargetGeneration"] >> NewCamera.lockOnTarget.generation;
                }

                if (CameraNode.has_child("cameraFlags")) {
                    CameraNode["cameraFlags"] >> NewCamera.cameraFlags;
                }

                OutScene.GetWorld().AddComponent(Entity, NewCamera);
            }

            if (ComponentsNode.has_child(CameraIntentTypeName)) {
                CameraIntent NewCameraIntent{};
                const c4::yml::ConstNodeRef CameraIntentNode{ ComponentsNode[CameraIntentTypeName] };
                if (CameraIntentNode.has_child("moveDirection")) {
                    ReadVector3(CameraIntentNode["moveDirection"], NewCameraIntent.moveDirection);
                }

                if (CameraIntentNode.has_child("lookDelta")) {
                    ReadVector2(CameraIntentNode["lookDelta"], NewCameraIntent.lookDelta);
                }

                if (CameraIntentNode.has_child("zoomDelta")) {
                    CameraIntentNode["zoomDelta"] >> NewCameraIntent.zoomDelta;
                }

                if (CameraIntentNode.has_child("targetToLockOnIndex")) {
                    CameraIntentNode["targetToLockOnIndex"] >> NewCameraIntent.targetToLockOn.index;
                }

                if (CameraIntentNode.has_child("targetToLockOnGeneration")) {
                    CameraIntentNode["targetToLockOnGeneration"] >> NewCameraIntent.targetToLockOn.generation;
                }

                if (CameraIntentNode.has_child("requestUnlock")) {
                    CameraIntentNode["requestUnlock"] >> NewCameraIntent.requestUnlock;
                }

                if (CameraIntentNode.has_child("requestSkip")) {
                    CameraIntentNode["requestSkip"] >> NewCameraIntent.requestSkip;
                }

                if (CameraIntentNode.has_child("shakeImpulse")) {
                    CameraIntentNode["shakeImpulse"] >> NewCameraIntent.shakeImpulse;
                }

                OutScene.GetWorld().AddComponent(Entity, NewCameraIntent);
            }

            if (ComponentsNode.has_child(LocalPlayerTagTypeName)) {
                LocalPlayerTag NewLocalPlayerTag{};
                OutScene.GetWorld().AddComponent(Entity, NewLocalPlayerTag);
            }
        }

        OutScene.BuildSystemExecutionPlan();
        return LoadResult;
    }

    SceneYamlLoadResult SceneYamlSerializer::DeserializeFromFile(const std::string& YamlFilePath, Scene& OutScene) const {
        std::ifstream InputStream{ YamlFilePath, std::ios::in | std::ios::binary };
        SceneYamlLoadResult LoadResult{};

        if (InputStream.is_open() == false) {
            LoadResult.IsSuccess = false;
            LoadResult.UndecidedItems.push_back(std::string{ "YAML 파일을 열 수 없습니다: " } + YamlFilePath);
            return LoadResult;
        }

        std::stringstream Buffer{};
        Buffer << InputStream.rdbuf();
        return Deserialize(Buffer.str(), OutScene);
    }
}
