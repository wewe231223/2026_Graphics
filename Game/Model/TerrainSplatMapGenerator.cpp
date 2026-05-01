#include "TerrainSplatMapGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {
    float Saturate(float Value) {
        return std::clamp(Value, 0.0f, 1.0f);
    }

    float SmoothStep(float Edge0, float Edge1, float Value) {
        const float Range{ Edge1 - Edge0 };
        if (Range <= 0.0f) {
            return Value >= Edge1 ? 1.0f : 0.0f;
        }

        const float T{ Saturate((Value - Edge0) / Range) };
        return T * T * (3.0f - (2.0f * T));
    }

    std::size_t CalculateIndex(std::uint32_t Width, std::uint32_t X, std::uint32_t Z) {
        return (static_cast<std::size_t>(Z) * static_cast<std::size_t>(Width)) + static_cast<std::size_t>(X);
    }

    std::uint32_t ClampCoordinate(std::int32_t Value, std::uint32_t MaxValue) {
        const std::int32_t ClampedValue{ std::clamp(Value, 0, static_cast<std::int32_t>(MaxValue)) };
        return static_cast<std::uint32_t>(ClampedValue);
    }

    float SampleHeight01(const Game::HeightFieldData& Field, std::int32_t X, std::int32_t Z) {
        const std::uint32_t ClampedX{ ClampCoordinate(X, Field.Width - 1u) };
        const std::uint32_t ClampedZ{ ClampCoordinate(Z, Field.Height - 1u) };
        return Saturate(Field.HeightValues[CalculateIndex(Field.Width, ClampedX, ClampedZ)]);
    }

    float CalculateWorldHeight(const Game::HeightFieldData& Field, const Game::TerrainBuildDesc& Desc, std::int32_t X, std::int32_t Z) {
        return SampleHeight01(Field, X, Z) * Desc.MaxHeight;
    }

    float CalculateSlope01(const Game::HeightFieldData& Field, const Game::TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Z) {
        const std::int32_t SampleX{ static_cast<std::int32_t>(X) };
        const std::int32_t SampleZ{ static_cast<std::int32_t>(Z) };
        const float HeightNegativeX{ CalculateWorldHeight(Field, Desc, SampleX - 1, SampleZ) };
        const float HeightPositiveX{ CalculateWorldHeight(Field, Desc, SampleX + 1, SampleZ) };
        const float HeightNegativeZ{ CalculateWorldHeight(Field, Desc, SampleX, SampleZ - 1) };
        const float HeightPositiveZ{ CalculateWorldHeight(Field, Desc, SampleX, SampleZ + 1) };
        const float CellSpanX{ (std::max)(Desc.CellSizeX * 2.0f, 0.0001f) };
        const float CellSpanZ{ (std::max)(Desc.CellSizeZ * 2.0f, 0.0001f) };
        const float GradientX{ (HeightPositiveX - HeightNegativeX) / CellSpanX };
        const float GradientZ{ (HeightPositiveZ - HeightNegativeZ) / CellSpanZ };
        const float NormalY{ 1.0f / std::sqrt((GradientX * GradientX) + 1.0f + (GradientZ * GradientZ)) };
        return Saturate((1.0f - NormalY) * 2.5f);
    }

    asset::Vec4 NormalizeWeights(const asset::Vec4& Weights) {
        const float LowWeight{ Saturate(Weights.x) };
        const float GrassWeight{ Saturate(Weights.y) };
        const float RockWeight{ Saturate(Weights.z) };
        const float SnowWeight{ Saturate(Weights.w) };
        const float WeightSum{ LowWeight + GrassWeight + RockWeight + SnowWeight };
        if (WeightSum <= 0.0f) {
            return asset::Vec4{ 0.0f, 1.0f, 0.0f, 0.0f };
        }

        const float InverseWeightSum{ 1.0f / WeightSum };
        return asset::Vec4{ LowWeight * InverseWeightSum, GrassWeight * InverseWeightSum, RockWeight * InverseWeightSum, SnowWeight * InverseWeightSum };
    }

    asset::Vec4 BuildSplatWeights(float HeightValue, float SlopeValue) {
        const float LowWeight{ 1.0f - SmoothStep(0.16f, 0.32f, HeightValue) };
        const float SnowBaseWeight{ SmoothStep(0.68f, 0.88f, HeightValue) };
        const float SnowSlopeWeight{ 1.0f - SmoothStep(0.24f, 0.62f, SlopeValue) };
        const float SnowWeight{ SnowBaseWeight * SnowSlopeWeight };
        const float RockSlopeWeight{ SmoothStep(0.16f, 0.55f, SlopeValue) * (1.0f - (LowWeight * 0.8f)) };
        const float HighRockWeight{ SmoothStep(0.50f, 0.74f, HeightValue) * SmoothStep(0.08f, 0.28f, SlopeValue) * (1.0f - SnowWeight) };
        const float RockWeight{ (std::max)(RockSlopeWeight, HighRockWeight) };
        const float GrassHeightWeight{ SmoothStep(0.20f, 0.38f, HeightValue) * (1.0f - SmoothStep(0.74f, 0.92f, HeightValue)) };
        const float GrassSlopeWeight{ 1.0f - SmoothStep(0.30f, 0.70f, SlopeValue) };
        const float GrassWeight{ GrassHeightWeight * GrassSlopeWeight * (1.0f - (RockWeight * 0.5f)) };
        return NormalizeWeights(asset::Vec4{ LowWeight, GrassWeight, RockWeight, SnowWeight });
    }

    void ValidateSplatMapInput(const Game::HeightFieldData& Field, const Game::TerrainBuildDesc& Desc) {
        if (Field.Width < 2u || Field.Height < 2u) {
            throw std::runtime_error{ "Splat map height field size must be at least 2x2." };
        }

        const std::size_t ExpectedSize{ static_cast<std::size_t>(Field.Width) * static_cast<std::size_t>(Field.Height) };
        if (Field.HeightValues.size() != ExpectedSize) {
            throw std::runtime_error{ "Splat map height field buffer size mismatch." };
        }

        if (Desc.MaxHeight <= 0.0f || Desc.CellSizeX <= 0.0f || Desc.CellSizeZ <= 0.0f) {
            throw std::runtime_error{ "Splat map terrain scale must be valid." };
        }
    }
}

namespace Game {
    TerrainSplatMapGenerator::TerrainSplatMapGenerator() {
    }

    TerrainSplatMapGenerator::~TerrainSplatMapGenerator() {
    }

    TerrainSplatMapGenerator::TerrainSplatMapGenerator(const TerrainSplatMapGenerator& Other) {
        (void)Other;
    }

    TerrainSplatMapGenerator& TerrainSplatMapGenerator::operator=(const TerrainSplatMapGenerator& Other) {
        (void)Other;
        return *this;
    }

    TerrainSplatMapGenerator::TerrainSplatMapGenerator(TerrainSplatMapGenerator&& Other) noexcept {
        (void)Other;
    }

    TerrainSplatMapGenerator& TerrainSplatMapGenerator::operator=(TerrainSplatMapGenerator&& Other) noexcept {
        (void)Other;
        return *this;
    }

    SplatMapData TerrainSplatMapGenerator::Generate(const HeightFieldData& Field, const TerrainBuildDesc& Desc) const {
        ValidateSplatMapInput(Field, Desc);

        SplatMapData SplatMap{};
        SplatMap.Width = Field.Width;
        SplatMap.Height = Field.Height;
        const std::size_t PixelCount{ static_cast<std::size_t>(SplatMap.Width) * static_cast<std::size_t>(SplatMap.Height) };
        SplatMap.WeightValues.resize(PixelCount);

        for (std::uint32_t Z{ 0u }; Z < Field.Height; ++Z) {
            for (std::uint32_t X{ 0u }; X < Field.Width; ++X) {
                const std::size_t Index{ CalculateIndex(Field.Width, X, Z) };
                const float HeightValue{ SampleHeight01(Field, static_cast<std::int32_t>(X), static_cast<std::int32_t>(Z)) };
                const float SlopeValue{ CalculateSlope01(Field, Desc, X, Z) };
                SplatMap.WeightValues[Index] = BuildSplatWeights(HeightValue, SlopeValue);
            }
        }

        return SplatMap;
    }
}
