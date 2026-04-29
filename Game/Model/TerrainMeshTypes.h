#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Asset/Common.h"

namespace Game {
    enum class TerrainHeightSourceType : std::uint32_t {
        HeightMap,
        Procedural
    };

    struct TerrainProceduralHeightFieldDesc final {
        std::uint32_t mWidth{ 257 };
        std::uint32_t mHeight{ 257 };
        std::uint32_t mSeed{ 2026 };
        std::uint32_t mOctaveCount{ 3 };
        float mNoiseScale{ 220.0f };
        float mPersistence{ 0.3f };
        float mLacunarity{ 1.7f };
        float mBaseHeight{ 0.2f };
        float mHeightAmplitude{ 0.45f };
        std::uint32_t mSmoothingPassCount{ 8 };
        std::uint32_t mMinimumWidth{ 2 };
        std::uint32_t mMinimumHeight{ 2 };
        std::uint32_t mMaximumOctaveCount{ 16 };
        std::uint32_t mMaximumSmoothingPassCount{ 64 };
        float mMinimumHeightValue{ 0.0f };
        float mMaximumHeightValue{ 1.0f };
        float mSampleScaleX{ 1.0f };
        float mSampleScaleZ{ 1.0f };
        float mInitialFrequency{ 1.0f };
        float mInitialAmplitude{ 1.0f };
        std::uint32_t mOctaveSeedStep{ 1013 };
        float mNoiseNormalizationScale{ 0.5f };
        float mNoiseNormalizationBias{ 0.5f };
        std::uint32_t mHashShiftA{ 16 };
        std::uint32_t mHashShiftB{ 15 };
        std::uint32_t mHashShiftC{ 16 };
        std::uint32_t mHashShiftLimitExclusive{ 32 };
        std::uint32_t mHashMultiplierA{ 0x7feb352du };
        std::uint32_t mHashMultiplierB{ 0x846ca68bu };
        std::uint32_t mHashCoordinateOffsetX{ 0x9e3779b9u };
        std::uint32_t mHashCoordinateOffsetZ{ 0x85ebca6bu };
        std::uint32_t mGradientDirectionCount{ 8 };
        float mFadeCoefficientA{ 6.0f };
        float mFadeCoefficientB{ -15.0f };
        float mFadeCoefficientC{ 10.0f };
        float mSmoothingCornerWeight{ 1.0f };
        float mSmoothingEdgeWeight{ 2.0f };
        float mSmoothingCenterWeight{ 4.0f };
        float mSmoothingWeightSum{ 16.0f };
    };

    struct TerrainBuildDesc final {
        std::string HeightMapPath{};
        TerrainHeightSourceType mHeightSourceType{ TerrainHeightSourceType::HeightMap };
        std::string mProceduralHeightFieldPath{};
        TerrainProceduralHeightFieldDesc mProceduralHeightFieldDesc{};
        float MaxHeight{ 1.0f };
        float CellSizeX{ 1.0f };
        float CellSizeZ{ 1.0f };
        bool FlipV{ false };
        bool CenterOrigin{ false };
        std::uint32_t TileQuadCount{ 56 };
        std::uint32_t LodCount{ 8 };
        std::vector<float> LodDistances{ 45.0f, 75.0f, 110.0f, 155.0f, 220.0f, 310.0f, 430.0f };
    };

    struct HeightFieldData final {
        std::uint32_t Width{ 0 };
        std::uint32_t Height{ 0 };
        std::vector<float> HeightValues{};
    };

    struct TerrainMeshData final {
        asset::VertexAttributes Vertices{};
        std::vector<std::uint32_t> Indices{};
    };
}
