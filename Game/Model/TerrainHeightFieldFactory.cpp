#include "TerrainHeightFieldFactory.h"

#include "Game/Model/HeightMapLoader.h"
#include "Game/Model/TerrainProceduralHeightFieldConfigLoader.h"
#include "Game/Model/TerrainProceduralHeightFieldGenerator.h"

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

    HeightFieldData TerrainHeightFieldFactory::Build(const TerrainBuildDesc& Desc) const {
        if (Desc.mHeightSourceType == TerrainHeightSourceType::Procedural) {
            TerrainProceduralHeightFieldGenerator Generator{};
            if (Desc.mProceduralHeightFieldPath.empty() == false) {
                TerrainProceduralHeightFieldConfigLoader ConfigLoader{};
                TerrainProceduralHeightFieldDesc ProceduralDesc{ ConfigLoader.Load(Desc.mProceduralHeightFieldPath) };
                if (Desc.mStreamingEnabled == true || Desc.mProceduralHeightFieldDesc.mSampleOffsetX != 0 || Desc.mProceduralHeightFieldDesc.mSampleOffsetZ != 0) {
                    ProceduralDesc.mSampleOffsetX = Desc.mProceduralHeightFieldDesc.mSampleOffsetX;
                    ProceduralDesc.mSampleOffsetZ = Desc.mProceduralHeightFieldDesc.mSampleOffsetZ;
                }

                return Generator.Generate(ProceduralDesc);
            }

            return Generator.Generate(Desc.mProceduralHeightFieldDesc);
        }

        HeightMapLoader Loader{};
        return Loader.LoadHeightField(Desc.HeightMapPath);
    }
}
