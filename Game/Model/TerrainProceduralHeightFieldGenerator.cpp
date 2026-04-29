#include "TerrainProceduralHeightFieldGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
    float ClampHeightValue(const Game::TerrainProceduralHeightFieldDesc& Desc, float Value) {
        return std::clamp(Value, Desc.mMinimumHeightValue, Desc.mMaximumHeightValue);
    }

    std::uint32_t MixHash(std::uint32_t Value, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        Value ^= Value >> Desc.mHashShiftA;
        Value *= Desc.mHashMultiplierA;
        Value ^= Value >> Desc.mHashShiftB;
        Value *= Desc.mHashMultiplierB;
        Value ^= Value >> Desc.mHashShiftC;
        return Value;
    }

    std::uint32_t HashGrid(std::int32_t X, std::int32_t Z, std::uint32_t Seed, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        std::uint32_t Hash{ MixHash(Seed, Desc) };
        Hash ^= MixHash(static_cast<std::uint32_t>(X) + Desc.mHashCoordinateOffsetX, Desc);
        Hash = MixHash(Hash, Desc);
        Hash ^= MixHash(static_cast<std::uint32_t>(Z) + Desc.mHashCoordinateOffsetZ, Desc);
        return MixHash(Hash, Desc);
    }

    float Fade(float Value, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        return Value * Value * Value * ((Value * ((Value * Desc.mFadeCoefficientA) + Desc.mFadeCoefficientB)) + Desc.mFadeCoefficientC);
    }

    float Lerp(float Start, float End, float Alpha) {
        return Start + ((End - Start) * Alpha);
    }

    std::uint32_t ClampCoordinate(std::int32_t Value, std::uint32_t MaxValue) {
        const std::int32_t ClampedValue{ std::clamp(Value, 0, static_cast<std::int32_t>(MaxValue)) };
        return static_cast<std::uint32_t>(ClampedValue);
    }

    std::size_t CalculateHeightIndex(std::uint32_t Width, std::uint32_t X, std::uint32_t Z) {
        return (static_cast<std::size_t>(Z) * static_cast<std::size_t>(Width)) + static_cast<std::size_t>(X);
    }

    float SampleHeightValue(const Game::HeightFieldData& Field, std::int32_t X, std::int32_t Z) {
        const std::uint32_t ClampedX{ ClampCoordinate(X, Field.Width - 1u) };
        const std::uint32_t ClampedZ{ ClampCoordinate(Z, Field.Height - 1u) };
        return Field.HeightValues[CalculateHeightIndex(Field.Width, ClampedX, ClampedZ)];
    }

    float GradientDot(std::int32_t GridX, std::int32_t GridZ, float X, float Z, std::uint32_t Seed, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        const float DistanceX{ X - static_cast<float>(GridX) };
        const float DistanceZ{ Z - static_cast<float>(GridZ) };
        const std::uint32_t Direction{ HashGrid(GridX, GridZ, Seed, Desc) % Desc.mGradientDirectionCount };

        if (Direction == 0u) {
            return DistanceX + DistanceZ;
        }

        if (Direction == 1u) {
            return -DistanceX + DistanceZ;
        }

        if (Direction == 2u) {
            return DistanceX - DistanceZ;
        }

        if (Direction == 3u) {
            return -DistanceX - DistanceZ;
        }

        if (Direction == 4u) {
            return DistanceX;
        }

        if (Direction == 5u) {
            return -DistanceX;
        }

        if (Direction == 6u) {
            return DistanceZ;
        }

        return -DistanceZ;
    }

    float PerlinNoise(float X, float Z, std::uint32_t Seed, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        const std::int32_t X0{ static_cast<std::int32_t>(std::floor(X)) };
        const std::int32_t Z0{ static_cast<std::int32_t>(std::floor(Z)) };
        const std::int32_t X1{ X0 + 1 };
        const std::int32_t Z1{ Z0 + 1 };
        const float AlphaX{ Fade(X - static_cast<float>(X0), Desc) };
        const float AlphaZ{ Fade(Z - static_cast<float>(Z0), Desc) };
        const float Noise00{ GradientDot(X0, Z0, X, Z, Seed, Desc) };
        const float Noise10{ GradientDot(X1, Z0, X, Z, Seed, Desc) };
        const float Noise01{ GradientDot(X0, Z1, X, Z, Seed, Desc) };
        const float Noise11{ GradientDot(X1, Z1, X, Z, Seed, Desc) };
        const float NoiseX0{ Lerp(Noise00, Noise10, AlphaX) };
        const float NoiseX1{ Lerp(Noise01, Noise11, AlphaX) };
        return Lerp(NoiseX0, NoiseX1, AlphaZ);
    }

    void ValidateProceduralHeightFieldDesc(const Game::TerrainProceduralHeightFieldDesc& Desc) {
        if (Desc.mMinimumWidth == 0u || Desc.mMinimumHeight == 0u) {
            throw std::runtime_error{ "Procedural height field minimum size must be greater than zero." };
        }

        if (Desc.mWidth < Desc.mMinimumWidth || Desc.mHeight < Desc.mMinimumHeight) {
            throw std::runtime_error{ "Procedural height field size must be at least 2x2." };
        }

        if (Desc.mMaximumOctaveCount == 0u) {
            throw std::runtime_error{ "Procedural height field maximum octave count must be greater than zero." };
        }

        if (Desc.mOctaveCount == 0u || Desc.mOctaveCount > Desc.mMaximumOctaveCount) {
            throw std::runtime_error{ "Procedural height field octave count must be between 1 and 16." };
        }

        if (Desc.mNoiseScale <= 0.0f) {
            throw std::runtime_error{ "Procedural height field noise scale must be greater than zero." };
        }

        if (Desc.mSampleScaleX == 0.0f || Desc.mSampleScaleZ == 0.0f) {
            throw std::runtime_error{ "Procedural height field sample scale must not be zero." };
        }

        if (Desc.mInitialFrequency <= 0.0f) {
            throw std::runtime_error{ "Procedural height field initial frequency must be greater than zero." };
        }

        if (Desc.mInitialAmplitude < 0.0f) {
            throw std::runtime_error{ "Procedural height field initial amplitude must be zero or greater." };
        }

        if (Desc.mPersistence < 0.0f) {
            throw std::runtime_error{ "Procedural height field persistence must be zero or greater." };
        }

        if (Desc.mLacunarity <= 0.0f) {
            throw std::runtime_error{ "Procedural height field lacunarity must be greater than zero." };
        }

        if (Desc.mHeightAmplitude < 0.0f) {
            throw std::runtime_error{ "Procedural height field height amplitude must be zero or greater." };
        }

        if (Desc.mMaximumHeightValue < Desc.mMinimumHeightValue) {
            throw std::runtime_error{ "Procedural height field maximum height value must be greater than or equal to minimum height value." };
        }

        if (Desc.mMaximumSmoothingPassCount == 0u && Desc.mSmoothingPassCount > 0u) {
            throw std::runtime_error{ "Procedural height field maximum smoothing pass count must be greater than zero when smoothing is enabled." };
        }

        if (Desc.mSmoothingPassCount > Desc.mMaximumSmoothingPassCount) {
            throw std::runtime_error{ "Procedural height field smoothing pass count must be 64 or less." };
        }

        if (Desc.mHashShiftLimitExclusive == 0u || Desc.mHashShiftA >= Desc.mHashShiftLimitExclusive || Desc.mHashShiftB >= Desc.mHashShiftLimitExclusive || Desc.mHashShiftC >= Desc.mHashShiftLimitExclusive) {
            throw std::runtime_error{ "Procedural height field hash shifts must be less than 32." };
        }

        if (Desc.mGradientDirectionCount == 0u || Desc.mGradientDirectionCount > 8u) {
            throw std::runtime_error{ "Procedural height field gradient direction count must be between 1 and 8." };
        }

        if (Desc.mSmoothingCornerWeight < 0.0f || Desc.mSmoothingEdgeWeight < 0.0f || Desc.mSmoothingCenterWeight < 0.0f || Desc.mSmoothingWeightSum <= 0.0f) {
            throw std::runtime_error{ "Procedural height field smoothing weights must be valid." };
        }
    }

    float CalculateFractalHeight01(const Game::TerrainProceduralHeightFieldDesc& Desc, std::uint32_t X, std::uint32_t Z) {
        float Frequency{ Desc.mInitialFrequency / Desc.mNoiseScale };
        float Amplitude{ Desc.mInitialAmplitude };
        float NoiseSum{ 0.0f };
        float AmplitudeSum{ 0.0f };

        for (std::uint32_t OctaveIndex{ 0u }; OctaveIndex < Desc.mOctaveCount; ++OctaveIndex) {
            const float SampleX{ static_cast<float>(X) * Desc.mSampleScaleX * Frequency };
            const float SampleZ{ static_cast<float>(Z) * Desc.mSampleScaleZ * Frequency };
            const std::uint32_t OctaveSeed{ Desc.mSeed + (OctaveIndex * Desc.mOctaveSeedStep) };
            NoiseSum += PerlinNoise(SampleX, SampleZ, OctaveSeed, Desc) * Amplitude;
            AmplitudeSum += Amplitude;
            Amplitude *= Desc.mPersistence;
            Frequency *= Desc.mLacunarity;
        }

        if (AmplitudeSum <= 0.0f) {
            return ClampHeightValue(Desc, Desc.mBaseHeight);
        }

        const float NormalizedNoise{ (NoiseSum / AmplitudeSum) * Desc.mNoiseNormalizationScale + Desc.mNoiseNormalizationBias };
        return ClampHeightValue(Desc, Desc.mBaseHeight + (NormalizedNoise * Desc.mHeightAmplitude));
    }

    void ApplySmoothingPass(Game::HeightFieldData& Field, std::vector<float>& TemporaryValues, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        TemporaryValues.resize(Field.HeightValues.size());

        for (std::uint32_t Z{ 0u }; Z < Field.Height; ++Z) {
            for (std::uint32_t X{ 0u }; X < Field.Width; ++X) {
                const float Height00{ SampleHeightValue(Field, static_cast<std::int32_t>(X) - 1, static_cast<std::int32_t>(Z) - 1) };
                const float Height10{ SampleHeightValue(Field, static_cast<std::int32_t>(X), static_cast<std::int32_t>(Z) - 1) };
                const float Height20{ SampleHeightValue(Field, static_cast<std::int32_t>(X) + 1, static_cast<std::int32_t>(Z) - 1) };
                const float Height01{ SampleHeightValue(Field, static_cast<std::int32_t>(X) - 1, static_cast<std::int32_t>(Z)) };
                const float Height11{ SampleHeightValue(Field, static_cast<std::int32_t>(X), static_cast<std::int32_t>(Z)) };
                const float Height21{ SampleHeightValue(Field, static_cast<std::int32_t>(X) + 1, static_cast<std::int32_t>(Z)) };
                const float Height02{ SampleHeightValue(Field, static_cast<std::int32_t>(X) - 1, static_cast<std::int32_t>(Z) + 1) };
                const float Height12{ SampleHeightValue(Field, static_cast<std::int32_t>(X), static_cast<std::int32_t>(Z) + 1) };
                const float Height22{ SampleHeightValue(Field, static_cast<std::int32_t>(X) + 1, static_cast<std::int32_t>(Z) + 1) };
                const float SmoothedHeight{ ((Height00 + Height20 + Height02 + Height22) * Desc.mSmoothingCornerWeight + ((Height10 + Height01 + Height21 + Height12) * Desc.mSmoothingEdgeWeight) + (Height11 * Desc.mSmoothingCenterWeight)) / Desc.mSmoothingWeightSum };
                const std::size_t Index{ CalculateHeightIndex(Field.Width, X, Z) };
                TemporaryValues[Index] = ClampHeightValue(Desc, SmoothedHeight);
            }
        }

        Field.HeightValues.swap(TemporaryValues);
    }

    void SmoothHeightField(Game::HeightFieldData& Field, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        if (Desc.mSmoothingPassCount == 0u) {
            return;
        }

        std::vector<float> TemporaryValues{};
        for (std::uint32_t PassIndex{ 0u }; PassIndex < Desc.mSmoothingPassCount; ++PassIndex) {
            ApplySmoothingPass(Field, TemporaryValues, Desc);
        }
    }
}

namespace Game {
    TerrainProceduralHeightFieldGenerator::TerrainProceduralHeightFieldGenerator()
        : mConfigLoader{} {
    }

    TerrainProceduralHeightFieldGenerator::~TerrainProceduralHeightFieldGenerator() {
    }

    TerrainProceduralHeightFieldGenerator::TerrainProceduralHeightFieldGenerator(const TerrainProceduralHeightFieldGenerator& Other)
        : mConfigLoader{ Other.mConfigLoader } {
    }

    TerrainProceduralHeightFieldGenerator& TerrainProceduralHeightFieldGenerator::operator=(const TerrainProceduralHeightFieldGenerator& Other) {
        if (this == &Other) {
            return *this;
        }

        mConfigLoader = Other.mConfigLoader;
        return *this;
    }

    TerrainProceduralHeightFieldGenerator::TerrainProceduralHeightFieldGenerator(TerrainProceduralHeightFieldGenerator&& Other) noexcept
        : mConfigLoader{ std::move(Other.mConfigLoader) } {
    }

    TerrainProceduralHeightFieldGenerator& TerrainProceduralHeightFieldGenerator::operator=(TerrainProceduralHeightFieldGenerator&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mConfigLoader = std::move(Other.mConfigLoader);
        return *this;
    }

    HeightFieldData TerrainProceduralHeightFieldGenerator::GenerateFromConfig(const std::string& ConfigPath) const {
        const TerrainProceduralHeightFieldDesc Desc{ mConfigLoader.Load(ConfigPath) };
        return Generate(Desc);
    }

    HeightFieldData TerrainProceduralHeightFieldGenerator::Generate(const TerrainProceduralHeightFieldDesc& Desc) const {
        ValidateProceduralHeightFieldDesc(Desc);

        HeightFieldData Field{};
        Field.Width = Desc.mWidth;
        Field.Height = Desc.mHeight;
        const std::size_t PixelCount{ static_cast<std::size_t>(Field.Width) * static_cast<std::size_t>(Field.Height) };
        Field.HeightValues.resize(PixelCount);

        for (std::uint32_t Z{ 0u }; Z < Field.Height; ++Z) {
            for (std::uint32_t X{ 0u }; X < Field.Width; ++X) {
                const std::size_t Index{ CalculateHeightIndex(Field.Width, X, Z) };
                Field.HeightValues[Index] = CalculateFractalHeight01(Desc, X, Z);
            }
        }

        SmoothHeightField(Field, Desc);
        return Field;
    }
}
