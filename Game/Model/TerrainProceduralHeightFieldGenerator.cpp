#include "TerrainProceduralHeightFieldGenerator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <execution>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
    struct GradientVector final {
        float mX{};
        float mZ{};
    };

    constexpr std::uint32_t GradientDirectionCount12{ 12u };
    constexpr std::uint32_t GradientDirectionCount16{ 16u };
    constexpr std::uint32_t PcgStateMultiplier{ 747796405u };
    constexpr std::uint32_t PcgStateIncrement{ 2891336453u };
    constexpr std::uint32_t PcgOutputMultiplier{ 277803737u };
    constexpr std::uint32_t PcgRotationShift{ 28u };
    constexpr std::uint32_t PcgRotationBias{ 4u };
    constexpr std::uint32_t PcgOutputShift{ 22u };
    constexpr std::array<GradientVector, GradientDirectionCount12> GradientVectors12{ {
        GradientVector{ 1.0f, 0.0f },
        GradientVector{ 0.866025404f, 0.5f },
        GradientVector{ 0.5f, 0.866025404f },
        GradientVector{ 0.0f, 1.0f },
        GradientVector{ -0.5f, 0.866025404f },
        GradientVector{ -0.866025404f, 0.5f },
        GradientVector{ -1.0f, 0.0f },
        GradientVector{ -0.866025404f, -0.5f },
        GradientVector{ -0.5f, -0.866025404f },
        GradientVector{ 0.0f, -1.0f },
        GradientVector{ 0.5f, -0.866025404f },
        GradientVector{ 0.866025404f, -0.5f }
    } };
    constexpr std::array<GradientVector, GradientDirectionCount16> GradientVectors16{ {
        GradientVector{ 1.0f, 0.0f },
        GradientVector{ 0.923879533f, 0.382683432f },
        GradientVector{ 0.707106781f, 0.707106781f },
        GradientVector{ 0.382683432f, 0.923879533f },
        GradientVector{ 0.0f, 1.0f },
        GradientVector{ -0.382683432f, 0.923879533f },
        GradientVector{ -0.707106781f, 0.707106781f },
        GradientVector{ -0.923879533f, 0.382683432f },
        GradientVector{ -1.0f, 0.0f },
        GradientVector{ -0.923879533f, -0.382683432f },
        GradientVector{ -0.707106781f, -0.707106781f },
        GradientVector{ -0.382683432f, -0.923879533f },
        GradientVector{ 0.0f, -1.0f },
        GradientVector{ 0.382683432f, -0.923879533f },
        GradientVector{ 0.707106781f, -0.707106781f },
        GradientVector{ 0.923879533f, -0.382683432f }
    } };

    float ClampHeightValue(const Game::TerrainProceduralHeightFieldDesc& Desc, float Value) {
        return std::clamp(Value, Desc.mMinimumHeightValue, Desc.mMaximumHeightValue);
    }

    std::uint32_t MixHash(std::uint32_t Value) {
        const std::uint32_t State{ (Value * PcgStateMultiplier) + PcgStateIncrement };
        const std::uint32_t Shift{ (State >> PcgRotationShift) + PcgRotationBias };
        const std::uint32_t Word{ ((State >> Shift) ^ State) * PcgOutputMultiplier };
        return (Word >> PcgOutputShift) ^ Word;
    }

    std::uint32_t HashGrid(std::int32_t X, std::int32_t Z, std::uint32_t Seed) {
        const std::uint32_t SeedHash{ MixHash(Seed) };
        const std::uint32_t ZSeedHash{ MixHash(SeedHash) };
        const std::uint32_t XHash{ MixHash(static_cast<std::uint32_t>(X) ^ SeedHash) };
        const std::uint32_t ZHash{ MixHash(static_cast<std::uint32_t>(Z) ^ ZSeedHash) };
        return MixHash(XHash ^ ZHash ^ Seed);
    }

    const GradientVector& SelectGradientVector(std::uint32_t Hash, std::uint32_t GradientDirectionCount) {
        if (GradientDirectionCount == GradientDirectionCount12) {
            const std::size_t GradientIndex{ static_cast<std::size_t>(Hash % GradientDirectionCount12) };
            return GradientVectors12[GradientIndex];
        }

        const std::size_t GradientIndex{ static_cast<std::size_t>(Hash % GradientDirectionCount16) };
        return GradientVectors16[GradientIndex];
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

    std::vector<std::uint32_t> CreateRowIndices(std::uint32_t Height) {
        std::vector<std::uint32_t> RowIndices{};
        RowIndices.resize(Height);
        std::iota(RowIndices.begin(), RowIndices.end(), 0u);
        return RowIndices;
    }

    std::uint32_t CreateRandomSeed() {
        std::random_device RandomDevice{};
        const std::uint64_t TimeSeed{ static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) };
        const std::uint32_t RandomValueA{ RandomDevice() };
        const std::uint32_t RandomValueB{ RandomDevice() };
        return RandomValueA ^ (RandomValueB << 1u) ^ static_cast<std::uint32_t>(TimeSeed) ^ static_cast<std::uint32_t>(TimeSeed >> 32u);
    }

    Game::TerrainProceduralHeightFieldDesc ResolveRandomSeed(const Game::TerrainProceduralHeightFieldDesc& Desc) {
        Game::TerrainProceduralHeightFieldDesc ResolvedDesc{ Desc };
        if (ResolvedDesc.mUseRandomSeed == true && ResolvedDesc.mHasResolvedRandomSeed == false) {
            ResolvedDesc.mSeed = CreateRandomSeed();
            ResolvedDesc.mHasResolvedRandomSeed = true;
        }

        return ResolvedDesc;
    }

    float SampleHeightValue(const Game::HeightFieldData& Field, std::int32_t X, std::int32_t Z) {
        const std::uint32_t ClampedX{ ClampCoordinate(X, Field.Width - 1u) };
        const std::uint32_t ClampedZ{ ClampCoordinate(Z, Field.Height - 1u) };
        return Field.HeightValues[CalculateHeightIndex(Field.Width, ClampedX, ClampedZ)];
    }

    float GradientDot(std::int32_t GridX, std::int32_t GridZ, float X, float Z, std::uint32_t Seed, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        const float DistanceX{ X - static_cast<float>(GridX) };
        const float DistanceZ{ Z - static_cast<float>(GridZ) };
        const std::uint32_t Hash{ HashGrid(GridX, GridZ, Seed) };
        const GradientVector& Gradient{ SelectGradientVector(Hash, Desc.mGradientDirectionCount) };
        return (Gradient.mX * DistanceX) + (Gradient.mZ * DistanceZ);
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

        if (Desc.mGradientDirectionCount != GradientDirectionCount12 && Desc.mGradientDirectionCount != GradientDirectionCount16) {
            throw std::runtime_error{ "Procedural height field gradient direction count must be 12 or 16." };
        }

        if (Desc.mSmoothingCornerWeight < 0.0f || Desc.mSmoothingEdgeWeight < 0.0f || Desc.mSmoothingCenterWeight < 0.0f || Desc.mSmoothingWeightSum <= 0.0f) {
            throw std::runtime_error{ "Procedural height field smoothing weights must be valid." };
        }
    }

    float CalculateFractalHeight01(const Game::TerrainProceduralHeightFieldDesc& Desc, std::int32_t X, std::int32_t Z) {
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

    void ApplySmoothingPass(Game::HeightFieldData& Field, std::vector<float>& TemporaryValues, const std::vector<std::uint32_t>& RowIndices, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        std::for_each(std::execution::par, RowIndices.cbegin(), RowIndices.cend(), [&](std::uint32_t Z) {
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
        });

        Field.HeightValues.swap(TemporaryValues);
    }

    void SmoothHeightField(Game::HeightFieldData& Field, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        if (Desc.mSmoothingPassCount == 0u) {
            return;
        }

        std::vector<float> TemporaryValues{};
        TemporaryValues.resize(Field.HeightValues.size());
        const std::vector<std::uint32_t> RowIndices{ CreateRowIndices(Field.Height) };

        for (std::uint32_t PassIndex{ 0u }; PassIndex < Desc.mSmoothingPassCount; ++PassIndex) {
            ApplySmoothingPass(Field, TemporaryValues, RowIndices, Desc);
        }
    }

    Game::HeightFieldData GenerateRawHeightField(const Game::TerrainProceduralHeightFieldDesc& Desc, std::uint32_t Width, std::uint32_t Height, std::int32_t SampleOffsetX, std::int32_t SampleOffsetZ) {
        Game::HeightFieldData Field{};
        Field.Width = Width;
        Field.Height = Height;
        const std::size_t PixelCount{ static_cast<std::size_t>(Field.Width) * static_cast<std::size_t>(Field.Height) };
        Field.HeightValues.resize(PixelCount);

        const std::vector<std::uint32_t> RowIndices{ CreateRowIndices(Field.Height) };
        std::for_each(std::execution::par, RowIndices.cbegin(), RowIndices.cend(), [&](std::uint32_t Z) {
            for (std::uint32_t X{ 0u }; X < Field.Width; ++X) {
                const std::int32_t SampleX{ SampleOffsetX + static_cast<std::int32_t>(X) };
                const std::int32_t SampleZ{ SampleOffsetZ + static_cast<std::int32_t>(Z) };
                const std::size_t Index{ CalculateHeightIndex(Field.Width, X, Z) };
                Field.HeightValues[Index] = CalculateFractalHeight01(Desc, SampleX, SampleZ);
            }
        });

        return Field;
    }

    Game::HeightFieldData CropHeightField(const Game::HeightFieldData& SourceField, std::uint32_t Width, std::uint32_t Height, std::uint32_t StartX, std::uint32_t StartZ) {
        Game::HeightFieldData Field{};
        Field.Width = Width;
        Field.Height = Height;
        const std::size_t PixelCount{ static_cast<std::size_t>(Field.Width) * static_cast<std::size_t>(Field.Height) };
        Field.HeightValues.resize(PixelCount);

        const std::vector<std::uint32_t> RowIndices{ CreateRowIndices(Field.Height) };
        std::for_each(std::execution::par, RowIndices.cbegin(), RowIndices.cend(), [&](std::uint32_t Z) {
            for (std::uint32_t X{ 0u }; X < Field.Width; ++X) {
                const std::size_t SourceIndex{ CalculateHeightIndex(SourceField.Width, StartX + X, StartZ + Z) };
                const std::size_t TargetIndex{ CalculateHeightIndex(Field.Width, X, Z) };
                Field.HeightValues[TargetIndex] = SourceField.HeightValues[SourceIndex];
            }
        });

        return Field;
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
        const TerrainProceduralHeightFieldDesc ResolvedDesc{ ResolveRandomSeed(Desc) };
        ValidateProceduralHeightFieldDesc(ResolvedDesc);

        const std::uint32_t Padding{ ResolvedDesc.mSmoothingPassCount };
        if (Padding == 0u) {
            return GenerateRawHeightField(ResolvedDesc, ResolvedDesc.mWidth, ResolvedDesc.mHeight, ResolvedDesc.mSampleOffsetX, ResolvedDesc.mSampleOffsetZ);
        }

        const std::uint32_t ExpandedWidth{ ResolvedDesc.mWidth + (Padding * 2u) };
        const std::uint32_t ExpandedHeight{ ResolvedDesc.mHeight + (Padding * 2u) };
        const std::int32_t ExpandedSampleOffsetX{ ResolvedDesc.mSampleOffsetX - static_cast<std::int32_t>(Padding) };
        const std::int32_t ExpandedSampleOffsetZ{ ResolvedDesc.mSampleOffsetZ - static_cast<std::int32_t>(Padding) };
        HeightFieldData ExpandedField{ GenerateRawHeightField(ResolvedDesc, ExpandedWidth, ExpandedHeight, ExpandedSampleOffsetX, ExpandedSampleOffsetZ) };
        SmoothHeightField(ExpandedField, ResolvedDesc);
        return CropHeightField(ExpandedField, ResolvedDesc.mWidth, ResolvedDesc.mHeight, Padding, Padding);
    }
}
