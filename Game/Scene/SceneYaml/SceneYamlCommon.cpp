#include "SceneYamlInternal.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <ryml_std.hpp>
#include "Game/Model/Model.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Legacy/Systems/AnimationGraphSystem.h"
#include "Game/Scene/Legacy/Systems/AnimateSystem.h"
#include "Game/Scene/Systems/CameraRenderSystem.h"
#include "Game/Scene/Legacy/Systems/FootIKSystem.h"
#include "Game/Scene/Systems/PhysicsActorUpdateSystem.h"
#include "Game/Scene/Systems/ProceduralFoliageSystem.h"
#include "Game/Scene/Systems/ShadowMappingParameterSystem.h"
#include "Game/Scene/Legacy/Systems/SkinnedMeshPrepareSystem.h"
#include "Game/Scene/Legacy/Systems/SkinnedMeshRenderSystem.h"
#include "Game/Scene/Legacy/Systems/StaticRenderSystem.h"
#include "Game/Scene/Legacy/Systems/TerrainRenderSystem.h"
#include "Game/Scene/Systems/TerrainStreamingSystem.h"
#include "Game/Scene/Legacy/Systems/TransformWorldSystem.h"

namespace Game::SceneYaml {
    const char* const TransformTypeName{ "Transform" };
    const char* const MaterialTypeName{ "Material" };
    const char* const StaticMeshRendererTypeName{ "StaticMeshRenderer" };
    const char* const TerrainTypeName{ "Terrain" };
    const char* const CullingTypeName{ "Culling" };
    const char* const AnimationTypeName{ "Animation" };
    const char* const CameraTypeName{ "Camera" };
    const char* const DirectionalLightTypeName{ "DirectionalLight" };
    const char* const TagTypeName{ "Tag" };
    const char* const ScriptTypeName{ "Script" };
    const char* const ScriptComponentTypeName{ "ScriptComponent" };
    const char* const BehaviorInstanceComponentTypeName{ "BehaviorInstanceComponent" };
    const char* const NameTypeName{ "Name" };
    const char* const PrefabInstanceTypeName{ "PrefabInstance" };
    const char* const BoneSkinReferenceTypeName{ "BoneSkinReference" };
    const char* const FootIKRigTypeName{ "FootIKRig" };
    const char* const RuntimeVariablesTypeName{ "RuntimeVariables" };
    const char* const PhysicsTypeName{ "Physics" };
    const char* const BoundingBoxTypeName{ "BB" };
    const char* const DefaultMaterialPathText{ "Resources/DefaultResource/DefaultMaterial.json" };
    const char* const CameraModeFreeLookText{ "FreeLook" };
    const char* const CameraModeThirdPersonText{ "ThirdPerson" };
    const char* const CameraModeCinematicText{ "Cinematic" };

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
            { "ProceduralFoliageSystem", []() -> std::unique_ptr<Game::ISystem> { return std::make_unique<Game::ProceduralFoliageSystem>(); } },
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

        OutBinding.mEntityId = EntityId;
        OutBinding.mCenter = BoundingCenter;
        OutBinding.mExtents = BoundingExtents;
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
