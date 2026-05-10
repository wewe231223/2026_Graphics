#include "ProceduralFoliageSystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
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

#include "Game/Model/AssetRegistry.h"
#include "Game/Model/TerrainRenderResource.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "PhysicsLib/Actors/PhysicsStaticActor.h"
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
        float mMaximumDistance{ std::numeric_limits<float>::max() };
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
        std::vector<std::vector<Arche::EntityID>> mRenderEntityIdsByLod{};
        PhysicsActorBase* mCollisionActorPointer{ nullptr };
        std::uint32_t mCollisionActorIndex{ 0u };
        float mInactiveHeight{};
        std::uint32_t mRuleIndex{ 0u };
        std::uint32_t mActiveLodIndex{ std::numeric_limits<std::uint32_t>::max() };
        bool mActive{ false };
        bool mAssignedThisFrame{ false };
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
        ReadFloatChild(LodNode, "Distance", LodDesc.mMaximumDistance);
        ReadFloatChild(LodNode, "MaximumDistance", LodDesc.mMaximumDistance);
        LodDesc.mModelPath = ResolveFoliageResourcePath(ConfigPath, LodDesc.mModelPath);
        LodDesc.mMaximumDistance = std::max(LodDesc.mMaximumDistance, 0.0f);

        if (LodDesc.mModelPath.empty() == true) {
            throw std::runtime_error{ "Procedural foliage lod model path is empty." };
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

    float SampleHeight01(const Game::HeightFieldData& Field, float GridX, float GridZ) {
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

    asset::Vec4 SampleSplatWeight(const Game::SplatMapData& SplatMap, float GridX, float GridZ, const Game::HeightFieldData& Field) {
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
        const asset::Vec4 Weight00{ SplatMap.WeightValues[CalculateIndex(SplatMap.Width, X0, Z0)] };
        const asset::Vec4 Weight10{ SplatMap.WeightValues[CalculateIndex(SplatMap.Width, X1, Z0)] };
        const asset::Vec4 Weight01{ SplatMap.WeightValues[CalculateIndex(SplatMap.Width, X0, Z1)] };
        const asset::Vec4 Weight11{ SplatMap.WeightValues[CalculateIndex(SplatMap.Width, X1, Z1)] };
        const asset::Vec4 WeightX0{ LerpSplatWeight(Weight00, Weight10, BlendX) };
        const asset::Vec4 WeightX1{ LerpSplatWeight(Weight01, Weight11, BlendX) };
        return LerpSplatWeight(WeightX0, WeightX1, BlendZ);
    }

    float GetSplatLayerWeight(const asset::Vec4& Weights, std::uint32_t LayerIndex) {
        if (LayerIndex == 0u) {
            return Weights.x;
        }

        if (LayerIndex == 1u) {
            return Weights.y;
        }

        if (LayerIndex == 2u) {
            return Weights.z;
        }

        if (LayerIndex == 3u) {
            return Weights.w;
        }

        return 0.0f;
    }

    bool TryResolveLayerIndexByName(const Game::TerrainBuildDesc& BuildDesc, const std::string& LayerName, std::uint32_t& OutLayerIndex) {
        const std::vector<Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapLayerDesc>& Layers{ BuildDesc.mProceduralHeightFieldDesc.mSplatMapDesc.mLayers };
        for (std::size_t LayerIndex{ 0ULL }; LayerIndex < Layers.size(); ++LayerIndex) {
            if (Layers[LayerIndex].mName == LayerName) {
                OutLayerIndex = static_cast<std::uint32_t>(LayerIndex);
                return true;
            }
        }

        return false;
    }

    std::uint32_t ResolveLayerIndex(const FoliagePlacementRule& Rule, const Game::TerrainBuildDesc& BuildDesc) {
        if (Rule.mLayerName.empty() == false) {
            std::uint32_t LayerIndex{};
            const bool IsLayerIndexResolved{ TryResolveLayerIndexByName(BuildDesc, Rule.mLayerName, LayerIndex) };
            if (IsLayerIndexResolved == true) {
                return LayerIndex;
            }
        }

        return Rule.mLayerIndex;
    }

    bool TryResolveExcludedLayerWeight(const FoliagePlacementRule& Rule, const Game::TerrainBuildDesc& BuildDesc, const asset::Vec4& SplatWeights, float& OutLayerWeight) {
        if (Rule.mExcludedLayerNames.empty() == true) {
            return false;
        }

        float ExcludedWeight{};
        std::uint32_t ResolvedLayerCount{};
        for (const std::string& LayerName : Rule.mExcludedLayerNames) {
            std::uint32_t LayerIndex{};
            const bool IsLayerIndexResolved{ TryResolveLayerIndexByName(BuildDesc, LayerName, LayerIndex) };
            if (IsLayerIndexResolved == false || LayerIndex >= 4u) {
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

        const Game::HeightFieldData& HeightField{ TerrainContext.mResource->GetHeightFieldData() };
        const Game::SplatMapData& SplatMap{ TerrainContext.mResource->GetSplatMapData() };
        if (HeightField.Width < 2u || HeightField.Height < 2u || HeightField.HeightValues.empty() == true || SplatMap.Width < 2u || SplatMap.Height < 2u || SplatMap.WeightValues.empty() == true) {
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

        const Game::TerrainBuildDesc& BuildDesc{ TerrainContext.mResource->GetBuildDesc() };
        const float Height01{ SampleHeight01(HeightField, GridX, GridZ) };
        const asset::Vec4 SplatWeights{ SampleSplatWeight(SplatMap, GridX, GridZ, HeightField) };
        OutWorldY = TerrainContext.mTransform.position.y + (Height01 * TerrainContext.mResource->GetMaxHeight() * ScaleY);
        const bool HasExcludedLayerWeight{ TryResolveExcludedLayerWeight(Rule, BuildDesc, SplatWeights, OutLayerWeight) };
        if (HasExcludedLayerWeight == true) {
            return true;
        }

        const std::uint32_t LayerIndex{ ResolveLayerIndex(Rule, BuildDesc) };
        if (LayerIndex >= 4u) {
            return false;
        }

        OutLayerWeight = std::clamp(GetSplatLayerWeight(SplatWeights, LayerIndex), 0.0f, 1.0f);
        return true;
    }

    bool IsForestAreaWideEnough(const TerrainSamplingContext& TerrainContext, const FoliagePlacementConfig& Config, const FoliagePlacementRule& Rule, std::uint32_t TerrainSeed, float WorldX, float WorldZ) {
        if (Config.mForestEnabled == false || Config.mForestMinimumWidth <= FoliageEpsilon) {
            return true;
        }

        const float CenterForestFactor{ SampleForestAreaFactor(TerrainSeed, Config, WorldX, WorldZ) };
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
            const float SampleForestFactor{ SampleForestAreaFactor(TerrainSeed, Config, WorldX + SampleOffset.x, WorldZ + SampleOffset.y) };
            if (HasSample == true && SampleLayerWeight >= Rule.mMinimumWeight && SampleForestFactor >= Config.mForestMinimumAreaFactor) {
                AcceptedSampleCount += 1u;
            }
        }

        return AcceptedSampleCount >= RequiredSampleCount;
    }

    bool IsCandidateInPlacementRadius(const SimpleMath::Vector3& FocusPosition, float WorldX, float WorldZ, float RadiusSquared) {
        const float DistanceX{ WorldX - FocusPosition.x };
        const float DistanceZ{ WorldZ - FocusPosition.z };
        return ((DistanceX * DistanceX) + (DistanceZ * DistanceZ)) <= RadiusSquared;
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

    void AttachChildEntity(Arche::World& World, Arche::EntityID ParentEntityId, Arche::EntityID ChildEntityId) {
        Game::EntityHierarchy* ParentHierarchy{ World.GetComponent<Game::EntityHierarchy>(ParentEntityId) };
        Game::EntityHierarchy* ChildHierarchy{ World.GetComponent<Game::EntityHierarchy>(ChildEntityId) };
        if (ParentHierarchy == nullptr || ChildHierarchy == nullptr) {
            return;
        }

        ChildHierarchy->parent = ParentEntityId;
        ChildHierarchy->nextSibling = Arche::NullEntityID;
        if (ParentHierarchy->firstChild == Arche::NullEntityID) {
            ParentHierarchy->firstChild = ChildEntityId;
            return;
        }

        Arche::EntityID SiblingEntityId{ ParentHierarchy->firstChild };
        while (SiblingEntityId != Arche::NullEntityID) {
            Game::EntityHierarchy* SiblingHierarchy{ World.GetComponent<Game::EntityHierarchy>(SiblingEntityId) };
            if (SiblingHierarchy == nullptr) {
                return;
            }

            if (SiblingHierarchy->nextSibling == Arche::NullEntityID) {
                SiblingHierarchy->nextSibling = ChildEntityId;
                return;
            }

            SiblingEntityId = SiblingHierarchy->nextSibling;
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
        PhysicsActorComponent.mActorType = PhysicsActorBase::PhysicsActorType::Static;
        World.AddComponent(RootEntityId, PhysicsActorComponent);

        Game::BoundingBox BoundingBoxComponent{};
        BoundingBoxComponent.SetObb(Desc.LocalBoundingBox);
        World.AddComponent(RootEntityId, BoundingBoxComponent);

        OutSlot.mCollisionActorPointer = ActorPointer;
        OutSlot.mCollisionActorIndex = ActorIndex;
        return true;
    }

    bool CreateFoliageLodEntities(Arche::World& World, const FoliageRuntimeLod& Lod, std::uint32_t MaterialGroupIndex, Arche::EntityID ParentEntityId, std::vector<Arche::EntityID>& OutRenderEntityIds) {
        if (Lod.mModel == nullptr || ParentEntityId == Arche::NullEntityID) {
            return false;
        }

        const std::vector<Game::ModelNode>& ModelNodes{ Lod.mModel->GetNodes() };
        const Game::ModelNode* RootNode{ Lod.mModel->GetRootNode() };
        if (RootNode == nullptr || ModelNodes.empty() == true) {
            return false;
        }

        const std::size_t RootNodeIndex{ static_cast<std::size_t>(RootNode - ModelNodes.data()) };
        std::vector<Arche::EntityID> NodeEntities(ModelNodes.size(), Arche::NullEntityID);
        for (std::size_t NodeIndex{ 0ULL }; NodeIndex < ModelNodes.size(); ++NodeIndex) {
            NodeEntities[NodeIndex] = CreateDerivedEntity(World);
        }

        for (std::size_t NodeIndex{ 0ULL }; NodeIndex < ModelNodes.size(); ++NodeIndex) {
            const Game::ModelNode& Node{ ModelNodes[NodeIndex] };
            Game::Transform TransformComponent{};
            TransformComponent.position = SimpleMath::Vector3::Zero;
            TransformComponent.nodeToParent = Node.GetNodeToParent();
            World.AddComponent(NodeEntities[NodeIndex], TransformComponent);

            if (Node.GetSubMeshes().empty() == false && Node.IsSkinnedMesh() == false) {
                Game::StaticMeshRenderer Renderer{};
                Renderer.model = Lod.mModel.get();
                Renderer.nodeIndex = static_cast<std::uint32_t>(NodeIndex);
                Renderer.active = false;
                World.AddComponent(NodeEntities[NodeIndex], Renderer);

                Game::Material MaterialComponent{};
                MaterialComponent.MaterialGroupIndex = MaterialGroupIndex;
                World.AddComponent(NodeEntities[NodeIndex], MaterialComponent);

                Game::Culling CullingComponent{};
                CullingComponent.frustumCulling = true;
                World.AddComponent(NodeEntities[NodeIndex], CullingComponent);

                Game::BoundingBox BoundingBoxComponent{};
                BoundingBoxComponent.UpdateFromModel(Lod.mModel.get(), static_cast<std::uint32_t>(NodeIndex));
                World.AddComponent(NodeEntities[NodeIndex], BoundingBoxComponent);
                OutRenderEntityIds.push_back(NodeEntities[NodeIndex]);
            }
        }

        for (std::size_t NodeIndex{ 0ULL }; NodeIndex < ModelNodes.size(); ++NodeIndex) {
            const std::vector<std::uint32_t>& Children{ ModelNodes[NodeIndex].GetChildren() };
            for (std::uint32_t ChildNodeIndex : Children) {
                if (ChildNodeIndex >= NodeEntities.size()) {
                    continue;
                }

                AttachChildEntity(World, NodeEntities[NodeIndex], NodeEntities[ChildNodeIndex]);
            }
        }

        AttachChildEntity(World, ParentEntityId, NodeEntities[RootNodeIndex]);
        return OutRenderEntityIds.empty() == false;
    }

    bool CreateFoliageSlotEntities(Arche::World& World, IPhysicsWorld* PhysicsWorldResource, const FoliagePlacementConfig& Config, const FoliageRuntimeRule& Rule, std::uint32_t RuleIndex, FoliageSlot& OutSlot) {
        if (Rule.mLods.empty() == true) {
            return false;
        }

        const Arche::EntityID RootEntityId{ CreateDerivedEntity(World) };
        Game::Transform RootTransform{};
        RootTransform.position = SimpleMath::Vector3{ 0.0f, Config.mInactiveHeight, 0.0f };
        World.AddComponent(RootEntityId, RootTransform);

        OutSlot.mRootEntityId = RootEntityId;
        OutSlot.mInactiveHeight = Config.mInactiveHeight;
        OutSlot.mRuleIndex = RuleIndex;
        OutSlot.mRenderEntityIdsByLod.clear();
        OutSlot.mRenderEntityIdsByLod.reserve(Rule.mLods.size());
        const bool IsCollisionActorCreated{ CreateFoliageCollisionActor(World, PhysicsWorldResource, Config, Rule, RootEntityId, OutSlot) };
        if (IsCollisionActorCreated == false) {
            return false;
        }

        for (const FoliageRuntimeLod& Lod : Rule.mLods) {
            std::vector<Arche::EntityID> LodRenderEntityIds{};
            const bool IsCreated{ CreateFoliageLodEntities(World, Lod, Rule.mMaterialGroupIndex, RootEntityId, LodRenderEntityIds) };
            if (IsCreated == false) {
                return false;
            }

            OutSlot.mRenderEntityIdsByLod.push_back(std::move(LodRenderEntityIds));
        }

        return OutSlot.mRootEntityId != Arche::NullEntityID && OutSlot.mRenderEntityIdsByLod.empty() == false;
    }

    void SetFoliageRenderEntitiesActive(Arche::World& World, const std::vector<Arche::EntityID>& RenderEntityIds, bool IsActive) {
        for (Arche::EntityID RenderEntityId : RenderEntityIds) {
            Game::StaticMeshRenderer* Renderer{ World.GetComponent<Game::StaticMeshRenderer>(RenderEntityId) };
            if (Renderer == nullptr) {
                continue;
            }

            Renderer->active = IsActive;
        }
    }

    void SetFoliageSlotActive(Arche::World& World, FoliageSlot& Slot, bool IsActive, std::uint32_t LodIndex) {
        const std::uint32_t ResolvedLodIndex{ Slot.mRenderEntityIdsByLod.empty() == true ? 0u : std::min(LodIndex, static_cast<std::uint32_t>(Slot.mRenderEntityIdsByLod.size() - 1ULL)) };
        for (std::size_t CurrentLodIndex{ 0ULL }; CurrentLodIndex < Slot.mRenderEntityIdsByLod.size(); ++CurrentLodIndex) {
            const bool ShouldActivate{ IsActive == true && CurrentLodIndex == static_cast<std::size_t>(ResolvedLodIndex) };
            SetFoliageRenderEntitiesActive(World, Slot.mRenderEntityIdsByLod[CurrentLodIndex], ShouldActivate);
        }

        Slot.mActive = IsActive;
        Slot.mActiveLodIndex = IsActive == true ? ResolvedLodIndex : std::numeric_limits<std::uint32_t>::max();
    }

    void ApplyFoliageCollisionActorToSlot(Arche::World& World, FoliageSlot& Slot, const Game::Transform& TransformComponent, bool IsActive) {
        if (Slot.mCollisionActorPointer == nullptr) {
            return;
        }

        Slot.mCollisionActorPointer->SetPosition(TransformComponent.position);
        Slot.mCollisionActorPointer->SetOrientation(TransformComponent.rotation);
        Slot.mCollisionActorPointer->SetScale(TransformComponent.scale);
        Slot.mCollisionActorPointer->SetIsActive(IsActive);

        Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(Slot.mRootEntityId) };
        if (BoundingBoxComponent != nullptr) {
            BoundingBoxComponent->SetWorldObb(Slot.mCollisionActorPointer->GetWorldBoundingBox());
        }
    }

    void ApplyFoliageCandidateToSlot(Arche::World& World, FoliageSlot& Slot, const FoliageCandidate& Candidate) {
        if (Slot.mActive == true && Slot.mKey == Candidate.mKey) {
            Slot.mAssignedThisFrame = true;
            if (Slot.mActiveLodIndex != Candidate.mLodIndex) {
                SetFoliageSlotActive(World, Slot, true, Candidate.mLodIndex);
            }

            return;
        }

        Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(Slot.mRootEntityId) };
        if (TransformComponent == nullptr) {
            return;
        }

        TransformComponent->position = Candidate.mPosition;
        TransformComponent->rotationEuler = SimpleMath::Vector3{ 0.0f, Candidate.mYawRadians, 0.0f };
        TransformComponent->UpdateRotationFromEulerRadians();
        TransformComponent->scale = SimpleMath::Vector3{ Candidate.mScale, Candidate.mScale, Candidate.mScale };
        ApplyFoliageCollisionActorToSlot(World, Slot, *TransformComponent, true);
        Slot.mKey = Candidate.mKey;
        Slot.mAssignedThisFrame = true;
        SetFoliageSlotActive(World, Slot, true, Candidate.mLodIndex);
    }

    void DeactivateFoliageSlot(Arche::World& World, FoliageSlot& Slot) {
        if (Slot.mActive == false) {
            return;
        }

        Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(Slot.mRootEntityId) };
        if (TransformComponent != nullptr) {
            TransformComponent->position.y = Slot.mInactiveHeight;
            ApplyFoliageCollisionActorToSlot(World, Slot, *TransformComponent, false);
        }
        else if (Slot.mCollisionActorPointer != nullptr) {
            Slot.mCollisionActorPointer->SetIsActive(false);
        }

        SetFoliageSlotActive(World, Slot, false, 0u);
    }
}

namespace Game {
    class ProceduralFoliageRuntime final {
    public:
        explicit ProceduralFoliageRuntime(std::string ConfigPath);
        ~ProceduralFoliageRuntime();
        ProceduralFoliageRuntime(const ProceduralFoliageRuntime& Other);
        ProceduralFoliageRuntime& operator=(const ProceduralFoliageRuntime& Other);
        ProceduralFoliageRuntime(ProceduralFoliageRuntime&& Other) noexcept;
        ProceduralFoliageRuntime& operator=(ProceduralFoliageRuntime&& Other) noexcept;

    public:
        void Update(Arche::World& World, FrameContext& Ctx, float Dt);

    private:
        bool Initialize(FrameContext& Ctx);
        void BeginFoliageUpdate(const SimpleMath::Vector3& FocusPosition);
        void ProcessFoliageUpdateBatch(Arche::World& World, FrameContext& Ctx, const TerrainSamplingContext& TerrainContext);
        void BuildCandidateBatch(const TerrainSamplingContext& TerrainContext);
        void ApplyCandidateBatch(Arche::World& World, FrameContext& Ctx);
        void RebuildSlotLookupBatch(Arche::World& World);
        bool TryCreateCandidate(const TerrainSamplingContext& TerrainContext, const SimpleMath::Vector3& FocusPosition, std::uint32_t RuleIndex, std::int32_t CellX, std::int32_t CellZ, std::uint32_t InstanceIndex, FoliageCandidate& OutCandidate) const;
        std::size_t FindReusableSlot(std::uint32_t RuleIndex) const;
        bool CreateSlot(Arche::World& World, FrameContext& Ctx, std::uint32_t RuleIndex, std::size_t& OutSlotIndex);

    private:
        std::string mConfigPath{};
        FoliagePlacementConfig mConfig{};
        std::vector<FoliageRuntimeRule> mRules{};
        std::vector<FoliageSlot> mSlots{};
        std::unordered_map<FoliageCandidateKey, std::size_t, FoliageCandidateKeyHasher> mSlotByKey{};
        std::vector<FoliageCandidate> mUpdateCandidates{};
        std::unordered_map<FoliageCandidateKey, std::size_t, FoliageCandidateKeyHasher> mUpdateSlotByKey{};
        SimpleMath::Vector3 mUpdateFocusPosition{ SimpleMath::Vector3::Zero };
        FoliageUpdatePhase mUpdatePhase{ FoliageUpdatePhase::Idle };
        std::int32_t mUpdateMinCellX{};
        std::int32_t mUpdateMaxCellX{};
        std::int32_t mUpdateMinCellZ{};
        std::int32_t mUpdateMaxCellZ{};
        std::int32_t mUpdateCurrentCellX{};
        std::int32_t mUpdateCurrentCellZ{};
        std::size_t mUpdateCandidateIndex{};
        std::size_t mUpdateSlotIndex{};
        bool mInitialized{ false };
        bool mValid{ false };
        bool mHasUpdatedOnce{ false };
        float mUpdateTimer{ 0.0f };
    };

    ProceduralFoliageRuntime::ProceduralFoliageRuntime(std::string ConfigPath)
        : mConfigPath{ std::move(ConfigPath) },
        mConfig{},
        mRules{},
        mSlots{},
        mSlotByKey{},
        mUpdateCandidates{},
        mUpdateSlotByKey{},
        mUpdateFocusPosition{ SimpleMath::Vector3::Zero },
        mUpdatePhase{ FoliageUpdatePhase::Idle },
        mUpdateMinCellX{},
        mUpdateMaxCellX{},
        mUpdateMinCellZ{},
        mUpdateMaxCellZ{},
        mUpdateCurrentCellX{},
        mUpdateCurrentCellZ{},
        mUpdateCandidateIndex{},
        mUpdateSlotIndex{},
        mInitialized{ false },
        mValid{ false },
        mHasUpdatedOnce{ false },
        mUpdateTimer{ 0.0f } {
    }

    ProceduralFoliageRuntime::~ProceduralFoliageRuntime() {
    }

    ProceduralFoliageRuntime::ProceduralFoliageRuntime(const ProceduralFoliageRuntime& Other)
        : mConfigPath{ Other.mConfigPath },
        mConfig{ Other.mConfig },
        mRules{},
        mSlots{},
        mSlotByKey{},
        mUpdateCandidates{},
        mUpdateSlotByKey{},
        mUpdateFocusPosition{ SimpleMath::Vector3::Zero },
        mUpdatePhase{ FoliageUpdatePhase::Idle },
        mUpdateMinCellX{},
        mUpdateMaxCellX{},
        mUpdateMinCellZ{},
        mUpdateMaxCellZ{},
        mUpdateCurrentCellX{},
        mUpdateCurrentCellZ{},
        mUpdateCandidateIndex{},
        mUpdateSlotIndex{},
        mInitialized{ false },
        mValid{ false },
        mHasUpdatedOnce{ false },
        mUpdateTimer{ 0.0f } {
    }

    ProceduralFoliageRuntime& ProceduralFoliageRuntime::operator=(const ProceduralFoliageRuntime& Other) {
        if (this == &Other) {
            return *this;
        }

        mConfigPath = Other.mConfigPath;
        mConfig = Other.mConfig;
        mRules.clear();
        mSlots.clear();
        mSlotByKey.clear();
        mUpdateCandidates.clear();
        mUpdateSlotByKey.clear();
        mUpdateFocusPosition = SimpleMath::Vector3::Zero;
        mUpdatePhase = FoliageUpdatePhase::Idle;
        mUpdateMinCellX = 0;
        mUpdateMaxCellX = 0;
        mUpdateMinCellZ = 0;
        mUpdateMaxCellZ = 0;
        mUpdateCurrentCellX = 0;
        mUpdateCurrentCellZ = 0;
        mUpdateCandidateIndex = 0ULL;
        mUpdateSlotIndex = 0ULL;
        mInitialized = false;
        mValid = false;
        mHasUpdatedOnce = false;
        mUpdateTimer = 0.0f;
        return *this;
    }

    ProceduralFoliageRuntime::ProceduralFoliageRuntime(ProceduralFoliageRuntime&& Other) noexcept
        : mConfigPath{ std::move(Other.mConfigPath) },
        mConfig{ std::move(Other.mConfig) },
        mRules{ std::move(Other.mRules) },
        mSlots{ std::move(Other.mSlots) },
        mSlotByKey{ std::move(Other.mSlotByKey) },
        mUpdateCandidates{ std::move(Other.mUpdateCandidates) },
        mUpdateSlotByKey{ std::move(Other.mUpdateSlotByKey) },
        mUpdateFocusPosition{ Other.mUpdateFocusPosition },
        mUpdatePhase{ Other.mUpdatePhase },
        mUpdateMinCellX{ Other.mUpdateMinCellX },
        mUpdateMaxCellX{ Other.mUpdateMaxCellX },
        mUpdateMinCellZ{ Other.mUpdateMinCellZ },
        mUpdateMaxCellZ{ Other.mUpdateMaxCellZ },
        mUpdateCurrentCellX{ Other.mUpdateCurrentCellX },
        mUpdateCurrentCellZ{ Other.mUpdateCurrentCellZ },
        mUpdateCandidateIndex{ Other.mUpdateCandidateIndex },
        mUpdateSlotIndex{ Other.mUpdateSlotIndex },
        mInitialized{ Other.mInitialized },
        mValid{ Other.mValid },
        mHasUpdatedOnce{ Other.mHasUpdatedOnce },
        mUpdateTimer{ Other.mUpdateTimer } {
        Other.mUpdatePhase = FoliageUpdatePhase::Idle;
        Other.mUpdateMinCellX = 0;
        Other.mUpdateMaxCellX = 0;
        Other.mUpdateMinCellZ = 0;
        Other.mUpdateMaxCellZ = 0;
        Other.mUpdateCurrentCellX = 0;
        Other.mUpdateCurrentCellZ = 0;
        Other.mUpdateCandidateIndex = 0ULL;
        Other.mUpdateSlotIndex = 0ULL;
        Other.mInitialized = false;
        Other.mValid = false;
        Other.mHasUpdatedOnce = false;
        Other.mUpdateTimer = 0.0f;
    }

    ProceduralFoliageRuntime& ProceduralFoliageRuntime::operator=(ProceduralFoliageRuntime&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mConfigPath = std::move(Other.mConfigPath);
        mConfig = std::move(Other.mConfig);
        mRules = std::move(Other.mRules);
        mSlots = std::move(Other.mSlots);
        mSlotByKey = std::move(Other.mSlotByKey);
        mUpdateCandidates = std::move(Other.mUpdateCandidates);
        mUpdateSlotByKey = std::move(Other.mUpdateSlotByKey);
        mUpdateFocusPosition = Other.mUpdateFocusPosition;
        mUpdatePhase = Other.mUpdatePhase;
        mUpdateMinCellX = Other.mUpdateMinCellX;
        mUpdateMaxCellX = Other.mUpdateMaxCellX;
        mUpdateMinCellZ = Other.mUpdateMinCellZ;
        mUpdateMaxCellZ = Other.mUpdateMaxCellZ;
        mUpdateCurrentCellX = Other.mUpdateCurrentCellX;
        mUpdateCurrentCellZ = Other.mUpdateCurrentCellZ;
        mUpdateCandidateIndex = Other.mUpdateCandidateIndex;
        mUpdateSlotIndex = Other.mUpdateSlotIndex;
        mInitialized = Other.mInitialized;
        mValid = Other.mValid;
        mHasUpdatedOnce = Other.mHasUpdatedOnce;
        mUpdateTimer = Other.mUpdateTimer;
        Other.mUpdatePhase = FoliageUpdatePhase::Idle;
        Other.mUpdateMinCellX = 0;
        Other.mUpdateMaxCellX = 0;
        Other.mUpdateMinCellZ = 0;
        Other.mUpdateMaxCellZ = 0;
        Other.mUpdateCurrentCellX = 0;
        Other.mUpdateCurrentCellZ = 0;
        Other.mUpdateCandidateIndex = 0ULL;
        Other.mUpdateSlotIndex = 0ULL;
        Other.mInitialized = false;
        Other.mValid = false;
        Other.mHasUpdatedOnce = false;
        Other.mUpdateTimer = 0.0f;
        return *this;
    }

    bool ProceduralFoliageRuntime::Initialize(FrameContext& Ctx) {
        if (mInitialized == true) {
            return mValid;
        }

        mInitialized = true;
        try {
            mConfig = LoadFoliagePlacementConfig(mConfigPath);
        }
        catch (const std::exception& Exception) {
            ErrorHandler::report("ProceduralFoliageSystem", Exception.what(), ErrorHandler::Level::Warning);
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
            RuntimeRule.mLods.reserve(RuleDesc.mLods.size());
            for (const FoliagePlacementLodDesc& LodDesc : RuleDesc.mLods) {
                FoliageRuntimeLod RuntimeLod{};
                RuntimeLod.mModel = Ctx.AssetRegistryResource->GetModel(LodDesc.mModelPath);
                RuntimeLod.mMaximumDistance = LodDesc.mMaximumDistance;
                if (RuntimeLod.mModel == nullptr) {
                    continue;
                }

                RuntimeRule.mLods.push_back(std::move(RuntimeLod));
            }

            if (RuntimeRule.mLods.empty() == true) {
                continue;
            }

            if (RuleDesc.mMaterialPath.empty() == false) {
                const bool IsLoaded{ Ctx.AssetRegistryResource->LoadMaterialGroups(RuleDesc.mMaterialPath) };
                if (IsLoaded == true) {
                    const std::uint32_t MaterialGroupIndex{ Ctx.AssetRegistryResource->FindMaterialGroupIndexBySourcePath(RuleDesc.mMaterialPath) };
                    RuntimeRule.mMaterialGroupIndex = MaterialGroupIndex == static_cast<std::uint32_t>(-1) ? 0u : MaterialGroupIndex;
                }
            }

            mRules.push_back(std::move(RuntimeRule));
        }

        mValid = mRules.empty() == false;
        return mValid;
    }

    void ProceduralFoliageRuntime::Update(Arche::World& World, FrameContext& Ctx, float Dt) {
        if (Initialize(Ctx) == false) {
            return;
        }

        if (mUpdatePhase == FoliageUpdatePhase::Idle) {
            mUpdateTimer += Dt;
            if (mHasUpdatedOnce == true && mUpdateTimer < mConfig.mUpdateInterval) {
                return;
            }

            SimpleMath::Vector3 FocusPosition{};
            if (TryResolveFocusPosition(World, FocusPosition) == false) {
                return;
            }

            TerrainSamplingContext TerrainContext{};
            if (TryResolveTerrainSamplingContext(World, TerrainContext) == false) {
                return;
            }

            mUpdateTimer = 0.0f;
            mHasUpdatedOnce = true;
            BeginFoliageUpdate(FocusPosition);
            ProcessFoliageUpdateBatch(World, Ctx, TerrainContext);
            return;
        }

        TerrainSamplingContext TerrainContext{};
        if (TryResolveTerrainSamplingContext(World, TerrainContext) == false) {
            return;
        }

        ProcessFoliageUpdateBatch(World, Ctx, TerrainContext);
    }

    void ProceduralFoliageRuntime::BeginFoliageUpdate(const SimpleMath::Vector3& FocusPosition) {
        for (FoliageSlot& Slot : mSlots) {
            Slot.mAssignedThisFrame = false;
        }

        const float Radius{ mConfig.mPlacementRadius };
        const float CellSize{ mConfig.mCellSize };
        mUpdateCandidates.clear();
        mUpdateSlotByKey.clear();
        mUpdateFocusPosition = FocusPosition;
        mUpdateMinCellX = static_cast<std::int32_t>(std::floor((FocusPosition.x - Radius) / CellSize));
        mUpdateMaxCellX = static_cast<std::int32_t>(std::floor((FocusPosition.x + Radius) / CellSize));
        mUpdateMinCellZ = static_cast<std::int32_t>(std::floor((FocusPosition.z - Radius) / CellSize));
        mUpdateMaxCellZ = static_cast<std::int32_t>(std::floor((FocusPosition.z + Radius) / CellSize));
        mUpdateCurrentCellX = mUpdateMinCellX;
        mUpdateCurrentCellZ = mUpdateMinCellZ;
        mUpdateCandidateIndex = 0ULL;
        mUpdateSlotIndex = 0ULL;
        mUpdatePhase = FoliageUpdatePhase::BuildCandidates;
    }

    void ProceduralFoliageRuntime::ProcessFoliageUpdateBatch(Arche::World& World, FrameContext& Ctx, const TerrainSamplingContext& TerrainContext) {
        if (mUpdatePhase == FoliageUpdatePhase::BuildCandidates) {
            BuildCandidateBatch(TerrainContext);
            return;
        }

        if (mUpdatePhase == FoliageUpdatePhase::ApplyCandidates) {
            ApplyCandidateBatch(World, Ctx);
            return;
        }

        if (mUpdatePhase == FoliageUpdatePhase::RebuildSlotLookup) {
            RebuildSlotLookupBatch(World);
        }
    }

    void ProceduralFoliageRuntime::BuildCandidateBatch(const TerrainSamplingContext& TerrainContext) {
        const float RadiusSquared{ mConfig.mPlacementRadius * mConfig.mPlacementRadius };
        std::uint32_t ProcessedCellCount{};
        while (mUpdateCurrentCellZ <= mUpdateMaxCellZ && ProcessedCellCount < mConfig.mUpdateCellBatchSize) {
            for (std::uint32_t RuleIndex{ 0u }; RuleIndex < mRules.size(); ++RuleIndex) {
                const FoliagePlacementRule& Rule{ mRules[RuleIndex].mDesc };
                for (std::uint32_t InstanceIndex{ 0u }; InstanceIndex < Rule.mInstancesPerCell; ++InstanceIndex) {
                    FoliageCandidate Candidate{};
                    const bool IsCandidateCreated{ TryCreateCandidate(TerrainContext, mUpdateFocusPosition, RuleIndex, mUpdateCurrentCellX, mUpdateCurrentCellZ, InstanceIndex, Candidate) };
                    if (IsCandidateCreated == false || IsCandidateInPlacementRadius(mUpdateFocusPosition, Candidate.mPosition.x, Candidate.mPosition.z, RadiusSquared) == false) {
                        continue;
                    }

                    mUpdateCandidates.push_back(Candidate);
                }
            }

            ProcessedCellCount += 1u;
            mUpdateCurrentCellX += 1;
            if (mUpdateCurrentCellX > mUpdateMaxCellX) {
                mUpdateCurrentCellX = mUpdateMinCellX;
                mUpdateCurrentCellZ += 1;
            }
        }

        if (mUpdateCurrentCellZ <= mUpdateMaxCellZ) {
            return;
        }

        const std::uint32_t TerrainSeed{ TerrainContext.mResource == nullptr ? 0u : TerrainContext.mResource->GetBuildDesc().mProceduralHeightFieldDesc.mSeed };
        mUpdateCandidates = FilterCandidatesByMinimumSpacing(std::move(mUpdateCandidates), mRules, mConfig, TerrainSeed);
        mUpdateCandidateIndex = 0ULL;
        mUpdatePhase = FoliageUpdatePhase::ApplyCandidates;
    }

    void ProceduralFoliageRuntime::ApplyCandidateBatch(Arche::World& World, FrameContext& Ctx) {
        std::uint32_t ProcessedCandidateCount{};
        while (mUpdateCandidateIndex < mUpdateCandidates.size() && ProcessedCandidateCount < mConfig.mUpdateCandidateBatchSize) {
            const FoliageCandidate& Candidate{ mUpdateCandidates[mUpdateCandidateIndex] };
            const std::unordered_map<FoliageCandidateKey, std::size_t, FoliageCandidateKeyHasher>::const_iterator SlotIter{ mSlotByKey.find(Candidate.mKey) };
            if (SlotIter != mSlotByKey.end() && SlotIter->second < mSlots.size() && mSlots[SlotIter->second].mRuleIndex == Candidate.mKey.mRuleIndex) {
                ApplyFoliageCandidateToSlot(World, mSlots[SlotIter->second], Candidate);
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

            ApplyFoliageCandidateToSlot(World, mSlots[SlotIndex], Candidate);
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

    void ProceduralFoliageRuntime::RebuildSlotLookupBatch(Arche::World& World) {
        std::uint32_t ProcessedSlotCount{};
        while (mUpdateSlotIndex < mSlots.size() && ProcessedSlotCount < mConfig.mUpdateSlotBatchSize) {
            FoliageSlot& Slot{ mSlots[mUpdateSlotIndex] };
            if (Slot.mAssignedThisFrame == false) {
                DeactivateFoliageSlot(World, Slot);
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

    bool ProceduralFoliageRuntime::TryCreateCandidate(const TerrainSamplingContext& TerrainContext, const SimpleMath::Vector3& FocusPosition, std::uint32_t RuleIndex, std::int32_t CellX, std::int32_t CellZ, std::uint32_t InstanceIndex, FoliageCandidate& OutCandidate) const {
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
        const float ForestFactor{ SampleForestAreaFactor(TerrainSeed, mConfig, WorldX, WorldZ) };
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
        OutCandidate.mLodIndex = ResolveFoliageLodIndex(Rule, FocusPosition, WorldX, WorldZ);
        return true;
    }

    std::size_t ProceduralFoliageRuntime::FindReusableSlot(std::uint32_t RuleIndex) const {
        for (std::size_t SlotIndex{ 0ULL }; SlotIndex < mSlots.size(); ++SlotIndex) {
            const FoliageSlot& Slot{ mSlots[SlotIndex] };
            if (Slot.mRuleIndex == RuleIndex && Slot.mActive == false && Slot.mAssignedThisFrame == false) {
                return SlotIndex;
            }
        }

        return std::numeric_limits<std::size_t>::max();
    }

    bool ProceduralFoliageRuntime::CreateSlot(Arche::World& World, FrameContext& Ctx, std::uint32_t RuleIndex, std::size_t& OutSlotIndex) {
        if (RuleIndex >= mRules.size()) {
            return false;
        }

        FoliageSlot Slot{};
        const bool IsCreated{ CreateFoliageSlotEntities(World, Ctx.PhysicsWorldResource, mConfig, mRules[RuleIndex], RuleIndex, Slot) };
        if (IsCreated == false) {
            return false;
        }

        OutSlotIndex = mSlots.size();
        mSlots.push_back(std::move(Slot));
        return true;
    }

    ProceduralFoliageSystem::ProceduralFoliageSystem()
        : mName{ "ProceduralFoliageSystem" },
        mConfigPath{ "Resources/DefaultScene/FoliagePlacement.yaml" },
        mRuntime{} {
    }

    ProceduralFoliageSystem::~ProceduralFoliageSystem() {
    }

    ProceduralFoliageSystem::ProceduralFoliageSystem(const ProceduralFoliageSystem& Other)
        : mName{ Other.mName },
        mConfigPath{ Other.mConfigPath },
        mRuntime{} {
    }

    ProceduralFoliageSystem& ProceduralFoliageSystem::operator=(const ProceduralFoliageSystem& Other) {
        if (this == &Other) {
            return *this;
        }

        mName = Other.mName;
        mConfigPath = Other.mConfigPath;
        mRuntime.reset();
        return *this;
    }

    ProceduralFoliageSystem::ProceduralFoliageSystem(ProceduralFoliageSystem&& Other) noexcept
        : mName{ std::move(Other.mName) },
        mConfigPath{ std::move(Other.mConfigPath) },
        mRuntime{ std::move(Other.mRuntime) } {
    }

    ProceduralFoliageSystem& ProceduralFoliageSystem::operator=(ProceduralFoliageSystem&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mName = std::move(Other.mName);
        mConfigPath = std::move(Other.mConfigPath);
        mRuntime = std::move(Other.mRuntime);
        return *this;
    }

    void ProceduralFoliageSystem::SetConfigPath(const std::string& ConfigPath) {
        mConfigPath = ConfigPath;
        mRuntime.reset();
    }

    const std::string& ProceduralFoliageSystem::Name() const {
        return mName;
    }

    Phase ProceduralFoliageSystem::GetPhase() const {
        return Phase::PhysicsActorUpdate;
    }

    std::span<const ComponentAccess> ProceduralFoliageSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 9> Accesses{ { { typeid(Game::Transform), Access::Write }, { typeid(Game::TerrainRenderer), Access::Read }, { typeid(Game::Camera), Access::Read }, { typeid(Game::EntityHierarchy), Access::Write }, { typeid(Game::StaticMeshRenderer), Access::Write }, { typeid(Game::Material), Access::Write }, { typeid(Game::Culling), Access::Write }, { typeid(Game::BoundingBox), Access::Write }, { typeid(Game::PhysicsActor), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> ProceduralFoliageSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 2> Accesses{ { { typeid(AssetRegistry), Access::Write }, { typeid(IPhysicsWorld), Access::Write } } };
        return Accesses;
    }

    void ProceduralFoliageSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        if (mRuntime == nullptr) {
            mRuntime = std::make_unique<ProceduralFoliageRuntime>(mConfigPath);
        }

        mRuntime->Update(World, Ctx, Dt);
    }
}
