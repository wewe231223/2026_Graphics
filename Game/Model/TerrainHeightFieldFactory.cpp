#include "TerrainHeightFieldFactory.h"

#include <chrono>
#include <random>

#include "Game/Model/HeightMapLoader.h"
#include "Game/Model/TerrainProceduralHeightFieldConfigLoader.h"
#include "Game/Model/TerrainProceduralHeightFieldGenerator.h"

namespace {
    std::uint32_t CreateRandomSeed() {
        std::random_device RandomDevice{};
        const std::uint64_t TimeSeed{ static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) };
        const std::uint32_t RandomValueA{ RandomDevice() };
        const std::uint32_t RandomValueB{ RandomDevice() };
        return RandomValueA ^ (RandomValueB << 1u) ^ static_cast<std::uint32_t>(TimeSeed) ^ static_cast<std::uint32_t>(TimeSeed >> 32u);
    }

    void ResolveRandomSeed(Game::TerrainProceduralHeightFieldDesc& Desc) {
        if (Desc.mUseRandomSeed == false || Desc.mHasResolvedRandomSeed == true) {
            return;
        }

        Desc.mSeed = CreateRandomSeed();
        Desc.mHasResolvedRandomSeed = true;
    }
}

namespace Game {
    TerrainHeightFieldFactory::TerrainHeightFieldFactory() {
    }

    TerrainHeightFieldFactory::~TerrainHeightFieldFactory() {
    }

    TerrainHeightFieldFactory::TerrainHeightFieldFactory(const TerrainHeightFieldFactory& Other) {
        (void)Other;
    }

    TerrainHeightFieldFactory& TerrainHeightFieldFactory::operator=(const TerrainHeightFieldFactory& Other) {
        (void)Other;
        return *this;
    }

    TerrainHeightFieldFactory::TerrainHeightFieldFactory(TerrainHeightFieldFactory&& Other) noexcept {
        (void)Other;
    }

    TerrainHeightFieldFactory& TerrainHeightFieldFactory::operator=(TerrainHeightFieldFactory&& Other) noexcept {
        (void)Other;
        return *this;
    }

    TerrainProceduralHeightFieldDesc TerrainHeightFieldFactory::ResolveProceduralHeightFieldDesc(const TerrainBuildDesc& Desc) const {
        TerrainProceduralHeightFieldDesc ProceduralDesc{ Desc.mProceduralHeightFieldDesc };
        if (Desc.mProceduralHeightFieldPath.empty() == false) {
            const std::uint32_t ResolvedSeed{ ProceduralDesc.mSeed };
            const bool HasResolvedRandomSeed{ ProceduralDesc.mHasResolvedRandomSeed };
            TerrainProceduralHeightFieldConfigLoader ConfigLoader{};
            ProceduralDesc = ConfigLoader.Load(Desc.mProceduralHeightFieldPath);
            if (HasResolvedRandomSeed == true) {
                ProceduralDesc.mSeed = ResolvedSeed;
                ProceduralDesc.mHasResolvedRandomSeed = true;
            }

            if (Desc.mStreamingEnabled == true || Desc.mProceduralHeightFieldDesc.mSampleOffsetX != 0 || Desc.mProceduralHeightFieldDesc.mSampleOffsetZ != 0) {
                ProceduralDesc.mSampleOffsetX = Desc.mProceduralHeightFieldDesc.mSampleOffsetX;
                ProceduralDesc.mSampleOffsetZ = Desc.mProceduralHeightFieldDesc.mSampleOffsetZ;
            }
        }

        ResolveRandomSeed(ProceduralDesc);
        return ProceduralDesc;
    }

    HeightFieldData TerrainHeightFieldFactory::Build(const TerrainBuildDesc& Desc) const {
        if (Desc.mHeightSourceType == TerrainHeightSourceType::Procedural) {
            TerrainProceduralHeightFieldGenerator Generator{};
            const TerrainProceduralHeightFieldDesc ProceduralDesc{ ResolveProceduralHeightFieldDesc(Desc) };
            return Generator.Generate(ProceduralDesc);
        }

        HeightMapLoader Loader{};
        return Loader.LoadHeightField(Desc.HeightMapPath);
    }
}
