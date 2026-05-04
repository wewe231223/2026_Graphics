#pragma once
#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <exception>
#include <format>
#include <initializer_list>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include "Game/Scene/SceneYamlSerializer.h"
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
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"
#include "Utility/StdOutput.h"

namespace Game::SceneYaml {
    extern const char* const TransformTypeName;
    extern const char* const MaterialTypeName;
    extern const char* const StaticMeshRendererTypeName;
    extern const char* const TerrainTypeName;
    extern const char* const CullingTypeName;
    extern const char* const AnimationTypeName;
    extern const char* const CameraTypeName;
    extern const char* const DirectionalLightTypeName;
    extern const char* const TagTypeName;
    extern const char* const ScriptTypeName;
    extern const char* const ScriptComponentTypeName;
    extern const char* const BehaviorInstanceComponentTypeName;
    extern const char* const NameTypeName;
    extern const char* const PrefabInstanceTypeName;
    extern const char* const BoneSkinReferenceTypeName;
    extern const char* const FootIKRigTypeName;
    extern const char* const RuntimeVariablesTypeName;
    extern const char* const PhysicsTypeName;
    extern const char* const BoundingBoxTypeName;
    extern const char* const DefaultMaterialPathText;
    extern const char* const CameraModeFreeLookText;
    extern const char* const CameraModeThirdPersonText;
    extern const char* const CameraModeCinematicText;

    struct PrefabDescriptor final {
    public:
        std::uint64_t mPrefabId{};
        std::string mModelSelector{};
        std::string mMaterialPath{};
        bool mActive{ true };
    };

    struct PendingAnimatorBinding final {
    public:
        struct PendingRuntimeVariableInitialization final {
        public:
            enum class RuntimeVariableType : std::uint8_t {
                Bool,
                Int,
                Float,
            };

        public:
            std::string mParameterName{};
            RuntimeVariableType mType{ RuntimeVariableType::Bool };
            bool mBoolValue{};
            std::int32_t mIntValue{};
            float mFloatValue{};
        };

    public:
        Arche::EntityID mSourceEntityId{ Arche::NullEntityID };
        std::string mTargetNodeName{};
        asset::Animation* mAnimationData{ nullptr };
        Game::AnimationGraphAsset* mAnimationGraphData{ nullptr };
        std::int32_t mClipIndex{ -1 };
        std::int32_t mFallbackClipIndex{ -1 };
        std::vector<PendingRuntimeVariableInitialization> mRuntimeVariableInitializations{};
    };

    struct PendingBoundingBoxBinding final {
    public:
        Arche::EntityID mEntityId{ Arche::NullEntityID };
        SimpleMath::Vector3 mCenter{ SimpleMath::Vector3::Zero };
        SimpleMath::Vector3 mExtents{ SimpleMath::Vector3::One };
    };

    struct PendingTerrainSnapBinding final {
    public:
        Arche::EntityID mEntityId{ Arche::NullEntityID };
        float mOffsetY{};
    };

    struct TerrainSurfaceBinding final {
    public:
        Arche::EntityID mEntityId{ Arche::NullEntityID };
        PhysicsTerrainActor::ActorDesc mTerrainActorDesc{};
    };

    class SceneYamlDeserializer final {
    public:
        SceneYamlLoadResult Deserialize(const std::string& YamlText, Scene& OutScene) const;
    };

    class SceneYamlWriter final {
    public:
        SceneYamlSaveResult Serialize(const SceneWorldSnapshot& TargetSnapshot, std::string& OutYamlText) const;
    };

    bool ReadVector3(c4::yml::ConstNodeRef TargetNode, SimpleMath::Vector3& OutValue);
    bool ReadVector2(c4::yml::ConstNodeRef TargetNode, SimpleMath::Vector2& OutValue);
    bool ReadQuaternion(c4::yml::ConstNodeRef TargetNode, SimpleMath::Quaternion& OutValue);
    bool ReadColor4(c4::yml::ConstNodeRef TargetNode, float* OutValue);
    bool TryParseCameraModeText(const std::string& CameraModeText, std::uint32_t& OutCameraFlags);
    const char* ResolveCameraModeText(std::uint32_t CameraFlags);
    std::unique_ptr<Game::ISystem> CreateSystemByName(const std::string& SystemName);
    bool TryReadSystemName(c4::yml::ConstNodeRef SystemNode, std::string& OutSystemName);
    bool StartsWith(const std::string& Text, const std::string& Prefix);
    std::string BuildPrimitiveSelector(const std::string& PrimitiveType, float PrimitiveSize, const std::array<float, 4>& PrimitiveColor);
    std::string ResolveSceneResourcePath(const std::string& SceneName, const std::string& FileName);
    std::string MakeSceneRelativeResourcePath(const std::string& SceneName, const std::string& SourcePath);
    bool IsDefaultMaterialPath(const std::string& MaterialPath);
    bool TryFindRendererInHierarchy(const Arche::World::WorldReadOnlyView* ReadOnlyWorld, Arche::EntityID EntityId, const Game::StaticMeshRenderer*& OutRenderer);
    bool TryResolveMaterialGroupIndexInHierarchy(const Arche::World::WorldReadOnlyView* ReadOnlyWorld, Arche::EntityID EntityId, std::uint32_t& OutMaterialGroupIndex);
    bool TryFindEntityByNameInHierarchy(const Arche::World* World, Arche::EntityID EntityId, const std::string& TargetNodeName, Arche::EntityID& OutEntityId, bool IncludeRoot);
    bool TryFindAnimatorForSerializationInHierarchy(const Arche::World::WorldReadOnlyView* ReadOnlyWorld, Arche::EntityID RootEntityId, const Game::Animator*& OutAnimator, Arche::EntityID& OutAnimatorEntityId);
    bool ShouldSkipEntityInSceneExport(const Game::StaticMeshRenderer* StaticMeshRendererComponent);
    void AppendLine(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Text);
    std::string ToYamlText(const char* Text);
    std::string ToYamlText(const std::string& Text);
    std::string ToYamlText(const std::string_view Text);
    std::string ToYamlBooleanText(bool Value);
    std::string BuildFloatListText(const std::vector<float>& Values);
    bool TryParseFloatListText(const std::string& Text, std::vector<float>& OutValues);
    std::string BuildTerrainHeightSourceTypeText(Game::TerrainHeightSourceType SourceType);
    bool TryParseYamlBoolText(const std::string& ValueText, bool& OutValue);
    bool TryParseTerrainHeightSourceTypeText(const std::string& ValueText, Game::TerrainHeightSourceType& OutSourceType);
    std::string TrimCopy(const std::string& Text);
    std::string ToLowerCopy(const std::string& Text);
    bool TryReadStringChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, std::string& OutValue);
    bool TryReadBoolChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, bool& OutValue);
    bool TryReadFloatChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, float& OutValue);
    bool TryParsePhysicsActorTypeText(const std::string& ActorTypeText, PhysicsActorBase::PhysicsActorType& OutActorType);
    const char* ResolvePhysicsActorTypeYamlText(PhysicsActorBase::PhysicsActorType ActorType);
    PhysicsActorBase::PhysicsActorFlags FilterPhysicsActorFlags(PhysicsActorBase::PhysicsActorFlags Flags);
    bool IsObsoletePhysicsActorFlagText(const std::string& FlagText);
    bool TryParsePhysicsActorFlagText(const std::string& FlagText, PhysicsActorBase::PhysicsActorFlags& OutFlag);
    bool TryAppendPhysicsActorFlagsFromText(const std::string& FlagsText, PhysicsActorBase::PhysicsActorFlags& InOutFlags);
    bool TryReadPhysicsActorFlagsNode(c4::yml::ConstNodeRef FlagsNode, PhysicsActorBase::PhysicsActorFlags& OutFlags);
    bool TryReadPhysicsActorFlagsChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, PhysicsActorBase::PhysicsActorFlags& OutFlags);
    bool HasPhysicsActorFlag(PhysicsActorBase::PhysicsActorFlags Flags, PhysicsActorBase::PhysicsActorFlags TargetFlag);
    std::string BuildPhysicsActorFlagsYamlText(PhysicsActorBase::PhysicsActorFlags Flags);
    bool TryReadBoundingBoxBinding(c4::yml::ConstNodeRef BoundingBoxNode, Arche::EntityID EntityId, PendingBoundingBoxBinding& OutBinding);
    bool TryReadPhysicsActorSettings(c4::yml::ConstNodeRef PhysicsNode, Game::PhysicsActorSettings& OutSettings, std::string& OutErrorText);
    SimpleMath::Matrix BuildTransformOnlyWorldMatrix(const Game::Transform& TransformComponent);
    SimpleMath::Matrix BuildTransformOffsetMatrix(const Game::Transform& TransformComponent);
    std::vector<SimpleMath::Vector2> BuildTerrainSnapSamplePoints(const Game::BoundingBox* BoundingBoxComponent, const Game::Transform& TransformComponent);
    float CalculateBottomOffsetY(const Game::BoundingBox* BoundingBoxComponent, const Game::Transform& TransformComponent);
    bool TryResolveHighestTerrainSurfaceHeight(const Arche::World& World, const std::vector<TerrainSurfaceBinding>& TerrainSurfaceBindings, const SimpleMath::Vector3& Position, float& OutSurfaceHeight);
    bool TryResolveHighestTerrainSurfaceHeight(const Arche::World& World, const std::vector<TerrainSurfaceBinding>& TerrainSurfaceBindings, const std::vector<SimpleMath::Vector2>& SamplePoints, float& OutSurfaceHeight);
    bool TryBuildTerrainActorDescFromHeightField(const Game::HeightFieldData& HeightFieldDataValue, const Game::TerrainBuildDesc& TerrainBuildDescValue, PhysicsTerrainActor::ActorDesc& OutActorDesc);
    bool TryBuildTerrainActorDescFromRenderResource(const Game::TerrainRenderResource& TerrainResource, PhysicsTerrainActor::ActorDesc& OutActorDesc);
    std::uint32_t ResolveTerrainStreamingGridStep(const Game::TerrainBuildDesc& TerrainBuildDescValue);
    std::int32_t FloorTerrainStreamingGridToStep(std::int32_t Value, std::uint32_t Step);
    std::int32_t CalculateTerrainStreamingOriginGrid(float FocusPosition, float CellSize, std::uint32_t HeightFieldVertexCount, std::uint32_t TileQuadCount, std::uint32_t Step);
    float CalculateTerrainStreamingWorldOrigin(std::int32_t OriginGrid, std::uint32_t HeightFieldVertexCount, float CellSize);
    bool TryResolveTerrainStreamingFocusPosition(Arche::World& World, SimpleMath::Vector3& OutFocusPosition);
    bool TryPrepareInitialStreamingTerrainBuildDesc(Arche::World& World, Game::TerrainBuildDesc& InOutTerrainBuildDesc);
    void ApplyInitialStreamingTerrainTransform(const Game::TerrainRenderResource& TerrainResource, Game::Transform* TerrainTransformComponent);
    void ApplyPendingTerrainSnapBindings(Arche::World& World, const std::vector<TerrainSurfaceBinding>& TerrainSurfaceBindings, const std::vector<PendingTerrainSnapBinding>& PendingTerrainSnapBindings);
    bool TryReadTerrainSplatMapExpressionEntry(c4::yml::ConstNodeRef EntryNode, std::string& OutName, std::vector<float>& OutParameters);
    bool TryReadTerrainSplatMapDesc(c4::yml::ConstNodeRef SplatMapNode, Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapDesc& OutSplatMapDesc);
    bool TryReadTerrainProceduralHeightFieldDesc(c4::yml::ConstNodeRef ProceduralNode, Game::TerrainProceduralHeightFieldDesc& OutDesc);
    bool TryReadTerrainBuildDesc(c4::yml::ConstNodeRef TerrainNode, const std::string& SceneName, Game::TerrainBuildDesc& OutDesc);
    bool TryParseTerrainModelSelector(const std::string& Selector, Game::TerrainBuildDesc& OutDesc);
    void AppendTerrainSplatMapDesc(std::ostringstream& Stream, std::size_t IndentLevel, const Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapDesc& SplatMapDesc);
    void AppendTerrainProceduralHeightFieldDesc(std::ostringstream& Stream, std::size_t IndentLevel, const Game::TerrainProceduralHeightFieldDesc& Desc);
    void AppendTerrainBuildDesc(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& SceneName, const Game::TerrainBuildDesc& Desc);
    void AppendVector3(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const SimpleMath::Vector3& Value);
    void AppendVector2(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const SimpleMath::Vector2& Value);
    void AppendQuaternion(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const SimpleMath::Quaternion& Value);
    void AppendColor4(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const float* Value);
    void AppendBoundingBox(std::ostringstream& Stream, std::size_t IndentLevel, const DirectX::BoundingOrientedBox& LocalBoundingBox);
    void AppendPhysicsActorSettings(std::ostringstream& Stream, std::size_t IndentLevel, const Game::PhysicsActorSettings& SettingsComponent, const Game::BoundingBox* BoundingBoxComponent);
}
