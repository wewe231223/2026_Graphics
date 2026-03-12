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
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Intents/CameraIntent.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Tags.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Systems/CameraInputSystem.h"
#include "Game/Scene/Systems/IntentClentUpSystem.h"
#include "Game/Scene/Systems/StaticRenderSystem.h"
#include "Game/Scene/Systems/PickingSystem.h"
#include "Game/Scene/Systems/CameraRenderSystem.h"

namespace {
    constexpr const char* TransformTypeName{ "Transform" };
    constexpr const char* MaterialTypeName{ "Material" };
    constexpr const char* StaticMeshRendererTypeName{ "StaticMeshRenderer" };
    constexpr const char* CameraTypeName{ "Camera" };
    constexpr const char* CameraIntentTypeName{ "CameraIntent" };
    constexpr const char* LocalPlayerTagTypeName{ "LocalPlayerTag" };
    constexpr const char* NameTypeName{ "Name" };

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

        if (SystemName == "PickingSystem") {
            return std::make_unique<Game::PickingSystem>();
        }

		if (SystemName == "CameraRenderSystem") {
			return std::make_unique<Game::CameraRenderSystem>();
		}

        if (SystemName == "CleanUpSystem" || SystemName == "CleanUpSystem<CameraIntent>") {
            return std::make_unique<Game::CleanUpSystem<Game::CameraIntent>>();
        }

        return nullptr;
    }


    std::string BuildPrimitiveSelector(const std::string& PrimitiveType, float PrimitiveSize, const std::array<float, 4>& PrimitiveColor) {
        std::string Selector{ PrimitiveType };
        Selector += std::string{ ";size=" } + std::to_string(PrimitiveSize);
        Selector += std::string{ ";color=" } + std::to_string(PrimitiveColor[0]) + std::string{ "," } + std::to_string(PrimitiveColor[1]) + std::string{ "," } + std::to_string(PrimitiveColor[2]) + std::string{ "," } + std::to_string(PrimitiveColor[3]);
        return Selector;
    }

    std::string ResolveSceneResourcePath(const std::string& SceneName, const std::string& FileName) {
        if (SceneName.empty() || FileName.empty()) {
            return FileName;
        }

        const std::filesystem::path ResolvedPath{ std::filesystem::path{ "Resources" } / SceneName / FileName };
        return ResolvedPath.generic_string();
    }
    bool ShouldSkipEntityInSceneExport(const Game::StaticMeshRenderer* StaticMeshRendererComponent) {
        if (StaticMeshRendererComponent == nullptr || StaticMeshRendererComponent->model == nullptr) {
            return false;
        }

        const Game::Model* SourceModel{ StaticMeshRendererComponent->model };
        const std::vector<Game::ModelNode>& ModelNodes{ SourceModel->GetNodes() };
        const Game::ModelNode* RootNode{ SourceModel->GetRootNode() };
        if (RootNode == nullptr || ModelNodes.empty()) {
            return false;
        }

        const std::size_t RootNodeIndex{ static_cast<std::size_t>(RootNode - ModelNodes.data()) };
        if (StaticMeshRendererComponent->nodeIndex >= ModelNodes.size()) {
            return true;
        }

        return static_cast<std::size_t>(StaticMeshRendererComponent->nodeIndex) != RootNodeIndex;
    }

    void AppendLine(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Text) {
        for (std::size_t IndentIndex{ 0 }; IndentIndex < IndentLevel; ++IndentIndex) {
            Stream << "  ";
        }

        Stream << Text << '\n';
    }

    std::string ToYamlText(const char* Text) {
        std::string Escaped{};
        Escaped.push_back('"');

        for (std::size_t Index{ 0 }; Text[Index] != '\0'; ++Index) {
            const char Character{ Text[Index] };
            if (Character == '\\' || Character == '"') {
                Escaped.push_back('\\');
            }

            Escaped.push_back(Character);
        }

        Escaped.push_back('"');
        return Escaped;
    }

    std::string ToYamlText(const std::string& Text) {
        return ToYamlText(Text.c_str());
    }

    std::string ToYamlBooleanText(bool Value) {
        return Value ? std::string{ "true" } : std::string{ "false" };
    }

    void AppendVector3(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const SimpleMath::Vector3& Value) {
        AppendLine(Stream, IndentLevel, Key + std::string{ ": [" } + std::to_string(Value.x) + std::string{ ", " } + std::to_string(Value.y) + std::string{ ", " } + std::to_string(Value.z) + std::string{ "]" });
    }

    void AppendVector2(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const SimpleMath::Vector2& Value) {
        AppendLine(Stream, IndentLevel, Key + std::string{ ": [" } + std::to_string(Value.x) + std::string{ ", " } + std::to_string(Value.y) + std::string{ "]" });
    }

    void AppendQuaternion(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const SimpleMath::Quaternion& Value) {
        AppendLine(Stream, IndentLevel, Key + std::string{ ": [" } + std::to_string(Value.x) + std::string{ ", " } + std::to_string(Value.y) + std::string{ ", " } + std::to_string(Value.z) + std::string{ ", " } + std::to_string(Value.w) + std::string{ "]" });
    }

    void AppendColor4(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const float* Value) {
        AppendLine(Stream, IndentLevel, Key + std::string{ ": [" } + std::to_string(Value[0]) + std::string{ ", " } + std::to_string(Value[1]) + std::string{ ", " } + std::to_string(Value[2]) + std::string{ ", " } + std::to_string(Value[3]) + std::string{ "]" });
    }
}

namespace Game {
    SceneYamlSaveResult::SceneYamlSaveResult()
        : IsSuccess{ true },
        UndecidedItems{} {
    }

    SceneYamlSaveResult::~SceneYamlSaveResult() {
    }

    SceneYamlSaveResult::SceneYamlSaveResult(const SceneYamlSaveResult& Other)
        : IsSuccess{ Other.IsSuccess },
        UndecidedItems{ Other.UndecidedItems } {
    }

    SceneYamlSaveResult& SceneYamlSaveResult::operator=(const SceneYamlSaveResult& Other) {
        if (this == &Other) {
            return *this;
        }

        IsSuccess = Other.IsSuccess;
        UndecidedItems = Other.UndecidedItems;
        return *this;
    }

    SceneYamlSaveResult::SceneYamlSaveResult(SceneYamlSaveResult&& Other) noexcept
        : IsSuccess{ Other.IsSuccess },
        UndecidedItems{ std::move(Other.UndecidedItems) } {
    }

    SceneYamlSaveResult& SceneYamlSaveResult::operator=(SceneYamlSaveResult&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        IsSuccess = Other.IsSuccess;
        UndecidedItems = std::move(Other.UndecidedItems);
        return *this;
    }

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
            OutScene.InitializePickingGizmoEntities();
            OutScene.BuildSystemExecutionPlan();
            return LoadResult;
        }

        const c4::yml::ConstNodeRef EntitiesNode{ RootNode["Entities"] };
        for (const c4::yml::ConstNodeRef EntityNode : EntitiesNode.children()) {
            const Arche::EntityID Entity{ OutScene.GetWorld().CreateEntity() };
            EntityHierarchy RootHierarchy{};
            RootHierarchy.self = Entity;
            OutScene.GetWorld().AddComponent(Entity, RootHierarchy);

            if (EntityNode.has_child("Components") == false) {
                continue;
            }

            const c4::yml::ConstNodeRef ComponentsNode{ EntityNode["Components"] };

            if (ComponentsNode.has_child(NameTypeName)) {
                const c4::yml::ConstNodeRef NameNode{ ComponentsNode[NameTypeName] };
                if (NameNode.has_child("text")) {
                    std::string NameText{};
                    NameNode["text"] >> NameText;
                    const Name NewName{ CreateNameComponent(NameText) };
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

            if (ComponentsNode.has_child(StaticMeshRendererTypeName)) {
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

                        std::array<float, 4> PrimitiveColor{ 1.0f, 1.0f, 1.0f, 1.0f };
                        if (StaticMeshRendererNode.has_child("modelPrimitiveColor")) {
                            ReadColor4(StaticMeshRendererNode["modelPrimitiveColor"], PrimitiveColor.data());
                        }

                        if (ModelSelector.empty() == false) {
                            ModelSelector = BuildPrimitiveSelector(ModelSelector, PrimitiveSize, PrimitiveColor);
                        }
                    }

                    if (ModelSelector.empty() && StaticMeshRendererNode.has_child("modelPath")) {
                        std::string ModelPath{};
                        StaticMeshRendererNode["modelPath"] >> ModelPath;
                        ResolvedModelPath = ResolveSceneResourcePath(SceneName, ModelPath);
                        ModelSelector = ResolvedModelPath;
                    }

                    const std::shared_ptr<Model> ModelData{ OutScene.GetAssetRegistry().GetModel(ModelSelector) };
                    if (ModelData == nullptr) {
                        LoadResult.IsSuccess = false;
                        if (ResolvedModelPath.empty()) {
                            LoadResult.UndecidedItems.push_back(std::string{ "modelPrimitive 로 Model 로드 실패: " } + ModelSelector);
                        }
                        else {
                            LoadResult.UndecidedItems.push_back(std::string{ "modelPath 로 Model 로드 실패: " } + ResolvedModelPath);
                        }
                    }
                    else {
                        const Model* SourceModel{ ModelData.get() };
                        const std::vector<ModelNode>& ModelNodes{ SourceModel->GetNodes() };
                        const ModelNode* RootNode{ SourceModel->GetRootNode() };
                        if (RootNode == nullptr || ModelNodes.empty()) {
                            LoadResult.IsSuccess = false;
                            LoadResult.UndecidedItems.push_back(std::string{ "Model RootNode 를 찾을 수 없습니다: " } + ModelSelector);
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
                                NodeRenderer.active = IsActive;
                                OutScene.GetWorld().AddComponent(NodeEntities[NodeIndex], NodeRenderer);

                                BoundingBox NodeBoundingBox{};
                                NodeBoundingBox.UpdateFromModel(ModelData.get(), static_cast<std::uint32_t>(NodeIndex));
                                OutScene.GetWorld().AddComponent(NodeEntities[NodeIndex], NodeBoundingBox);

                                EntityHierarchy Hierarchy{};
                                Hierarchy.self = NodeEntities[NodeIndex];
                                if (NodeIndex == RootNodeIndex) {
                                    EntityHierarchy* ExistingHierarchy{ OutScene.GetWorld().GetComponent<EntityHierarchy>(NodeEntities[NodeIndex]) };
                                    if (ExistingHierarchy == nullptr) {
                                        OutScene.GetWorld().AddComponent(NodeEntities[NodeIndex], Hierarchy);
                                    }
                                    else {
                                        ExistingHierarchy->self = NodeEntities[NodeIndex];
                                    }
                                }
                                else {
                                    OutScene.GetWorld().AddComponent(NodeEntities[NodeIndex], Hierarchy);
                                }

                                const Name NodeName{ CreateNameComponent(ModelNodes[NodeIndex].GetName()) };
                                if (NodeIndex == RootNodeIndex) {
                                    Name* ExistingName{ OutScene.GetWorld().GetComponent<Name>(NodeEntities[NodeIndex]) };
                                    if (ExistingName == nullptr) {
                                        OutScene.GetWorld().AddComponent(NodeEntities[NodeIndex], NodeName);
                                    }
                                    else if (GetNameText(*ExistingName)[0] == '\0') {
                                        *ExistingName = NodeName;
                                    }
                                }
                                else {
                                    OutScene.GetWorld().AddComponent(NodeEntities[NodeIndex], NodeName);
                                }
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
                    LoadResult.UndecidedItems.push_back(std::string{ "StaticMeshRenderer 의 modelPath/modelPrimitive 없음" });
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

        OutScene.InitializePickingGizmoEntities();
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


    SceneYamlSaveResult SceneYamlSerializer::Serialize(const Scene& TargetScene, std::string& OutYamlText) const {
        return Serialize(TargetScene.GetWorldSnapshot(), OutYamlText);
    }

    SceneYamlSaveResult SceneYamlSerializer::Serialize(const SceneWorldSnapshot& TargetSnapshot, std::string& OutYamlText) const {
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
                AppendLine(Stream, 1, std::string{ "- " } + ToYamlText(SystemName));
            }
        }

        AppendLine(Stream, 0, "Entities:");

        for (const SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot : TargetSnapshot.GetEntities()) {
            const Arche::EntityID EntityId{ EntitySnapshot.mEntityId };
            const Name* NameComponent{ ReadOnlyWorld->GetComponent<Name>(EntityId) };
            const Transform* TransformComponent{ ReadOnlyWorld->GetComponent<Transform>(EntityId) };
            const Material* MaterialComponent{ ReadOnlyWorld->GetComponent<Material>(EntityId) };
            const StaticMeshRenderer* StaticMeshRendererComponent{ ReadOnlyWorld->GetComponent<StaticMeshRenderer>(EntityId) };
            const Camera* CameraComponent{ ReadOnlyWorld->GetComponent<Camera>(EntityId) };
            const CameraIntent* CameraIntentComponent{ ReadOnlyWorld->GetComponent<CameraIntent>(EntityId) };
            const LocalPlayerTag* LocalPlayerTagComponent{ ReadOnlyWorld->GetComponent<LocalPlayerTag>(EntityId) };

            if (ShouldSkipEntityInSceneExport(StaticMeshRendererComponent)) {
                continue;
            }

            AppendLine(Stream, 1, "- Components:");

            if (NameComponent != nullptr) {
                AppendLine(Stream, 2, std::string{ NameTypeName } + std::string{ ":" });
                AppendLine(Stream, 3, std::string{ "text: " } + ToYamlText(GetNameText(*NameComponent)));
            }

            if (TransformComponent != nullptr) {
                AppendLine(Stream, 2, std::string{ TransformTypeName } + std::string{ ":" });
                AppendVector3(Stream, 3, "position", TransformComponent->position);
                AppendVector3(Stream, 3, "rotationEuler", TransformComponent->rotationEuler);
                AppendQuaternion(Stream, 3, "rotation", TransformComponent->rotation);
                AppendVector3(Stream, 3, "scale", TransformComponent->scale);
            }

            if (MaterialComponent != nullptr) {
                AppendLine(Stream, 2, std::string{ MaterialTypeName } + std::string{ ":" });
                const std::string MaterialPath{ AssetRegistryInstance->FindMaterialGroupSourcePathByIndex(MaterialComponent->MaterialGroupIndex) };
                if (MaterialPath.empty()) {
                    SaveResult.IsSuccess = false;
                    SaveResult.UndecidedItems.push_back(std::string{ "MaterialGroupIndex 에 대응되는 materialPath 를 찾지 못했습니다: " } + std::to_string(MaterialComponent->MaterialGroupIndex));
                }

                AppendLine(Stream, 3, std::string{ "materialPath: " } + ToYamlText(MaterialPath));
            }

            if (StaticMeshRendererComponent != nullptr) {
                AppendLine(Stream, 2, std::string{ StaticMeshRendererTypeName } + std::string{ ":" });
                const std::string ModelSelector{ AssetRegistryInstance->FindModelSelectorByPointer(StaticMeshRendererComponent->model) };
                if (ModelSelector.empty()) {
                    SaveResult.IsSuccess = false;
                    SaveResult.UndecidedItems.push_back("StaticMeshRenderer model 포인터에 대응되는 selector 를 찾지 못했습니다.");
                }

                AppendLine(Stream, 3, std::string{ "modelPath: " } + ToYamlText(ModelSelector));
                AppendLine(Stream, 3, std::string{ "active: " } + ToYamlBooleanText(StaticMeshRendererComponent->active));
            }

            if (CameraComponent != nullptr) {
                AppendLine(Stream, 2, std::string{ CameraTypeName } + std::string{ ":" });
                AppendLine(Stream, 3, std::string{ "fov: " } + std::to_string(CameraComponent->fov));
                AppendLine(Stream, 3, std::string{ "aspectRatio: " } + std::to_string(CameraComponent->aspectRatio));
                AppendLine(Stream, 3, std::string{ "nearPlane: " } + std::to_string(CameraComponent->nearPlane));
                AppendLine(Stream, 3, std::string{ "farPlane: " } + std::to_string(CameraComponent->farPlane));
                AppendLine(Stream, 3, std::string{ "isActive: " } + ToYamlBooleanText(CameraComponent->isActive));
                AppendLine(Stream, 3, std::string{ "isOrthographic: " } + ToYamlBooleanText(CameraComponent->isOrthographic));
                AppendLine(Stream, 3, std::string{ "orthoSize: " } + std::to_string(CameraComponent->orthoSize));
                AppendLine(Stream, 3, std::string{ "cullingMask: " } + std::to_string(CameraComponent->cullingMask));
                AppendColor4(Stream, 3, "clearColor", CameraComponent->clearColor);
                AppendLine(Stream, 3, std::string{ "priority: " } + std::to_string(CameraComponent->priority));
                AppendLine(Stream, 3, std::string{ "smoothingFactor: " } + std::to_string(CameraComponent->smoothingFactor));
                AppendLine(Stream, 3, std::string{ "currentFovBias: " } + std::to_string(CameraComponent->currentFovBias));
                AppendLine(Stream, 3, std::string{ "currentShakeIntensity: " } + std::to_string(CameraComponent->currentShakeIntensity));
                AppendLine(Stream, 3, std::string{ "lockOnTargetIndex: " } + std::to_string(CameraComponent->lockOnTarget.index));
                AppendLine(Stream, 3, std::string{ "lockOnTargetGeneration: " } + std::to_string(CameraComponent->lockOnTarget.generation));
                AppendLine(Stream, 3, std::string{ "cameraFlags: " } + std::to_string(CameraComponent->cameraFlags));
            }

            if (CameraIntentComponent != nullptr) {
                AppendLine(Stream, 2, std::string{ CameraIntentTypeName } + std::string{ ":" });
                AppendVector3(Stream, 3, "moveDirection", CameraIntentComponent->moveDirection);
                AppendVector2(Stream, 3, "lookDelta", CameraIntentComponent->lookDelta);
                AppendLine(Stream, 3, std::string{ "zoomDelta: " } + std::to_string(CameraIntentComponent->zoomDelta));
                AppendLine(Stream, 3, std::string{ "targetToLockOnIndex: " } + std::to_string(CameraIntentComponent->targetToLockOn.index));
                AppendLine(Stream, 3, std::string{ "targetToLockOnGeneration: " } + std::to_string(CameraIntentComponent->targetToLockOn.generation));
                AppendLine(Stream, 3, std::string{ "requestUnlock: " } + ToYamlBooleanText(CameraIntentComponent->requestUnlock));
                AppendLine(Stream, 3, std::string{ "requestSkip: " } + ToYamlBooleanText(CameraIntentComponent->requestSkip));
                AppendLine(Stream, 3, std::string{ "shakeImpulse: " } + std::to_string(CameraIntentComponent->shakeImpulse));
            }

            if (LocalPlayerTagComponent != nullptr) {
                AppendLine(Stream, 2, std::string{ LocalPlayerTagTypeName } + std::string{ ": {}" });
            }
        }

        OutYamlText = Stream.str();
        return SaveResult;
    }

    SceneYamlSaveResult SceneYamlSerializer::SerializeToFile(const Scene& TargetScene, const std::string& YamlFilePath) const {
        return SerializeToFile(TargetScene.GetWorldSnapshot(), YamlFilePath);
    }

    SceneYamlSaveResult SceneYamlSerializer::SerializeToFile(const SceneWorldSnapshot& TargetSnapshot, const std::string& YamlFilePath) const {
        std::string YamlText{};
        SceneYamlSaveResult SaveResult{ Serialize(TargetSnapshot, YamlText) };

        std::ofstream OutputStream{ YamlFilePath, std::ios::out | std::ios::binary | std::ios::trunc };
        if (OutputStream.is_open() == false) {
            SaveResult.IsSuccess = false;
            SaveResult.UndecidedItems.push_back(std::string{ "YAML 파일을 쓸 수 없습니다: " } + YamlFilePath);
            return SaveResult;
        }

        OutputStream << YamlText;
        if (OutputStream.good() == false) {
            SaveResult.IsSuccess = false;
            SaveResult.UndecidedItems.push_back(std::string{ "YAML 파일 쓰기 실패: " } + YamlFilePath);
        }

        return SaveResult;
    }

}
