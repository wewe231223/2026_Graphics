#include "SceneYamlSerializer.h"
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <exception>
#include <fstream>
#include <format>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/DirectionalLight.h"
#include "Game/Scene/Components/Frustum.h"
#include "Game/Scene/Components/SkySphere.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/AnimatorGraphPlayer.h"
#include "Game/Scene/Components/RuntimeVariableTable.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/PrefabInstance.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Tags.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Systems/AnimationGraphSystem.h"
#include "Game/Scene/Systems/AnimateSystem.h"
#include "Game/Scene/Systems/FootIKSystem.h"
#include "Game/Scene/Systems/PhysicsActorUpdateSystem.h"
#include "Game/Scene/Systems/SkinnedMeshPrepareSystem.h"
#include "Game/Scene/Systems/SkinnedMeshRenderSystem.h"
#include "Game/Scene/Systems/StaticRenderSystem.h"
#include "Game/Scene/Systems/TerrainRenderSystem.h"
#include "Game/Scene/Systems/TerrainStreamingSystem.h"
#include "Game/Scene/Systems/TransformWorldSystem.h"
#include "Game/Scene/Systems/CameraRenderSystem.h"
#include "Game/Scene/Systems/ShadowMappingParameterSystem.h"
#include "Game/Scene/SceneEntityFactory.h"
#include "Game/Model/TerrainRenderResource.h"
#include "Game/Model/TerrainMeshTypes.h"
#include "Game/Model/TerrainHeightFieldFactory.h"
#include "Utility/StdOutput.h"

namespace {
    constexpr const char* TransformTypeName{ "Transform" };
    constexpr const char* MaterialTypeName{ "Material" };
    constexpr const char* StaticMeshRendererTypeName{ "StaticMeshRenderer" };
    constexpr const char* TerrainTypeName{ "Terrain" };
    constexpr const char* CullingTypeName{ "Culling" };
    constexpr const char* AnimationTypeName{ "Animation" };
    constexpr const char* CameraTypeName{ "Camera" };
    constexpr const char* DirectionalLightTypeName{ "DirectionalLight" };
    constexpr const char* TagTypeName{ "Tag" };
    constexpr const char* ScriptTypeName{ "Script" };
    constexpr const char* ScriptComponentTypeName{ "ScriptComponent" };
    constexpr const char* BehaviorInstanceComponentTypeName{ "BehaviorInstanceComponent" };
    constexpr const char* NameTypeName{ "Name" };
    constexpr const char* PrefabInstanceTypeName{ "PrefabInstance" };
    constexpr const char* BoneSkinReferenceTypeName{ "BoneSkinReference" };
    constexpr const char* FootIKRigTypeName{ "FootIKRig" };
    constexpr const char* RuntimeVariablesTypeName{ "RuntimeVariables" };
    constexpr const char* PhysicsTypeName{ "Physics" };
    constexpr const char* BoundingBoxTypeName{ "BB" };
    constexpr const char* DefaultMaterialPathText{ "Resources/DefaultResource/DefaultMaterial.json" };
    constexpr const char* CameraModeFreeLookText{ "FreeLook" };
    constexpr const char* CameraModeThirdPersonText{ "ThirdPerson" };
    constexpr const char* CameraModeCinematicText{ "Cinematic" };


    struct PrefabDescriptor final {
        std::uint64_t PrefabId{};
        std::string ModelSelector{};
        std::string MaterialPath{};
        bool Active{ true };
    };

    struct PendingAnimatorBinding final {
        struct PendingRuntimeVariableInitialization final {
            enum class RuntimeVariableType : std::uint8_t {
                Bool,
                Int,
                Float,
            };

            std::string ParameterName{};
            RuntimeVariableType Type{ RuntimeVariableType::Bool };
            bool BoolValue{};
            std::int32_t IntValue{};
            float FloatValue{};
        };

        Arche::EntityID SourceEntityId{ Arche::NullEntityID };
        std::string TargetNodeName{};
        asset::Animation* AnimationData{ nullptr };
        Game::AnimationGraphAsset* AnimationGraphData{ nullptr };
        std::int32_t ClipIndex{ -1 };
        std::int32_t FallbackClipIndex{ -1 };
        std::vector<PendingRuntimeVariableInitialization> RuntimeVariableInitializations{};
    };

    struct PendingBoundingBoxBinding final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        SimpleMath::Vector3 Center{ SimpleMath::Vector3::Zero };
        SimpleMath::Vector3 Extents{ SimpleMath::Vector3::One };
    };

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

    bool TryParseCameraModeText(const std::string& CameraModeText, std::uint32_t& OutCameraFlags) {
        if (CameraModeText == CameraModeFreeLookText) {
            OutCameraFlags &= ~Game::CameraFlagThirdPerson;
            OutCameraFlags &= ~Game::CameraFlagCinematic;
            OutCameraFlags |= Game::CameraFlagFreeLook;
            return true;
        }

        if (CameraModeText == CameraModeThirdPersonText) {
            OutCameraFlags &= ~Game::CameraFlagFreeLook;
            OutCameraFlags &= ~Game::CameraFlagCinematic;
            OutCameraFlags |= Game::CameraFlagThirdPerson;
            return true;
        }

        if (CameraModeText == CameraModeCinematicText) {
            OutCameraFlags &= ~Game::CameraFlagFreeLook;
            OutCameraFlags &= ~Game::CameraFlagThirdPerson;
            OutCameraFlags |= Game::CameraFlagCinematic;
            return true;
        }

        return false;
    }

    const char* ResolveCameraModeText(std::uint32_t CameraFlags) {
        if ((CameraFlags & Game::CameraFlagCinematic) != 0u) {
            return CameraModeCinematicText;
        }

        if ((CameraFlags & Game::CameraFlagThirdPerson) != 0u) {
            return CameraModeThirdPersonText;
        }

        return CameraModeFreeLookText;
    }

    std::unique_ptr<Game::ISystem> CreateSystemByName(const std::string& SystemName) {
        using SystemFactory = std::unique_ptr<Game::ISystem>(*)();
        static const std::unordered_map<std::string_view, SystemFactory> SystemFactories{
            { "SkinnedMeshPrepareSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::SkinnedMeshPrepareSystem>(); } },
            { "StaticRenderSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::StaticRenderSystem>(); } },
            { "TerrainStreamingSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::TerrainStreamingSystem>(); } },
            { "TerrainRenderSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::TerrainRenderSystem>(); } },
            { "TransformWorldSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::TransformWorldSystem>(); } },
            { "SkinnedMeshRenderSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::SkinnedMeshRenderSystem>(); } },
            { "AnimationGraphSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::AnimationGraphSystem>(); } },
            { "AnimateSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::AnimateSystem>(); } },
            { "FootIKSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::FootIKSystem>(); } },
            { "PhysicsActorUpdateSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::PhysicsActorUpdateSystem>(); } },
            { "CameraRenderSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::CameraRenderSystem>(); } },
            { "ShadowMappingParameterSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::ShadowMappingParameterSystem>(); } },
        };
        const std::unordered_map<std::string_view, SystemFactory>::const_iterator FactoryIter{ SystemFactories.find(SystemName) };
        if (FactoryIter == SystemFactories.end()) {
            return nullptr;
        }

        return FactoryIter->second();
    }

    bool TryReadSystemName(c4::yml::ConstNodeRef SystemNode, std::string& OutSystemName) {
        if (SystemNode.is_val() || SystemNode.is_keyval()) {
            SystemNode >> OutSystemName;
            return OutSystemName.empty() == false;
        }

        if (SystemNode.is_map()) {
            if (SystemNode.has_child("Type")) {
                SystemNode["Type"] >> OutSystemName;
                return OutSystemName.empty() == false;
            }

            if (SystemNode.has_child("Name")) {
                SystemNode["Name"] >> OutSystemName;
                return OutSystemName.empty() == false;
            }
        }

        return false;
    }

    bool StartsWith(const std::string& Text, const std::string& Prefix) {
        if (Text.size() < Prefix.size()) {
            return false;
        }

        return Text.compare(0, Prefix.size(), Prefix) == 0;
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

        if (StartsWith(FileName, "primitive:")) {
            return FileName;
        }

        if (StartsWith(FileName, "terrain:")) {
            return FileName;
        }

        const std::filesystem::path SourcePath{ FileName };
        if (SourcePath.is_absolute()) {
            return SourcePath.lexically_normal().generic_string();
        }

        const std::string NormalizedText{ SourcePath.lexically_normal().generic_string() };
        if (StartsWith(NormalizedText, "Resources/")) {
            return NormalizedText;
        }

        if (StartsWith(NormalizedText, "Script/")) {
            return NormalizedText;
        }

        const std::filesystem::path ResolvedPath{ std::filesystem::path{ "Resources" } / SceneName / FileName };
        return ResolvedPath.lexically_normal().generic_string();
    }

    std::string MakeSceneRelativeResourcePath(const std::string& SceneName, const std::string& SourcePath) {
        if (SceneName.empty() || SourcePath.empty()) {
            return SourcePath;
        }

        if (StartsWith(SourcePath, "primitive:")) {
            return SourcePath;
        }

        if (StartsWith(SourcePath, "terrain:")) {
            return SourcePath;
        }

        const std::string NormalizedText{ std::filesystem::path{ SourcePath }.lexically_normal().generic_string() };
        const std::string SceneRootPrefix{ std::string{ "Resources/" } + SceneName + std::string{ "/" } };
        if (StartsWith(NormalizedText, SceneRootPrefix)) {
            return NormalizedText.substr(SceneRootPrefix.size());
        }

        const std::size_t ResourcesRootIndex{ NormalizedText.find("Resources/") };
        if (ResourcesRootIndex != std::string::npos) {
            const std::string ResourceRelativePath{ NormalizedText.substr(ResourcesRootIndex) };
            if (StartsWith(ResourceRelativePath, SceneRootPrefix)) {
                return ResourceRelativePath.substr(SceneRootPrefix.size());
            }

            return ResourceRelativePath;
        }

        return NormalizedText;
    }



    bool IsDefaultMaterialPath(const std::string& MaterialPath) {
        if (MaterialPath.empty()) {
            return true;
        }

        const std::filesystem::path NormalizedPath{ std::filesystem::path{ MaterialPath }.lexically_normal() };
        const std::string NormalizedText{ NormalizedPath.generic_string() };
        return NormalizedText == DefaultMaterialPathText;
    }

    bool TryFindRendererInHierarchy(const Arche::World::WorldReadOnlyView* ReadOnlyWorld, Arche::EntityID EntityId, const Game::StaticMeshRenderer*& OutRenderer) {
        const Game::StaticMeshRenderer* CurrentRenderer{ ReadOnlyWorld->GetComponent<Game::StaticMeshRenderer>(EntityId) };
        if (CurrentRenderer != nullptr && CurrentRenderer->model != nullptr) {
            OutRenderer = CurrentRenderer;
            return true;
        }

        const Game::EntityHierarchy* HierarchyComponent{ ReadOnlyWorld->GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr) {
            return false;
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            if (TryFindRendererInHierarchy(ReadOnlyWorld, ChildEntityId, OutRenderer) == true) {
                return true;
            }

            const Game::EntityHierarchy* ChildHierarchyComponent{ ReadOnlyWorld->GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }

        return false;
    }

    bool TryResolveMaterialGroupIndexInHierarchy(const Arche::World::WorldReadOnlyView* ReadOnlyWorld, Arche::EntityID EntityId, std::uint32_t& OutMaterialGroupIndex) {
        const Game::Material* CurrentMaterial{ ReadOnlyWorld->GetComponent<Game::Material>(EntityId) };
        if (CurrentMaterial != nullptr) {
            OutMaterialGroupIndex = CurrentMaterial->MaterialGroupIndex;
            return true;
        }

        const Game::EntityHierarchy* HierarchyComponent{ ReadOnlyWorld->GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr) {
            return false;
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            if (TryResolveMaterialGroupIndexInHierarchy(ReadOnlyWorld, ChildEntityId, OutMaterialGroupIndex) == true) {
                return true;
            }

            const Game::EntityHierarchy* ChildHierarchyComponent{ ReadOnlyWorld->GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }

        return false;
    }

    bool TryFindEntityByNameInHierarchy(const Arche::World* World, Arche::EntityID EntityId, const std::string& TargetNodeName, Arche::EntityID& OutEntityId, bool IncludeSelf) {
        if (IncludeSelf == true) {
            const Game::Name* NameComponent{ World->GetComponent<Game::Name>(EntityId) };
            if (NameComponent != nullptr) {
                const char* CurrentNameText{ Game::GetNameText(*NameComponent) };
                if (CurrentNameText != nullptr && TargetNodeName == CurrentNameText) {
                    OutEntityId = EntityId;
                    return true;
                }
            }
        }

        const Game::EntityHierarchy* HierarchyComponent{ World->GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr) {
            return false;
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            if (TryFindEntityByNameInHierarchy(World, ChildEntityId, TargetNodeName, OutEntityId, true) == true) {
                return true;
            }

            const Game::EntityHierarchy* ChildHierarchyComponent{ World->GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }

        return false;
    }

    bool TryFindAnimatorForSerializationInHierarchy(const Arche::World::WorldReadOnlyView* ReadOnlyWorld, Arche::EntityID EntityId, const Game::Animator*& OutAnimatorComponent, Arche::EntityID& OutAnimatorEntityId) {
        const Game::Animator* CurrentAnimatorComponent{ ReadOnlyWorld->GetComponent<Game::Animator>(EntityId) };
        if (CurrentAnimatorComponent != nullptr && CurrentAnimatorComponent->animation != nullptr) {
            OutAnimatorComponent = CurrentAnimatorComponent;
            OutAnimatorEntityId = EntityId;
            return true;
        }

        const Game::EntityHierarchy* HierarchyComponent{ ReadOnlyWorld->GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr) {
            return false;
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            if (TryFindAnimatorForSerializationInHierarchy(ReadOnlyWorld, ChildEntityId, OutAnimatorComponent, OutAnimatorEntityId) == true) {
                return true;
            }

            const Game::EntityHierarchy* ChildHierarchyComponent{ ReadOnlyWorld->GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }

        return false;
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

    std::string ToYamlText(const std::string_view Text) {
        return ToYamlText(std::string{ Text });
    }

    std::string ToYamlBooleanText(bool Value) {
        return Value ? std::string{ "true" } : std::string{ "false" };
    }

    std::string BuildFloatListText(const std::vector<float>& Values) {
        std::string Text{};
        for (std::size_t Index{ 0 }; Index < Values.size(); ++Index) {
            if (Index > 0ULL) {
                Text += ",";
            }

            Text += std::to_string(Values[Index]);
        }

        return Text;
    }

    bool TryParseFloatListText(const std::string& Text, std::vector<float>& OutValues) {
        if (Text.empty() == true) {
            return false;
        }

        std::stringstream Stream{ Text };
        std::string ValueToken{};
        std::vector<float> Values{};

        while (std::getline(Stream, ValueToken, ',')) {
            if (ValueToken.empty() == true) {
                return false;
            }

            Values.push_back(std::stof(ValueToken));
        }

        OutValues = std::move(Values);
        return true;
    }

    std::string BuildTerrainHeightSourceTypeText(Game::TerrainHeightSourceType SourceType) {
        if (SourceType == Game::TerrainHeightSourceType::Procedural) {
            return "Procedural";
        }

        return "HeightMap";
    }

    bool TryParseYamlBoolText(const std::string& ValueText, bool& OutValue) {
        if (ValueText == "true" || ValueText == "True" || ValueText == "1") {
            OutValue = true;
            return true;
        }

        if (ValueText == "false" || ValueText == "False" || ValueText == "0") {
            OutValue = false;
            return true;
        }

        return false;
    }

    bool TryParseTerrainHeightSourceTypeText(const std::string& ValueText, Game::TerrainHeightSourceType& OutValue) {
        if (ValueText == "HeightMap" || ValueText == "heightmap" || ValueText == "HeightMapPath") {
            OutValue = Game::TerrainHeightSourceType::HeightMap;
            return true;
        }

        if (ValueText == "Procedural" || ValueText == "procedural") {
            OutValue = Game::TerrainHeightSourceType::Procedural;
            return true;
        }

        return false;
    }

    std::string TrimCopy(const std::string& Text) {
        std::size_t BeginIndex{ 0 };
        while (BeginIndex < Text.size() && std::isspace(static_cast<unsigned char>(Text[BeginIndex])) != 0) {
            BeginIndex += 1;
        }

        std::size_t EndIndex{ Text.size() };
        while (EndIndex > BeginIndex && std::isspace(static_cast<unsigned char>(Text[EndIndex - 1])) != 0) {
            EndIndex -= 1;
        }

        return Text.substr(BeginIndex, EndIndex - BeginIndex);
    }

    std::string ToLowerCopy(const std::string& Text) {
        std::string LowerText{ Text };
        for (char& Character : LowerText) {
            Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
        }

        return LowerText;
    }

    bool TryReadStringChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, std::string& OutValue) {
        if (TargetNode.readable() == false || TargetNode.is_map() == false) {
            return false;
        }

        for (const char* Key : Keys) {
            if (TargetNode.has_child(Key) == false) {
                continue;
            }

            TargetNode[Key] >> OutValue;
            return true;
        }

        return false;
    }

    bool TryReadBoolChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, bool& OutValue) {
        if (TargetNode.readable() == false || TargetNode.is_map() == false) {
            return false;
        }

        for (const char* Key : Keys) {
            if (TargetNode.has_child(Key) == false) {
                continue;
            }

            std::string ValueText{};
            TargetNode[Key] >> ValueText;
            if (TryParseYamlBoolText(ValueText, OutValue) == true) {
                return true;
            }

            TargetNode[Key] >> OutValue;
            return true;
        }

        return false;
    }

    bool TryReadFloatChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, float& OutValue) {
        if (TargetNode.readable() == false || TargetNode.is_map() == false) {
            return false;
        }

        for (const char* Key : Keys) {
            if (TargetNode.has_child(Key) == false) {
                continue;
            }

            TargetNode[Key] >> OutValue;
            return true;
        }

        return false;
    }

    bool TryParsePhysicsActorTypeText(const std::string& ActorTypeText, PhysicsActorBase::PhysicsActorType& OutActorType) {
        const std::string NormalizedText{ ToLowerCopy(TrimCopy(ActorTypeText)) };
        if (NormalizedText == "dynamic" || NormalizedText == "0") {
            OutActorType = PhysicsActorBase::PhysicsActorType::Dynamic;
            return true;
        }

        if (NormalizedText == "kinematic" || NormalizedText == "1") {
            OutActorType = PhysicsActorBase::PhysicsActorType::Kinematic;
            return true;
        }

        if (NormalizedText == "static" || NormalizedText == "2") {
            OutActorType = PhysicsActorBase::PhysicsActorType::Static;
            return true;
        }

        return false;
    }

    const char* ResolvePhysicsActorTypeYamlText(PhysicsActorBase::PhysicsActorType ActorType) {
        switch (ActorType) {
            case PhysicsActorBase::PhysicsActorType::Dynamic:
                return "Dynamic";

            case PhysicsActorBase::PhysicsActorType::Kinematic:
                return "Kinematic";

            case PhysicsActorBase::PhysicsActorType::Static:
                return "Static";

            default:
                return "Dynamic";
        }
    }

    PhysicsActorBase::PhysicsActorFlags FilterPhysicsActorFlags(PhysicsActorBase::PhysicsActorFlags Flags) {
        constexpr std::uint32_t ValidFlagValue{ static_cast<std::uint32_t>(PhysicsActorBase::PhysicsActorFlags::Trigger) | static_cast<std::uint32_t>(PhysicsActorBase::PhysicsActorFlags::Sleeping) | static_cast<std::uint32_t>(PhysicsActorBase::PhysicsActorFlags::IgnoreTerrainCollide) | static_cast<std::uint32_t>(PhysicsActorBase::PhysicsActorFlags::IgnoreGravity) };
        std::uint32_t FlagValue{ static_cast<std::uint32_t>(Flags) };
        PhysicsActorBase::PhysicsActorFlags FilteredFlags{ static_cast<PhysicsActorBase::PhysicsActorFlags>(FlagValue & ValidFlagValue) };
        return FilteredFlags;
    }

    bool IsObsoletePhysicsActorFlagText(const std::string& FlagText) {
        const std::string NormalizedText{ ToLowerCopy(TrimCopy(FlagText)) };
        bool IsObsoleteFlag{ NormalizedText == "static" || NormalizedText == "kinematic" || NormalizedText == "terraincollide" || NormalizedText == "terrain_collide" || NormalizedText == "terrain-collide" || NormalizedText == "terrain" };
        return IsObsoleteFlag;
    }

    bool TryParsePhysicsActorFlagText(const std::string& FlagText, PhysicsActorBase::PhysicsActorFlags& OutFlag) {
        const std::string NormalizedText{ ToLowerCopy(TrimCopy(FlagText)) };
        if (NormalizedText.empty() == true) {
            return false;
        }

        bool IsNumericText{ true };
        for (char Character : NormalizedText) {
            if (std::isdigit(static_cast<unsigned char>(Character)) == 0) {
                IsNumericText = false;
                break;
            }
        }

        if (IsNumericText == true) {
            try {
                OutFlag = FilterPhysicsActorFlags(static_cast<PhysicsActorBase::PhysicsActorFlags>(std::stoul(NormalizedText)));
                return true;
            }
            catch (const std::exception&) {
                return false;
            }
        }

        if (NormalizedText == "none") {
            OutFlag = PhysicsActorBase::PhysicsActorFlags::None;
            return true;
        }

        if (NormalizedText == "trigger") {
            OutFlag = PhysicsActorBase::PhysicsActorFlags::Trigger;
            return true;
        }

        if (NormalizedText == "sleeping") {
            OutFlag = PhysicsActorBase::PhysicsActorFlags::Sleeping;
            return true;
        }

        if (NormalizedText == "ignoreterraincollide" || NormalizedText == "ignore_terrain_collide" || NormalizedText == "ignore-terrain-collide" || NormalizedText == "noterraincollide" || NormalizedText == "no_terrain_collide" || NormalizedText == "no-terrain-collide") {
            OutFlag = PhysicsActorBase::PhysicsActorFlags::IgnoreTerrainCollide;
            return true;
        }

        if (NormalizedText == "ignoregravity" || NormalizedText == "ignore_gravity" || NormalizedText == "ignore-gravity" || NormalizedText == "nogravity" || NormalizedText == "no_gravity" || NormalizedText == "no-gravity") {
            OutFlag = PhysicsActorBase::PhysicsActorFlags::IgnoreGravity;
            return true;
        }

        return false;
    }

    bool TryAppendPhysicsActorFlagsFromText(const std::string& FlagsText, PhysicsActorBase::PhysicsActorFlags& InOutFlags) {
        std::size_t CurrentStart{ 0 };
        while (CurrentStart < FlagsText.size()) {
            const std::size_t TokenEnd{ FlagsText.find_first_of("|,", CurrentStart) };
            const std::size_t TokenLength{ TokenEnd == std::string::npos ? FlagsText.size() - CurrentStart : TokenEnd - CurrentStart };
            const std::string Token{ TrimCopy(FlagsText.substr(CurrentStart, TokenLength)) };
            if (Token.empty() == false) {
                if (IsObsoletePhysicsActorFlagText(Token) == true) {
                    if (TokenEnd == std::string::npos) {
                        break;
                    }

                    CurrentStart = TokenEnd + 1;
                    continue;
                }

                PhysicsActorBase::PhysicsActorFlags Flag{};
                if (TryParsePhysicsActorFlagText(Token, Flag) == false) {
                    return false;
                }

                if (Flag == PhysicsActorBase::PhysicsActorFlags::None) {
                    InOutFlags = PhysicsActorBase::PhysicsActorFlags::None;
                }
                else {
                    InOutFlags = InOutFlags | Flag;
                }
            }

            if (TokenEnd == std::string::npos) {
                break;
            }

            CurrentStart = TokenEnd + 1;
        }

        return true;
    }

    bool TryReadPhysicsActorFlagsNode(c4::yml::ConstNodeRef FlagsNode, PhysicsActorBase::PhysicsActorFlags& OutFlags) {
        if (FlagsNode.readable() == false) {
            return false;
        }

        PhysicsActorBase::PhysicsActorFlags ParsedFlags{ PhysicsActorBase::PhysicsActorFlags::None };
        if (FlagsNode.is_seq() == true) {
            for (const c4::yml::ConstNodeRef FlagNode : FlagsNode.children()) {
                std::string FlagText{};
                FlagNode >> FlagText;
                if (TryAppendPhysicsActorFlagsFromText(FlagText, ParsedFlags) == false) {
                    return false;
                }
            }

            OutFlags = ParsedFlags;
            return true;
        }

        std::string FlagsText{};
        FlagsNode >> FlagsText;
        if (TryAppendPhysicsActorFlagsFromText(FlagsText, ParsedFlags) == false) {
            return false;
        }

        OutFlags = ParsedFlags;
        return true;
    }

    bool TryReadPhysicsActorFlagsChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, PhysicsActorBase::PhysicsActorFlags& OutFlags) {
        if (TargetNode.readable() == false || TargetNode.is_map() == false) {
            return false;
        }

        for (const char* Key : Keys) {
            if (TargetNode.has_child(Key) == false) {
                continue;
            }

            return TryReadPhysicsActorFlagsNode(TargetNode[Key], OutFlags);
        }

        return false;
    }

    bool HasPhysicsActorFlag(PhysicsActorBase::PhysicsActorFlags Flags, PhysicsActorBase::PhysicsActorFlags Flag) {
        return (Flags & Flag) != PhysicsActorBase::PhysicsActorFlags::None;
    }

    std::string BuildPhysicsActorFlagsYamlText(PhysicsActorBase::PhysicsActorFlags Flags) {
        if (Flags == PhysicsActorBase::PhysicsActorFlags::None) {
            return "None";
        }

        std::vector<std::string> FlagTexts{};
        if (HasPhysicsActorFlag(Flags, PhysicsActorBase::PhysicsActorFlags::Trigger) == true) {
            FlagTexts.push_back("Trigger");
        }

        if (HasPhysicsActorFlag(Flags, PhysicsActorBase::PhysicsActorFlags::Sleeping) == true) {
            FlagTexts.push_back("Sleeping");
        }

        if (HasPhysicsActorFlag(Flags, PhysicsActorBase::PhysicsActorFlags::IgnoreTerrainCollide) == true) {
            FlagTexts.push_back("IgnoreTerrainCollide");
        }

        if (HasPhysicsActorFlag(Flags, PhysicsActorBase::PhysicsActorFlags::IgnoreGravity) == true) {
            FlagTexts.push_back("IgnoreGravity");
        }

        if (FlagTexts.empty() == true) {
            return std::to_string(static_cast<std::uint32_t>(Flags));
        }

        std::string JoinedText{ FlagTexts[0] };
        for (std::size_t FlagIndex{ 1 }; FlagIndex < FlagTexts.size(); ++FlagIndex) {
            JoinedText += std::string{ ", " } + FlagTexts[FlagIndex];
        }

        return JoinedText;
    }

    bool TryReadBoundingBoxBinding(c4::yml::ConstNodeRef BoundingBoxNode, Arche::EntityID EntityId, PendingBoundingBoxBinding& OutBinding) {
        if (BoundingBoxNode.readable() == false || BoundingBoxNode.is_map() == false) {
            return false;
        }

        SimpleMath::Vector3 BoundingCenter{};
        SimpleMath::Vector3 BoundingExtents{};
        const bool IsCenterRead{ BoundingBoxNode.has_child("Center") && ReadVector3(BoundingBoxNode["Center"], BoundingCenter) };
        const bool IsExtentsRead{ BoundingBoxNode.has_child("Extents") && ReadVector3(BoundingBoxNode["Extents"], BoundingExtents) };
        if (IsCenterRead == false || IsExtentsRead == false) {
            return false;
        }

        OutBinding.EntityId = EntityId;
        OutBinding.Center = BoundingCenter;
        OutBinding.Extents = BoundingExtents;
        return true;
    }

    bool TryReadPhysicsActorSettings(c4::yml::ConstNodeRef PhysicsNode, Game::PhysicsActorSettings& OutSettings, std::string& OutErrorText) {
        if (PhysicsNode.readable() == false || PhysicsNode.is_map() == false) {
            OutErrorText = "Physics 섹터가 map 형식이 아닙니다.";
            return false;
        }

        std::string NameText{};
        if (TryReadStringChild(PhysicsNode, { "name", "Name" }, NameText) == true) {
            Game::SetPhysicsActorSettingsName(OutSettings, NameText);
        }

        TryReadBoolChild(PhysicsNode, { "active", "isActive", "IsActive" }, OutSettings.mIsActive);
        TryReadFloatChild(PhysicsNode, { "mass", "Mass" }, OutSettings.mMass);
        TryReadFloatChild(PhysicsNode, { "friction", "Friction" }, OutSettings.mFriction);
        TryReadFloatChild(PhysicsNode, { "restitution", "Restitution" }, OutSettings.mRestitution);

        std::string ActorTypeText{};
        if (TryReadStringChild(PhysicsNode, { "actorType", "ActorType", "type", "Type" }, ActorTypeText) == true) {
            if (TryParsePhysicsActorTypeText(ActorTypeText, OutSettings.mActorType) == false) {
                OutErrorText = std::string{ "Physics ActorType 값 오류: " } + ActorTypeText;
                return false;
            }
        }

        if (TryReadPhysicsActorFlagsChild(PhysicsNode, { "flags", "Flags" }, OutSettings.mFlags) == false) {
            if (PhysicsNode.has_child("flags") == true || PhysicsNode.has_child("Flags") == true) {
                OutErrorText = "Physics Flags 값 오류";
                return false;
            }
        }

        return true;
    }

    bool TryReadTerrainSplatMapExpressionEntry(c4::yml::ConstNodeRef EntryNode, std::string& OutName, std::string& OutFormula) {
        if (EntryNode.readable() == false || EntryNode.is_map() == false) {
            return false;
        }

        TryReadStringChild(EntryNode, { "Name" }, OutName);
        TryReadStringChild(EntryNode, { "Formula" }, OutFormula);
        return OutName.empty() == false && OutFormula.empty() == false;
    }

    bool TryReadTerrainSplatMapDesc(c4::yml::ConstNodeRef SplatMapNode, Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapDesc& OutDesc) {
        if (SplatMapNode.readable() == false || SplatMapNode.is_map() == false) {
            return false;
        }

        if (SplatMapNode.has_child("FallbackLayerIndex")) {
            SplatMapNode["FallbackLayerIndex"] >> OutDesc.mFallbackLayerIndex;
        }

        TryReadBoolChild(SplatMapNode, { "NormalizeWeights" }, OutDesc.mNormalizeWeights);

        if (SplatMapNode.has_child("MinimumWeightSum")) {
            SplatMapNode["MinimumWeightSum"] >> OutDesc.mMinimumWeightSum;
        }

        if (SplatMapNode.has_child("Variables")) {
            const c4::yml::ConstNodeRef VariablesNode{ SplatMapNode["Variables"] };
            if (VariablesNode.is_seq() == false) {
                return false;
            }

            OutDesc.mVariables.clear();
            for (const c4::yml::ConstNodeRef VariableNode : VariablesNode.children()) {
                Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapVariableDesc VariableDesc{};
                if (TryReadTerrainSplatMapExpressionEntry(VariableNode, VariableDesc.mName, VariableDesc.mFormula) == false) {
                    return false;
                }

                OutDesc.mVariables.push_back(std::move(VariableDesc));
            }
        }

        if (SplatMapNode.has_child("Layers")) {
            const c4::yml::ConstNodeRef LayersNode{ SplatMapNode["Layers"] };
            if (LayersNode.is_seq() == false) {
                return false;
            }

            OutDesc.mLayers.clear();
            for (const c4::yml::ConstNodeRef LayerNode : LayersNode.children()) {
                Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapLayerDesc LayerDesc{};
                if (TryReadTerrainSplatMapExpressionEntry(LayerNode, LayerDesc.mName, LayerDesc.mFormula) == false) {
                    return false;
                }

                OutDesc.mLayers.push_back(std::move(LayerDesc));
            }
        }

        return true;
    }

    bool TryReadTerrainProceduralHeightFieldDesc(c4::yml::ConstNodeRef ProceduralNode, Game::TerrainProceduralHeightFieldDesc& OutDesc) {
        if (ProceduralNode.readable() == false || ProceduralNode.is_map() == false) {
            return false;
        }

        if (ProceduralNode.has_child("Width")) {
            ProceduralNode["Width"] >> OutDesc.mWidth;
        }

        if (ProceduralNode.has_child("Height")) {
            ProceduralNode["Height"] >> OutDesc.mHeight;
        }

        if (ProceduralNode.has_child("Seed")) {
            ProceduralNode["Seed"] >> OutDesc.mSeed;
        }

        TryReadBoolChild(ProceduralNode, { "UseRandomSeed" }, OutDesc.mUseRandomSeed);

        if (ProceduralNode.has_child("OctaveCount")) {
            ProceduralNode["OctaveCount"] >> OutDesc.mOctaveCount;
        }

        if (ProceduralNode.has_child("NoiseScale")) {
            ProceduralNode["NoiseScale"] >> OutDesc.mNoiseScale;
        }

        if (ProceduralNode.has_child("Persistence")) {
            ProceduralNode["Persistence"] >> OutDesc.mPersistence;
        }

        if (ProceduralNode.has_child("Lacunarity")) {
            ProceduralNode["Lacunarity"] >> OutDesc.mLacunarity;
        }

        if (ProceduralNode.has_child("BaseHeight")) {
            ProceduralNode["BaseHeight"] >> OutDesc.mBaseHeight;
        }

        if (ProceduralNode.has_child("HeightAmplitude")) {
            ProceduralNode["HeightAmplitude"] >> OutDesc.mHeightAmplitude;
        }

        if (ProceduralNode.has_child("LodExponent")) {
            ProceduralNode["LodExponent"] >> OutDesc.mLodExponent;
        }

        if (ProceduralNode.has_child("SmoothingPassCount")) {
            ProceduralNode["SmoothingPassCount"] >> OutDesc.mSmoothingPassCount;
        }

        if (ProceduralNode.has_child("MinimumWidth")) {
            ProceduralNode["MinimumWidth"] >> OutDesc.mMinimumWidth;
        }

        if (ProceduralNode.has_child("MinimumHeight")) {
            ProceduralNode["MinimumHeight"] >> OutDesc.mMinimumHeight;
        }

        if (ProceduralNode.has_child("MaximumOctaveCount")) {
            ProceduralNode["MaximumOctaveCount"] >> OutDesc.mMaximumOctaveCount;
        }

        if (ProceduralNode.has_child("MaximumSmoothingPassCount")) {
            ProceduralNode["MaximumSmoothingPassCount"] >> OutDesc.mMaximumSmoothingPassCount;
        }

        if (ProceduralNode.has_child("MinimumHeightValue")) {
            ProceduralNode["MinimumHeightValue"] >> OutDesc.mMinimumHeightValue;
        }

        if (ProceduralNode.has_child("MaximumHeightValue")) {
            ProceduralNode["MaximumHeightValue"] >> OutDesc.mMaximumHeightValue;
        }

        if (ProceduralNode.has_child("SampleScaleX")) {
            ProceduralNode["SampleScaleX"] >> OutDesc.mSampleScaleX;
        }

        if (ProceduralNode.has_child("SampleScaleZ")) {
            ProceduralNode["SampleScaleZ"] >> OutDesc.mSampleScaleZ;
        }

        if (ProceduralNode.has_child("SampleOffsetX")) {
            ProceduralNode["SampleOffsetX"] >> OutDesc.mSampleOffsetX;
        }

        if (ProceduralNode.has_child("SampleOffsetZ")) {
            ProceduralNode["SampleOffsetZ"] >> OutDesc.mSampleOffsetZ;
        }

        if (ProceduralNode.has_child("InitialFrequency")) {
            ProceduralNode["InitialFrequency"] >> OutDesc.mInitialFrequency;
        }

        if (ProceduralNode.has_child("InitialAmplitude")) {
            ProceduralNode["InitialAmplitude"] >> OutDesc.mInitialAmplitude;
        }

        if (ProceduralNode.has_child("OctaveSeedStep")) {
            ProceduralNode["OctaveSeedStep"] >> OutDesc.mOctaveSeedStep;
        }

        if (ProceduralNode.has_child("NoiseNormalizationScale")) {
            ProceduralNode["NoiseNormalizationScale"] >> OutDesc.mNoiseNormalizationScale;
        }

        if (ProceduralNode.has_child("NoiseNormalizationBias")) {
            ProceduralNode["NoiseNormalizationBias"] >> OutDesc.mNoiseNormalizationBias;
        }

        if (ProceduralNode.has_child("HashShiftA")) {
            ProceduralNode["HashShiftA"] >> OutDesc.mHashShiftA;
        }

        if (ProceduralNode.has_child("HashShiftB")) {
            ProceduralNode["HashShiftB"] >> OutDesc.mHashShiftB;
        }

        if (ProceduralNode.has_child("HashShiftC")) {
            ProceduralNode["HashShiftC"] >> OutDesc.mHashShiftC;
        }

        if (ProceduralNode.has_child("HashShiftLimitExclusive")) {
            ProceduralNode["HashShiftLimitExclusive"] >> OutDesc.mHashShiftLimitExclusive;
        }

        if (ProceduralNode.has_child("HashMultiplierA")) {
            ProceduralNode["HashMultiplierA"] >> OutDesc.mHashMultiplierA;
        }

        if (ProceduralNode.has_child("HashMultiplierB")) {
            ProceduralNode["HashMultiplierB"] >> OutDesc.mHashMultiplierB;
        }

        if (ProceduralNode.has_child("HashCoordinateOffsetX")) {
            ProceduralNode["HashCoordinateOffsetX"] >> OutDesc.mHashCoordinateOffsetX;
        }

        if (ProceduralNode.has_child("HashCoordinateOffsetZ")) {
            ProceduralNode["HashCoordinateOffsetZ"] >> OutDesc.mHashCoordinateOffsetZ;
        }

        if (ProceduralNode.has_child("GradientDirectionCount")) {
            ProceduralNode["GradientDirectionCount"] >> OutDesc.mGradientDirectionCount;
        }

        if (ProceduralNode.has_child("FadeCoefficientA")) {
            ProceduralNode["FadeCoefficientA"] >> OutDesc.mFadeCoefficientA;
        }

        if (ProceduralNode.has_child("FadeCoefficientB")) {
            ProceduralNode["FadeCoefficientB"] >> OutDesc.mFadeCoefficientB;
        }

        if (ProceduralNode.has_child("FadeCoefficientC")) {
            ProceduralNode["FadeCoefficientC"] >> OutDesc.mFadeCoefficientC;
        }

        if (ProceduralNode.has_child("SmoothingCornerWeight")) {
            ProceduralNode["SmoothingCornerWeight"] >> OutDesc.mSmoothingCornerWeight;
        }

        if (ProceduralNode.has_child("SmoothingEdgeWeight")) {
            ProceduralNode["SmoothingEdgeWeight"] >> OutDesc.mSmoothingEdgeWeight;
        }

        if (ProceduralNode.has_child("SmoothingCenterWeight")) {
            ProceduralNode["SmoothingCenterWeight"] >> OutDesc.mSmoothingCenterWeight;
        }

        if (ProceduralNode.has_child("SmoothingWeightSum")) {
            ProceduralNode["SmoothingWeightSum"] >> OutDesc.mSmoothingWeightSum;
        }

        if (ProceduralNode.has_child("SplatMap")) {
            if (TryReadTerrainSplatMapDesc(ProceduralNode["SplatMap"], OutDesc.mSplatMapDesc) == false) {
                return false;
            }
        }

        return true;
    }

    bool TryReadTerrainBuildDesc(c4::yml::ConstNodeRef TerrainNode, const std::string& SceneName, Game::TerrainBuildDesc& OutDesc) {
        if (TerrainNode.readable() == false || TerrainNode.is_map() == false) {
            return false;
        }

        if (TerrainNode.has_child("HeightSourceType")) {
            std::string HeightSourceTypeText{};
            TerrainNode["HeightSourceType"] >> HeightSourceTypeText;
            if (TryParseTerrainHeightSourceTypeText(HeightSourceTypeText, OutDesc.mHeightSourceType) == false) {
                return false;
            }
        }
        else if (TerrainNode.has_child("HeightSource")) {
            std::string HeightSourceTypeText{};
            TerrainNode["HeightSource"] >> HeightSourceTypeText;
            if (TryParseTerrainHeightSourceTypeText(HeightSourceTypeText, OutDesc.mHeightSourceType) == false) {
                return false;
            }
        }

        if (TerrainNode.has_child("ProceduralHeightField")) {
            const bool IsProceduralDescRead{ TryReadTerrainProceduralHeightFieldDesc(TerrainNode["ProceduralHeightField"], OutDesc.mProceduralHeightFieldDesc) };
            if (IsProceduralDescRead == false) {
                return false;
            }

            if (TerrainNode.has_child("HeightSourceType") == false && TerrainNode.has_child("HeightSource") == false && TerrainNode.has_child("HeightMapPath") == false) {
                OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
        }

        if (TerrainNode.has_child("ProceduralHeightFieldPath")) {
            std::string ProceduralHeightFieldPath{};
            TerrainNode["ProceduralHeightFieldPath"] >> ProceduralHeightFieldPath;
            if (ProceduralHeightFieldPath.empty()) {
                return false;
            }

            OutDesc.mProceduralHeightFieldPath = ResolveSceneResourcePath(SceneName, ProceduralHeightFieldPath);
            OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
        }

        if (TerrainNode.has_child("HeightMapPath")) {
            std::string HeightMapPath{};
            TerrainNode["HeightMapPath"] >> HeightMapPath;
            if (HeightMapPath.empty()) {
                return false;
            }

            OutDesc.HeightMapPath = ResolveSceneResourcePath(SceneName, HeightMapPath);
        }

        if (OutDesc.mHeightSourceType == Game::TerrainHeightSourceType::HeightMap && OutDesc.HeightMapPath.empty() == true) {
            return false;
        }

        if (TerrainNode.has_child("MaxHeight")) {
            TerrainNode["MaxHeight"] >> OutDesc.MaxHeight;
        }

        if (TerrainNode.has_child("CellSizeX")) {
            TerrainNode["CellSizeX"] >> OutDesc.CellSizeX;
        }

        if (TerrainNode.has_child("CellSizeZ")) {
            TerrainNode["CellSizeZ"] >> OutDesc.CellSizeZ;
        }

        if (TerrainNode.has_child("FlipV")) {
            std::string ValueText{};
            TerrainNode["FlipV"] >> ValueText;
            bool Value{};
            if (TryParseYamlBoolText(ValueText, Value) == false) {
                return false;
            }

            OutDesc.FlipV = Value;
        }

        if (TerrainNode.has_child("CenterOrigin")) {
            std::string ValueText{};
            TerrainNode["CenterOrigin"] >> ValueText;
            bool Value{};
            if (TryParseYamlBoolText(ValueText, Value) == false) {
                return false;
            }

            OutDesc.CenterOrigin = Value;
        }

        if (TerrainNode.has_child("TileQuadCount")) {
            TerrainNode["TileQuadCount"] >> OutDesc.TileQuadCount;
        }

        if (TerrainNode.has_child("LodCount")) {
            TerrainNode["LodCount"] >> OutDesc.LodCount;
        }

        if (TerrainNode.has_child("LodDistances")) {
            const c4::yml::ConstNodeRef LodDistancesNode{ TerrainNode["LodDistances"] };
            if (LodDistancesNode.is_seq() == false) {
                return false;
            }

            OutDesc.LodDistances.clear();
            for (const c4::yml::ConstNodeRef LodDistanceNode : LodDistancesNode.children()) {
                float LodDistance{};
                LodDistanceNode >> LodDistance;
                OutDesc.LodDistances.push_back(LodDistance);
            }
        }

        if (TerrainNode.has_child("StreamingEnabled")) {
            std::string ValueText{};
            TerrainNode["StreamingEnabled"] >> ValueText;
            bool Value{};
            if (TryParseYamlBoolText(ValueText, Value) == false) {
                return false;
            }

            OutDesc.mStreamingEnabled = Value;
        }

        if (TerrainNode.has_child("StreamingGridStep")) {
            TerrainNode["StreamingGridStep"] >> OutDesc.mStreamingGridStep;
        }

        return true;
    }

    bool TryParseTerrainModelSelector(const std::string& Selector, Game::TerrainBuildDesc& OutDesc) {
        if (StartsWith(Selector, "terrain:") == false) {
            return false;
        }

        const std::string ParameterText{ Selector.substr(8) };
        if (ParameterText.empty()) {
            return false;
        }

        try {
            std::size_t CurrentStart{ 0 };
            while (CurrentStart < ParameterText.size()) {
                const std::size_t TokenEnd{ ParameterText.find(';', CurrentStart) };
                const std::size_t TokenLength{ TokenEnd == std::string::npos ? ParameterText.size() - CurrentStart : TokenEnd - CurrentStart };
                const std::string Token{ ParameterText.substr(CurrentStart, TokenLength) };
                const std::size_t EqualsIndex{ Token.find('=') };
                if (EqualsIndex == std::string::npos || EqualsIndex == 0 || EqualsIndex + 1 >= Token.size()) {
                    return false;
                }

                const std::string Key{ Token.substr(0, EqualsIndex) };
                const std::string Value{ Token.substr(EqualsIndex + 1) };
                if (Key == "HeightSourceType") {
                    if (TryParseTerrainHeightSourceTypeText(Value, OutDesc.mHeightSourceType) == false) {
                        return false;
                    }
                }
                else if (Key == "HeightMapPath") {
                    OutDesc.HeightMapPath = Value;
                }
                else if (Key == "ProceduralHeightFieldPath") {
                    OutDesc.mProceduralHeightFieldPath = Value;
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralWidth") {
                    OutDesc.mProceduralHeightFieldDesc.mWidth = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHeight") {
                    OutDesc.mProceduralHeightFieldDesc.mHeight = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSeed") {
                    OutDesc.mProceduralHeightFieldDesc.mSeed = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralUseRandomSeed") {
                    bool BoolValue{};
                    if (TryParseYamlBoolText(Value, BoolValue) == false) {
                        return false;
                    }

                    OutDesc.mProceduralHeightFieldDesc.mUseRandomSeed = BoolValue;
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralOctaveCount") {
                    OutDesc.mProceduralHeightFieldDesc.mOctaveCount = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralNoiseScale") {
                    OutDesc.mProceduralHeightFieldDesc.mNoiseScale = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralPersistence") {
                    OutDesc.mProceduralHeightFieldDesc.mPersistence = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralLacunarity") {
                    OutDesc.mProceduralHeightFieldDesc.mLacunarity = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralBaseHeight") {
                    OutDesc.mProceduralHeightFieldDesc.mBaseHeight = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHeightAmplitude") {
                    OutDesc.mProceduralHeightFieldDesc.mHeightAmplitude = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralLodExponent") {
                    OutDesc.mProceduralHeightFieldDesc.mLodExponent = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSmoothingPassCount") {
                    OutDesc.mProceduralHeightFieldDesc.mSmoothingPassCount = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMinimumWidth") {
                    OutDesc.mProceduralHeightFieldDesc.mMinimumWidth = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMinimumHeight") {
                    OutDesc.mProceduralHeightFieldDesc.mMinimumHeight = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMaximumOctaveCount") {
                    OutDesc.mProceduralHeightFieldDesc.mMaximumOctaveCount = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMaximumSmoothingPassCount") {
                    OutDesc.mProceduralHeightFieldDesc.mMaximumSmoothingPassCount = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMinimumHeightValue") {
                    OutDesc.mProceduralHeightFieldDesc.mMinimumHeightValue = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMaximumHeightValue") {
                    OutDesc.mProceduralHeightFieldDesc.mMaximumHeightValue = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSampleScaleX") {
                    OutDesc.mProceduralHeightFieldDesc.mSampleScaleX = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSampleScaleZ") {
                    OutDesc.mProceduralHeightFieldDesc.mSampleScaleZ = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSampleOffsetX") {
                    OutDesc.mProceduralHeightFieldDesc.mSampleOffsetX = static_cast<std::int32_t>(std::stol(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSampleOffsetZ") {
                    OutDesc.mProceduralHeightFieldDesc.mSampleOffsetZ = static_cast<std::int32_t>(std::stol(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralInitialFrequency") {
                    OutDesc.mProceduralHeightFieldDesc.mInitialFrequency = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralInitialAmplitude") {
                    OutDesc.mProceduralHeightFieldDesc.mInitialAmplitude = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralOctaveSeedStep") {
                    OutDesc.mProceduralHeightFieldDesc.mOctaveSeedStep = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralNoiseNormalizationScale") {
                    OutDesc.mProceduralHeightFieldDesc.mNoiseNormalizationScale = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralNoiseNormalizationBias") {
                    OutDesc.mProceduralHeightFieldDesc.mNoiseNormalizationBias = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashShiftA") {
                    OutDesc.mProceduralHeightFieldDesc.mHashShiftA = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashShiftB") {
                    OutDesc.mProceduralHeightFieldDesc.mHashShiftB = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashShiftC") {
                    OutDesc.mProceduralHeightFieldDesc.mHashShiftC = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashShiftLimitExclusive") {
                    OutDesc.mProceduralHeightFieldDesc.mHashShiftLimitExclusive = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashMultiplierA") {
                    OutDesc.mProceduralHeightFieldDesc.mHashMultiplierA = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashMultiplierB") {
                    OutDesc.mProceduralHeightFieldDesc.mHashMultiplierB = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashCoordinateOffsetX") {
                    OutDesc.mProceduralHeightFieldDesc.mHashCoordinateOffsetX = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashCoordinateOffsetZ") {
                    OutDesc.mProceduralHeightFieldDesc.mHashCoordinateOffsetZ = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralGradientDirectionCount") {
                    OutDesc.mProceduralHeightFieldDesc.mGradientDirectionCount = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralFadeCoefficientA") {
                    OutDesc.mProceduralHeightFieldDesc.mFadeCoefficientA = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralFadeCoefficientB") {
                    OutDesc.mProceduralHeightFieldDesc.mFadeCoefficientB = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralFadeCoefficientC") {
                    OutDesc.mProceduralHeightFieldDesc.mFadeCoefficientC = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSmoothingCornerWeight") {
                    OutDesc.mProceduralHeightFieldDesc.mSmoothingCornerWeight = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSmoothingEdgeWeight") {
                    OutDesc.mProceduralHeightFieldDesc.mSmoothingEdgeWeight = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSmoothingCenterWeight") {
                    OutDesc.mProceduralHeightFieldDesc.mSmoothingCenterWeight = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSmoothingWeightSum") {
                    OutDesc.mProceduralHeightFieldDesc.mSmoothingWeightSum = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "MaxHeight") {
                    OutDesc.MaxHeight = std::stof(Value);
                }
                else if (Key == "CellSizeX") {
                    OutDesc.CellSizeX = std::stof(Value);
                }
                else if (Key == "CellSizeZ") {
                    OutDesc.CellSizeZ = std::stof(Value);
                }
                else if (Key == "FlipV") {
                    bool BoolValue{};
                    if (TryParseYamlBoolText(Value, BoolValue) == false) {
                        return false;
                    }

                    OutDesc.FlipV = BoolValue;
                }
                else if (Key == "CenterOrigin") {
                    bool BoolValue{};
                    if (TryParseYamlBoolText(Value, BoolValue) == false) {
                        return false;
                    }

                    OutDesc.CenterOrigin = BoolValue;
                }
                else if (Key == "TileQuadCount") {
                    OutDesc.TileQuadCount = static_cast<std::uint32_t>(std::stoul(Value));
                }
                else if (Key == "LodCount") {
                    OutDesc.LodCount = static_cast<std::uint32_t>(std::stoul(Value));
                }
                else if (Key == "LodDistances") {
                    std::vector<float> LodDistances{};
                    if (TryParseFloatListText(Value, LodDistances) == false) {
                        return false;
                    }

                    OutDesc.LodDistances = std::move(LodDistances);
                }
                else if (Key == "StreamingEnabled") {
                    bool BoolValue{};
                    if (TryParseYamlBoolText(Value, BoolValue) == false) {
                        return false;
                    }

                    OutDesc.mStreamingEnabled = BoolValue;
                }
                else if (Key == "StreamingGridStep") {
                    OutDesc.mStreamingGridStep = static_cast<std::uint32_t>(std::stoul(Value));
                }
                else {
                    return false;
                }

                if (TokenEnd == std::string::npos) {
                    break;
                }

                CurrentStart = TokenEnd + 1;
            }
        }
        catch (const std::exception&) {
            return false;
        }

        if (OutDesc.mHeightSourceType == Game::TerrainHeightSourceType::Procedural) {
            return true;
        }

        return OutDesc.HeightMapPath.empty() == false;
    }

    void AppendTerrainSplatMapDesc(std::ostringstream& Stream, std::size_t IndentLevel, const Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapDesc& Desc) {
        if (Desc.mVariables.empty() == true && Desc.mLayers.empty() == true) {
            return;
        }

        AppendLine(Stream, IndentLevel, "SplatMap:");
        AppendLine(Stream, IndentLevel + 1, std::string{ "NormalizeWeights: " } + ToYamlBooleanText(Desc.mNormalizeWeights));
        AppendLine(Stream, IndentLevel + 1, std::string{ "FallbackLayerIndex: " } + std::to_string(Desc.mFallbackLayerIndex));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MinimumWeightSum: " } + std::to_string(Desc.mMinimumWeightSum));

        if (Desc.mVariables.empty() == false) {
            AppendLine(Stream, IndentLevel + 1, "Variables:");
            for (const Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapVariableDesc& VariableDesc : Desc.mVariables) {
                AppendLine(Stream, IndentLevel + 2, "- Name: " + ToYamlText(VariableDesc.mName));
                AppendLine(Stream, IndentLevel + 3, "Formula: " + ToYamlText(VariableDesc.mFormula));
            }
        }

        if (Desc.mLayers.empty() == false) {
            AppendLine(Stream, IndentLevel + 1, "Layers:");
            for (const Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapLayerDesc& LayerDesc : Desc.mLayers) {
                AppendLine(Stream, IndentLevel + 2, "- Name: " + ToYamlText(LayerDesc.mName));
                AppendLine(Stream, IndentLevel + 3, "Formula: " + ToYamlText(LayerDesc.mFormula));
            }
        }
    }

    void AppendTerrainProceduralHeightFieldDesc(std::ostringstream& Stream, std::size_t IndentLevel, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        AppendLine(Stream, IndentLevel, "ProceduralHeightField:");
        AppendLine(Stream, IndentLevel + 1, std::string{ "Width: " } + std::to_string(Desc.mWidth));
        AppendLine(Stream, IndentLevel + 1, std::string{ "Height: " } + std::to_string(Desc.mHeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "Seed: " } + std::to_string(Desc.mSeed));
        AppendLine(Stream, IndentLevel + 1, std::string{ "UseRandomSeed: " } + ToYamlBooleanText(Desc.mUseRandomSeed));
        AppendLine(Stream, IndentLevel + 1, std::string{ "OctaveCount: " } + std::to_string(Desc.mOctaveCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "NoiseScale: " } + std::to_string(Desc.mNoiseScale));
        AppendLine(Stream, IndentLevel + 1, std::string{ "Persistence: " } + std::to_string(Desc.mPersistence));
        AppendLine(Stream, IndentLevel + 1, std::string{ "Lacunarity: " } + std::to_string(Desc.mLacunarity));
        AppendLine(Stream, IndentLevel + 1, std::string{ "BaseHeight: " } + std::to_string(Desc.mBaseHeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HeightAmplitude: " } + std::to_string(Desc.mHeightAmplitude));
        AppendLine(Stream, IndentLevel + 1, std::string{ "LodExponent: " } + std::to_string(Desc.mLodExponent));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SmoothingPassCount: " } + std::to_string(Desc.mSmoothingPassCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MinimumWidth: " } + std::to_string(Desc.mMinimumWidth));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MinimumHeight: " } + std::to_string(Desc.mMinimumHeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MaximumOctaveCount: " } + std::to_string(Desc.mMaximumOctaveCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MaximumSmoothingPassCount: " } + std::to_string(Desc.mMaximumSmoothingPassCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MinimumHeightValue: " } + std::to_string(Desc.mMinimumHeightValue));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MaximumHeightValue: " } + std::to_string(Desc.mMaximumHeightValue));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SampleScaleX: " } + std::to_string(Desc.mSampleScaleX));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SampleScaleZ: " } + std::to_string(Desc.mSampleScaleZ));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SampleOffsetX: " } + std::to_string(Desc.mSampleOffsetX));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SampleOffsetZ: " } + std::to_string(Desc.mSampleOffsetZ));
        AppendLine(Stream, IndentLevel + 1, std::string{ "InitialFrequency: " } + std::to_string(Desc.mInitialFrequency));
        AppendLine(Stream, IndentLevel + 1, std::string{ "InitialAmplitude: " } + std::to_string(Desc.mInitialAmplitude));
        AppendLine(Stream, IndentLevel + 1, std::string{ "OctaveSeedStep: " } + std::to_string(Desc.mOctaveSeedStep));
        AppendLine(Stream, IndentLevel + 1, std::string{ "NoiseNormalizationScale: " } + std::to_string(Desc.mNoiseNormalizationScale));
        AppendLine(Stream, IndentLevel + 1, std::string{ "NoiseNormalizationBias: " } + std::to_string(Desc.mNoiseNormalizationBias));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashShiftA: " } + std::to_string(Desc.mHashShiftA));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashShiftB: " } + std::to_string(Desc.mHashShiftB));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashShiftC: " } + std::to_string(Desc.mHashShiftC));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashShiftLimitExclusive: " } + std::to_string(Desc.mHashShiftLimitExclusive));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashMultiplierA: " } + std::to_string(Desc.mHashMultiplierA));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashMultiplierB: " } + std::to_string(Desc.mHashMultiplierB));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashCoordinateOffsetX: " } + std::to_string(Desc.mHashCoordinateOffsetX));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashCoordinateOffsetZ: " } + std::to_string(Desc.mHashCoordinateOffsetZ));
        AppendLine(Stream, IndentLevel + 1, std::string{ "GradientDirectionCount: " } + std::to_string(Desc.mGradientDirectionCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "FadeCoefficientA: " } + std::to_string(Desc.mFadeCoefficientA));
        AppendLine(Stream, IndentLevel + 1, std::string{ "FadeCoefficientB: " } + std::to_string(Desc.mFadeCoefficientB));
        AppendLine(Stream, IndentLevel + 1, std::string{ "FadeCoefficientC: " } + std::to_string(Desc.mFadeCoefficientC));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SmoothingCornerWeight: " } + std::to_string(Desc.mSmoothingCornerWeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SmoothingEdgeWeight: " } + std::to_string(Desc.mSmoothingEdgeWeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SmoothingCenterWeight: " } + std::to_string(Desc.mSmoothingCenterWeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SmoothingWeightSum: " } + std::to_string(Desc.mSmoothingWeightSum));
        AppendTerrainSplatMapDesc(Stream, IndentLevel + 1, Desc.mSplatMapDesc);
    }

    void AppendTerrainBuildDesc(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& SceneName, const Game::TerrainBuildDesc& Desc) {
        AppendLine(Stream, IndentLevel, std::string{ TerrainTypeName } + std::string{ ":" });
        AppendLine(Stream, IndentLevel + 1, std::string{ "HeightSourceType: " } + BuildTerrainHeightSourceTypeText(Desc.mHeightSourceType));
        if (Desc.mHeightSourceType == Game::TerrainHeightSourceType::HeightMap) {
            AppendLine(Stream, IndentLevel + 1, std::string{ "HeightMapPath: " } + ToYamlText(MakeSceneRelativeResourcePath(SceneName, Desc.HeightMapPath)));
        }
        else if (Desc.mProceduralHeightFieldPath.empty() == false) {
            AppendLine(Stream, IndentLevel + 1, std::string{ "ProceduralHeightFieldPath: " } + ToYamlText(MakeSceneRelativeResourcePath(SceneName, Desc.mProceduralHeightFieldPath)));
        }
        else {
            AppendTerrainProceduralHeightFieldDesc(Stream, IndentLevel + 1, Desc.mProceduralHeightFieldDesc);
        }

        AppendLine(Stream, IndentLevel + 1, std::string{ "MaxHeight: " } + std::to_string(Desc.MaxHeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "CellSizeX: " } + std::to_string(Desc.CellSizeX));
        AppendLine(Stream, IndentLevel + 1, std::string{ "CellSizeZ: " } + std::to_string(Desc.CellSizeZ));
        AppendLine(Stream, IndentLevel + 1, std::string{ "FlipV: " } + ToYamlBooleanText(Desc.FlipV));
        AppendLine(Stream, IndentLevel + 1, std::string{ "CenterOrigin: " } + ToYamlBooleanText(Desc.CenterOrigin));
        AppendLine(Stream, IndentLevel + 1, std::string{ "TileQuadCount: " } + std::to_string(Desc.TileQuadCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "LodCount: " } + std::to_string(Desc.LodCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "LodDistances: [" } + BuildFloatListText(Desc.LodDistances) + std::string{ "]" });
        AppendLine(Stream, IndentLevel + 1, std::string{ "StreamingEnabled: " } + ToYamlBooleanText(Desc.mStreamingEnabled));
        AppendLine(Stream, IndentLevel + 1, std::string{ "StreamingGridStep: " } + std::to_string(Desc.mStreamingGridStep));
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

    void AppendBoundingBox(std::ostringstream& Stream, std::size_t IndentLevel, const DirectX::BoundingOrientedBox& LocalBoundingBox) {
        AppendLine(Stream, IndentLevel, std::string{ BoundingBoxTypeName } + std::string{ ":" });
        AppendVector3(Stream, IndentLevel + 1, "Center", SimpleMath::Vector3{ LocalBoundingBox.Center.x, LocalBoundingBox.Center.y, LocalBoundingBox.Center.z });
        AppendVector3(Stream, IndentLevel + 1, "Extents", SimpleMath::Vector3{ LocalBoundingBox.Extents.x, LocalBoundingBox.Extents.y, LocalBoundingBox.Extents.z });
    }

    void AppendPhysicsActorSettings(std::ostringstream& Stream, std::size_t IndentLevel, const Game::PhysicsActorSettings& SettingsComponent, const Game::BoundingBox* BoundingBoxComponent) {
        AppendLine(Stream, IndentLevel, std::string{ PhysicsTypeName } + std::string{ ":" });
        AppendLine(Stream, IndentLevel + 1, std::string{ "name: " } + ToYamlText(Game::GetPhysicsActorSettingsNameTextView(SettingsComponent)));
        AppendLine(Stream, IndentLevel + 1, std::string{ "active: " } + ToYamlBooleanText(SettingsComponent.mIsActive));
        AppendLine(Stream, IndentLevel + 1, std::string{ "actorType: " } + ResolvePhysicsActorTypeYamlText(SettingsComponent.mActorType));
        AppendLine(Stream, IndentLevel + 1, std::string{ "mass: " } + std::to_string(SettingsComponent.mMass));
        AppendLine(Stream, IndentLevel + 1, std::string{ "flags: [" } + BuildPhysicsActorFlagsYamlText(SettingsComponent.mFlags) + std::string{ "]" });
        AppendLine(Stream, IndentLevel + 1, std::string{ "friction: " } + std::to_string(SettingsComponent.mFriction));
        AppendLine(Stream, IndentLevel + 1, std::string{ "restitution: " } + std::to_string(SettingsComponent.mRestitution));

        if (BoundingBoxComponent != nullptr) {
            AppendBoundingBox(Stream, IndentLevel + 1, BoundingBoxComponent->GetObb());
        }
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
                    PrefabNode["prefabId"] >> Descriptor.PrefabId;
                }

                if (PrefabNode.has_child("modelPath")) {
                    std::string ModelPath{};
                    PrefabNode["modelPath"] >> ModelPath;
                    Descriptor.ModelSelector = ResolveSceneResourcePath(SceneName, ModelPath);
                }

                if (PrefabNode.has_child("materialPath")) {
                    PrefabNode["materialPath"] >> Descriptor.MaterialPath;
                }

                if (PrefabNode.has_child("active")) {
                    PrefabNode["active"] >> Descriptor.Active;
                }

                if (Descriptor.PrefabId != 0ull) {
                    PrefabDescriptors[Descriptor.PrefabId] = Descriptor;
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
                        PrefabModelSelector = PrefabIter->second.ModelSelector;
                        PrefabIsActive = PrefabIter->second.Active;

                        if (PrefabIter->second.MaterialPath.empty() == false) {
                            const std::string ResolvedPrefabMaterialPath{ ResolveSceneResourcePath(SceneName, PrefabIter->second.MaterialPath) };
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

                        TerrainHeightFieldFactory HeightFieldFactory{};
                        const HeightFieldData HeightFieldDataValue{ HeightFieldFactory.Build(Desc) };
                        PhysicsTerrainActor::ActorDesc TerrainActorDesc{ PhysicsTerrainActor::BuildHeightFieldActorDesc(HeightFieldDataValue.Width, HeightFieldDataValue.Height, HeightFieldDataValue.HeightValues, Desc.MaxHeight, Desc.CellSizeX, Desc.CellSizeZ, Desc.CenterOrigin) };

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
                NewBinding.SourceEntityId = Entity;

                if (AnimationNode.has_child("text")) {
                    AnimationNode["text"] >> AnimationPath;
                }

                if (AnimationNode.has_child("initclip")) {
                    AnimationNode["initclip"] >> NewBinding.ClipIndex;
                    NewBinding.FallbackClipIndex = NewBinding.ClipIndex;
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
                        NewBinding.AnimationGraphData = AnimationGraphData.get();
                    }
                }

                if (AnimationNode.has_child("node")) {
                    AnimationNode["node"] >> NewBinding.TargetNodeName;
                }

                if (ComponentsNode.has_child(RuntimeVariablesTypeName)) {
                    const c4::yml::ConstNodeRef RuntimeVariablesNode{ ComponentsNode[RuntimeVariablesTypeName] };
                    for (const c4::yml::ConstNodeRef RuntimeVariableNode : RuntimeVariablesNode.children()) {
                        PendingAnimatorBinding::PendingRuntimeVariableInitialization NewInitialization{};
                        std::string TypeText{};
                        RuntimeVariableNode["Name"] >> NewInitialization.ParameterName;
                        RuntimeVariableNode["Type"] >> TypeText;

                        if (TypeText == "Bool") {
                            NewInitialization.Type = PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Bool;
                            RuntimeVariableNode["Value"] >> NewInitialization.BoolValue;
                        }
                        else if (TypeText == "Int") {
                            NewInitialization.Type = PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Int;
                            RuntimeVariableNode["Value"] >> NewInitialization.IntValue;
                        }
                        else if (TypeText == "Float") {
                            NewInitialization.Type = PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Float;
                            RuntimeVariableNode["Value"] >> NewInitialization.FloatValue;
                        }
                        else {
                            LoadResult.IsSuccess = false;
                            LoadResult.UndecidedItems.push_back(std::string{ "RuntimeVariables Type 값 오류: " } + TypeText);
                            continue;
                        }

                        NewBinding.RuntimeVariableInitializations.push_back(std::move(NewInitialization));
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
                        NewBinding.AnimationData = AnimationData.get();
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
            if (PendingBoundingBoxBindingItem.EntityId == Arche::NullEntityID) {
                continue;
            }

            DirectX::BoundingOrientedBox LocalBoundingBox{};
            LocalBoundingBox.Center = DirectX::XMFLOAT3{ PendingBoundingBoxBindingItem.Center.x, PendingBoundingBoxBindingItem.Center.y, PendingBoundingBoxBindingItem.Center.z };
            LocalBoundingBox.Extents = DirectX::XMFLOAT3{ PendingBoundingBoxBindingItem.Extents.x, PendingBoundingBoxBindingItem.Extents.y, PendingBoundingBoxBindingItem.Extents.z };
            LocalBoundingBox.Orientation = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };

            BoundingBox* ExistingBoundingBox{ OutScene.GetWorld().GetComponent<BoundingBox>(PendingBoundingBoxBindingItem.EntityId) };
            if (ExistingBoundingBox == nullptr) {
                BoundingBox NewBoundingBox{};
                NewBoundingBox.SetObb(LocalBoundingBox);
                const Transform* TransformComponent{ std::as_const(OutScene.GetWorld()).GetComponent<Transform>(PendingBoundingBoxBindingItem.EntityId) };
                if (TransformComponent != nullptr) {
                    const SimpleMath::Matrix TransformOnlyWorldMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent->scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent->rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent->position) };
                    DirectX::BoundingOrientedBox WorldBoundingBox{};
                    LocalBoundingBox.Transform(WorldBoundingBox, TransformOnlyWorldMatrix);
                    NewBoundingBox.SetWorldObb(WorldBoundingBox);
                }
                OutScene.GetWorld().AddComponent(PendingBoundingBoxBindingItem.EntityId, NewBoundingBox);
            }
            else {
                ExistingBoundingBox->SetObb(LocalBoundingBox);
                const Transform* TransformComponent{ std::as_const(OutScene.GetWorld()).GetComponent<Transform>(PendingBoundingBoxBindingItem.EntityId) };
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
            if (Binding.AnimationData == nullptr) {
                StdOutput::WriteWarningLine(std::format("[SceneYamlSerializer] Animation data is null. source={}:{} node={}", Binding.SourceEntityId.index, Binding.SourceEntityId.generation, Binding.TargetNodeName));
                continue;
            }

            Arche::EntityID TargetEntityId{ Binding.SourceEntityId };
            if (Binding.TargetNodeName.empty() == false) {
                const bool IsFound{ TryFindEntityByNameInHierarchy(&OutScene.GetWorld(), Binding.SourceEntityId, Binding.TargetNodeName, TargetEntityId, false) };
                if (IsFound == false) {
                    StdOutput::WriteWarningLine(std::format("[SceneYamlSerializer] Animation target node not found. source={}:{} node={}", Binding.SourceEntityId.index, Binding.SourceEntityId.generation, Binding.TargetNodeName));
                    continue;
                }
            }

            Animator NewAnimator{};
            NewAnimator.animation = Binding.AnimationData;
            NewAnimator.clipIndex = Binding.ClipIndex;
            NewAnimator.FallbackClipIndex = Binding.FallbackClipIndex;
            NewAnimator.GraphAsset = Binding.AnimationGraphData;
            NewAnimator.IsGraphEnabled = Binding.AnimationGraphData != nullptr;

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

                for (const PendingAnimatorBinding::PendingRuntimeVariableInitialization& Initialization : Binding.RuntimeVariableInitializations) {
                    if (Initialization.Type == PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Bool) {
                        VariableTable.TrySetBoolParameter(Definitions, Initialization.ParameterName, Initialization.BoolValue);
                    }
                    else if (Initialization.Type == PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Int) {
                        VariableTable.TrySetIntParameter(Definitions, Initialization.ParameterName, Initialization.IntValue);
                    }
                    else {
                        VariableTable.TrySetFloatParameter(Definitions, Initialization.ParameterName, Initialization.FloatValue);
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
            Descriptor.PrefabId = PrefabInstanceComponent->PrefabId;

            const StaticMeshRenderer* ResolvedRenderer{ nullptr };
            if (TryFindRendererInHierarchy(ReadOnlyWorld, EntityId, ResolvedRenderer) == true && ResolvedRenderer != nullptr) {
                const std::string ModelSelector{ AssetRegistryInstance->FindModelSelectorByPointer(ResolvedRenderer->model) };
                Descriptor.ModelSelector = MakeSceneRelativeResourcePath(TargetSnapshot.GetSceneName(), ModelSelector);
                Descriptor.Active = ResolvedRenderer->active;
            }

            std::uint32_t ResolvedMaterialGroupIndex{ 0 };
            if (TryResolveMaterialGroupIndexInHierarchy(ReadOnlyWorld, EntityId, ResolvedMaterialGroupIndex) == true) {
                const std::string MaterialPath{ AssetRegistryInstance->FindMaterialGroupSourcePathByIndex(ResolvedMaterialGroupIndex) };
                if (IsDefaultMaterialPath(MaterialPath) == false) {
                    Descriptor.MaterialPath = MakeSceneRelativeResourcePath(TargetSnapshot.GetSceneName(), MaterialPath);
                }
            }

            PrefabDescriptors[Descriptor.PrefabId] = Descriptor;
        }

        if (PrefabDescriptors.empty() == false) {
            AppendLine(Stream, 0, "Prefabs:");
            for (const std::pair<const std::uint64_t, PrefabDescriptor>& PrefabPair : PrefabDescriptors) {
                const PrefabDescriptor& Descriptor{ PrefabPair.second };
                AppendLine(Stream, 1, std::string{ "- &Prefab" } + std::to_string(Descriptor.PrefabId));
                AppendLine(Stream, 2, std::string{ "prefabId: " } + std::to_string(Descriptor.PrefabId));
                if (Descriptor.ModelSelector.empty()) {
                    SaveResult.IsSuccess = false;
                    SaveResult.UndecidedItems.push_back(std::string{ "PrefabId 에 대응되는 modelPath 를 찾지 못했습니다: " } + std::to_string(Descriptor.PrefabId));
                }

                AppendLine(Stream, 2, std::string{ "modelPath: " } + ToYamlText(Descriptor.ModelSelector));
                AppendLine(Stream, 2, std::string{ "materialPath: " } + ToYamlText(Descriptor.MaterialPath));
                AppendLine(Stream, 2, std::string{ "active: " } + ToYamlBooleanText(Descriptor.Active));
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
