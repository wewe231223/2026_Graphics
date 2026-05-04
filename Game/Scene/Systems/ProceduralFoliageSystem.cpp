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
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Utility/ErrorHandler.h"

namespace {
    constexpr float FoliageInactiveHeight{ -100000.0f };
    constexpr float FoliageEpsilon{ 0.0001f };
    constexpr std::uint32_t FoliageHashOffset{ 2166136261u };
    constexpr std::uint32_t FoliageHashPrime{ 16777619u };

    struct FoliagePlacementRule final {
    public:
        std::string mName{};
        std::string mLayerName{};
        std::string mModelPath{};
        std::string mMaterialPath{};
        std::uint32_t mLayerIndex{ 0u };
        std::uint32_t mInstancesPerCell{ 1u };
        float mDensityMultiplier{ 1.0f };
        float mSpawnChance{ 1.0f };
        float mMinimumWeight{ 0.35f };
        float mMinimumScale{ 0.85f };
        float mMaximumScale{ 1.25f };
        float mMinimumYawDegrees{ 0.0f };
        float mMaximumYawDegrees{ 360.0f };
        float mClusterStrength{ 0.0f };
        float mClusterScale{ 96.0f };
        float mClusterContrast{ 1.0f };
        float mClusterCoverage{ 1.0f };
        float mClusterEdgeSoftness{ 0.12f };
        float mClusterOutsideDensity{ 0.0f };
        float mOffsetY{ 0.0f };
    };

    struct FoliagePlacementConfig final {
    public:
        std::vector<FoliagePlacementRule> mRules{};
        bool mEnabled{ true };
        float mPlacementRadius{ 220.0f };
        float mCellSize{ 28.0f };
        float mUpdateInterval{ 0.15f };
        float mDensityMultiplier{ 1.0f };
        std::uint32_t mSeedSalt{ 64123u };
    };

    struct FoliageRuntimeRule final {
    public:
        FoliagePlacementRule mDesc{};
        std::shared_ptr<Game::Model> mModel{};
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
    };

    struct FoliageSlot final {
    public:
        FoliageCandidateKey mKey{};
        Arche::EntityID mRootEntityId{ Arche::NullEntityID };
        std::vector<Arche::EntityID> mRenderEntityIds{};
        std::uint32_t mRuleIndex{ 0u };
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

    float SampleClusterCorner(std::uint32_t TerrainSeed, std::uint32_t Salt, std::int32_t GridX, std::int32_t GridZ, std::uint32_t ClusterIndex) {
        return HashToUnitFloat(BuildClusterHash(TerrainSeed, Salt, GridX, GridZ, ClusterIndex, 71u));
    }

    float SampleValueNoise01(std::uint32_t TerrainSeed, std::uint32_t Salt, float X, float Z, std::uint32_t ClusterIndex) {
        const std::int32_t X0{ static_cast<std::int32_t>(std::floor(X)) };
        const std::int32_t Z0{ static_cast<std::int32_t>(std::floor(Z)) };
        const std::int32_t X1{ X0 + 1 };
        const std::int32_t Z1{ Z0 + 1 };
        const float BlendX{ SmoothStep01(X - static_cast<float>(X0)) };
        const float BlendZ{ SmoothStep01(Z - static_cast<float>(Z0)) };
        const float Value00{ SampleClusterCorner(TerrainSeed, Salt, X0, Z0, ClusterIndex) };
        const float Value10{ SampleClusterCorner(TerrainSeed, Salt, X1, Z0, ClusterIndex) };
        const float Value01{ SampleClusterCorner(TerrainSeed, Salt, X0, Z1, ClusterIndex) };
        const float Value11{ SampleClusterCorner(TerrainSeed, Salt, X1, Z1, ClusterIndex) };
        const float ValueX0{ Lerp(Value00, Value10, BlendX) };
        const float ValueX1{ Lerp(Value01, Value11, BlendX) };
        return Lerp(ValueX0, ValueX1, BlendZ);
    }

    float SampleFoliageClusterFactor(std::uint32_t TerrainSeed, std::uint32_t Salt, const FoliagePlacementRule& Rule, std::uint32_t ClusterIndex, float WorldX, float WorldZ) {
        if (Rule.mClusterStrength <= 0.0f) {
            return 1.0f;
        }

        const float ClusterScale{ (std::max)(Rule.mClusterScale, 1.0f) };
        const float NoiseValue{ SampleValueNoise01(TerrainSeed, Salt, WorldX / ClusterScale, WorldZ / ClusterScale, ClusterIndex) };
        const float ContrastedValue{ std::clamp(((NoiseValue - 0.5f) * Rule.mClusterContrast) + 0.5f, 0.0f, 1.0f) };
        const float Coverage{ std::clamp(Rule.mClusterCoverage, 0.01f, 1.0f) };
        const float Threshold{ 1.0f - Coverage };
        const float EdgeSoftness{ (std::max)(Rule.mClusterEdgeSoftness, 0.0f) };
        const float ClusterMask{ SmoothStepRange(Threshold - EdgeSoftness, Threshold + EdgeSoftness, ContrastedValue) };
        const float ClusterBoost{ std::min(1.0f / Coverage, 2.5f) };
        const float ClusterDensity{ Lerp(Rule.mClusterOutsideDensity, ClusterBoost, ClusterMask) };
        return std::clamp(Lerp(1.0f, ClusterDensity, Rule.mClusterStrength), 0.0f, 2.5f);
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

    FoliagePlacementRule ReadFoliageRule(c4::yml::ConstNodeRef RuleNode, const std::string& ConfigPath) {
        if (RuleNode.readable() == false || RuleNode.is_map() == false) {
            throw std::runtime_error{ "Procedural foliage rule must be a map." };
        }

        FoliagePlacementRule Rule{};
        ReadStringChild(RuleNode, "Name", Rule.mName);
        ReadStringChild(RuleNode, "Layer", Rule.mLayerName);
        ReadStringChild(RuleNode, "LayerName", Rule.mLayerName);
        ReadUInt32Child(RuleNode, "LayerIndex", Rule.mLayerIndex);
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
        Rule.mModelPath = ResolveFoliageResourcePath(ConfigPath, Rule.mModelPath);
        Rule.mMaterialPath = ResolveFoliageResourcePath(ConfigPath, Rule.mMaterialPath);

        if (Rule.mModelPath.empty() == true) {
            throw std::runtime_error{ "Procedural foliage rule model path is empty." };
        }

        if (Rule.mName.empty() == true) {
            Rule.mName = Rule.mModelPath;
        }

        Rule.mInstancesPerCell = (std::max)(Rule.mInstancesPerCell, 1u);
        Rule.mDensityMultiplier = (std::max)(Rule.mDensityMultiplier, 0.0f);
        Rule.mSpawnChance = std::clamp(Rule.mSpawnChance, 0.0f, 1.0f);
        Rule.mMinimumWeight = std::clamp(Rule.mMinimumWeight, 0.0f, 1.0f);
        Rule.mClusterStrength = std::clamp(Rule.mClusterStrength, 0.0f, 1.0f);
        Rule.mClusterScale = (std::max)(Rule.mClusterScale, 1.0f);
        Rule.mClusterContrast = (std::max)(Rule.mClusterContrast, 0.0f);
        Rule.mClusterCoverage = std::clamp(Rule.mClusterCoverage, 0.01f, 1.0f);
        Rule.mClusterEdgeSoftness = (std::max)(Rule.mClusterEdgeSoftness, 0.0f);
        Rule.mClusterOutsideDensity = std::clamp(Rule.mClusterOutsideDensity, 0.0f, 1.0f);
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
        ReadFloatChild(ConfigNode, "PlacementRadius", Config.mPlacementRadius);
        ReadFloatChild(ConfigNode, "CellSize", Config.mCellSize);
        ReadFloatChild(ConfigNode, "UpdateInterval", Config.mUpdateInterval);
        ReadFloatChild(ConfigNode, "Density", Config.mDensityMultiplier);
        ReadFloatChild(ConfigNode, "DensityMultiplier", Config.mDensityMultiplier);
        ReadUInt32Child(ConfigNode, "SeedSalt", Config.mSeedSalt);
        Config.mPlacementRadius = (std::max)(Config.mPlacementRadius, 1.0f);
        Config.mCellSize = (std::max)(Config.mCellSize, 1.0f);
        Config.mUpdateInterval = (std::max)(Config.mUpdateInterval, 0.0f);
        Config.mDensityMultiplier = (std::max)(Config.mDensityMultiplier, 0.0f);

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
        const std::uint32_t X1{ (std::min)(X0 + 1u, Field.Width - 1u) };
        const std::uint32_t Z1{ (std::min)(Z0 + 1u, Field.Height - 1u) };
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
        const std::uint32_t X1{ (std::min)(X0 + 1u, SplatMap.Width - 1u) };
        const std::uint32_t Z1{ (std::min)(Z0 + 1u, SplatMap.Height - 1u) };
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

    std::uint32_t ResolveLayerIndex(const FoliagePlacementRule& Rule, const Game::TerrainBuildDesc& BuildDesc) {
        if (Rule.mLayerName.empty() == false) {
            const std::vector<Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapLayerDesc>& Layers{ BuildDesc.mProceduralHeightFieldDesc.mSplatMapDesc.mLayers };
            for (std::size_t LayerIndex{ 0ULL }; LayerIndex < Layers.size(); ++LayerIndex) {
                if (Layers[LayerIndex].mName == Rule.mLayerName) {
                    return static_cast<std::uint32_t>(LayerIndex);
                }
            }
        }

        return Rule.mLayerIndex;
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

        const std::uint32_t LayerIndex{ ResolveLayerIndex(Rule, TerrainContext.mResource->GetBuildDesc()) };
        if (LayerIndex >= 4u) {
            return false;
        }

        const float Height01{ SampleHeight01(HeightField, GridX, GridZ) };
        const asset::Vec4 SplatWeights{ SampleSplatWeight(SplatMap, GridX, GridZ, HeightField) };
        OutWorldY = TerrainContext.mTransform.position.y + (Height01 * TerrainContext.mResource->GetMaxHeight() * ScaleY);
        OutLayerWeight = std::clamp(GetSplatLayerWeight(SplatWeights, LayerIndex), 0.0f, 1.0f);
        return true;
    }

    bool IsCandidateInPlacementRadius(const SimpleMath::Vector3& FocusPosition, float WorldX, float WorldZ, float RadiusSquared) {
        const float DistanceX{ WorldX - FocusPosition.x };
        const float DistanceZ{ WorldZ - FocusPosition.z };
        return ((DistanceX * DistanceX) + (DistanceZ * DistanceZ)) <= RadiusSquared;
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

    bool CreateFoliageSlotEntities(Arche::World& World, const FoliageRuntimeRule& Rule, std::uint32_t RuleIndex, FoliageSlot& OutSlot) {
        if (Rule.mModel == nullptr) {
            return false;
        }

        const std::vector<Game::ModelNode>& ModelNodes{ Rule.mModel->GetNodes() };
        const Game::ModelNode* RootNode{ Rule.mModel->GetRootNode() };
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
            TransformComponent.position = NodeIndex == RootNodeIndex ? SimpleMath::Vector3{ 0.0f, FoliageInactiveHeight, 0.0f } : SimpleMath::Vector3::Zero;
            TransformComponent.nodeToParent = Node.GetNodeToParent();
            World.AddComponent(NodeEntities[NodeIndex], TransformComponent);

            if (Node.GetSubMeshes().empty() == false && Node.IsSkinnedMesh() == false) {
                Game::StaticMeshRenderer Renderer{};
                Renderer.model = Rule.mModel.get();
                Renderer.nodeIndex = static_cast<std::uint32_t>(NodeIndex);
                Renderer.active = false;
                World.AddComponent(NodeEntities[NodeIndex], Renderer);

                Game::Material MaterialComponent{};
                MaterialComponent.MaterialGroupIndex = Rule.mMaterialGroupIndex;
                World.AddComponent(NodeEntities[NodeIndex], MaterialComponent);

                Game::Culling CullingComponent{};
                CullingComponent.frustumCulling = true;
                World.AddComponent(NodeEntities[NodeIndex], CullingComponent);

                Game::BoundingBox BoundingBoxComponent{};
                BoundingBoxComponent.UpdateFromModel(Rule.mModel.get(), static_cast<std::uint32_t>(NodeIndex));
                World.AddComponent(NodeEntities[NodeIndex], BoundingBoxComponent);
                OutSlot.mRenderEntityIds.push_back(NodeEntities[NodeIndex]);
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

        OutSlot.mRootEntityId = NodeEntities[RootNodeIndex];
        OutSlot.mRuleIndex = RuleIndex;
        return OutSlot.mRootEntityId != Arche::NullEntityID && OutSlot.mRenderEntityIds.empty() == false;
    }

    void SetFoliageSlotActive(Arche::World& World, FoliageSlot& Slot, bool IsActive) {
        for (Arche::EntityID RenderEntityId : Slot.mRenderEntityIds) {
            Game::StaticMeshRenderer* Renderer{ World.GetComponent<Game::StaticMeshRenderer>(RenderEntityId) };
            if (Renderer == nullptr) {
                continue;
            }

            Renderer->active = IsActive;
        }

        Slot.mActive = IsActive;
    }

    void ApplyFoliageCandidateToSlot(Arche::World& World, FoliageSlot& Slot, const FoliageCandidate& Candidate) {
        Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(Slot.mRootEntityId) };
        if (TransformComponent == nullptr) {
            return;
        }

        TransformComponent->position = Candidate.mPosition;
        TransformComponent->rotationEuler = SimpleMath::Vector3{ 0.0f, Candidate.mYawRadians, 0.0f };
        TransformComponent->UpdateRotationFromEulerRadians();
        TransformComponent->scale = SimpleMath::Vector3{ Candidate.mScale, Candidate.mScale, Candidate.mScale };
        Slot.mKey = Candidate.mKey;
        Slot.mAssignedThisFrame = true;
        SetFoliageSlotActive(World, Slot, true);
    }

    void DeactivateFoliageSlot(Arche::World& World, FoliageSlot& Slot) {
        if (Slot.mActive == false) {
            return;
        }

        Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(Slot.mRootEntityId) };
        if (TransformComponent != nullptr) {
            TransformComponent->position.y = FoliageInactiveHeight;
        }

        SetFoliageSlotActive(World, Slot, false);
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
        std::vector<FoliageCandidate> BuildCandidates(const TerrainSamplingContext& TerrainContext, const SimpleMath::Vector3& FocusPosition) const;
        bool TryCreateCandidate(const TerrainSamplingContext& TerrainContext, const SimpleMath::Vector3& FocusPosition, std::uint32_t RuleIndex, std::int32_t CellX, std::int32_t CellZ, std::uint32_t InstanceIndex, FoliageCandidate& OutCandidate) const;
        std::size_t FindReusableSlot(std::uint32_t RuleIndex) const;
        bool CreateSlot(Arche::World& World, std::uint32_t RuleIndex, std::size_t& OutSlotIndex);
        void RebuildSlotLookup(Arche::World& World);

    private:
        std::string mConfigPath{};
        FoliagePlacementConfig mConfig{};
        std::vector<FoliageRuntimeRule> mRules{};
        std::vector<FoliageSlot> mSlots{};
        std::unordered_map<FoliageCandidateKey, std::size_t, FoliageCandidateKeyHasher> mSlotByKey{};
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
        mInitialized{ Other.mInitialized },
        mValid{ Other.mValid },
        mHasUpdatedOnce{ Other.mHasUpdatedOnce },
        mUpdateTimer{ Other.mUpdateTimer } {
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
        mInitialized = Other.mInitialized;
        mValid = Other.mValid;
        mHasUpdatedOnce = Other.mHasUpdatedOnce;
        mUpdateTimer = Other.mUpdateTimer;
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
            RuntimeRule.mModel = Ctx.AssetRegistryResource->GetModel(RuleDesc.mModelPath);
            if (RuntimeRule.mModel == nullptr) {
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

        mUpdateTimer += Dt;
        if (mHasUpdatedOnce == true && mUpdateTimer < mConfig.mUpdateInterval) {
            return;
        }

        mUpdateTimer = 0.0f;
        mHasUpdatedOnce = true;

        SimpleMath::Vector3 FocusPosition{};
        if (TryResolveFocusPosition(World, FocusPosition) == false) {
            return;
        }

        TerrainSamplingContext TerrainContext{};
        if (TryResolveTerrainSamplingContext(World, TerrainContext) == false) {
            return;
        }

        for (FoliageSlot& Slot : mSlots) {
            Slot.mAssignedThisFrame = false;
        }

        const std::vector<FoliageCandidate> Candidates{ BuildCandidates(TerrainContext, FocusPosition) };
        for (const FoliageCandidate& Candidate : Candidates) {
            const std::unordered_map<FoliageCandidateKey, std::size_t, FoliageCandidateKeyHasher>::const_iterator SlotIter{ mSlotByKey.find(Candidate.mKey) };
            if (SlotIter != mSlotByKey.end() && SlotIter->second < mSlots.size() && mSlots[SlotIter->second].mRuleIndex == Candidate.mKey.mRuleIndex) {
                ApplyFoliageCandidateToSlot(World, mSlots[SlotIter->second], Candidate);
            }
        }

        for (const FoliageCandidate& Candidate : Candidates) {
            const std::unordered_map<FoliageCandidateKey, std::size_t, FoliageCandidateKeyHasher>::const_iterator SlotIter{ mSlotByKey.find(Candidate.mKey) };
            if (SlotIter != mSlotByKey.end() && SlotIter->second < mSlots.size() && mSlots[SlotIter->second].mAssignedThisFrame == true) {
                continue;
            }

            std::size_t SlotIndex{ FindReusableSlot(Candidate.mKey.mRuleIndex) };
            if (SlotIndex == std::numeric_limits<std::size_t>::max()) {
                const bool IsCreated{ CreateSlot(World, Candidate.mKey.mRuleIndex, SlotIndex) };
                if (IsCreated == false) {
                    continue;
                }
            }

            ApplyFoliageCandidateToSlot(World, mSlots[SlotIndex], Candidate);
        }

        RebuildSlotLookup(World);
    }

    std::vector<FoliageCandidate> ProceduralFoliageRuntime::BuildCandidates(const TerrainSamplingContext& TerrainContext, const SimpleMath::Vector3& FocusPosition) const {
        std::vector<FoliageCandidate> Candidates{};
        const float Radius{ mConfig.mPlacementRadius };
        const float RadiusSquared{ Radius * Radius };
        const float CellSize{ mConfig.mCellSize };
        const std::int32_t MinCellX{ static_cast<std::int32_t>(std::floor((FocusPosition.x - Radius) / CellSize)) };
        const std::int32_t MaxCellX{ static_cast<std::int32_t>(std::floor((FocusPosition.x + Radius) / CellSize)) };
        const std::int32_t MinCellZ{ static_cast<std::int32_t>(std::floor((FocusPosition.z - Radius) / CellSize)) };
        const std::int32_t MaxCellZ{ static_cast<std::int32_t>(std::floor((FocusPosition.z + Radius) / CellSize)) };

        for (std::int32_t CellZ{ MinCellZ }; CellZ <= MaxCellZ; ++CellZ) {
            for (std::int32_t CellX{ MinCellX }; CellX <= MaxCellX; ++CellX) {
                for (std::uint32_t RuleIndex{ 0u }; RuleIndex < mRules.size(); ++RuleIndex) {
                    const FoliagePlacementRule& Rule{ mRules[RuleIndex].mDesc };
                    for (std::uint32_t InstanceIndex{ 0u }; InstanceIndex < Rule.mInstancesPerCell; ++InstanceIndex) {
                        FoliageCandidate Candidate{};
                        const bool IsCandidateCreated{ TryCreateCandidate(TerrainContext, FocusPosition, RuleIndex, CellX, CellZ, InstanceIndex, Candidate) };
                        if (IsCandidateCreated == false || IsCandidateInPlacementRadius(FocusPosition, Candidate.mPosition.x, Candidate.mPosition.z, RadiusSquared) == false) {
                            continue;
                        }

                        Candidates.push_back(Candidate);
                    }
                }
            }
        }

        return Candidates;
    }

    bool ProceduralFoliageRuntime::TryCreateCandidate(const TerrainSamplingContext& TerrainContext, const SimpleMath::Vector3& FocusPosition, std::uint32_t RuleIndex, std::int32_t CellX, std::int32_t CellZ, std::uint32_t InstanceIndex, FoliageCandidate& OutCandidate) const {
        (void)FocusPosition;
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
        const float RandomX{ HashToUnitFloat(BuildCandidateHash(TerrainSeed, mConfig.mSeedSalt, Key, 11u)) };
        const float RandomZ{ HashToUnitFloat(BuildCandidateHash(TerrainSeed, mConfig.mSeedSalt, Key, 23u)) };
        const float RandomChance{ HashToUnitFloat(BuildCandidateHash(TerrainSeed, mConfig.mSeedSalt, Key, 37u)) };
        const float WorldX{ (static_cast<float>(CellX) + RandomX) * mConfig.mCellSize };
        const float WorldZ{ (static_cast<float>(CellZ) + RandomZ) * mConfig.mCellSize };
        const std::uint32_t ClusterIndex{ ResolveLayerIndex(Rule, TerrainContext.mResource->GetBuildDesc()) };
        float WorldY{};
        float LayerWeight{};
        const bool HasTerrainSample{ TrySampleTerrain(TerrainContext, Rule, WorldX, WorldZ, WorldY, LayerWeight) };
        const float ClusterFactor{ SampleFoliageClusterFactor(TerrainSeed, mConfig.mSeedSalt, Rule, ClusterIndex, WorldX, WorldZ) };
        const float EffectiveSpawnChance{ std::clamp(Rule.mSpawnChance * Rule.mDensityMultiplier * mConfig.mDensityMultiplier * LayerWeight * ClusterFactor, 0.0f, 1.0f) };
        if (HasTerrainSample == false || LayerWeight < Rule.mMinimumWeight || RandomChance > EffectiveSpawnChance) {
            return false;
        }

        const float RandomYaw{ HashToUnitFloat(BuildCandidateHash(TerrainSeed, mConfig.mSeedSalt, Key, 41u)) };
        const float RandomScale{ HashToUnitFloat(BuildCandidateHash(TerrainSeed, mConfig.mSeedSalt, Key, 53u)) };
        OutCandidate.mKey = Key;
        OutCandidate.mPosition = SimpleMath::Vector3{ WorldX, WorldY + Rule.mOffsetY, WorldZ };
        OutCandidate.mYawRadians = DirectX::XMConvertToRadians(Lerp(Rule.mMinimumYawDegrees, Rule.mMaximumYawDegrees, RandomYaw));
        OutCandidate.mScale = Lerp(Rule.mMinimumScale, Rule.mMaximumScale, RandomScale);
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

    bool ProceduralFoliageRuntime::CreateSlot(Arche::World& World, std::uint32_t RuleIndex, std::size_t& OutSlotIndex) {
        if (RuleIndex >= mRules.size()) {
            return false;
        }

        FoliageSlot Slot{};
        const bool IsCreated{ CreateFoliageSlotEntities(World, mRules[RuleIndex], RuleIndex, Slot) };
        if (IsCreated == false) {
            return false;
        }

        OutSlotIndex = mSlots.size();
        mSlots.push_back(std::move(Slot));
        return true;
    }

    void ProceduralFoliageRuntime::RebuildSlotLookup(Arche::World& World) {
        mSlotByKey.clear();
        for (std::size_t SlotIndex{ 0ULL }; SlotIndex < mSlots.size(); ++SlotIndex) {
            FoliageSlot& Slot{ mSlots[SlotIndex] };
            if (Slot.mAssignedThisFrame == false) {
                DeactivateFoliageSlot(World, Slot);
                continue;
            }

            mSlotByKey.insert_or_assign(Slot.mKey, SlotIndex);
        }
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
        static std::array<ComponentAccess, 8> Accesses{ { { typeid(Game::Transform), Access::Write }, { typeid(Game::TerrainRenderer), Access::Read }, { typeid(Game::Camera), Access::Read }, { typeid(Game::EntityHierarchy), Access::Write }, { typeid(Game::StaticMeshRenderer), Access::Write }, { typeid(Game::Material), Access::Write }, { typeid(Game::Culling), Access::Write }, { typeid(Game::BoundingBox), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> ProceduralFoliageSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 1> Accesses{ { { typeid(AssetRegistry), Access::Write } } };
        return Accesses;
    }

    void ProceduralFoliageSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        if (mRuntime == nullptr) {
            mRuntime = std::make_unique<ProceduralFoliageRuntime>(mConfigPath);
        }

        mRuntime->Update(World, Ctx, Dt);
    }
}
