#include "TerrainProceduralHeightFieldConfigLoader.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <ryml.hpp>
#include <ryml_std.hpp>

namespace {
    c4::yml::ConstNodeRef ResolveConfigNode(c4::yml::ConstNodeRef RootNode) {
        if (RootNode.readable() == false || RootNode.is_map() == false) {
            throw std::runtime_error{ "Procedural height field config root must be a map." };
        }

        if (RootNode.has_child("ProceduralHeightField") == true) {
            const c4::yml::ConstNodeRef ConfigNode{ RootNode["ProceduralHeightField"] };
            if (ConfigNode.readable() == false || ConfigNode.is_map() == false) {
                throw std::runtime_error{ "ProceduralHeightField config must be a map." };
            }

            return ConfigNode;
        }

        return RootNode;
    }

    void ReadUInt32Child(c4::yml::ConstNodeRef Node, const char* Key, std::uint32_t& OutValue) {
        if (Node.has_child(Key) == false) {
            return;
        }

        Node[Key] >> OutValue;
    }

    void ReadInt32Child(c4::yml::ConstNodeRef Node, const char* Key, std::int32_t& OutValue) {
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

    Game::TerrainProceduralHeightFieldDesc ReadConfig(c4::yml::ConstNodeRef ConfigNode) {
        Game::TerrainProceduralHeightFieldDesc Desc{};
        ReadUInt32Child(ConfigNode, "Width", Desc.mWidth);
        ReadUInt32Child(ConfigNode, "Height", Desc.mHeight);
        ReadUInt32Child(ConfigNode, "Seed", Desc.mSeed);
        ReadUInt32Child(ConfigNode, "OctaveCount", Desc.mOctaveCount);
        ReadFloatChild(ConfigNode, "NoiseScale", Desc.mNoiseScale);
        ReadFloatChild(ConfigNode, "Persistence", Desc.mPersistence);
        ReadFloatChild(ConfigNode, "Lacunarity", Desc.mLacunarity);
        ReadFloatChild(ConfigNode, "BaseHeight", Desc.mBaseHeight);
        ReadFloatChild(ConfigNode, "HeightAmplitude", Desc.mHeightAmplitude);
        ReadUInt32Child(ConfigNode, "SmoothingPassCount", Desc.mSmoothingPassCount);
        ReadUInt32Child(ConfigNode, "MinimumWidth", Desc.mMinimumWidth);
        ReadUInt32Child(ConfigNode, "MinimumHeight", Desc.mMinimumHeight);
        ReadUInt32Child(ConfigNode, "MaximumOctaveCount", Desc.mMaximumOctaveCount);
        ReadUInt32Child(ConfigNode, "MaximumSmoothingPassCount", Desc.mMaximumSmoothingPassCount);
        ReadFloatChild(ConfigNode, "MinimumHeightValue", Desc.mMinimumHeightValue);
        ReadFloatChild(ConfigNode, "MaximumHeightValue", Desc.mMaximumHeightValue);
        ReadFloatChild(ConfigNode, "SampleScaleX", Desc.mSampleScaleX);
        ReadFloatChild(ConfigNode, "SampleScaleZ", Desc.mSampleScaleZ);
        ReadInt32Child(ConfigNode, "SampleOffsetX", Desc.mSampleOffsetX);
        ReadInt32Child(ConfigNode, "SampleOffsetZ", Desc.mSampleOffsetZ);
        ReadFloatChild(ConfigNode, "InitialFrequency", Desc.mInitialFrequency);
        ReadFloatChild(ConfigNode, "InitialAmplitude", Desc.mInitialAmplitude);
        ReadUInt32Child(ConfigNode, "OctaveSeedStep", Desc.mOctaveSeedStep);
        ReadFloatChild(ConfigNode, "NoiseNormalizationScale", Desc.mNoiseNormalizationScale);
        ReadFloatChild(ConfigNode, "NoiseNormalizationBias", Desc.mNoiseNormalizationBias);
        ReadUInt32Child(ConfigNode, "HashShiftA", Desc.mHashShiftA);
        ReadUInt32Child(ConfigNode, "HashShiftB", Desc.mHashShiftB);
        ReadUInt32Child(ConfigNode, "HashShiftC", Desc.mHashShiftC);
        ReadUInt32Child(ConfigNode, "HashShiftLimitExclusive", Desc.mHashShiftLimitExclusive);
        ReadUInt32Child(ConfigNode, "HashMultiplierA", Desc.mHashMultiplierA);
        ReadUInt32Child(ConfigNode, "HashMultiplierB", Desc.mHashMultiplierB);
        ReadUInt32Child(ConfigNode, "HashCoordinateOffsetX", Desc.mHashCoordinateOffsetX);
        ReadUInt32Child(ConfigNode, "HashCoordinateOffsetZ", Desc.mHashCoordinateOffsetZ);
        ReadUInt32Child(ConfigNode, "GradientDirectionCount", Desc.mGradientDirectionCount);
        ReadFloatChild(ConfigNode, "FadeCoefficientA", Desc.mFadeCoefficientA);
        ReadFloatChild(ConfigNode, "FadeCoefficientB", Desc.mFadeCoefficientB);
        ReadFloatChild(ConfigNode, "FadeCoefficientC", Desc.mFadeCoefficientC);
        ReadFloatChild(ConfigNode, "SmoothingCornerWeight", Desc.mSmoothingCornerWeight);
        ReadFloatChild(ConfigNode, "SmoothingEdgeWeight", Desc.mSmoothingEdgeWeight);
        ReadFloatChild(ConfigNode, "SmoothingCenterWeight", Desc.mSmoothingCenterWeight);
        ReadFloatChild(ConfigNode, "SmoothingWeightSum", Desc.mSmoothingWeightSum);
        return Desc;
    }
}

namespace Game {
    TerrainProceduralHeightFieldConfigLoader::TerrainProceduralHeightFieldConfigLoader() {
    }

    TerrainProceduralHeightFieldConfigLoader::~TerrainProceduralHeightFieldConfigLoader() {
    }

    TerrainProceduralHeightFieldConfigLoader::TerrainProceduralHeightFieldConfigLoader(const TerrainProceduralHeightFieldConfigLoader& Other) {
        (void)Other;
    }

    TerrainProceduralHeightFieldConfigLoader& TerrainProceduralHeightFieldConfigLoader::operator=(const TerrainProceduralHeightFieldConfigLoader& Other) {
        (void)Other;
        return *this;
    }

    TerrainProceduralHeightFieldConfigLoader::TerrainProceduralHeightFieldConfigLoader(TerrainProceduralHeightFieldConfigLoader&& Other) noexcept {
        (void)Other;
    }

    TerrainProceduralHeightFieldConfigLoader& TerrainProceduralHeightFieldConfigLoader::operator=(TerrainProceduralHeightFieldConfigLoader&& Other) noexcept {
        (void)Other;
        return *this;
    }

    TerrainProceduralHeightFieldDesc TerrainProceduralHeightFieldConfigLoader::Load(const std::string& Path) const {
        if (Path.empty() == true) {
            throw std::runtime_error{ "Procedural height field config path is empty." };
        }

        std::ifstream InputStream{ Path, std::ios::in | std::ios::binary };
        if (InputStream.is_open() == false) {
            throw std::runtime_error{ "Procedural height field config open failed." };
        }

        std::ostringstream Buffer{};
        Buffer << InputStream.rdbuf();
        std::string YamlText{ Buffer.str() };
        if (YamlText.empty() == true) {
            throw std::runtime_error{ "Procedural height field config is empty." };
        }

        c4::yml::Tree Tree{ c4::yml::parse_in_arena(c4::to_csubstr(YamlText)) };
        Tree.resolve();
        const c4::yml::ConstNodeRef ConfigNode{ ResolveConfigNode(Tree.rootref()) };
        return ReadConfig(ConfigNode);
    }
}
