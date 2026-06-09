#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "Arche/Common.h"
#include "Asset/AnimationClipResult.h"
#include "DirectXTK12/SimpleMath.h"
#include "Game/Scene/SceneYamlSerializer.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

namespace Game {
    class AnimationGraphAsset;

    namespace Pipeline {
        class Scene;
    }
}

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
        DirectX::SimpleMath::Vector3 mCenter{ DirectX::SimpleMath::Vector3::Zero };
        DirectX::SimpleMath::Vector3 mExtents{ DirectX::SimpleMath::Vector3::One };
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
        SceneYamlLoadResult Deserialize(const std::string& YamlText, Pipeline::Scene& OutScene, std::unordered_map<std::int64_t, Arche::EntityID>& OutEntityIdMap) const;
    };

    class SceneYamlWriter final {
    public:
        SceneYamlSaveResult Serialize(const SceneWorldSnapshot& TargetSnapshot, std::string& OutYamlText) const;
    };
}
