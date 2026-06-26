#include "Environment/EnvironmentFoliageRuntime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <ryml.hpp>
#include <ryml_std.hpp>

#include "Core/Config.h"
#include "Environment/EnvironmentObjectRenderContext.h"
#include "Environment/EnvironmentObjectTypes.h"
#include "Game/Model/AssetRegistry.h"
#include "Game/Model/PrimitiveModelFactory.h"
#include "Game/Model/TerrainRenderResource.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Frustum.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "RenderContract/Gather/RenderGatherResultMerger.h"
#include "PhysicsLib/Actors/PhysicsStaticActor.h"
#include "PhysicsLib/Runtime/PhysicsRuntime.h"
#include "Utility/ErrorHandler.h"

namespace {
    constexpr float FoliageEpsilon{ 0.0001f };
    constexpr float FoliageTwoPi{ 6.28318530717958647692f };
    constexpr std::uint32_t FoliageHashOffset{ 2166136261u };
    constexpr std::uint32_t FoliageHashPrime{ 16777619u };

    enum class FoliageUpdatePhase {
        Idle,
        BuildCandidates,
        ApplyCandidates,
        RebuildSlotLookup
    };

    struct FoliagePlacementLodDesc final {
    public:
        std::string mModelPath{};
        std::string mMaterialPath{};
        float mMaximumDistance{ std::numeric_limits<float>::max() };
        bool mEnabled{ true };
    };

    struct FoliagePlacementRule final {
    public:
        std::string mName{};
        std::string mLayerName{};
        std::string mModelPath{};
        std::string mMaterialPath{};
        std::vector<FoliagePlacementLodDesc> mLods{};
        std::vector<std::string> mExcludedLayerNames{};
        std::uint32_t mLayerIndex{ 0u };
        std::uint32_t mInstancesPerCell{ 1u };
        float mDensityMultiplier{ 1.0f };
        float mSpawnChance{ 1.0f };
        float mMinimumWeight{};
        float mMinimumScale{ 1.0f };
        float mMaximumScale{ 1.0f };
        float mMinimumYawDegrees{};
        float mMaximumYawDegrees{};
        float mClusterStrength{};
        float mClusterScale{};
        float mClusterContrast{ 1.0f };
        float mClusterCoverage{ 1.0f };
        float mClusterEdgeSoftness{};
        float mClusterOutsideDensity{};
        float mForestStrength{ 1.0f };
        float mOffsetY{};
        float mMinimumSpacing{};
        bool mCollisionActorEnabled{ false };
        SimpleMath::Vector3 mCollisionCenter{};
        SimpleMath::Vector3 mCollisionExtents{};
        float mCollisionFriction{};
        float mCollisionRestitution{};
    };

    struct FoliagePlacementConfig final {
    public:
        std::vector<FoliagePlacementRule> mRules{};
        bool mEnabled{ true };
        float mInactiveHeight{};
        float mPlacementRadius{};
        float mCellSize{ 1.0f };
        float mUpdateInterval{};
        std::uint32_t mUpdateCellBatchSize{ 96u };
        std::uint32_t mUpdateCandidateBatchSize{ 512u };
        std::uint32_t mUpdateSlotBatchSize{ 512u };
        float mDensityMultiplier{ 1.0f };
        std::uint32_t mCandidateRandomXStream{};
        std::uint32_t mCandidateRandomZStream{};
        std::uint32_t mCandidateRandomChanceStream{};
        std::uint32_t mCandidateRandomYawStream{};
        std::uint32_t mCandidateRandomScaleStream{};
        std::uint32_t mClusterCornerStream{};
        float mClusterDensityMaximum{ 1.0f };
        float mClusterScaleMinimumCellMultiplier{};
        float mClumpGridScaleMultiplier{};
        float mClumpGridScaleMinimumCellMultiplier{};
        float mClumpCenterOffset{};
        float mClumpCenterJitter{};
        std::uint32_t mClumpCenterXStream{};
        std::uint32_t mClumpCenterZStream{};
        std::uint32_t mClumpAngleStream{};
        std::uint32_t mClumpDistanceStream{};
        float mClumpRadiusScale{};
        float mClumpPullStrengthScale{};
        float mClumpPullStrengthMaximum{};
        bool mForestEnabled{ false };
        float mForestMinimumWidth{};
        float mForestPatchScale{ 1.0f };
        float mForestPatchCoverage{ 1.0f };
        float mForestPatchContrast{ 1.0f };
        float mForestPatchEdgeSoftness{};
        float mForestOutsideDensity{};
        std::uint32_t mForestPatchNoiseIndex{};
        std::uint32_t mForestPatchCornerStream{};
        float mForestMinimumAreaFactor{};
        float mForestWidthSampleRadiusScale{};
        float mForestWidthDiagonalSampleScale{};
        float mForestWidthRequiredSampleRatio{};
        std::uint32_t mMinimumSpacingPriorityStream{};
        std::uint32_t mSeedSalt{};
    };

    struct FoliageRuntimeLod final {
    public:
        std::shared_ptr<Game::Model> mModel{};
        float mMaximumDistance{ std::numeric_limits<float>::max() };
        std::uint32_t mMaterialGroupIndex{ 0u };
        bool mEnabled{ true };
    };

    struct FoliageRuntimeRule final {
    public:
        FoliagePlacementRule mDesc{};
        std::vector<FoliageRuntimeLod> mLods{};
        std::uint32_t mMaterialGroupIndex{ 0u };
    };

    struct FoliageCandidateKey final {
    public:
        std::int32_t mCellX{ 0 };
        std::int32_t mCellZ{ 0 };
        std::uint32_t mRuleIndex{ 0u };
        std::uint32_t mInstanceIndex{ 0u };
    };

    bool operator==(const FoliageCandidateKey& Left, const FoliageCandidateKey& Right) {
        return Left.mCellX == Right.mCellX && Left.mCellZ == Right.mCellZ && Left.mRuleIndex == Right.mRuleIndex && Left.mInstanceIndex == Right.mInstanceIndex;
    }

    struct FoliageCandidateKeyHasher final {
    public:
        std::size_t operator()(const FoliageCandidateKey& Key) const {
            std::uint32_t Hash{ FoliageHashOffset };
            Hash = (Hash ^ static_cast<std::uint32_t>(Key.mCellX)) * FoliageHashPrime;
            Hash = (Hash ^ static_cast<std::uint32_t>(Key.mCellZ)) * FoliageHashPrime;
            Hash = (Hash ^ Key.mRuleIndex) * FoliageHashPrime;
            Hash = (Hash ^ Key.mInstanceIndex) * FoliageHashPrime;
            return static_cast<std::size_t>(Hash);
        }
    };

    struct FoliageCandidate final {
    public:
        FoliageCandidateKey mKey{};
        SimpleMath::Vector3 mPosition{ SimpleMath::Vector3::Zero };
        float mYawRadians{ 0.0f };
        float mScale{ 1.0f };
        std::uint32_t mLodIndex{ 0u };
    };

    struct FoliageSlot final {
    public:
        FoliageCandidateKey mKey{};
        Arche::EntityID mRootEntityId{ Arche::NullEntityID };
        PhysicsActorBase* mCollisionActorPointer{ nullptr };
        std::uint32_t mCollisionActorIndex{ 0u };
        float mInactiveHeight{};
        std::uint32_t mRuleIndex{ 0u };
        std::uint32_t mActiveLodIndex{ std::numeric_limits<std::uint32_t>::max() };
        SimpleMath::Vector3 mRuntimePosition{ SimpleMath::Vector3::Zero };
        SimpleMath::Vector3 mRuntimeRotationEuler{ SimpleMath::Vector3::Zero };
        SimpleMath::Vector3 mRuntimeScale{ SimpleMath::Vector3::One };
        std::uint32_t mRuntimePhysicsWorldVersion{};
        bool mActive{ false };
        bool mAssignedThisFrame{ false };
        bool mHasRuntimeState{ false };
        bool mRuntimeActive{ false };
    };

    struct GeneratedFoliageCell final {
    public:
        Game::EnvironmentObjectCell mCell{};
        std::vector<FoliageCandidate> mCandidates{};
    };

    struct TerrainSamplingContext final {
    public:
        const Game::TerrainRenderResource* mResource{ nullptr };
        Game::Transform mTransform{};
    };

    std::uint32_t MixHash(std::uint32_t Value) {
        Value ^= Value >> 16u;
        Value *= 0x7feb352du;
        Value ^= Value >> 15u;
        Value *= 0x846ca68bu;
        Value ^= Value >> 16u;
        return Value;
    }

    std::uint32_t BuildCandidateHash(std::uint32_t TerrainSeed, std::uint32_t Salt, const FoliageCandidateKey& Key, std::uint32_t Stream) {
        std::uint32_t Hash{ MixHash(TerrainSeed ^ Salt ^ Stream) };
        Hash = MixHash(Hash ^ static_cast<std::uint32_t>(Key.mCellX));
        Hash = MixHash(Hash ^ (static_cast<std::uint32_t>(Key.mCellZ) * 0x9e3779b9u));
        Hash = MixHash(Hash ^ (Key.mRuleIndex * 0x85ebca6bu));
        Hash = MixHash(Hash ^ (Key.mInstanceIndex * 0xc2b2ae35u));
        return Hash;
    }

    std::uint32_t BuildClusterHash(std::uint32_t TerrainSeed, std::uint32_t Salt, std::int32_t GridX, std::int32_t GridZ, std::uint32_t ClusterIndex, std::uint32_t Stream) {
        std::uint32_t Hash{ MixHash(TerrainSeed ^ Salt ^ Stream) };
        Hash = MixHash(Hash ^ static_cast<std::uint32_t>(GridX));
        Hash = MixHash(Hash ^ (static_cast<std::uint32_t>(GridZ) * 0x9e3779b9u));
        Hash = MixHash(Hash ^ (ClusterIndex * 0x85ebca6bu));
        return Hash;
    }

    float HashToUnitFloat(std::uint32_t Hash) {
        constexpr float InverseValue{ 1.0f / 16777215.0f };
        return static_cast<float>(Hash & 0x00ffffffu) * InverseValue;
    }

    float Lerp(float Start, float End, float Alpha) {
        return Start + ((End - Start) * Alpha);
    }

    float SmoothStep01(float Value) {
        const float ClampedValue{ std::clamp(Value, 0.0f, 1.0f) };
        return ClampedValue * ClampedValue * (3.0f - (2.0f * ClampedValue));
    }

    float SmoothStepRange(float Start, float End, float Value) {
        if (End <= Start) {
            return Value >= End ? 1.0f : 0.0f;
        }

        return SmoothStep01((Value - Start) / (End - Start));
    }

    float SampleClusterCorner(std::uint32_t TerrainSeed, std::uint32_t Salt, std::int32_t GridX, std::int32_t GridZ, std::uint32_t ClusterIndex, std::uint32_t Stream) {
        return HashToUnitFloat(BuildClusterHash(TerrainSeed, Salt, GridX, GridZ, ClusterIndex, Stream));
    }

    float SampleValueNoise01(std::uint32_t TerrainSeed, std::uint32_t Salt, float X, float Z, std::uint32_t ClusterIndex, std::uint32_t Stream) {
        const std::int32_t X0{ static_cast<std::int32_t>(std::floor(X)) };
        const std::int32_t Z0{ static_cast<std::int32_t>(std::floor(Z)) };
        const std::int32_t X1{ X0 + 1 };
        const std::int32_t Z1{ Z0 + 1 };
        const float BlendX{ SmoothStep01(X - static_cast<float>(X0)) };
        const float BlendZ{ SmoothStep01(Z - static_cast<float>(Z0)) };
        const float Value00{ SampleClusterCorner(TerrainSeed, Salt, X0, Z0, ClusterIndex, Stream) };
        const float Value10{ SampleClusterCorner(TerrainSeed, Salt, X1, Z0, ClusterIndex, Stream) };
        const float Value01{ SampleClusterCorner(TerrainSeed, Salt, X0, Z1, ClusterIndex, Stream) };
        const float Value11{ SampleClusterCorner(TerrainSeed, Salt, X1, Z1, ClusterIndex, Stream) };
        const float ValueX0{ Lerp(Value00, Value10, BlendX) };
        const float ValueX1{ Lerp(Value01, Value11, BlendX) };
        return Lerp(ValueX0, ValueX1, BlendZ);
    }

    float SampleFoliageClusterFactor(std::uint32_t TerrainSeed, const FoliagePlacementConfig& Config, const FoliagePlacementRule& Rule, std::uint32_t ClusterIndex, float WorldX, float WorldZ) {
        if (Rule.mClusterStrength <= 0.0f) {
            return 1.0f;
        }

        const float ClusterScale{ std::max(Rule.mClusterScale, 1.0f) };
        const float NoiseValue{ SampleValueNoise01(TerrainSeed, Config.mSeedSalt, WorldX / ClusterScale, WorldZ / ClusterScale, ClusterIndex, Config.mClusterCornerStream) };
        const float ContrastedValue{ std::clamp(((NoiseValue - 0.5f) * Rule.mClusterContrast) + 0.5f, 0.0f, 1.0f) };
        const float Coverage{ std::clamp(Rule.mClusterCoverage, 0.01f, 1.0f) };
        const float Threshold{ 1.0f - Coverage };
        const float EdgeSoftness{ std::max(Rule.mClusterEdgeSoftness, 0.0f) };
        const float ClusterMask{ SmoothStepRange(Threshold - EdgeSoftness, Threshold + EdgeSoftness, ContrastedValue) };
        const float ClusterBoost{ std::min(1.0f / Coverage, Config.mClusterDensityMaximum) };
        const float ClusterDensity{ Lerp(Rule.mClusterOutsideDensity, ClusterBoost, ClusterMask) };
        return std::clamp(Lerp(1.0f, ClusterDensity, Rule.mClusterStrength), 0.0f, Config.mClusterDensityMaximum);
    }

    float SampleForestAreaFactor(std::uint32_t TerrainSeed, const FoliagePlacementConfig& Config, float WorldX, float WorldZ) {
        if (Config.mForestEnabled == false) {
            return 1.0f;
        }

        const float PatchScale{ std::max(Config.mForestPatchScale, 1.0f) };
        const float NoiseValue{ SampleValueNoise01(TerrainSeed, Config.mSeedSalt, WorldX / PatchScale, WorldZ / PatchScale, Config.mForestPatchNoiseIndex, Config.mForestPatchCornerStream) };
        const float ContrastedValue{ std::clamp(((NoiseValue - 0.5f) * Config.mForestPatchContrast) + 0.5f, 0.0f, 1.0f) };
        const float Coverage{ std::clamp(Config.mForestPatchCoverage, 0.01f, 1.0f) };
        const float Threshold{ 1.0f - Coverage };
        const float EdgeSoftness{ std::max(Config.mForestPatchEdgeSoftness, 0.0f) };
        const float ForestMask{ SmoothStepRange(Threshold - EdgeSoftness, Threshold + EdgeSoftness, ContrastedValue) };
        return std::clamp(Lerp(Config.mForestOutsideDensity, 1.0f, ForestMask), 0.0f, 1.0f);
    }

    float ResolveRuleForestFactor(std::uint32_t TerrainSeed, const FoliagePlacementConfig& Config, const FoliagePlacementRule& Rule, float WorldX, float WorldZ) {
        if (Rule.mForestStrength <= 0.0f) {
            return 1.0f;
        }

        const float ForestFactor{ SampleForestAreaFactor(TerrainSeed, Config, WorldX, WorldZ) };
        return std::clamp(Lerp(1.0f, ForestFactor, Rule.mForestStrength), 0.0f, 1.0f);
    }

    void ApplyFoliageClumpPosition(std::uint32_t TerrainSeed, const FoliagePlacementConfig& Config, const FoliagePlacementRule& Rule, const FoliageCandidateKey& Key, std::uint32_t ClusterIndex, float CellSize, float& WorldX, float& WorldZ) {
        if (Rule.mClusterStrength <= 0.0f) {
            return;
        }

        const float ClusterScale{ std::max(Rule.mClusterScale, CellSize * Config.mClusterScaleMinimumCellMultiplier) };
        const float ClumpGridScale{ std::max(ClusterScale * Config.mClumpGridScaleMultiplier, CellSize * Config.mClumpGridScaleMinimumCellMultiplier) };
        const std::int32_t ClumpGridX{ static_cast<std::int32_t>(std::floor(WorldX / ClumpGridScale)) };
        const std::int32_t ClumpGridZ{ static_cast<std::int32_t>(std::floor(WorldZ / ClumpGridScale)) };
        const float CenterX{ (static_cast<float>(ClumpGridX) + Config.mClumpCenterOffset + (HashToUnitFloat(BuildClusterHash(TerrainSeed, Config.mSeedSalt, ClumpGridX, ClumpGridZ, ClusterIndex, Config.mClumpCenterXStream)) * Config.mClumpCenterJitter)) * ClumpGridScale };
        const float CenterZ{ (static_cast<float>(ClumpGridZ) + Config.mClumpCenterOffset + (HashToUnitFloat(BuildClusterHash(TerrainSeed, Config.mSeedSalt, ClumpGridX, ClumpGridZ, ClusterIndex, Config.mClumpCenterZStream)) * Config.mClumpCenterJitter)) * ClumpGridScale };
        const float Angle{ HashToUnitFloat(BuildCandidateHash(TerrainSeed, Config.mSeedSalt, Key, Config.mClumpAngleStream)) * FoliageTwoPi };
        const float DistanceAlpha{ std::sqrt(HashToUnitFloat(BuildCandidateHash(TerrainSeed, Config.mSeedSalt, Key, Config.mClumpDistanceStream))) };
        const float ClumpRadius{ ClumpGridScale * Config.mClumpRadiusScale };
        const float TargetX{ CenterX + (std::cos(Angle) * DistanceAlpha * ClumpRadius) };
        const float TargetZ{ CenterZ + (std::sin(Angle) * DistanceAlpha * ClumpRadius) };
        const float PullStrength{ std::clamp(Rule.mClusterStrength * Config.mClumpPullStrengthScale, 0.0f, Config.mClumpPullStrengthMaximum) };
        WorldX = Lerp(WorldX, TargetX, PullStrength);
        WorldZ = Lerp(WorldZ, TargetZ, PullStrength);
    }

    bool StartsWithPathPrefix(const std::string& Text, const std::string& Prefix) {
        if (Text.size() < Prefix.size()) {
            return false;
        }

        return Text.compare(0, Prefix.size(), Prefix) == 0;
    }

    std::string ResolveFoliageResourcePath(const std::string& ConfigPath, const std::string& ResourcePath) {
        if (ResourcePath.empty() == true || StartsWithPathPrefix(ResourcePath, "primitive:") == true || StartsWithPathPrefix(ResourcePath, "Resources/") == true || StartsWithPathPrefix(ResourcePath, "Resources\\") == true) {
            return ResourcePath;
        }

        const std::filesystem::path ResourceFilePath{ ResourcePath };
        if (ResourceFilePath.is_absolute() == true) {
            return ResourceFilePath.generic_string();
        }

        const std::filesystem::path ConfigParentPath{ std::filesystem::path{ ConfigPath }.parent_path() };
        if (ConfigParentPath.empty() == true) {
            return ResourcePath;
        }

        return (ConfigParentPath / ResourceFilePath).lexically_normal().generic_string();
    }

    c4::yml::ConstNodeRef ResolveConfigNode(c4::yml::ConstNodeRef RootNode) {
        if (RootNode.readable() == false || RootNode.is_map() == false) {
            throw std::runtime_error{ "Procedural foliage config root must be a map." };
        }

        if (RootNode.has_child("ProceduralFoliage") == true) {
            const c4::yml::ConstNodeRef ConfigNode{ RootNode["ProceduralFoliage"] };
            if (ConfigNode.readable() == false || ConfigNode.is_map() == false) {
                throw std::runtime_error{ "ProceduralFoliage config must be a map." };
            }

            return ConfigNode;
        }

        return RootNode;
    }

    void ReadStringChild(c4::yml::ConstNodeRef Node, const char* Key, std::string& OutValue) {
        if (Node.has_child(Key) == false) {
            return;
        }

        Node[Key] >> OutValue;
    }

    void ReadUInt32Child(c4::yml::ConstNodeRef Node, const char* Key, std::uint32_t& OutValue) {
        if (Node.has_child(Key) == false) {
            return;
        }

        Node[Key] >> OutValue;
    }

    void ReadFloatChild(c4::yml::ConstNodeRef Node, const char* Key, float& OutValue) {
        if (Node.has_child(Key) == false) {
            return;
        }

        Node[Key] >> OutValue;
    }

    void ReadBoolChild(c4::yml::ConstNodeRef Node, const char* Key, bool& OutValue) {
        if (Node.has_child(Key) == false) {
            return;
        }

        Node[Key] >> OutValue;
    }

    void ReadVector3Child(c4::yml::ConstNodeRef Node, const char* Key, SimpleMath::Vector3& OutValue) {
        if (Node.has_child(Key) == false) {
            return;
        }

        const c4::yml::ConstNodeRef VectorNode{ Node[Key] };
        if (VectorNode.is_seq() == false) {
            throw std::runtime_error{ "Procedural foliage vector value must be a sequence." };
        }

        std::array<float, 3ULL> Values{};
        std::size_t ValueIndex{};
        for (const c4::yml::ConstNodeRef ValueNode : VectorNode.children()) {
            if (ValueIndex >= Values.size()) {
                throw std::runtime_error{ "Procedural foliage vector value must have exactly three elements." };
            }

            ValueNode >> Values[ValueIndex];
            ValueIndex += 1ULL;
        }

        if (ValueIndex != Values.size()) {
            throw std::runtime_error{ "Procedural foliage vector value must have exactly three elements." };
        }

        OutValue = SimpleMath::Vector3{ Values[0], Values[1], Values[2] };
    }

    void ReadStringListChild(c4::yml::ConstNodeRef Node, const char* Key, std::vector<std::string>& OutValues) {
        if (Node.has_child(Key) == false) {
            return;
        }

        const c4::yml::ConstNodeRef ListNode{ Node[Key] };
        if (ListNode.is_seq() == false) {
            throw std::runtime_error{ "Procedural foliage string list must be a sequence." };
        }

        OutValues.clear();
        for (const c4::yml::ConstNodeRef ItemNode : ListNode.children()) {
            std::string Value{};
            ItemNode >> Value;
            if (Value.empty() == false) {
                OutValues.push_back(std::move(Value));
            }
        }
    }

    FoliagePlacementLodDesc ReadFoliageLodDesc(c4::yml::ConstNodeRef LodNode, const std::string& ConfigPath) {
        if (LodNode.readable() == false || LodNode.is_map() == false) {
            throw std::runtime_error{ "Procedural foliage lod must be a map." };
        }

        FoliagePlacementLodDesc LodDesc{};
        ReadStringChild(LodNode, "ModelPath", LodDesc.mModelPath);
        ReadStringChild(LodNode, "MaterialPath", LodDesc.mMaterialPath);
        ReadFloatChild(LodNode, "Distance", LodDesc.mMaximumDistance);
        ReadFloatChild(LodNode, "MaximumDistance", LodDesc.mMaximumDistance);
        ReadBoolChild(LodNode, "Enabled", LodDesc.mEnabled);
        ReadBoolChild(LodNode, "Visible", LodDesc.mEnabled);
        LodDesc.mModelPath = ResolveFoliageResourcePath(ConfigPath, LodDesc.mModelPath);
        LodDesc.mMaterialPath = ResolveFoliageResourcePath(ConfigPath, LodDesc.mMaterialPath);
        LodDesc.mMaximumDistance = std::max(LodDesc.mMaximumDistance, 0.0f);

        if (LodDesc.mModelPath.empty() == true && LodDesc.mMaterialPath.empty() == true) {
            LodDesc.mEnabled = false;
        }

        return LodDesc;
    }

    void ReadFoliageLods(c4::yml::ConstNodeRef RuleNode, const std::string& ConfigPath, FoliagePlacementRule& Rule) {
        Rule.mLods.clear();
        if (RuleNode.has_child("Lods") == true) {
            const c4::yml::ConstNodeRef LodsNode{ RuleNode["Lods"] };
            if (LodsNode.is_seq() == false) {
                throw std::runtime_error{ "Procedural foliage lods must be a sequence." };
            }

            for (const c4::yml::ConstNodeRef LodNode : LodsNode.children()) {
                Rule.mLods.push_back(ReadFoliageLodDesc(LodNode, ConfigPath));
            }
        }

        if (Rule.mLods.empty() == true && Rule.mModelPath.empty() == false) {
            FoliagePlacementLodDesc LodDesc{};
            LodDesc.mModelPath = Rule.mModelPath;
            LodDesc.mMaterialPath = Rule.mMaterialPath;
            LodDesc.mMaximumDistance = std::numeric_limits<float>::max();
            Rule.mLods.push_back(std::move(LodDesc));
        }

        std::sort(Rule.mLods.begin(), Rule.mLods.end(), [](const FoliagePlacementLodDesc& Left, const FoliagePlacementLodDesc& Right) {
            return Left.mMaximumDistance < Right.mMaximumDistance;
        });
    }

    FoliagePlacementRule ReadFoliageRule(c4::yml::ConstNodeRef RuleNode, const std::string& ConfigPath) {
        if (RuleNode.readable() == false || RuleNode.is_map() == false) {
            throw std::runtime_error{ "Procedural foliage rule must be a map." };
        }

        FoliagePlacementRule Rule{};
        ReadStringChild(RuleNode, "Name", Rule.mName);
        ReadStringChild(RuleNode, "Layer", Rule.mLayerName);
        ReadStringChild(RuleNode, "LayerName", Rule.mLayerName);
        ReadUInt32Child(RuleNode, "LayerIndex", Rule.mLayerIndex);
        ReadStringListChild(RuleNode, "ExcludedLayers", Rule.mExcludedLayerNames);
        ReadStringListChild(RuleNode, "BlockedLayers", Rule.mExcludedLayerNames);
        ReadStringChild(RuleNode, "ModelPath", Rule.mModelPath);
        ReadStringChild(RuleNode, "MaterialPath", Rule.mMaterialPath);
        ReadUInt32Child(RuleNode, "InstancesPerCell", Rule.mInstancesPerCell);
        ReadFloatChild(RuleNode, "Density", Rule.mDensityMultiplier);
        ReadFloatChild(RuleNode, "DensityMultiplier", Rule.mDensityMultiplier);
        ReadFloatChild(RuleNode, "SpawnChance", Rule.mSpawnChance);
        ReadFloatChild(RuleNode, "MinimumWeight", Rule.mMinimumWeight);
        ReadFloatChild(RuleNode, "MinimumScale", Rule.mMinimumScale);
        ReadFloatChild(RuleNode, "MaximumScale", Rule.mMaximumScale);
        ReadFloatChild(RuleNode, "MinimumYawDegrees", Rule.mMinimumYawDegrees);
        ReadFloatChild(RuleNode, "MaximumYawDegrees", Rule.mMaximumYawDegrees);
        ReadFloatChild(RuleNode, "ClusterStrength", Rule.mClusterStrength);
        ReadFloatChild(RuleNode, "ClusterScale", Rule.mClusterScale);
        ReadFloatChild(RuleNode, "ClusterContrast", Rule.mClusterContrast);
        ReadFloatChild(RuleNode, "ClusterCoverage", Rule.mClusterCoverage);
        ReadFloatChild(RuleNode, "ClusterEdgeSoftness", Rule.mClusterEdgeSoftness);
        ReadFloatChild(RuleNode, "ClusterOutsideDensity", Rule.mClusterOutsideDensity);
        ReadFloatChild(RuleNode, "ForestStrength", Rule.mForestStrength);
        ReadFloatChild(RuleNode, "OffsetY", Rule.mOffsetY);
        ReadFloatChild(RuleNode, "MinimumSpacing", Rule.mMinimumSpacing);
        ReadFloatChild(RuleNode, "TreeMinimumSpacing", Rule.mMinimumSpacing);
        ReadBoolChild(RuleNode, "CollisionActor", Rule.mCollisionActorEnabled);
        ReadVector3Child(RuleNode, "CollisionCenter", Rule.mCollisionCenter);
        ReadVector3Child(RuleNode, "CollisionExtents", Rule.mCollisionExtents);
        ReadFloatChild(RuleNode, "CollisionFriction", Rule.mCollisionFriction);
        ReadFloatChild(RuleNode, "CollisionRestitution", Rule.mCollisionRestitution);
        Rule.mModelPath = ResolveFoliageResourcePath(ConfigPath, Rule.mModelPath);
        Rule.mMaterialPath = ResolveFoliageResourcePath(ConfigPath, Rule.mMaterialPath);
        ReadFoliageLods(RuleNode, ConfigPath, Rule);

        if (Rule.mLods.empty() == true) {
            throw std::runtime_error{ "Procedural foliage rule model path is empty." };
        }

        if (Rule.mName.empty() == true) {
            Rule.mName = Rule.mLods.front().mModelPath;
        }

        Rule.mInstancesPerCell = std::max(Rule.mInstancesPerCell, 1u);
        Rule.mDensityMultiplier = std::max(Rule.mDensityMultiplier, 0.0f);
        Rule.mSpawnChance = std::clamp(Rule.mSpawnChance, 0.0f, 1.0f);
        Rule.mMinimumWeight = std::clamp(Rule.mMinimumWeight, 0.0f, 1.0f);
        Rule.mClusterStrength = std::clamp(Rule.mClusterStrength, 0.0f, 1.0f);
        Rule.mClusterScale = std::max(Rule.mClusterScale, 1.0f);
        Rule.mClusterContrast = std::max(Rule.mClusterContrast, 0.0f);
        Rule.mClusterCoverage = std::clamp(Rule.mClusterCoverage, 0.01f, 1.0f);
        Rule.mClusterEdgeSoftness = std::max(Rule.mClusterEdgeSoftness, 0.0f);
        Rule.mClusterOutsideDensity = std::clamp(Rule.mClusterOutsideDensity, 0.0f, 1.0f);
        Rule.mForestStrength = std::clamp(Rule.mForestStrength, 0.0f, 1.0f);
        Rule.mMinimumSpacing = std::max(Rule.mMinimumSpacing, 0.0f);
        Rule.mCollisionExtents.x = std::max(Rule.mCollisionExtents.x, FoliageEpsilon);
        Rule.mCollisionExtents.y = std::max(Rule.mCollisionExtents.y, FoliageEpsilon);
        Rule.mCollisionExtents.z = std::max(Rule.mCollisionExtents.z, FoliageEpsilon);
        Rule.mCollisionFriction = std::max(Rule.mCollisionFriction, 0.0f);
        Rule.mCollisionRestitution = std::max(Rule.mCollisionRestitution, 0.0f);
        if (Rule.mMaximumScale < Rule.mMinimumScale) {
            std::swap(Rule.mMinimumScale, Rule.mMaximumScale);
        }

        return Rule;
    }

    FoliagePlacementConfig LoadFoliagePlacementConfig(const std::string& ConfigPath) {
        if (ConfigPath.empty() == true) {
            throw std::runtime_error{ "Procedural foliage config path is empty." };
        }

        std::ifstream InputStream{ ConfigPath, std::ios::in | std::ios::binary };
        if (InputStream.is_open() == false) {
            throw std::runtime_error{ "Procedural foliage config open failed." };
        }

        std::ostringstream Buffer{};
        Buffer << InputStream.rdbuf();
        const std::string YamlText{ Buffer.str() };
        if (YamlText.empty() == true) {
            throw std::runtime_error{ "Procedural foliage config is empty." };
        }

        c4::yml::Tree Tree{ c4::yml::parse_in_arena(c4::to_csubstr(YamlText)) };
        Tree.resolve();
        const c4::yml::ConstNodeRef ConfigNode{ ResolveConfigNode(Tree.rootref()) };
        FoliagePlacementConfig Config{};
        ReadBoolChild(ConfigNode, "Enabled", Config.mEnabled);
        ReadFloatChild(ConfigNode, "InactiveHeight", Config.mInactiveHeight);
        ReadFloatChild(ConfigNode, "PlacementRadius", Config.mPlacementRadius);
        ReadFloatChild(ConfigNode, "CellSize", Config.mCellSize);
        ReadFloatChild(ConfigNode, "UpdateInterval", Config.mUpdateInterval);
        ReadUInt32Child(ConfigNode, "UpdateCellBatchSize", Config.mUpdateCellBatchSize);
        ReadUInt32Child(ConfigNode, "UpdateCandidateBatchSize", Config.mUpdateCandidateBatchSize);
        ReadUInt32Child(ConfigNode, "UpdateSlotBatchSize", Config.mUpdateSlotBatchSize);
        ReadFloatChild(ConfigNode, "Density", Config.mDensityMultiplier);
        ReadFloatChild(ConfigNode, "DensityMultiplier", Config.mDensityMultiplier);
        ReadUInt32Child(ConfigNode, "CandidateRandomXStream", Config.mCandidateRandomXStream);
        ReadUInt32Child(ConfigNode, "CandidateRandomZStream", Config.mCandidateRandomZStream);
        ReadUInt32Child(ConfigNode, "CandidateRandomChanceStream", Config.mCandidateRandomChanceStream);
        ReadUInt32Child(ConfigNode, "CandidateRandomYawStream", Config.mCandidateRandomYawStream);
        ReadUInt32Child(ConfigNode, "CandidateRandomScaleStream", Config.mCandidateRandomScaleStream);
        ReadUInt32Child(ConfigNode, "ClusterCornerStream", Config.mClusterCornerStream);
        ReadFloatChild(ConfigNode, "ClusterDensityMaximum", Config.mClusterDensityMaximum);
        ReadFloatChild(ConfigNode, "ClusterScaleMinimumCellMultiplier", Config.mClusterScaleMinimumCellMultiplier);
        ReadFloatChild(ConfigNode, "ClumpGridScaleMultiplier", Config.mClumpGridScaleMultiplier);
        ReadFloatChild(ConfigNode, "ClumpGridScaleMinimumCellMultiplier", Config.mClumpGridScaleMinimumCellMultiplier);
        ReadFloatChild(ConfigNode, "ClumpCenterOffset", Config.mClumpCenterOffset);
        ReadFloatChild(ConfigNode, "ClumpCenterJitter", Config.mClumpCenterJitter);
        ReadUInt32Child(ConfigNode, "ClumpCenterXStream", Config.mClumpCenterXStream);
        ReadUInt32Child(ConfigNode, "ClumpCenterZStream", Config.mClumpCenterZStream);
        ReadUInt32Child(ConfigNode, "ClumpAngleStream", Config.mClumpAngleStream);
        ReadUInt32Child(ConfigNode, "ClumpDistanceStream", Config.mClumpDistanceStream);
        ReadFloatChild(ConfigNode, "ClumpRadiusScale", Config.mClumpRadiusScale);
        ReadFloatChild(ConfigNode, "ClumpPullStrengthScale", Config.mClumpPullStrengthScale);
        ReadFloatChild(ConfigNode, "ClumpPullStrengthMaximum", Config.mClumpPullStrengthMaximum);
        ReadBoolChild(ConfigNode, "ForestEnabled", Config.mForestEnabled);
        ReadFloatChild(ConfigNode, "ForestMinimumWidth", Config.mForestMinimumWidth);
        ReadFloatChild(ConfigNode, "ForestPatchScale", Config.mForestPatchScale);
        ReadFloatChild(ConfigNode, "ForestPatchCoverage", Config.mForestPatchCoverage);
        ReadFloatChild(ConfigNode, "ForestPatchContrast", Config.mForestPatchContrast);
        ReadFloatChild(ConfigNode, "ForestPatchEdgeSoftness", Config.mForestPatchEdgeSoftness);
        ReadFloatChild(ConfigNode, "ForestOutsideDensity", Config.mForestOutsideDensity);
        ReadUInt32Child(ConfigNode, "ForestPatchNoiseIndex", Config.mForestPatchNoiseIndex);
        ReadUInt32Child(ConfigNode, "ForestPatchCornerStream", Config.mForestPatchCornerStream);
        ReadFloatChild(ConfigNode, "ForestMinimumAreaFactor", Config.mForestMinimumAreaFactor);
        ReadFloatChild(ConfigNode, "ForestWidthSampleRadiusScale", Config.mForestWidthSampleRadiusScale);
        ReadFloatChild(ConfigNode, "ForestWidthDiagonalSampleScale", Config.mForestWidthDiagonalSampleScale);
        ReadFloatChild(ConfigNode, "ForestWidthRequiredSampleRatio", Config.mForestWidthRequiredSampleRatio);
        ReadUInt32Child(ConfigNode, "MinimumSpacingPriorityStream", Config.mMinimumSpacingPriorityStream);
        ReadUInt32Child(ConfigNode, "SeedSalt", Config.mSeedSalt);
        Config.mPlacementRadius = std::max(Config.mPlacementRadius, 1.0f);
        Config.mCellSize = std::max(Config.mCellSize, 1.0f);
        Config.mUpdateInterval = std::max(Config.mUpdateInterval, 0.0f);
        Config.mUpdateCellBatchSize = std::max(Config.mUpdateCellBatchSize, 1u);
        Config.mUpdateCandidateBatchSize = std::max(Config.mUpdateCandidateBatchSize, 1u);
        Config.mUpdateSlotBatchSize = std::max(Config.mUpdateSlotBatchSize, 1u);
        Config.mDensityMultiplier = std::max(Config.mDensityMultiplier, 0.0f);
        Config.mClusterDensityMaximum = std::max(Config.mClusterDensityMaximum, 1.0f);
        Config.mClusterScaleMinimumCellMultiplier = std::max(Config.mClusterScaleMinimumCellMultiplier, 0.0f);
        Config.mClumpGridScaleMultiplier = std::max(Config.mClumpGridScaleMultiplier, 0.0f);
        Config.mClumpGridScaleMinimumCellMultiplier = std::max(Config.mClumpGridScaleMinimumCellMultiplier, 0.0f);
        Config.mClumpCenterOffset = std::clamp(Config.mClumpCenterOffset, 0.0f, 1.0f);
        Config.mClumpCenterJitter = std::clamp(Config.mClumpCenterJitter, 0.0f, 1.0f);
        Config.mClumpRadiusScale = std::max(Config.mClumpRadiusScale, 0.0f);
        Config.mClumpPullStrengthScale = std::max(Config.mClumpPullStrengthScale, 0.0f);
        Config.mClumpPullStrengthMaximum = std::clamp(Config.mClumpPullStrengthMaximum, 0.0f, 1.0f);
        Config.mForestMinimumWidth = std::max(Config.mForestMinimumWidth, 0.0f);
        Config.mForestPatchScale = std::max(Config.mForestPatchScale, 1.0f);
        Config.mForestPatchCoverage = std::clamp(Config.mForestPatchCoverage, 0.01f, 1.0f);
        Config.mForestPatchContrast = std::max(Config.mForestPatchContrast, 0.0f);
        Config.mForestPatchEdgeSoftness = std::max(Config.mForestPatchEdgeSoftness, 0.0f);
        Config.mForestOutsideDensity = std::clamp(Config.mForestOutsideDensity, 0.0f, 1.0f);
        Config.mForestMinimumAreaFactor = std::clamp(Config.mForestMinimumAreaFactor, 0.0f, 1.0f);
        Config.mForestWidthSampleRadiusScale = std::max(Config.mForestWidthSampleRadiusScale, 0.0f);
        Config.mForestWidthDiagonalSampleScale = std::max(Config.mForestWidthDiagonalSampleScale, 0.0f);
        Config.mForestWidthRequiredSampleRatio = std::clamp(Config.mForestWidthRequiredSampleRatio, 0.0f, 1.0f);

        if (ConfigNode.has_child("Rules") == true) {
            const c4::yml::ConstNodeRef RulesNode{ ConfigNode["Rules"] };
            if (RulesNode.is_seq() == false) {
                throw std::runtime_error{ "Procedural foliage rules must be a sequence." };
            }

            for (const c4::yml::ConstNodeRef RuleNode : RulesNode.children()) {
                Config.mRules.push_back(ReadFoliageRule(RuleNode, ConfigPath));
            }
        }

        if (Config.mRules.empty() == true) {
            throw std::runtime_error{ "Procedural foliage config must have at least one rule." };
        }

        return Config;
    }

    bool TryResolveFocusPosition(Arche::World& World, SimpleMath::Vector3& OutFocusPosition) {
        for (auto [TransformComponent, CameraComponent] : World.Query<Game::Transform, Game::Camera>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            OutFocusPosition = TransformComponent.position;
            return true;
        }

        return false;
    }

    bool TryResolveTerrainSamplingContext(Arche::World& World, TerrainSamplingContext& OutContext) {
        for (auto [TransformComponent, Renderer, HierarchyComponent] : World.Query<Game::Transform, Game::TerrainRenderer, Game::EntityHierarchy>()) {
            (void)HierarchyComponent;
            if (Renderer.mResource == nullptr || Renderer.mActive == false || Renderer.mTileMetadataIndex != Game::InvalidTerrainTileMetadataIndex) {
                continue;
            }

            OutContext.mResource = Renderer.mResource;
            OutContext.mTransform = TransformComponent;
            return true;
        }

        return false;
    }

    std::uint32_t CalculateIndex(std::uint32_t Width, std::uint32_t X, std::uint32_t Z) {
        return (Z * Width) + X;
    }

    float SampleHeight01(const Terrain::HeightFieldData& Field, float GridX, float GridZ) {
        const float ClampedGridX{ std::clamp(GridX, 0.0f, static_cast<float>(Field.Width - 1u)) };
        const float ClampedGridZ{ std::clamp(GridZ, 0.0f, static_cast<float>(Field.Height - 1u)) };
        const std::uint32_t X0{ static_cast<std::uint32_t>(std::floor(ClampedGridX)) };
        const std::uint32_t Z0{ static_cast<std::uint32_t>(std::floor(ClampedGridZ)) };
        const std::uint32_t X1{ std::min(X0 + 1u, Field.Width - 1u) };
        const std::uint32_t Z1{ std::min(Z0 + 1u, Field.Height - 1u) };
        const float BlendX{ ClampedGridX - static_cast<float>(X0) };
        const float BlendZ{ ClampedGridZ - static_cast<float>(Z0) };
        const float Height00{ Field.HeightValues[CalculateIndex(Field.Width, X0, Z0)] };
        const float Height10{ Field.HeightValues[CalculateIndex(Field.Width, X1, Z0)] };
        const float Height01{ Field.HeightValues[CalculateIndex(Field.Width, X0, Z1)] };
        const float Height11{ Field.HeightValues[CalculateIndex(Field.Width, X1, Z1)] };
        const float HeightX0{ Lerp(Height00, Height10, BlendX) };
        const float HeightX1{ Lerp(Height01, Height11, BlendX) };
        return Lerp(HeightX0, HeightX1, BlendZ);
    }

    asset::Vec4 LerpSplatWeight(const asset::Vec4& Start, const asset::Vec4& End, float Alpha) {
        return asset::Vec4{ Lerp(Start.x, End.x, Alpha), Lerp(Start.y, End.y, Alpha), Lerp(Start.z, End.z, Alpha), Lerp(Start.w, End.w, Alpha) };
    }

    using SplatWeightMapValues = std::array<asset::Vec4, Terrain::SplatMapData::WeightMapCount>;

    SplatWeightMapValues SampleSplatWeight(const Terrain::SplatMapData& SplatMap, float GridX, float GridZ, const Terrain::HeightFieldData& Field) {
        const float ScaleX{ Field.Width > 1u ? static_cast<float>(SplatMap.Width - 1u) / static_cast<float>(Field.Width - 1u) : 1.0f };
        const float ScaleZ{ Field.Height > 1u ? static_cast<float>(SplatMap.Height - 1u) / static_cast<float>(Field.Height - 1u) : 1.0f };
        const float SplatGridX{ GridX * ScaleX };
        const float SplatGridZ{ GridZ * ScaleZ };
        const float ClampedGridX{ std::clamp(SplatGridX, 0.0f, static_cast<float>(SplatMap.Width - 1u)) };
        const float ClampedGridZ{ std::clamp(SplatGridZ, 0.0f, static_cast<float>(SplatMap.Height - 1u)) };
        const std::uint32_t X0{ static_cast<std::uint32_t>(std::floor(ClampedGridX)) };
        const std::uint32_t Z0{ static_cast<std::uint32_t>(std::floor(ClampedGridZ)) };
        const std::uint32_t X1{ std::min(X0 + 1u, SplatMap.Width - 1u) };
        const std::uint32_t Z1{ std::min(Z0 + 1u, SplatMap.Height - 1u) };
        const float BlendX{ ClampedGridX - static_cast<float>(X0) };
        const float BlendZ{ ClampedGridZ - static_cast<float>(Z0) };

        SplatWeightMapValues SampledWeights{};
        for (std::size_t WeightMapIndex{ 0ULL }; WeightMapIndex < Terrain::SplatMapData::WeightMapCount; ++WeightMapIndex) {
            const std::vector<asset::Vec4>& WeightMapValues{ SplatMap.WeightMapValues[WeightMapIndex] };
            const asset::Vec4 Weight00{ WeightMapValues[CalculateIndex(SplatMap.Width, X0, Z0)] };
            const asset::Vec4 Weight10{ WeightMapValues[CalculateIndex(SplatMap.Width, X1, Z0)] };
            const asset::Vec4 Weight01{ WeightMapValues[CalculateIndex(SplatMap.Width, X0, Z1)] };
            const asset::Vec4 Weight11{ WeightMapValues[CalculateIndex(SplatMap.Width, X1, Z1)] };
            const asset::Vec4 WeightX0{ LerpSplatWeight(Weight00, Weight10, BlendX) };
            const asset::Vec4 WeightX1{ LerpSplatWeight(Weight01, Weight11, BlendX) };
            SampledWeights[WeightMapIndex] = LerpSplatWeight(WeightX0, WeightX1, BlendZ);
        }

        return SampledWeights;
    }

    float GetSplatLayerWeight(const SplatWeightMapValues& WeightMapValues, std::uint32_t LayerIndex) {
        const std::size_t WeightMapIndex{ static_cast<std::size_t>(LayerIndex / 4u) };
        if (WeightMapIndex >= Terrain::SplatMapData::WeightMapCount) {
            return 0.0f;
        }

        const asset::Vec4& Weights{ WeightMapValues[WeightMapIndex] };
        const std::uint32_t ChannelIndex{ LayerIndex % 4u };
        if (ChannelIndex == 0u) {
            return Weights.x;
        }

        if (ChannelIndex == 1u) {
            return Weights.y;
        }

        if (ChannelIndex == 2u) {
            return Weights.z;
        }

        if (ChannelIndex == 3u) {
            return Weights.w;
        }

        return 0.0f;
    }

    bool TryResolveLayerIndexByName(const Terrain::TerrainBuildDesc& BuildDesc, const std::string& LayerName, std::uint32_t& OutLayerIndex) {
        const std::vector<Terrain::TerrainProceduralHeightFieldDesc::TerrainSplatMapLayerDesc>& Layers{ BuildDesc.mProceduralHeightFieldDesc.mSplatMapDesc.mLayers };
        for (std::size_t LayerIndex{ 0ULL }; LayerIndex < Layers.size(); ++LayerIndex) {
            if (Layers[LayerIndex].mName == LayerName) {
                OutLayerIndex = static_cast<std::uint32_t>(LayerIndex);
                return true;
            }
        }

        return false;
    }

    std::uint32_t ResolveLayerIndex(const FoliagePlacementRule& Rule, const Terrain::TerrainBuildDesc& BuildDesc) {
        if (Rule.mLayerName.empty() == false) {
            std::uint32_t LayerIndex{};
            const bool IsLayerIndexResolved{ TryResolveLayerIndexByName(BuildDesc, Rule.mLayerName, LayerIndex) };
            if (IsLayerIndexResolved == true) {
                return LayerIndex;
            }
        }

        return Rule.mLayerIndex;
    }

    bool TryResolveExcludedLayerWeight(const FoliagePlacementRule& Rule, const Terrain::TerrainBuildDesc& BuildDesc, const SplatWeightMapValues& SplatWeights, float& OutLayerWeight) {
        if (Rule.mExcludedLayerNames.empty() == true) {
            return false;
        }

        float ExcludedWeight{};
        std::uint32_t ResolvedLayerCount{};
        for (const std::string& LayerName : Rule.mExcludedLayerNames) {
            std::uint32_t LayerIndex{};
            const bool IsLayerIndexResolved{ TryResolveLayerIndexByName(BuildDesc, LayerName, LayerIndex) };
            if (IsLayerIndexResolved == false || LayerIndex >= Terrain::SplatMapData::LayerCount) {
                continue;
            }

            ExcludedWeight += GetSplatLayerWeight(SplatWeights, LayerIndex);
            ResolvedLayerCount += 1u;
        }

        if (ResolvedLayerCount == 0u) {
            return false;
        }

        OutLayerWeight = std::clamp(1.0f - ExcludedWeight, 0.0f, 1.0f);
        return true;
    }

    bool TrySampleTerrain(const TerrainSamplingContext& TerrainContext, const FoliagePlacementRule& Rule, float WorldX, float WorldZ, float& OutWorldY, float& OutLayerWeight) {
        if (TerrainContext.mResource == nullptr) {
            return false;
        }

        const Terrain::HeightFieldData& HeightField{ TerrainContext.mResource->GetHeightFieldData() };
        const Terrain::SplatMapData& SplatMap{ TerrainContext.mResource->GetSplatMapData() };
        if (HeightField.Width < 2u || HeightField.Height < 2u || HeightField.HeightValues.empty() == true || SplatMap.Width < 2u || SplatMap.Height < 2u || SplatMap.WeightMapValues[0].empty() == true || SplatMap.WeightMapValues[1].empty() == true) {
            return false;
        }

        const float ScaleX{ std::abs(TerrainContext.mTransform.scale.x) > FoliageEpsilon ? TerrainContext.mTransform.scale.x : 1.0f };
        const float ScaleY{ std::abs(TerrainContext.mTransform.scale.y) > FoliageEpsilon ? TerrainContext.mTransform.scale.y : 1.0f };
        const float ScaleZ{ std::abs(TerrainContext.mTransform.scale.z) > FoliageEpsilon ? TerrainContext.mTransform.scale.z : 1.0f };
        const float LocalX{ (WorldX - TerrainContext.mTransform.position.x) / ScaleX };
        const float LocalZ{ (WorldZ - TerrainContext.mTransform.position.z) / ScaleZ };
        const float GridX{ (LocalX + TerrainContext.mResource->GetOriginOffsetX()) / TerrainContext.mResource->GetCellSizeX() };
        const float GridZ{ (LocalZ + TerrainContext.mResource->GetOriginOffsetZ()) / TerrainContext.mResource->GetCellSizeZ() };
        if (GridX < 0.0f || GridZ < 0.0f || GridX > static_cast<float>(HeightField.Width - 1u) || GridZ > static_cast<float>(HeightField.Height - 1u)) {
            return false;
        }

        const Terrain::TerrainBuildDesc& BuildDesc{ TerrainContext.mResource->GetBuildDesc() };
        const float Height01{ SampleHeight01(HeightField, GridX, GridZ) };
        const SplatWeightMapValues SplatWeights{ SampleSplatWeight(SplatMap, GridX, GridZ, HeightField) };
        OutWorldY = TerrainContext.mTransform.position.y + (Height01 * TerrainContext.mResource->GetMaxHeight() * ScaleY);
        const bool HasExcludedLayerWeight{ TryResolveExcludedLayerWeight(Rule, BuildDesc, SplatWeights, OutLayerWeight) };
        if (HasExcludedLayerWeight == true) {
            return true;
        }

        const std::uint32_t LayerIndex{ ResolveLayerIndex(Rule, BuildDesc) };
        if (LayerIndex >= Terrain::SplatMapData::LayerCount) {
            return false;
        }

        OutLayerWeight = std::clamp(GetSplatLayerWeight(SplatWeights, LayerIndex), 0.0f, 1.0f);
        return true;
    }

    bool IsForestAreaWideEnough(const TerrainSamplingContext& TerrainContext, const FoliagePlacementConfig& Config, const FoliagePlacementRule& Rule, std::uint32_t TerrainSeed, float WorldX, float WorldZ) {
        if (Config.mForestEnabled == false || Config.mForestMinimumWidth <= FoliageEpsilon || Rule.mForestStrength <= FoliageEpsilon) {
            return true;
        }

        const float CenterForestFactor{ ResolveRuleForestFactor(TerrainSeed, Config, Rule, WorldX, WorldZ) };
        if (CenterForestFactor < Config.mForestMinimumAreaFactor) {
            return false;
        }

        const float SampleDistance{ Config.mForestMinimumWidth * Config.mForestWidthSampleRadiusScale };
        if (SampleDistance <= FoliageEpsilon) {
            return true;
        }

        const float DiagonalDistance{ SampleDistance * Config.mForestWidthDiagonalSampleScale };
        const std::array<SimpleMath::Vector2, 8ULL> SampleOffsets{ { SimpleMath::Vector2{ SampleDistance, 0.0f }, SimpleMath::Vector2{ -SampleDistance, 0.0f }, SimpleMath::Vector2{ 0.0f, SampleDistance }, SimpleMath::Vector2{ 0.0f, -SampleDistance }, SimpleMath::Vector2{ DiagonalDistance, DiagonalDistance }, SimpleMath::Vector2{ -DiagonalDistance, DiagonalDistance }, SimpleMath::Vector2{ DiagonalDistance, -DiagonalDistance }, SimpleMath::Vector2{ -DiagonalDistance, -DiagonalDistance } } };
        const std::uint32_t RequiredSampleCount{ static_cast<std::uint32_t>(std::ceil(static_cast<float>(SampleOffsets.size()) * Config.mForestWidthRequiredSampleRatio)) };
        std::uint32_t AcceptedSampleCount{};
        for (const SimpleMath::Vector2& SampleOffset : SampleOffsets) {
            float SampleWorldY{};
            float SampleLayerWeight{};
            const bool HasSample{ TrySampleTerrain(TerrainContext, Rule, WorldX + SampleOffset.x, WorldZ + SampleOffset.y, SampleWorldY, SampleLayerWeight) };
            const float SampleForestFactor{ ResolveRuleForestFactor(TerrainSeed, Config, Rule, WorldX + SampleOffset.x, WorldZ + SampleOffset.y) };
            if (HasSample == true && SampleLayerWeight >= Rule.mMinimumWeight && SampleForestFactor >= Config.mForestMinimumAreaFactor) {
                AcceptedSampleCount += 1u;
            }
        }

        return AcceptedSampleCount >= RequiredSampleCount;
    }

    float ResolveCandidateMinimumSpacing(const std::vector<FoliageRuntimeRule>& Rules, const FoliageCandidate& Candidate) {
        if (Candidate.mKey.mRuleIndex >= Rules.size()) {
            return 0.0f;
        }

        return Rules[Candidate.mKey.mRuleIndex].mDesc.mMinimumSpacing;
    }

    bool IsCandidateKeyInList(const std::vector<FoliageCandidateKey>& Keys, const FoliageCandidateKey& TargetKey) {
        for (const FoliageCandidateKey& Key : Keys) {
            if (Key == TargetKey) {
                return true;
            }
        }

        return false;
    }

    bool CanAcceptMinimumSpacingCandidate(const std::vector<FoliageRuntimeRule>& Rules, const std::vector<FoliageCandidate>& AcceptedCandidates, const FoliageCandidate& Candidate) {
        const float MinimumSpacing{ ResolveCandidateMinimumSpacing(Rules, Candidate) };
        if (MinimumSpacing <= FoliageEpsilon) {
            return true;
        }

        for (const FoliageCandidate& AcceptedCandidate : AcceptedCandidates) {
            const float AcceptedMinimumSpacing{ ResolveCandidateMinimumSpacing(Rules, AcceptedCandidate) };
            const float ResolvedMinimumSpacing{ std::max(MinimumSpacing, AcceptedMinimumSpacing) };
            const float DistanceX{ Candidate.mPosition.x - AcceptedCandidate.mPosition.x };
            const float DistanceZ{ Candidate.mPosition.z - AcceptedCandidate.mPosition.z };
            if (((DistanceX * DistanceX) + (DistanceZ * DistanceZ)) < (ResolvedMinimumSpacing * ResolvedMinimumSpacing)) {
                return false;
            }
        }

        return true;
    }

    void AppendHashValue(std::uint64_t& InOutHash, std::uint64_t Value) {
        InOutHash ^= Value;
        InOutHash *= 1099511628211ULL;
    }

    std::uint32_t GetFloatHashBits(float Value) {
        std::uint32_t Bits{};
        std::memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }

    std::uint64_t BuildEnvironmentCellGenerationVersion(const Game::EnvironmentObjectCell& Cell) {
        std::uint64_t Hash{ 1469598103934665603ULL };
        AppendHashValue(Hash, static_cast<std::uint32_t>(Cell.mKey.mX));
        AppendHashValue(Hash, static_cast<std::uint32_t>(Cell.mKey.mZ));
        AppendHashValue(Hash, Cell.mInstances.size());
        for (const Game::EnvironmentObjectInstance& Instance : Cell.mInstances) {
            AppendHashValue(Hash, Instance.mPrototypeIndex);
            AppendHashValue(Hash, Instance.mVariation);
            AppendHashValue(Hash, GetFloatHashBits(Instance.mPosition.x));
            AppendHashValue(Hash, GetFloatHashBits(Instance.mPosition.y));
            AppendHashValue(Hash, GetFloatHashBits(Instance.mPosition.z));
            AppendHashValue(Hash, GetFloatHashBits(Instance.mYawRadians));
            AppendHashValue(Hash, GetFloatHashBits(Instance.mScale));
        }

        return Hash == 0ULL ? 1ULL : Hash;
    }

    bool ContainsEnvironmentCellKey(std::span<const Game::EnvironmentObjectCellKey> CellKeys, const Game::EnvironmentObjectCellKey& Key) {
        return std::binary_search(CellKeys.begin(), CellKeys.end(), Key);
    }

    Game::EnvironmentObjectCellKey BuildEnvironmentCellKey(std::int32_t X, std::int32_t Z) {
        Game::EnvironmentObjectCellKey Key{};
        Key.mX = X;
        Key.mZ = Z;
        return Key;
    }

    float CalculateMaximumMinimumSpacing(const std::vector<FoliageRuntimeRule>& Rules) {
        float MaximumMinimumSpacing{};
        for (const FoliageRuntimeRule& Rule : Rules) {
            MaximumMinimumSpacing = std::max(MaximumMinimumSpacing, Rule.mDesc.mMinimumSpacing);
        }

        return MaximumMinimumSpacing;
    }

    std::int32_t CalculateMinimumSpacingCellRadius(const std::vector<FoliageRuntimeRule>& Rules, const FoliagePlacementConfig& Config) {
        const float MaximumMinimumSpacing{ CalculateMaximumMinimumSpacing(Rules) };
        if (MaximumMinimumSpacing <= FoliageEpsilon) {
            return 0;
        }

        const float CellSize{ std::max(Config.mCellSize, FoliageEpsilon) };
        return static_cast<std::int32_t>(std::ceil(MaximumMinimumSpacing / CellSize)) + 1;
    }

    bool DoesEnvironmentCellIntersectPlacementRadius(const SimpleMath::Vector3& FocusPosition, const Game::EnvironmentObjectCellKey& CellKey, float CellSize, float RadiusSquared) {
        const float MinimumX{ static_cast<float>(CellKey.mX) * CellSize };
        const float MaximumX{ MinimumX + CellSize };
        const float MinimumZ{ static_cast<float>(CellKey.mZ) * CellSize };
        const float MaximumZ{ MinimumZ + CellSize };
        const float ClosestX{ std::clamp(FocusPosition.x, MinimumX, MaximumX) };
        const float ClosestZ{ std::clamp(FocusPosition.z, MinimumZ, MaximumZ) };
        const float DistanceX{ FocusPosition.x - ClosestX };
        const float DistanceZ{ FocusPosition.z - ClosestZ };
        return ((DistanceX * DistanceX) + (DistanceZ * DistanceZ)) <= RadiusSquared;
    }

    std::vector<Game::EnvironmentObjectCellKey> BuildEnvironmentCellKeysInPlacementRadius(const SimpleMath::Vector3& FocusPosition, const FoliagePlacementConfig& Config) {
        const float Radius{ Config.mPlacementRadius };
        const float RadiusSquared{ Radius * Radius };
        const float CellSize{ std::max(Config.mCellSize, FoliageEpsilon) };
        const std::int32_t MinimumCellX{ static_cast<std::int32_t>(std::floor((FocusPosition.x - Radius) / CellSize)) };
        const std::int32_t MaximumCellX{ static_cast<std::int32_t>(std::floor((FocusPosition.x + Radius) / CellSize)) };
        const std::int32_t MinimumCellZ{ static_cast<std::int32_t>(std::floor((FocusPosition.z - Radius) / CellSize)) };
        const std::int32_t MaximumCellZ{ static_cast<std::int32_t>(std::floor((FocusPosition.z + Radius) / CellSize)) };
        std::vector<Game::EnvironmentObjectCellKey> CellKeys{};

        for (std::int32_t CellZ{ MinimumCellZ }; CellZ <= MaximumCellZ; CellZ += 1) {
            for (std::int32_t CellX{ MinimumCellX }; CellX <= MaximumCellX; CellX += 1) {
                Game::EnvironmentObjectCellKey CellKey{ BuildEnvironmentCellKey(CellX, CellZ) };
                if (DoesEnvironmentCellIntersectPlacementRadius(FocusPosition, CellKey, CellSize, RadiusSquared) == false) {
                    continue;
                }

                CellKeys.push_back(CellKey);
            }
        }

        std::sort(CellKeys.begin(), CellKeys.end());
        CellKeys.erase(std::unique(CellKeys.begin(), CellKeys.end()), CellKeys.end());
        return CellKeys;
    }

    bool IsEnvironmentObjectCellRenderable(const Game::EnvironmentObjectCell& Cell) {
        return Cell.mInstances.empty() == false && Cell.mBatchesByLodLevel.empty() == false;
    }

    constexpr std::uint32_t EnvironmentMainVisibilityMaskBit{ 1u };

    std::uint32_t BuildEnvironmentShadowVisibilityMaskBit(std::uint32_t CascadeIndex) {
        return 1u << (CascadeIndex + 1u);
    }

    struct EnvironmentMergedBatchKey final {
    public:
        const RenderContract::IPipeline* mPipeline{};
        const RenderContract::IModelNode* mMesh{};
        std::array<std::uint32_t, 16ULL> mLocalTransformBits{};
        std::uint32_t mSubMesh{};
        std::uint32_t mPass{};
        std::uint32_t mMaterialIndex{};
        std::uint32_t mFlags{};
        bool mCastsShadow{ true };
    };

    bool operator==(const EnvironmentMergedBatchKey& Left, const EnvironmentMergedBatchKey& Right) {
        return Left.mPipeline == Right.mPipeline && Left.mMesh == Right.mMesh && Left.mLocalTransformBits == Right.mLocalTransformBits && Left.mSubMesh == Right.mSubMesh && Left.mPass == Right.mPass && Left.mMaterialIndex == Right.mMaterialIndex && Left.mFlags == Right.mFlags && Left.mCastsShadow == Right.mCastsShadow;
    }

    struct EnvironmentMergedBatchKeyHasher final {
    public:
        std::size_t operator()(const EnvironmentMergedBatchKey& Key) const {
            std::uint64_t Hash{ 1469598103934665603ULL };
            AppendHashValue(Hash, reinterpret_cast<std::uintptr_t>(Key.mPipeline));
            AppendHashValue(Hash, reinterpret_cast<std::uintptr_t>(Key.mMesh));
            AppendHashValue(Hash, Key.mSubMesh);
            AppendHashValue(Hash, Key.mPass);
            AppendHashValue(Hash, Key.mMaterialIndex);
            AppendHashValue(Hash, Key.mFlags);
            AppendHashValue(Hash, Key.mCastsShadow == true ? 1u : 0u);
            for (std::uint32_t MatrixValue : Key.mLocalTransformBits) {
                AppendHashValue(Hash, MatrixValue);
            }

            return static_cast<std::size_t>(Hash);
        }
    };

    struct EnvironmentMergedInstanceRun final {
    public:
        std::vector<RenderContract::EnvironmentInstanceContext> mInstanceContexts{};
        std::uint32_t mVisibilityMask{};
    };

    struct EnvironmentMergedBatch final {
    public:
        EnvironmentMergedBatchKey mKey{};
        RenderContract::EnvironmentSegmentContext mSegmentContext{};
        RenderContract::EnvironmentDrawRecord mDrawRecord{};
        std::vector<EnvironmentMergedInstanceRun> mRuns{};
    };

    std::array<std::uint32_t, 16ULL> BuildMatrixHashBits(const SimpleMath::Matrix& MatrixValue) {
        return std::array<std::uint32_t, 16ULL>{ GetFloatHashBits(MatrixValue._11), GetFloatHashBits(MatrixValue._12), GetFloatHashBits(MatrixValue._13), GetFloatHashBits(MatrixValue._14), GetFloatHashBits(MatrixValue._21), GetFloatHashBits(MatrixValue._22), GetFloatHashBits(MatrixValue._23), GetFloatHashBits(MatrixValue._24), GetFloatHashBits(MatrixValue._31), GetFloatHashBits(MatrixValue._32), GetFloatHashBits(MatrixValue._33), GetFloatHashBits(MatrixValue._34), GetFloatHashBits(MatrixValue._41), GetFloatHashBits(MatrixValue._42), GetFloatHashBits(MatrixValue._43), GetFloatHashBits(MatrixValue._44) };
    }

    EnvironmentMergedBatchKey BuildEnvironmentMergedBatchKey(const RenderContract::EnvironmentDrawRecord& DrawRecord, const RenderContract::EnvironmentSegmentContext& SegmentContext) {
        EnvironmentMergedBatchKey Key{};
        Key.mPipeline = DrawRecord.mPipeline;
        Key.mMesh = DrawRecord.mMesh;
        Key.mLocalTransformBits = BuildMatrixHashBits(SegmentContext.mLocalTransform);
        Key.mSubMesh = DrawRecord.mSubMesh;
        Key.mPass = DrawRecord.mPass;
        Key.mMaterialIndex = DrawRecord.mMaterialIndex;
        Key.mFlags = DrawRecord.mFlags;
        Key.mCastsShadow = DrawRecord.mCastsShadow;
        return Key;
    }

    EnvironmentMergedInstanceRun& ResolveEnvironmentMergedInstanceRun(EnvironmentMergedBatch& Batch, std::uint32_t VisibilityMask) {
        for (EnvironmentMergedInstanceRun& Run : Batch.mRuns) {
            if (Run.mVisibilityMask == VisibilityMask) {
                return Run;
            }
        }

        EnvironmentMergedInstanceRun Run{};
        Run.mVisibilityMask = VisibilityMask;
        Batch.mRuns.push_back(std::move(Run));
        return Batch.mRuns.back();
    }

    EnvironmentMergedBatch& ResolveEnvironmentMergedBatch(std::vector<EnvironmentMergedBatch>& Batches, std::unordered_map<EnvironmentMergedBatchKey, std::size_t, EnvironmentMergedBatchKeyHasher>& BatchIndexByKey, const RenderContract::EnvironmentDrawRecord& DrawRecord, const RenderContract::EnvironmentSegmentContext& SegmentContext) {
        const EnvironmentMergedBatchKey Key{ BuildEnvironmentMergedBatchKey(DrawRecord, SegmentContext) };
        const std::unordered_map<EnvironmentMergedBatchKey, std::size_t, EnvironmentMergedBatchKeyHasher>::const_iterator FoundIterator{ BatchIndexByKey.find(Key) };
        if (FoundIterator != BatchIndexByKey.end()) {
            return Batches[FoundIterator->second];
        }

        EnvironmentMergedBatch Batch{};
        Batch.mKey = Key;
        Batch.mSegmentContext = SegmentContext;
        Batch.mDrawRecord = DrawRecord;
        const std::size_t BatchIndex{ Batches.size() };
        Batches.push_back(std::move(Batch));
        BatchIndexByKey.insert_or_assign(Key, BatchIndex);
        return Batches.back();
    }

    void AppendEnvironmentInstancesToMergedBatch(EnvironmentMergedBatch& Batch, const Game::EnvironmentObjectRenderPacket& Packet, const RenderContract::EnvironmentDrawRecord& DrawRecord, std::uint32_t VisibilityMask) {
        if (VisibilityMask == 0u || DrawRecord.mInstanceCount == 0u) {
            return;
        }

        const std::size_t InstanceBegin{ DrawRecord.mInstanceOffset };
        const std::size_t InstanceEnd{ InstanceBegin + DrawRecord.mInstanceCount };
        if (InstanceEnd > Packet.mInstanceContexts.size()) {
            return;
        }

        EnvironmentMergedInstanceRun& Run{ ResolveEnvironmentMergedInstanceRun(Batch, VisibilityMask) };
        Run.mInstanceContexts.insert(Run.mInstanceContexts.end(), Packet.mInstanceContexts.begin() + InstanceBegin, Packet.mInstanceContexts.begin() + InstanceEnd);
    }

    std::uint32_t ResolveEnvironmentDrawRecordPrototypeIndex(const Game::EnvironmentObjectRenderPacket& Packet, const RenderContract::EnvironmentDrawRecord& DrawRecord) {
        if (DrawRecord.mInstanceOffset >= Packet.mInstancePrototypeIndices.size()) {
            return Game::InvalidEnvironmentObjectIndex;
        }

        return Packet.mInstancePrototypeIndices[DrawRecord.mInstanceOffset];
    }

    void AppendEnvironmentPacketToMergedBatches(const Game::EnvironmentObjectRenderPacket& Packet, std::span<const std::uint32_t> PrototypeLodLevels, std::uint32_t VisibilityMask, std::vector<EnvironmentMergedBatch>& Batches, std::unordered_map<EnvironmentMergedBatchKey, std::size_t, EnvironmentMergedBatchKeyHasher>& BatchIndexByKey) {
        if (VisibilityMask == 0u || Packet.mLods.empty() == true) {
            return;
        }

        for (std::size_t LodLevel{}; LodLevel < Packet.mLods.size(); LodLevel += 1ULL) {
            const Game::EnvironmentObjectRenderPacketLod& Lod{ Packet.mLods[LodLevel] };
            for (const RenderContract::EnvironmentDrawRecord& DrawRecord : Lod.mDrawRecords) {
                if (DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u || DrawRecord.mSegmentContextIndex >= Lod.mSegmentContexts.size()) {
                    continue;
                }

                const std::uint32_t PrototypeIndex{ ResolveEnvironmentDrawRecordPrototypeIndex(Packet, DrawRecord) };
                std::uint32_t TargetLodLevel{};
                if (PrototypeIndex < PrototypeLodLevels.size()) {
                    TargetLodLevel = PrototypeLodLevels[PrototypeIndex];
                }

                if (static_cast<std::size_t>(TargetLodLevel) != LodLevel) {
                    continue;
                }

                const RenderContract::EnvironmentSegmentContext& SegmentContext{ Lod.mSegmentContexts[DrawRecord.mSegmentContextIndex] };
                EnvironmentMergedBatch& Batch{ ResolveEnvironmentMergedBatch(Batches, BatchIndexByKey, DrawRecord, SegmentContext) };
                AppendEnvironmentInstancesToMergedBatch(Batch, Packet, DrawRecord, VisibilityMask);
            }
        }
    }

    void AppendEnvironmentMergedBatchesToRenderGatherResult(const std::vector<EnvironmentMergedBatch>& Batches, RenderContract::RenderGatherResult& OutRenderGatherResult) {
        for (const EnvironmentMergedBatch& Batch : Batches) {
            const std::uint32_t SegmentContextIndex{ static_cast<std::uint32_t>(OutRenderGatherResult.GetEnvironmentSegmentContexts().size()) };
            bool HasVisibleRun{};
            for (const EnvironmentMergedInstanceRun& Run : Batch.mRuns) {
                if (Run.mInstanceContexts.empty() == false) {
                    HasVisibleRun = true;
                    break;
                }
            }

            if (HasVisibleRun == false) {
                continue;
            }

            OutRenderGatherResult.GetEnvironmentSegmentContexts().push_back(Batch.mSegmentContext);
            for (const EnvironmentMergedInstanceRun& Run : Batch.mRuns) {
                if (Run.mInstanceContexts.empty() == true) {
                    continue;
                }

                const std::uint32_t InstanceOffset{ static_cast<std::uint32_t>(OutRenderGatherResult.GetEnvironmentInstanceContexts().size()) };
                OutRenderGatherResult.GetEnvironmentInstanceContexts().insert(OutRenderGatherResult.GetEnvironmentInstanceContexts().end(), Run.mInstanceContexts.begin(), Run.mInstanceContexts.end());

                RenderContract::EnvironmentDrawRecord DrawRecord{ Batch.mDrawRecord };
                DrawRecord.mInstanceOffset = InstanceOffset;
                DrawRecord.mInstanceCount = static_cast<std::uint32_t>(Run.mInstanceContexts.size());
                DrawRecord.mSegmentContextIndex = SegmentContextIndex;

                if ((Run.mVisibilityMask & EnvironmentMainVisibilityMaskBit) != 0u) {
                    OutRenderGatherResult.GetEnvironmentDrawRecords().push_back(DrawRecord);
                }

                std::array<RenderContract::ShadowRenderContext, RenderContract::ShadowCascadeMaxCount>& ShadowRenderContexts{ OutRenderGatherResult.GetShadowRenderContexts() };
                if (DrawRecord.mCastsShadow == false) {
                    continue;
                }

                for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowRenderContexts.size(); CascadeIndex += 1u) {
                    if ((Run.mVisibilityMask & BuildEnvironmentShadowVisibilityMaskBit(CascadeIndex)) != 0u) {
                        ShadowRenderContexts[CascadeIndex].mEnvironmentDrawRecords.push_back(DrawRecord);
                    }
                }
            }
        }
    }

    bool TryResolveActiveCameraFrustum(Arche::World& World, Game::Frustum& OutFrustum) {
        for (auto [CameraComponent, FrustumComponent] : World.Query<Game::Camera, Game::Frustum>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            OutFrustum = FrustumComponent;
            return true;
        }

        return false;
    }

    bool IsEnvironmentPacketVisibleByMainFrustum(const Game::EnvironmentObjectRenderPacket& Packet, const Game::Frustum* ActiveFrustum) {
        if (ActiveFrustum == nullptr || Packet.mHasWorldBoundingBox == false) {
            return true;
        }

        return ActiveFrustum->Intersects(Packet.mWorldBoundingBox);
    }

    std::uint32_t BuildEnvironmentPacketShadowCascadeMask(const Game::EnvironmentObjectRenderPacket& Packet, const RenderContract::ShadowMappingParameter& ShadowMappingParameter) {
        const std::uint32_t ShadowCascadeCount{ RenderContract::ResolveShadowCascadeCount(ShadowMappingParameter) };
        if (Packet.mHasWorldBoundingBox == false) {
            std::uint32_t ShadowCascadeMask{};
            for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1u) {
                ShadowCascadeMask |= 1u << CascadeIndex;
            }

            return ShadowCascadeMask;
        }

        const std::array<DirectX::BoundingOrientedBox, RenderContract::ShadowCascadeMaxCount> ShadowCullingBoxes{ RenderContract::BuildShadowCullingBoxes(ShadowMappingParameter) };
        std::uint32_t ShadowCascadeMask{};
        for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1u) {
            if (ShadowCullingBoxes[CascadeIndex].Intersects(Packet.mWorldBoundingBox) == true) {
                ShadowCascadeMask |= 1u << CascadeIndex;
            }
        }

        return ShadowCascadeMask;
    }

    std::uint32_t BuildEnvironmentPacketVisibilityMask(bool IsMainVisible, std::uint32_t ShadowCascadeMask) {
        std::uint32_t VisibilityMask{};
        if (IsMainVisible == true) {
            VisibilityMask |= EnvironmentMainVisibilityMaskBit;
        }

        for (std::uint32_t CascadeIndex{}; CascadeIndex < RenderContract::ShadowCascadeMaxCount; CascadeIndex += 1u) {
            if ((ShadowCascadeMask & (1u << CascadeIndex)) != 0u) {
                VisibilityMask |= BuildEnvironmentShadowVisibilityMaskBit(CascadeIndex);
            }
        }

        return VisibilityMask;
    }

    std::uint32_t ResolveFoliageMaterialGroupIndex(Game::AssetRegistry& AssetRegistryValue, const std::string& MaterialPath, std::uint32_t FallbackIndex) {
        if (MaterialPath.empty() == true) {
            return FallbackIndex;
        }

        const bool IsLoaded{ AssetRegistryValue.LoadMaterialGroups(MaterialPath) };
        if (IsLoaded == false) {
            return FallbackIndex;
        }

        const std::uint32_t MaterialGroupIndex{ AssetRegistryValue.FindMaterialGroupIndexBySourcePath(MaterialPath) };
        if (MaterialGroupIndex == static_cast<std::uint32_t>(-1)) {
            return FallbackIndex;
        }

        return MaterialGroupIndex;
    }

    SimpleMath::Vector3 ResolveEnvironmentObjectLodAnchor(const Game::EnvironmentObjectLod& Lod) {
        const DirectX::BoundingOrientedBox& BoundingBox{ Lod.mLocalBoundingBox };
        return SimpleMath::Vector3{ BoundingBox.Center.x, BoundingBox.Center.y - BoundingBox.Extents.y, BoundingBox.Center.z };
    }

    bool AlignEnvironmentObjectPrototypeLodAnchors(Game::EnvironmentObjectPrototype& Prototype) {
        if (Prototype.mLods.size() <= 1ULL || Prototype.mLods.front().mHasLocalBoundingBox == false) {
            return false;
        }

        const SimpleMath::Vector3 ReferenceAnchor{ ResolveEnvironmentObjectLodAnchor(Prototype.mLods.front()) };
        bool IsChanged{};
        for (std::size_t LodIndex{ 1ULL }; LodIndex < Prototype.mLods.size(); LodIndex += 1ULL) {
            Game::EnvironmentObjectLod& Lod{ Prototype.mLods[LodIndex] };
            if (Lod.mHasLocalBoundingBox == false) {
                continue;
            }

            const SimpleMath::Vector3 CurrentAnchor{ ResolveEnvironmentObjectLodAnchor(Lod) };
            const SimpleMath::Vector3 AnchorOffset{ ReferenceAnchor - CurrentAnchor };
            if (std::abs(AnchorOffset.x) <= FoliageEpsilon && std::abs(AnchorOffset.y) <= FoliageEpsilon && std::abs(AnchorOffset.z) <= FoliageEpsilon) {
                continue;
            }

            const SimpleMath::Matrix AnchorTransform{ SimpleMath::Matrix::CreateTranslation(AnchorOffset) };
            for (Game::EnvironmentObjectPart& Part : Lod.mParts) {
                Part.mLocalTransform = Part.mLocalTransform * AnchorTransform;
            }

            IsChanged = true;
        }

        return IsChanged;
    }

    SimpleMath::Vector2 ResolveBillboardSizeFromReferenceLod(const Game::EnvironmentObjectPrototype& Prototype) {
        if (Prototype.mLods.empty() == true || Prototype.mLods.front().mHasLocalBoundingBox == false) {
            return SimpleMath::Vector2{};
        }

        const DirectX::BoundingOrientedBox& BoundingBox{ Prototype.mLods.front().mLocalBoundingBox };
        const float Width{ std::max(BoundingBox.Extents.x, BoundingBox.Extents.z) * 2.0f };
        const float Height{ BoundingBox.Extents.y * 2.0f };
        if (Width <= FoliageEpsilon || Height <= FoliageEpsilon) {
            return SimpleMath::Vector2{};
        }

        return SimpleMath::Vector2{ Width, Height };
    }

    constexpr const char* BillboardPrimitiveSelector{ "primitive:point" };

    std::shared_ptr<Game::Model> CreateBillboardPointModel(Game::AssetRegistry& AssetRegistryValue) {
        asset::ModelResult ModelData{};
        const bool IsModelDataCreated{ Game::PrimitiveModelFactory::TryCreateModelResult(BillboardPrimitiveSelector, ModelData) };
        if (IsModelDataCreated == false) {
            return nullptr;
        }

        asset::ModelNode* RootNode{ ModelData.GetRoot() };
        if (RootNode != nullptr) {
            DirectX::BoundingOrientedBox BoundingBox{};
            BoundingBox.Center = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
            BoundingBox.Extents = DirectX::XMFLOAT3{ 0.5f, 0.5f, 0.5f };
            BoundingBox.Orientation = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };
            RootNode->SetBoundingBox(BoundingBox);
        }

        return AssetRegistryValue.CreateRuntimeModel(ModelData);
    }

    SimpleMath::Matrix BuildBillboardLocalTransform(const SimpleMath::Vector2& Size, const SimpleMath::Matrix& SourceTransform) {
        const SimpleMath::Matrix Scale{ SimpleMath::Matrix::CreateScale(Size.x, Size.y, Size.x) };
        const SimpleMath::Matrix Translation{ SimpleMath::Matrix::CreateTranslation(SimpleMath::Vector3{ 0.0f, Size.y * 0.5f, 0.0f }) };
        return Scale * Translation * SourceTransform;
    }

    Game::EnvironmentObjectPart BuildBillboardPart(const Game::EnvironmentObjectPart& SourcePart, const std::shared_ptr<Game::Model>& ModelValue, const SimpleMath::Vector2& Size) {
        Game::EnvironmentObjectPart Part{ SourcePart };
        Part.mModel = ModelValue;
        Part.mLocalTransform = BuildBillboardLocalTransform(Size, SourcePart.mLocalTransform);
        Part.mLocalBoundingBox = DirectX::BoundingOrientedBox{};
        Part.mSegments.clear();
        Part.mCastsShadow = false;
        Part.mHasLocalBoundingBox = false;
        return Part;
    }

    bool AssignBillboardModel(Game::AssetRegistry& AssetRegistryValue, Game::EnvironmentObjectPrototype& Prototype) {
        const SimpleMath::Vector2 Size{ ResolveBillboardSizeFromReferenceLod(Prototype) };
        if (Size.x <= FoliageEpsilon || Size.y <= FoliageEpsilon) {
            return false;
        }

        std::shared_ptr<Game::Model> BillboardModel{};
        bool IsChanged{};
        for (Game::EnvironmentObjectLod& Lod : Prototype.mLods) {
            std::vector<Game::EnvironmentObjectPart> Parts{};
            Parts.reserve(Lod.mParts.size());
            bool IsLodChanged{};
            for (const Game::EnvironmentObjectPart& Part : Lod.mParts) {
                if (Part.mModel != nullptr) {
                    Parts.push_back(Part);
                    continue;
                }

                if (BillboardModel == nullptr) {
                    BillboardModel = CreateBillboardPointModel(AssetRegistryValue);
                }

                if (BillboardModel == nullptr) {
                    Parts.push_back(Part);
                    continue;
                }

                Parts.push_back(BuildBillboardPart(Part, BillboardModel, Size));
                IsLodChanged = true;
            }

            if (IsLodChanged == true) {
                Lod.mParts = std::move(Parts);
                IsChanged = true;
            }
        }

        return IsChanged;
    }

    std::vector<FoliageCandidate> FilterCandidatesByMinimumSpacing(std::vector<FoliageCandidate> Candidates, const std::vector<FoliageRuntimeRule>& Rules, const FoliagePlacementConfig& Config, std::uint32_t TerrainSeed) {
        std::vector<FoliageCandidate> SpacingCandidates{};
        for (const FoliageCandidate& Candidate : Candidates) {
            if (ResolveCandidateMinimumSpacing(Rules, Candidate) > FoliageEpsilon) {
                SpacingCandidates.push_back(Candidate);
            }
        }

        if (SpacingCandidates.empty() == true) {
            return Candidates;
        }

        std::stable_sort(SpacingCandidates.begin(), SpacingCandidates.end(), [&](const FoliageCandidate& Left, const FoliageCandidate& Right) {
            const std::uint32_t LeftPriority{ BuildCandidateHash(TerrainSeed, Config.mSeedSalt, Left.mKey, Config.mMinimumSpacingPriorityStream) };
            const std::uint32_t RightPriority{ BuildCandidateHash(TerrainSeed, Config.mSeedSalt, Right.mKey, Config.mMinimumSpacingPriorityStream) };
            return LeftPriority < RightPriority;
        });

        std::vector<FoliageCandidate> AcceptedSpacingCandidates{};
        std::vector<FoliageCandidateKey> AcceptedSpacingKeys{};
        AcceptedSpacingCandidates.reserve(SpacingCandidates.size());
        AcceptedSpacingKeys.reserve(SpacingCandidates.size());
        for (const FoliageCandidate& Candidate : SpacingCandidates) {
            const bool IsAccepted{ CanAcceptMinimumSpacingCandidate(Rules, AcceptedSpacingCandidates, Candidate) };
            if (IsAccepted == false) {
                continue;
            }

            AcceptedSpacingCandidates.push_back(Candidate);
            AcceptedSpacingKeys.push_back(Candidate.mKey);
        }

        std::vector<FoliageCandidate> FilteredCandidates{};
        FilteredCandidates.reserve(Candidates.size());
        for (const FoliageCandidate& Candidate : Candidates) {
            if (ResolveCandidateMinimumSpacing(Rules, Candidate) > FoliageEpsilon && IsCandidateKeyInList(AcceptedSpacingKeys, Candidate.mKey) == false) {
                continue;
            }

            FilteredCandidates.push_back(Candidate);
        }

        return FilteredCandidates;
    }

    std::uint32_t ResolveFoliageLodIndex(const FoliagePlacementRule& Rule, const SimpleMath::Vector3& FocusPosition, float WorldX, float WorldZ) {
        if (Rule.mLods.size() <= 1ULL) {
            return 0u;
        }

        const float DistanceX{ WorldX - FocusPosition.x };
        const float DistanceZ{ WorldZ - FocusPosition.z };
        const float Distance{ std::sqrt((DistanceX * DistanceX) + (DistanceZ * DistanceZ)) };
        for (std::size_t LodIndex{ 0ULL }; LodIndex < Rule.mLods.size(); ++LodIndex) {
            if (Distance <= Rule.mLods[LodIndex].mMaximumDistance) {
                return static_cast<std::uint32_t>(LodIndex);
            }
        }

        return static_cast<std::uint32_t>(Rule.mLods.size() - 1ULL);
    }

    Arche::EntityID CreateDerivedEntity(Arche::World& World) {
        Arche::EntityID EntityId{ World.CreateEntity() };
        EntityId.SetDerivedEntity(true);
        Game::EntityHierarchy Hierarchy{};
        Hierarchy.self = EntityId;
        World.AddComponent(EntityId, Hierarchy);
        return EntityId;
    }

    void RemoveRuntimePipelineAssignment(Game::FrameContext& Ctx, Arche::EntityID UnitEntityId) {
        if (UnitEntityId == Arche::NullEntityID) {
            return;
        }

        const std::size_t PreviousAssignmentCount{ Ctx.mRuntimePipelineAssignments.size() };
        const std::vector<Game::FramePipelineAssignment>::iterator RemoveBegin{ std::remove_if(Ctx.mRuntimePipelineAssignments.begin(), Ctx.mRuntimePipelineAssignments.end(), [UnitEntityId](const Game::FramePipelineAssignment& Assignment) { return Assignment.mUnitEntityId == UnitEntityId; }) };
        Ctx.mRuntimePipelineAssignments.erase(RemoveBegin, Ctx.mRuntimePipelineAssignments.end());
        if (Ctx.mRuntimePipelineAssignments.size() != PreviousAssignmentCount) {
            Ctx.mRuntimePipelineAssignmentVersion += 1ULL;
        }
    }

    DirectX::BoundingOrientedBox BuildFoliageCollisionLocalBoundingBox(const FoliagePlacementRule& Rule) {
        DirectX::BoundingOrientedBox BoundingBox{};
        BoundingBox.Center = DirectX::XMFLOAT3{ Rule.mCollisionCenter.x, Rule.mCollisionCenter.y, Rule.mCollisionCenter.z };
        BoundingBox.Extents = DirectX::XMFLOAT3{ Rule.mCollisionExtents.x, Rule.mCollisionExtents.y, Rule.mCollisionExtents.z };
        BoundingBox.Orientation = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };
        return BoundingBox;
    }

    PhysicsActorBase::ActorDesc BuildFoliageCollisionActorDesc(const FoliagePlacementConfig& Config, const FoliagePlacementRule& Rule) {
        PhysicsActorBase::ActorDesc Desc{};
        Desc.Name = Rule.mName.empty() == true ? "FoliageStaticActor" : Rule.mName + std::string{ "CollisionActor" };
        Desc.IsActive = false;
        Desc.Mass = 0.0f;
        Desc.ActorType = PhysicsActorBase::PhysicsActorType::Static;
        Desc.LocalBoundingBox = BuildFoliageCollisionLocalBoundingBox(Rule);
        Desc.Position = SimpleMath::Vector3{ 0.0f, Config.mInactiveHeight, 0.0f };
        Desc.Rotation = SimpleMath::Vector3{};
        Desc.Scale = SimpleMath::Vector3{ 1.0f, 1.0f, 1.0f };
        Desc.Friction = Rule.mCollisionFriction;
        Desc.Restitution = Rule.mCollisionRestitution;
        return Desc;
    }

    void EnqueueFoliageCollisionActorRuntimeState(PhysicsRuntime* PhysicsRuntimeResource, bool IsPhysicsRuntimeModeEnabled, std::uint32_t PhysicsWorldVersion, FoliageSlot& Slot, const Game::Transform& TransformComponent, bool IsActive) {
        if (IsPhysicsRuntimeModeEnabled == false || PhysicsRuntimeResource == nullptr || PhysicsRuntimeResource->IsRunning() == false || Slot.mCollisionActorPointer == nullptr) {
            return;
        }

        const bool IsRuntimeStateChanged{ Slot.mHasRuntimeState == false || Slot.mRuntimePhysicsWorldVersion != PhysicsWorldVersion || Slot.mRuntimePosition != TransformComponent.position || Slot.mRuntimeRotationEuler != TransformComponent.rotationEuler || Slot.mRuntimeScale != TransformComponent.scale || Slot.mRuntimeActive != IsActive };
        if (IsRuntimeStateChanged == false) {
            return;
        }

        PhysicsStaticActor::ActorDesc Desc{};
        Desc.Name = Slot.mCollisionActorPointer->GetName();
        Desc.IsActive = IsActive;
        Desc.Mass = 0.0f;
        Desc.Flags = Slot.mCollisionActorPointer->GetFlags();
        Desc.ActorType = PhysicsActorBase::PhysicsActorType::Static;
        Desc.LocalBoundingBox = Slot.mCollisionActorPointer->GetLocalBoundingBox();
        Desc.Position = TransformComponent.position;
        Desc.Rotation = TransformComponent.rotationEuler;
        Desc.Scale = TransformComponent.scale;
        Desc.Friction = Slot.mCollisionActorPointer->GetFriction();
        Desc.Restitution = Slot.mCollisionActorPointer->GetRestitution();

        PhysicsCommand Command{};
        Command.mType = PhysicsCommandType::AddStaticActor;
        Command.mActorId = static_cast<ActorId>(Slot.mCollisionActorIndex);
        Command.mStaticActorDesc = Desc;
        const bool IsCommandEnqueued{ PhysicsRuntimeResource->EnqueueCommand(Command) };
        if (IsCommandEnqueued == false) {
            return;
        }

        Slot.mRuntimePosition = TransformComponent.position;
        Slot.mRuntimeRotationEuler = TransformComponent.rotationEuler;
        Slot.mRuntimeScale = TransformComponent.scale;
        Slot.mRuntimePhysicsWorldVersion = PhysicsWorldVersion;
        Slot.mRuntimeActive = IsActive;
        Slot.mHasRuntimeState = true;
    }

    bool CreateFoliageCollisionActor(Arche::World& World, IPhysicsWorld* PhysicsWorldResource, const FoliagePlacementConfig& Config, const FoliageRuntimeRule& Rule, Arche::EntityID RootEntityId, FoliageSlot& OutSlot) {
        if (Rule.mDesc.mCollisionActorEnabled == false) {
            return true;
        }

        if (RootEntityId == Arche::NullEntityID) {
            return false;
        }

        if (PhysicsWorldResource == nullptr) {
            return true;
        }

        const PhysicsActorBase::ActorDesc Desc{ BuildFoliageCollisionActorDesc(Config, Rule.mDesc) };
        const std::uint32_t ActorIndex{ static_cast<std::uint32_t>(PhysicsWorldResource->GetActorCount()) };
        std::unique_ptr<PhysicsActorBase> NewActor{ std::make_unique<PhysicsStaticActor>(Desc) };
        PhysicsActorBase* ActorPointer{ NewActor.get() };
        PhysicsWorldResource->AddActor(std::move(NewActor));

        Game::PhysicsActor PhysicsActorComponent{};
        PhysicsActorComponent.mActorPointer = ActorPointer;
        PhysicsActorComponent.mActorIndex = ActorIndex;
        PhysicsActorComponent.mActorId = ActorIndex;
        PhysicsActorComponent.mActorType = PhysicsActorBase::PhysicsActorType::Static;
        Game::UpdatePhysicsActorCachedSnapshot(PhysicsActorComponent, ActorPointer->GetPosition(), ActorPointer->GetOrientation(), ActorPointer->GetScale(), ActorPointer->GetVelocity(), ActorPointer->GetWorldBoundingBox());
        World.AddComponent(RootEntityId, PhysicsActorComponent);

        Game::BoundingBox BoundingBoxComponent{};
        BoundingBoxComponent.SetObb(Desc.LocalBoundingBox);
        World.AddComponent(RootEntityId, BoundingBoxComponent);

        OutSlot.mCollisionActorPointer = ActorPointer;
        OutSlot.mCollisionActorIndex = ActorIndex;
        return true;
    }

    bool CreateFoliageSlotEntities(Arche::World& World, IPhysicsWorld* PhysicsWorldResource, const FoliagePlacementConfig& Config, const FoliageRuntimeRule& Rule, std::uint32_t RuleIndex, FoliageSlot& OutSlot) {
        if (Rule.mDesc.mCollisionActorEnabled == false) {
            return false;
        }

        const Arche::EntityID RootEntityId{ CreateDerivedEntity(World) };
        Game::Transform RootTransform{};
        RootTransform.position = SimpleMath::Vector3{ 0.0f, Config.mInactiveHeight, 0.0f };
        World.AddComponent(RootEntityId, RootTransform);

        OutSlot.mRootEntityId = RootEntityId;
        OutSlot.mInactiveHeight = Config.mInactiveHeight;
        OutSlot.mRuleIndex = RuleIndex;
        const bool IsCollisionActorCreated{ CreateFoliageCollisionActor(World, PhysicsWorldResource, Config, Rule, RootEntityId, OutSlot) };
        if (IsCollisionActorCreated == false) {
            return false;
        }

        return OutSlot.mRootEntityId != Arche::NullEntityID;
    }

    void SetFoliageSlotActive(FoliageSlot& Slot, bool IsActive, std::uint32_t LodIndex) {
        Slot.mActive = IsActive;
        Slot.mActiveLodIndex = IsActive == true ? LodIndex : std::numeric_limits<std::uint32_t>::max();
    }

    void ApplyFoliageCollisionActorToSlot(Arche::World& World, FoliageSlot& Slot, PhysicsRuntime* PhysicsRuntimeResource, bool IsPhysicsRuntimeModeEnabled, std::uint32_t PhysicsWorldVersion, const Game::Transform& TransformComponent, bool IsActive) {
        if (Slot.mCollisionActorPointer == nullptr) {
            return;
        }

        const bool IsCollisionActorStateChanged{ Slot.mCollisionActorPointer->GetPosition() != TransformComponent.position || Slot.mCollisionActorPointer->GetOrientation() != TransformComponent.rotation || Slot.mCollisionActorPointer->GetScale() != TransformComponent.scale || Slot.mCollisionActorPointer->GetIsActive() != IsActive };
        if (IsCollisionActorStateChanged == true) {
            Slot.mCollisionActorPointer->SetPosition(TransformComponent.position);
            Slot.mCollisionActorPointer->SetOrientation(TransformComponent.rotation);
            Slot.mCollisionActorPointer->SetScale(TransformComponent.scale);
            Slot.mCollisionActorPointer->SetIsActive(IsActive);

            Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(Slot.mRootEntityId) };
            if (BoundingBoxComponent != nullptr) {
                BoundingBoxComponent->SetWorldObb(Slot.mCollisionActorPointer->GetWorldBoundingBox());
            }
        }

        EnqueueFoliageCollisionActorRuntimeState(PhysicsRuntimeResource, IsPhysicsRuntimeModeEnabled, PhysicsWorldVersion, Slot, TransformComponent, IsActive);
    }

    void ApplyFoliageCandidateToSlot(Arche::World& World, Game::FrameContext& Ctx, FoliageSlot& Slot, const FoliageCandidate& Candidate) {
        if (Slot.mActive == true && Slot.mKey == Candidate.mKey) {
            Slot.mAssignedThisFrame = true;
            Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(Slot.mRootEntityId) };
            if (TransformComponent != nullptr) {
                ApplyFoliageCollisionActorToSlot(World, Slot, Ctx.PhysicsRuntimeResource, Ctx.IsPhysicsRuntimeModeEnabled, Ctx.mPhysicsWorldVersion, *TransformComponent, true);
            }
            else {
                RemoveRuntimePipelineAssignment(Ctx, Slot.mRootEntityId);
                return;
            }

            if (Slot.mActiveLodIndex != Candidate.mLodIndex) {
                SetFoliageSlotActive(Slot, true, Candidate.mLodIndex);
            }

            return;
        }

        Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(Slot.mRootEntityId) };
        if (TransformComponent == nullptr) {
            RemoveRuntimePipelineAssignment(Ctx, Slot.mRootEntityId);
            return;
        }

        TransformComponent->position = Candidate.mPosition;
        TransformComponent->rotationEuler = SimpleMath::Vector3{ 0.0f, Candidate.mYawRadians, 0.0f };
        TransformComponent->UpdateRotationFromEulerRadians();
        TransformComponent->scale = SimpleMath::Vector3{ Candidate.mScale, Candidate.mScale, Candidate.mScale };
        ApplyFoliageCollisionActorToSlot(World, Slot, Ctx.PhysicsRuntimeResource, Ctx.IsPhysicsRuntimeModeEnabled, Ctx.mPhysicsWorldVersion, *TransformComponent, true);
        Slot.mKey = Candidate.mKey;
        Slot.mAssignedThisFrame = true;
        SetFoliageSlotActive(Slot, true, Candidate.mLodIndex);
    }

    void DeactivateFoliageSlot(Arche::World& World, Game::FrameContext& Ctx, FoliageSlot& Slot) {
        if (Slot.mActive == false) {
            RemoveRuntimePipelineAssignment(Ctx, Slot.mRootEntityId);
            return;
        }

        Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(Slot.mRootEntityId) };
        if (TransformComponent != nullptr) {
            TransformComponent->position.y = Slot.mInactiveHeight;
            ApplyFoliageCollisionActorToSlot(World, Slot, Ctx.PhysicsRuntimeResource, Ctx.IsPhysicsRuntimeModeEnabled, Ctx.mPhysicsWorldVersion, *TransformComponent, false);
        }
        else if (Slot.mCollisionActorPointer != nullptr) {
            Slot.mCollisionActorPointer->SetIsActive(false);
            if (Ctx.IsPhysicsRuntimeModeEnabled == true && Ctx.PhysicsRuntimeResource != nullptr && Ctx.PhysicsRuntimeResource->IsRunning() == true) {
                PhysicsCommand Command{};
                Command.mType = PhysicsCommandType::SetActorActive;
                Command.mActorId = static_cast<ActorId>(Slot.mCollisionActorIndex);
                Command.mIsActive = false;
                static_cast<void>(Ctx.PhysicsRuntimeResource->EnqueueCommand(Command));
            }
        }

        SetFoliageSlotActive(Slot, false, 0u);
        RemoveRuntimePipelineAssignment(Ctx, Slot.mRootEntityId);
    }
}

namespace Game {
    class EnvironmentFoliageRuntime::Impl final {
    public:
        explicit Impl(std::string ConfigPath);
        ~Impl();
        Impl(const Impl& Other);
        Impl& operator=(const Impl& Other);
        Impl(Impl&& Other) noexcept;
        Impl& operator=(Impl&& Other) noexcept;

    public:
        void Update(Arche::World& World, FrameContext& Ctx, float Dt);

    private:
        bool Initialize(FrameContext& Ctx);
        void BeginFoliageUpdate(FrameContext& Ctx, const SimpleMath::Vector3& FocusPosition);
        void ProcessFoliageUpdateBatch(Arche::World& World, FrameContext& Ctx, const TerrainSamplingContext& TerrainContext);
        void BuildCandidateBatch(FrameContext& Ctx, const TerrainSamplingContext& TerrainContext);
        void ApplyCandidateBatch(Arche::World& World, FrameContext& Ctx);
        void RebuildSlotLookupBatch(Arche::World& World, FrameContext& Ctx);
        void BuildEnvironmentPrototypes(AssetRegistry& AssetRegistryValue, const std::vector<RegisteredMaterialGroup>& MaterialGroups);
        EnvironmentObjectCell BuildEnvironmentObjectCell(const EnvironmentObjectCellKey& CellKey, std::span<const FoliageCandidate> Candidates, std::uint64_t FrameIndex) const;
        GeneratedFoliageCell GenerateEnvironmentObjectCell(const TerrainSamplingContext& TerrainContext, const EnvironmentObjectCellKey& CellKey, std::uint64_t FrameIndex) const;
        void RemoveUnloadedEnvironmentObjectCells(FrameContext& Ctx);
        void RefreshVisibleEnvironmentObjectCells(FrameContext& Ctx);
        void BuildLoadedCollisionCandidates();
        void AppendEnvironmentRenderData(Arche::World& World, FrameContext& Ctx);
        std::vector<std::uint32_t> ResolveEnvironmentPrototypeLodLevels(const EnvironmentObjectRenderPacket& Packet) const;
        bool TryCreateCandidate(const TerrainSamplingContext& TerrainContext, std::uint32_t RuleIndex, std::int32_t CellX, std::int32_t CellZ, std::uint32_t InstanceIndex, FoliageCandidate& OutCandidate) const;
        std::size_t FindReusableSlot(std::uint32_t RuleIndex) const;
        bool CreateSlot(Arche::World& World, FrameContext& Ctx, std::uint32_t RuleIndex, std::size_t& OutSlotIndex);

    private:
        std::string mConfigPath{};
        FoliagePlacementConfig mConfig{};
        std::vector<FoliageRuntimeRule> mRules{};
        std::vector<EnvironmentObjectPrototype> mEnvironmentPrototypes{};
        EnvironmentObjectRenderContext mEnvironmentRenderContext{};
        std::vector<EnvironmentObjectCellKey> mVisibleEnvironmentCellKeys{};
        std::vector<FoliageSlot> mSlots{};
        std::unordered_map<FoliageCandidateKey, std::size_t, FoliageCandidateKeyHasher> mSlotByKey{};
        std::unordered_map<EnvironmentObjectCellKey, GeneratedFoliageCell, EnvironmentObjectCellKeyHasher> mGeneratedEnvironmentCells{};
        std::vector<EnvironmentObjectCellKey> mDesiredEnvironmentCellKeys{};
        std::vector<EnvironmentObjectCellKey> mPendingEnvironmentCellKeys{};
        std::vector<FoliageCandidate> mUpdateCandidates{};
        std::unordered_map<FoliageCandidateKey, std::size_t, FoliageCandidateKeyHasher> mUpdateSlotByKey{};
        SimpleMath::Vector3 mCurrentFocusPosition{ SimpleMath::Vector3::Zero };
        FoliageUpdatePhase mUpdatePhase{ FoliageUpdatePhase::Idle };
        std::size_t mUpdateCellKeyIndex{};
        std::size_t mUpdateCandidateIndex{};
        std::size_t mUpdateSlotIndex{};
        bool mInitialized{ false };
        bool mValid{ false };
        bool mHasUpdatedOnce{ false };
        float mUpdateTimer{ 0.0f };
    };

    EnvironmentFoliageRuntime::Impl::Impl(std::string ConfigPath)
        : mConfigPath{ std::move(ConfigPath) },
        mConfig{},
        mRules{},
        mEnvironmentPrototypes{},
        mEnvironmentRenderContext{},
        mVisibleEnvironmentCellKeys{},
        mSlots{},
        mSlotByKey{},
        mGeneratedEnvironmentCells{},
        mDesiredEnvironmentCellKeys{},
        mPendingEnvironmentCellKeys{},
        mUpdateCandidates{},
        mUpdateSlotByKey{},
        mCurrentFocusPosition{ SimpleMath::Vector3::Zero },
        mUpdatePhase{ FoliageUpdatePhase::Idle },
        mUpdateCellKeyIndex{},
        mUpdateCandidateIndex{},
        mUpdateSlotIndex{},
        mInitialized{ false },
        mValid{ false },
        mHasUpdatedOnce{ false },
        mUpdateTimer{ 0.0f } {
    }

    EnvironmentFoliageRuntime::Impl::~Impl() {
    }

    EnvironmentFoliageRuntime::Impl::Impl(const Impl& Other)
        : mConfigPath{ Other.mConfigPath },
        mConfig{ Other.mConfig },
        mRules{},
        mEnvironmentPrototypes{},
        mEnvironmentRenderContext{},
        mVisibleEnvironmentCellKeys{},
        mSlots{},
        mSlotByKey{},
        mGeneratedEnvironmentCells{},
        mDesiredEnvironmentCellKeys{},
        mPendingEnvironmentCellKeys{},
        mUpdateCandidates{},
        mUpdateSlotByKey{},
        mCurrentFocusPosition{ SimpleMath::Vector3::Zero },
        mUpdatePhase{ FoliageUpdatePhase::Idle },
        mUpdateCellKeyIndex{},
        mUpdateCandidateIndex{},
        mUpdateSlotIndex{},
        mInitialized{ false },
        mValid{ false },
        mHasUpdatedOnce{ false },
        mUpdateTimer{ 0.0f } {
    }

    EnvironmentFoliageRuntime::Impl& EnvironmentFoliageRuntime::Impl::operator=(const Impl& Other) {
        if (this == &Other) {
            return *this;
        }

        mConfigPath = Other.mConfigPath;
        mConfig = Other.mConfig;
        mRules.clear();
        mEnvironmentPrototypes.clear();
        mEnvironmentRenderContext.Clear();
        mVisibleEnvironmentCellKeys.clear();
        mSlots.clear();
        mSlotByKey.clear();
        mGeneratedEnvironmentCells.clear();
        mDesiredEnvironmentCellKeys.clear();
        mPendingEnvironmentCellKeys.clear();
        mUpdateCandidates.clear();
        mUpdateSlotByKey.clear();
        mCurrentFocusPosition = SimpleMath::Vector3::Zero;
        mUpdatePhase = FoliageUpdatePhase::Idle;
        mUpdateCellKeyIndex = 0ULL;
        mUpdateCandidateIndex = 0ULL;
        mUpdateSlotIndex = 0ULL;
        mInitialized = false;
        mValid = false;
        mHasUpdatedOnce = false;
        mUpdateTimer = 0.0f;
        return *this;
    }

    EnvironmentFoliageRuntime::Impl::Impl(Impl&& Other) noexcept
        : mConfigPath{ std::move(Other.mConfigPath) },
        mConfig{ std::move(Other.mConfig) },
        mRules{ std::move(Other.mRules) },
        mEnvironmentPrototypes{ std::move(Other.mEnvironmentPrototypes) },
        mEnvironmentRenderContext{ std::move(Other.mEnvironmentRenderContext) },
        mVisibleEnvironmentCellKeys{ std::move(Other.mVisibleEnvironmentCellKeys) },
        mSlots{ std::move(Other.mSlots) },
        mSlotByKey{ std::move(Other.mSlotByKey) },
        mGeneratedEnvironmentCells{ std::move(Other.mGeneratedEnvironmentCells) },
        mDesiredEnvironmentCellKeys{ std::move(Other.mDesiredEnvironmentCellKeys) },
        mPendingEnvironmentCellKeys{ std::move(Other.mPendingEnvironmentCellKeys) },
        mUpdateCandidates{ std::move(Other.mUpdateCandidates) },
        mUpdateSlotByKey{ std::move(Other.mUpdateSlotByKey) },
        mCurrentFocusPosition{ Other.mCurrentFocusPosition },
        mUpdatePhase{ Other.mUpdatePhase },
        mUpdateCellKeyIndex{ Other.mUpdateCellKeyIndex },
        mUpdateCandidateIndex{ Other.mUpdateCandidateIndex },
        mUpdateSlotIndex{ Other.mUpdateSlotIndex },
        mInitialized{ Other.mInitialized },
        mValid{ Other.mValid },
        mHasUpdatedOnce{ Other.mHasUpdatedOnce },
        mUpdateTimer{ Other.mUpdateTimer } {
        Other.mUpdatePhase = FoliageUpdatePhase::Idle;
        Other.mCurrentFocusPosition = SimpleMath::Vector3::Zero;
        Other.mUpdateCellKeyIndex = 0ULL;
        Other.mUpdateCandidateIndex = 0ULL;
        Other.mUpdateSlotIndex = 0ULL;
        Other.mInitialized = false;
        Other.mValid = false;
        Other.mHasUpdatedOnce = false;
        Other.mUpdateTimer = 0.0f;
    }

    EnvironmentFoliageRuntime::Impl& EnvironmentFoliageRuntime::Impl::operator=(Impl&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mConfigPath = std::move(Other.mConfigPath);
        mConfig = std::move(Other.mConfig);
        mRules = std::move(Other.mRules);
        mEnvironmentPrototypes = std::move(Other.mEnvironmentPrototypes);
        mEnvironmentRenderContext = std::move(Other.mEnvironmentRenderContext);
        mVisibleEnvironmentCellKeys = std::move(Other.mVisibleEnvironmentCellKeys);
        mSlots = std::move(Other.mSlots);
        mSlotByKey = std::move(Other.mSlotByKey);
        mGeneratedEnvironmentCells = std::move(Other.mGeneratedEnvironmentCells);
        mDesiredEnvironmentCellKeys = std::move(Other.mDesiredEnvironmentCellKeys);
        mPendingEnvironmentCellKeys = std::move(Other.mPendingEnvironmentCellKeys);
        mUpdateCandidates = std::move(Other.mUpdateCandidates);
        mUpdateSlotByKey = std::move(Other.mUpdateSlotByKey);
        mCurrentFocusPosition = Other.mCurrentFocusPosition;
        mUpdatePhase = Other.mUpdatePhase;
        mUpdateCellKeyIndex = Other.mUpdateCellKeyIndex;
        mUpdateCandidateIndex = Other.mUpdateCandidateIndex;
        mUpdateSlotIndex = Other.mUpdateSlotIndex;
        mInitialized = Other.mInitialized;
        mValid = Other.mValid;
        mHasUpdatedOnce = Other.mHasUpdatedOnce;
        mUpdateTimer = Other.mUpdateTimer;
        Other.mUpdatePhase = FoliageUpdatePhase::Idle;
        Other.mCurrentFocusPosition = SimpleMath::Vector3::Zero;
        Other.mUpdateCellKeyIndex = 0ULL;
        Other.mUpdateCandidateIndex = 0ULL;
        Other.mUpdateSlotIndex = 0ULL;
        Other.mInitialized = false;
        Other.mValid = false;
        Other.mHasUpdatedOnce = false;
        Other.mUpdateTimer = 0.0f;
        return *this;
    }

    bool EnvironmentFoliageRuntime::Impl::Initialize(FrameContext& Ctx) {
        if (mInitialized == true) {
            return mValid;
        }

        mInitialized = true;
        try {
            mConfig = LoadFoliagePlacementConfig(mConfigPath);
        }
        catch (const std::exception& Exception) {
            ErrorHandler::report("EnvironmentFoliageRuntime", Exception.what(), ErrorHandler::Level::Warning);
            mValid = false;
            return false;
        }

        if (Ctx.AssetRegistryResource == nullptr || mConfig.mEnabled == false) {
            mValid = false;
            return false;
        }

        mRules.clear();
        mRules.reserve(mConfig.mRules.size());
        for (const FoliagePlacementRule& RuleDesc : mConfig.mRules) {
            FoliageRuntimeRule RuntimeRule{};
            RuntimeRule.mDesc = RuleDesc;
            RuntimeRule.mMaterialGroupIndex = ResolveFoliageMaterialGroupIndex(*Ctx.AssetRegistryResource, RuleDesc.mMaterialPath, 0u);
            RuntimeRule.mLods.reserve(RuleDesc.mLods.size());
            for (const FoliagePlacementLodDesc& LodDesc : RuleDesc.mLods) {
                FoliageRuntimeLod RuntimeLod{};
                RuntimeLod.mEnabled = LodDesc.mEnabled;
                RuntimeLod.mMaximumDistance = LodDesc.mMaximumDistance;
                RuntimeLod.mMaterialGroupIndex = ResolveFoliageMaterialGroupIndex(*Ctx.AssetRegistryResource, LodDesc.mMaterialPath, RuntimeRule.mMaterialGroupIndex);

                if (RuntimeLod.mEnabled == false) {
                    RuntimeRule.mLods.push_back(std::move(RuntimeLod));
                    continue;
                }

                if (LodDesc.mModelPath.empty() == false) {
                    RuntimeLod.mModel = Ctx.AssetRegistryResource->GetModel(LodDesc.mModelPath);
                    if (RuntimeLod.mModel == nullptr) {
                        continue;
                    }
                }

                if (RuntimeLod.mModel == nullptr && LodDesc.mMaterialPath.empty() == true && RuleDesc.mMaterialPath.empty() == true) {
                    continue;
                }

                RuntimeRule.mLods.push_back(std::move(RuntimeLod));
            }

            if (RuntimeRule.mLods.empty() == true) {
                continue;
            }

            mRules.push_back(std::move(RuntimeRule));
        }

        if (Ctx.MaterialGroups != nullptr) {
            BuildEnvironmentPrototypes(*Ctx.AssetRegistryResource, *Ctx.MaterialGroups);
        }

        mEnvironmentRenderContext.Clear();
        mGeneratedEnvironmentCells.clear();
        mDesiredEnvironmentCellKeys.clear();
        mPendingEnvironmentCellKeys.clear();
        mVisibleEnvironmentCellKeys.clear();
        mValid = mRules.empty() == false && mEnvironmentPrototypes.empty() == false;
        return mValid;
    }

    void EnvironmentFoliageRuntime::Impl::BuildEnvironmentPrototypes(AssetRegistry& AssetRegistryValue, const std::vector<RegisteredMaterialGroup>& MaterialGroups) {
        mEnvironmentPrototypes.clear();
        mEnvironmentPrototypes.reserve(mRules.size());
        for (const FoliageRuntimeRule& Rule : mRules) {
            EnvironmentObjectPrototype Prototype{};
            Prototype.mName = Rule.mDesc.mName;
            Prototype.mLods.reserve(Rule.mLods.size());
            for (const FoliageRuntimeLod& Lod : Rule.mLods) {
                EnvironmentObjectLod EnvironmentLod{};
                EnvironmentLod.mMaximumDistance = Lod.mMaximumDistance;
                if (Lod.mEnabled == false) {
                    Prototype.mLods.push_back(std::move(EnvironmentLod));
                    continue;
                }

                EnvironmentObjectPart Part{};
                Part.mModel = Lod.mModel;
                Part.mMaterialGroupIndex = Lod.mMaterialGroupIndex;
                if (Lod.mModel == nullptr) {
                    Part.mCastsShadow = false;
                }

                EnvironmentLod.mParts.push_back(std::move(Part));
                Prototype.mLods.push_back(std::move(EnvironmentLod));
            }

            RebuildEnvironmentObjectPrototypeRenderData(Prototype, MaterialGroups);
            const bool IsBillboardModelAssigned{ AssignBillboardModel(AssetRegistryValue, Prototype) };
            if (IsBillboardModelAssigned == true) {
                RebuildEnvironmentObjectPrototypeRenderData(Prototype, MaterialGroups);
            }

            const bool IsLodAnchorAdjusted{ AlignEnvironmentObjectPrototypeLodAnchors(Prototype) };
            if (IsLodAnchorAdjusted == true) {
                RebuildEnvironmentObjectPrototypeRenderData(Prototype, MaterialGroups);
            }

            mEnvironmentPrototypes.push_back(std::move(Prototype));
        }
    }

    EnvironmentObjectCell EnvironmentFoliageRuntime::Impl::BuildEnvironmentObjectCell(const EnvironmentObjectCellKey& CellKey, std::span<const FoliageCandidate> Candidates, std::uint64_t FrameIndex) const {
        EnvironmentObjectCell Cell{};
        Cell.mKey = CellKey;
        Cell.mState = EnvironmentObjectCellState::Generated;
        Cell.mLastTouchedFrame = FrameIndex;

        for (const FoliageCandidate& Candidate : Candidates) {
            if (Candidate.mKey.mRuleIndex >= mEnvironmentPrototypes.size()) {
                continue;
            }

            if (Candidate.mKey.mCellX != CellKey.mX || Candidate.mKey.mCellZ != CellKey.mZ) {
                continue;
            }

            EnvironmentObjectInstance Instance{};
            Instance.mPosition = Candidate.mPosition;
            Instance.mYawRadians = Candidate.mYawRadians;
            Instance.mScale = Candidate.mScale;
            Instance.mPrototypeIndex = Candidate.mKey.mRuleIndex;
            Instance.mVariation = Candidate.mKey.mInstanceIndex;
            Cell.mInstances.push_back(Instance);
        }

        Cell.mGenerationVersion = BuildEnvironmentCellGenerationVersion(Cell);
        RebuildEnvironmentObjectCellBatches(Cell, mEnvironmentPrototypes);
        return Cell;
    }

    GeneratedFoliageCell EnvironmentFoliageRuntime::Impl::GenerateEnvironmentObjectCell(const TerrainSamplingContext& TerrainContext, const EnvironmentObjectCellKey& CellKey, std::uint64_t FrameIndex) const {
        std::vector<FoliageCandidate> WindowCandidates{};
        const std::int32_t SpacingCellRadius{ CalculateMinimumSpacingCellRadius(mRules, mConfig) };
        const std::int32_t MinimumCellX{ CellKey.mX - SpacingCellRadius };
        const std::int32_t MaximumCellX{ CellKey.mX + SpacingCellRadius };
        const std::int32_t MinimumCellZ{ CellKey.mZ - SpacingCellRadius };
        const std::int32_t MaximumCellZ{ CellKey.mZ + SpacingCellRadius };

        for (std::int32_t CellZ{ MinimumCellZ }; CellZ <= MaximumCellZ; CellZ += 1) {
            for (std::int32_t CellX{ MinimumCellX }; CellX <= MaximumCellX; CellX += 1) {
                for (std::uint32_t RuleIndex{ 0u }; RuleIndex < mRules.size(); ++RuleIndex) {
                    const FoliagePlacementRule& Rule{ mRules[RuleIndex].mDesc };
                    for (std::uint32_t InstanceIndex{ 0u }; InstanceIndex < Rule.mInstancesPerCell; ++InstanceIndex) {
                        FoliageCandidate Candidate{};
                        const bool IsCandidateCreated{ TryCreateCandidate(TerrainContext, RuleIndex, CellX, CellZ, InstanceIndex, Candidate) };
                        if (IsCandidateCreated == false) {
                            continue;
                        }

                        WindowCandidates.push_back(Candidate);
                    }
                }
            }
        }

        const std::uint32_t TerrainSeed{ TerrainContext.mResource == nullptr ? 0u : TerrainContext.mResource->GetBuildDesc().mProceduralHeightFieldDesc.mSeed };
        WindowCandidates = FilterCandidatesByMinimumSpacing(std::move(WindowCandidates), mRules, mConfig, TerrainSeed);

        GeneratedFoliageCell GeneratedCell{};
        GeneratedCell.mCandidates.reserve(WindowCandidates.size());
        for (const FoliageCandidate& Candidate : WindowCandidates) {
            if (Candidate.mKey.mCellX != CellKey.mX || Candidate.mKey.mCellZ != CellKey.mZ) {
                continue;
            }

            GeneratedCell.mCandidates.push_back(Candidate);
        }

        GeneratedCell.mCell = BuildEnvironmentObjectCell(CellKey, GeneratedCell.mCandidates, FrameIndex);
        return GeneratedCell;
    }

    void EnvironmentFoliageRuntime::Impl::RemoveUnloadedEnvironmentObjectCells(FrameContext& Ctx) {
        static_cast<void>(Ctx);

        for (const EnvironmentObjectCellKey& PreviousCellKey : mVisibleEnvironmentCellKeys) {
            if (ContainsEnvironmentCellKey(mDesiredEnvironmentCellKeys, PreviousCellKey) == false) {
                mEnvironmentRenderContext.RemoveCell(PreviousCellKey);
            }
        }

        for (std::unordered_map<EnvironmentObjectCellKey, GeneratedFoliageCell, EnvironmentObjectCellKeyHasher>::iterator Iterator{ mGeneratedEnvironmentCells.begin() }; Iterator != mGeneratedEnvironmentCells.end();) {
            if (ContainsEnvironmentCellKey(mDesiredEnvironmentCellKeys, Iterator->first) == true) {
                ++Iterator;
                continue;
            }

            mEnvironmentRenderContext.RemoveCell(Iterator->first);
            Iterator = mGeneratedEnvironmentCells.erase(Iterator);
        }
    }

    void EnvironmentFoliageRuntime::Impl::RefreshVisibleEnvironmentObjectCells(FrameContext& Ctx) {
        mEnvironmentRenderContext.SetResidentCellLimit(mDesiredEnvironmentCellKeys.size());
        std::vector<EnvironmentObjectCellKey> NewVisibleCellKeys{};
        NewVisibleCellKeys.reserve(mDesiredEnvironmentCellKeys.size());
        for (const EnvironmentObjectCellKey& CellKey : mDesiredEnvironmentCellKeys) {
            std::unordered_map<EnvironmentObjectCellKey, GeneratedFoliageCell, EnvironmentObjectCellKeyHasher>::iterator FoundCellIterator{ mGeneratedEnvironmentCells.find(CellKey) };
            if (FoundCellIterator == mGeneratedEnvironmentCells.end()) {
                continue;
            }

            EnvironmentObjectCell& Cell{ FoundCellIterator->second.mCell };
            if (IsEnvironmentObjectCellRenderable(Cell) == false) {
                mEnvironmentRenderContext.RemoveCell(CellKey);
                continue;
            }

            Cell.mLastTouchedFrame = Ctx.RenderData.mFrameGlobals.mFrameIndex;
            mEnvironmentRenderContext.UpsertCell(Cell);
            NewVisibleCellKeys.push_back(CellKey);
        }

        std::sort(NewVisibleCellKeys.begin(), NewVisibleCellKeys.end());
        NewVisibleCellKeys.erase(std::unique(NewVisibleCellKeys.begin(), NewVisibleCellKeys.end()), NewVisibleCellKeys.end());

        mVisibleEnvironmentCellKeys = std::move(NewVisibleCellKeys);
        mEnvironmentRenderContext.SetResidentCellLimit(mVisibleEnvironmentCellKeys.size());
    }

    void EnvironmentFoliageRuntime::Impl::BuildLoadedCollisionCandidates() {
        mUpdateCandidates.clear();
        for (const EnvironmentObjectCellKey& CellKey : mDesiredEnvironmentCellKeys) {
            const std::unordered_map<EnvironmentObjectCellKey, GeneratedFoliageCell, EnvironmentObjectCellKeyHasher>::const_iterator FoundCellIterator{ mGeneratedEnvironmentCells.find(CellKey) };
            if (FoundCellIterator == mGeneratedEnvironmentCells.end()) {
                continue;
            }

            for (const FoliageCandidate& Candidate : FoundCellIterator->second.mCandidates) {
                if (Candidate.mKey.mRuleIndex >= mRules.size() || mRules[Candidate.mKey.mRuleIndex].mDesc.mCollisionActorEnabled == false) {
                    continue;
                }

                mUpdateCandidates.push_back(Candidate);
            }
        }
    }

    std::vector<std::uint32_t> EnvironmentFoliageRuntime::Impl::ResolveEnvironmentPrototypeLodLevels(const EnvironmentObjectRenderPacket& Packet) const {
        std::vector<std::uint32_t> LodLevels{};
        LodLevels.resize(mRules.size(), 0u);
        if (mRules.empty() == true || Packet.mPrototypeIndices.empty() == true) {
            return LodLevels;
        }

        float CellCenterX{ (static_cast<float>(Packet.mCellKey.mX) + 0.5f) * mConfig.mCellSize };
        float CellCenterZ{ (static_cast<float>(Packet.mCellKey.mZ) + 0.5f) * mConfig.mCellSize };
        if (Packet.mHasWorldBoundingBox == true) {
            CellCenterX = Packet.mWorldBoundingBox.Center.x;
            CellCenterZ = Packet.mWorldBoundingBox.Center.z;
        }

        for (std::uint32_t PrototypeIndex : Packet.mPrototypeIndices) {
            if (PrototypeIndex >= mRules.size()) {
                continue;
            }

            const FoliagePlacementRule& Rule{ mRules[PrototypeIndex].mDesc };
            if (Rule.mLods.size() <= 1ULL) {
                continue;
            }

            LodLevels[PrototypeIndex] = ResolveFoliageLodIndex(Rule, mCurrentFocusPosition, CellCenterX, CellCenterZ);
        }

        return LodLevels;
    }

    void EnvironmentFoliageRuntime::Impl::AppendEnvironmentRenderData(Arche::World& World, FrameContext& Ctx) {
        if (mVisibleEnvironmentCellKeys.empty() == true) {
            return;
        }

        Frustum ActiveFrustum{};
        const bool HasActiveFrustum{ TryResolveActiveCameraFrustum(World, ActiveFrustum) };
        const Frustum* ActiveFrustumPointer{ HasActiveFrustum == true ? &ActiveFrustum : nullptr };
        std::vector<EnvironmentMergedBatch> EnvironmentMergedBatches{};
        std::unordered_map<EnvironmentMergedBatchKey, std::size_t, EnvironmentMergedBatchKeyHasher> EnvironmentMergedBatchIndexByKey{};
        EnvironmentMergedBatches.reserve(mVisibleEnvironmentCellKeys.size());
        EnvironmentMergedBatchIndexByKey.reserve(mVisibleEnvironmentCellKeys.size());

        for (const EnvironmentObjectCellKey& CellKey : mVisibleEnvironmentCellKeys) {
            const EnvironmentObjectRenderPacket* Packet{ mEnvironmentRenderContext.FindCell(CellKey) };
            if (Packet == nullptr) {
                continue;
            }

            const bool IsMainVisible{ IsEnvironmentPacketVisibleByMainFrustum(*Packet, ActiveFrustumPointer) };
            const std::uint32_t ShadowCascadeMask{ BuildEnvironmentPacketShadowCascadeMask(*Packet, Ctx.RenderData.mShadowMappingParameter) };
            const std::uint32_t VisibilityMask{ BuildEnvironmentPacketVisibilityMask(IsMainVisible, ShadowCascadeMask) };
            if (VisibilityMask == 0u) {
                continue;
            }

            const std::vector<std::uint32_t> PrototypeLodLevels{ ResolveEnvironmentPrototypeLodLevels(*Packet) };
            AppendEnvironmentPacketToMergedBatches(*Packet, std::span<const std::uint32_t>{ PrototypeLodLevels.data(), PrototypeLodLevels.size() }, VisibilityMask, EnvironmentMergedBatches, EnvironmentMergedBatchIndexByKey);
        }

        RenderContract::RenderGatherResult RenderGatherResult{};
        AppendEnvironmentMergedBatchesToRenderGatherResult(EnvironmentMergedBatches, RenderGatherResult);

        if (RenderGatherResult.Empty() == false) {
            const std::span<const RenderContract::RenderGatherResult> RenderGatherResults{ &RenderGatherResult, 1ULL };
            RenderContract::RenderGatherResultMerger::Merge(RenderGatherResults, Ctx.RenderData);
        }
    }

    void EnvironmentFoliageRuntime::Impl::Update(Arche::World& World, FrameContext& Ctx, float Dt) {
        if (Initialize(Ctx) == false) {
            return;
        }

        SimpleMath::Vector3 FocusPosition{};
        const bool HasFocusPosition{ TryResolveFocusPosition(World, FocusPosition) };
        if (HasFocusPosition == true) {
            mCurrentFocusPosition = FocusPosition;
        }

        if (mUpdatePhase == FoliageUpdatePhase::Idle) {
            mUpdateTimer += Dt;
            if (mHasUpdatedOnce == true && mUpdateTimer < mConfig.mUpdateInterval) {
                AppendEnvironmentRenderData(World, Ctx);
                return;
            }

            if (HasFocusPosition == false) {
                AppendEnvironmentRenderData(World, Ctx);
                return;
            }

            TerrainSamplingContext TerrainContext{};
            if (TryResolveTerrainSamplingContext(World, TerrainContext) == false) {
                AppendEnvironmentRenderData(World, Ctx);
                return;
            }

            mUpdateTimer = 0.0f;
            mHasUpdatedOnce = true;
            BeginFoliageUpdate(Ctx, FocusPosition);
            ProcessFoliageUpdateBatch(World, Ctx, TerrainContext);
            AppendEnvironmentRenderData(World, Ctx);
            return;
        }

        TerrainSamplingContext TerrainContext{};
        if (TryResolveTerrainSamplingContext(World, TerrainContext) == false) {
            AppendEnvironmentRenderData(World, Ctx);
            return;
        }

        ProcessFoliageUpdateBatch(World, Ctx, TerrainContext);
        AppendEnvironmentRenderData(World, Ctx);
    }

    void EnvironmentFoliageRuntime::Impl::BeginFoliageUpdate(FrameContext& Ctx, const SimpleMath::Vector3& FocusPosition) {
        for (FoliageSlot& Slot : mSlots) {
            Slot.mAssignedThisFrame = false;
        }

        mUpdateCandidates.clear();
        mUpdateSlotByKey.clear();
        mDesiredEnvironmentCellKeys = BuildEnvironmentCellKeysInPlacementRadius(FocusPosition, mConfig);
        RemoveUnloadedEnvironmentObjectCells(Ctx);
        RefreshVisibleEnvironmentObjectCells(Ctx);
        mPendingEnvironmentCellKeys.clear();
        mPendingEnvironmentCellKeys.reserve(mDesiredEnvironmentCellKeys.size());
        for (const EnvironmentObjectCellKey& CellKey : mDesiredEnvironmentCellKeys) {
            if (mGeneratedEnvironmentCells.find(CellKey) != mGeneratedEnvironmentCells.end()) {
                continue;
            }

            mPendingEnvironmentCellKeys.push_back(CellKey);
        }

        mUpdateCellKeyIndex = 0ULL;
        mUpdateCandidateIndex = 0ULL;
        mUpdateSlotIndex = 0ULL;
        mUpdatePhase = FoliageUpdatePhase::BuildCandidates;
    }

    void EnvironmentFoliageRuntime::Impl::ProcessFoliageUpdateBatch(Arche::World& World, FrameContext& Ctx, const TerrainSamplingContext& TerrainContext) {
        if (mUpdatePhase == FoliageUpdatePhase::BuildCandidates) {
            BuildCandidateBatch(Ctx, TerrainContext);
            return;
        }

        if (mUpdatePhase == FoliageUpdatePhase::ApplyCandidates) {
            ApplyCandidateBatch(World, Ctx);
            return;
        }

        if (mUpdatePhase == FoliageUpdatePhase::RebuildSlotLookup) {
            RebuildSlotLookupBatch(World, Ctx);
        }
    }

    void EnvironmentFoliageRuntime::Impl::BuildCandidateBatch(FrameContext& Ctx, const TerrainSamplingContext& TerrainContext) {
        std::uint32_t ProcessedCellCount{};
        while (mUpdateCellKeyIndex < mPendingEnvironmentCellKeys.size() && ProcessedCellCount < mConfig.mUpdateCellBatchSize) {
            const EnvironmentObjectCellKey CellKey{ mPendingEnvironmentCellKeys[mUpdateCellKeyIndex] };
            GeneratedFoliageCell GeneratedCell{ GenerateEnvironmentObjectCell(TerrainContext, CellKey, Ctx.RenderData.mFrameGlobals.mFrameIndex) };
            mGeneratedEnvironmentCells.insert_or_assign(CellKey, std::move(GeneratedCell));
            mUpdateCellKeyIndex += 1ULL;
            ProcessedCellCount += 1u;
        }

        if (mUpdateCellKeyIndex < mPendingEnvironmentCellKeys.size()) {
            return;
        }

        RefreshVisibleEnvironmentObjectCells(Ctx);
        BuildLoadedCollisionCandidates();
        mUpdateCandidateIndex = 0ULL;
        mUpdatePhase = FoliageUpdatePhase::ApplyCandidates;
    }

    void EnvironmentFoliageRuntime::Impl::ApplyCandidateBatch(Arche::World& World, FrameContext& Ctx) {
        std::uint32_t ProcessedCandidateCount{};
        while (mUpdateCandidateIndex < mUpdateCandidates.size() && ProcessedCandidateCount < mConfig.mUpdateCandidateBatchSize) {
            FoliageCandidate Candidate{ mUpdateCandidates[mUpdateCandidateIndex] };
            if (Candidate.mKey.mRuleIndex >= mRules.size() || mRules[Candidate.mKey.mRuleIndex].mDesc.mCollisionActorEnabled == false) {
                mUpdateCandidateIndex += 1ULL;
                ProcessedCandidateCount += 1u;
                continue;
            }

            Candidate.mLodIndex = ResolveFoliageLodIndex(mRules[Candidate.mKey.mRuleIndex].mDesc, mCurrentFocusPosition, Candidate.mPosition.x, Candidate.mPosition.z);
            const std::unordered_map<FoliageCandidateKey, std::size_t, FoliageCandidateKeyHasher>::const_iterator SlotIter{ mSlotByKey.find(Candidate.mKey) };
            if (SlotIter != mSlotByKey.end() && SlotIter->second < mSlots.size() && mSlots[SlotIter->second].mRuleIndex == Candidate.mKey.mRuleIndex) {
                ApplyFoliageCandidateToSlot(World, Ctx, mSlots[SlotIter->second], Candidate);
                mUpdateCandidateIndex += 1ULL;
                ProcessedCandidateCount += 1u;
                continue;
            }

            std::size_t SlotIndex{ FindReusableSlot(Candidate.mKey.mRuleIndex) };
            if (SlotIndex == std::numeric_limits<std::size_t>::max()) {
                const bool IsCreated{ CreateSlot(World, Ctx, Candidate.mKey.mRuleIndex, SlotIndex) };
                if (IsCreated == false) {
                    mUpdateCandidateIndex += 1ULL;
                    ProcessedCandidateCount += 1u;
                    continue;
                }
            }

            ApplyFoliageCandidateToSlot(World, Ctx, mSlots[SlotIndex], Candidate);
            mUpdateCandidateIndex += 1ULL;
            ProcessedCandidateCount += 1u;
        }

        if (mUpdateCandidateIndex < mUpdateCandidates.size()) {
            return;
        }

        mUpdateCandidates.clear();
        mUpdateSlotByKey.clear();
        mUpdateSlotByKey.reserve(mSlots.size());
        mUpdateSlotIndex = 0ULL;
        mUpdatePhase = FoliageUpdatePhase::RebuildSlotLookup;
    }

    void EnvironmentFoliageRuntime::Impl::RebuildSlotLookupBatch(Arche::World& World, FrameContext& Ctx) {
        std::uint32_t ProcessedSlotCount{};
        while (mUpdateSlotIndex < mSlots.size() && ProcessedSlotCount < mConfig.mUpdateSlotBatchSize) {
            FoliageSlot& Slot{ mSlots[mUpdateSlotIndex] };
            if (Slot.mAssignedThisFrame == false) {
                DeactivateFoliageSlot(World, Ctx, Slot);
            }
            else {
                mUpdateSlotByKey.insert_or_assign(Slot.mKey, mUpdateSlotIndex);
            }

            mUpdateSlotIndex += 1ULL;
            ProcessedSlotCount += 1u;
        }

        if (mUpdateSlotIndex < mSlots.size()) {
            return;
        }

        mSlotByKey = std::move(mUpdateSlotByKey);
        mUpdateSlotByKey.clear();
        mUpdateSlotIndex = 0ULL;
        mUpdatePhase = FoliageUpdatePhase::Idle;
    }

    bool EnvironmentFoliageRuntime::Impl::TryCreateCandidate(const TerrainSamplingContext& TerrainContext, std::uint32_t RuleIndex, std::int32_t CellX, std::int32_t CellZ, std::uint32_t InstanceIndex, FoliageCandidate& OutCandidate) const {
        if (RuleIndex >= mRules.size() || TerrainContext.mResource == nullptr) {
            return false;
        }

        const FoliagePlacementRule& Rule{ mRules[RuleIndex].mDesc };
        FoliageCandidateKey Key{};
        Key.mCellX = CellX;
        Key.mCellZ = CellZ;
        Key.mRuleIndex = RuleIndex;
        Key.mInstanceIndex = InstanceIndex;

        const std::uint32_t TerrainSeed{ TerrainContext.mResource->GetBuildDesc().mProceduralHeightFieldDesc.mSeed };
        const float RandomX{ HashToUnitFloat(BuildCandidateHash(TerrainSeed, mConfig.mSeedSalt, Key, mConfig.mCandidateRandomXStream)) };
        const float RandomZ{ HashToUnitFloat(BuildCandidateHash(TerrainSeed, mConfig.mSeedSalt, Key, mConfig.mCandidateRandomZStream)) };
        const float RandomChance{ HashToUnitFloat(BuildCandidateHash(TerrainSeed, mConfig.mSeedSalt, Key, mConfig.mCandidateRandomChanceStream)) };
        float WorldX{ (static_cast<float>(CellX) + RandomX) * mConfig.mCellSize };
        float WorldZ{ (static_cast<float>(CellZ) + RandomZ) * mConfig.mCellSize };
        const std::uint32_t ClusterIndex{ ResolveLayerIndex(Rule, TerrainContext.mResource->GetBuildDesc()) };
        ApplyFoliageClumpPosition(TerrainSeed, mConfig, Rule, Key, ClusterIndex, mConfig.mCellSize, WorldX, WorldZ);
        float WorldY{};
        float LayerWeight{};
        const bool HasTerrainSample{ TrySampleTerrain(TerrainContext, Rule, WorldX, WorldZ, WorldY, LayerWeight) };
        const float ClusterFactor{ SampleFoliageClusterFactor(TerrainSeed, mConfig, Rule, ClusterIndex, WorldX, WorldZ) };
        const float ForestFactor{ ResolveRuleForestFactor(TerrainSeed, mConfig, Rule, WorldX, WorldZ) };
        const float EffectiveSpawnChance{ std::clamp(Rule.mSpawnChance * Rule.mDensityMultiplier * mConfig.mDensityMultiplier * LayerWeight * ClusterFactor * ForestFactor, 0.0f, 1.0f) };
        if (HasTerrainSample == false || LayerWeight < Rule.mMinimumWeight || EffectiveSpawnChance <= 0.0f || RandomChance > EffectiveSpawnChance) {
            return false;
        }

        if (IsForestAreaWideEnough(TerrainContext, mConfig, Rule, TerrainSeed, WorldX, WorldZ) == false) {
            return false;
        }

        const float RandomYaw{ HashToUnitFloat(BuildCandidateHash(TerrainSeed, mConfig.mSeedSalt, Key, mConfig.mCandidateRandomYawStream)) };
        const float RandomScale{ HashToUnitFloat(BuildCandidateHash(TerrainSeed, mConfig.mSeedSalt, Key, mConfig.mCandidateRandomScaleStream)) };
        OutCandidate.mKey = Key;
        OutCandidate.mPosition = SimpleMath::Vector3{ WorldX, WorldY + Rule.mOffsetY, WorldZ };
        OutCandidate.mYawRadians = DirectX::XMConvertToRadians(Lerp(Rule.mMinimumYawDegrees, Rule.mMaximumYawDegrees, RandomYaw));
        OutCandidate.mScale = Lerp(Rule.mMinimumScale, Rule.mMaximumScale, RandomScale);
        OutCandidate.mLodIndex = 0u;
        return true;
    }

    std::size_t EnvironmentFoliageRuntime::Impl::FindReusableSlot(std::uint32_t RuleIndex) const {
        for (std::size_t SlotIndex{ 0ULL }; SlotIndex < mSlots.size(); ++SlotIndex) {
            const FoliageSlot& Slot{ mSlots[SlotIndex] };
            if (Slot.mRuleIndex == RuleIndex && Slot.mActive == false && Slot.mAssignedThisFrame == false) {
                return SlotIndex;
            }
        }

        return std::numeric_limits<std::size_t>::max();
    }

    bool EnvironmentFoliageRuntime::Impl::CreateSlot(Arche::World& World, FrameContext& Ctx, std::uint32_t RuleIndex, std::size_t& OutSlotIndex) {
        if (RuleIndex >= mRules.size()) {
            return false;
        }

        FoliageSlot Slot{};
        IPhysicsWorld* FoliagePhysicsWorldResource{ Ctx.PhysicsWorldResource };
        const bool IsCreated{ CreateFoliageSlotEntities(World, FoliagePhysicsWorldResource, mConfig, mRules[RuleIndex], RuleIndex, Slot) };
        if (IsCreated == false) {
            return false;
        }

        OutSlotIndex = mSlots.size();
        mSlots.push_back(std::move(Slot));
        return true;
    }

    EnvironmentFoliageRuntime::EnvironmentFoliageRuntime(std::string ConfigPath)
        : mConfigPath{ std::move(ConfigPath) },
        mImpl{} {
    }

    EnvironmentFoliageRuntime::~EnvironmentFoliageRuntime() {
    }

    EnvironmentFoliageRuntime::EnvironmentFoliageRuntime(EnvironmentFoliageRuntime&& Other) noexcept
        : mConfigPath{ std::move(Other.mConfigPath) },
        mImpl{ std::move(Other.mImpl) } {
    }

    EnvironmentFoliageRuntime& EnvironmentFoliageRuntime::operator=(EnvironmentFoliageRuntime&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mConfigPath = std::move(Other.mConfigPath);
        mImpl = std::move(Other.mImpl);
        return *this;
    }

    void EnvironmentFoliageRuntime::SetConfigPath(const std::string& ConfigPath) {
        if (mConfigPath == ConfigPath) {
            return;
        }

        mConfigPath = ConfigPath;
        mImpl.reset();
    }

    const std::string& EnvironmentFoliageRuntime::GetConfigPath() const {
        return mConfigPath;
    }

    void EnvironmentFoliageRuntime::Update(Arche::World& World, FrameContext& Ctx, float Dt) {
        if (Config::Query()->Get<bool>("EnvironmentObjects_Enabled") == false) {
            return;
        }

        if (mImpl == nullptr) {
            mImpl = std::make_unique<Impl>(mConfigPath);
        }

        mImpl->Update(World, Ctx, Dt);
    }
}
