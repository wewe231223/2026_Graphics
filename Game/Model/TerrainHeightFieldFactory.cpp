#include "TerrainHeightFieldFactory.h"

#include "Game/Model/HeightMapLoader.h"
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
                return Generator.GenerateFromConfig(Desc.mProceduralHeightFieldPath);
            }

            return Generator.Generate(Desc.mProceduralHeightFieldDesc);
        }

        HeightMapLoader Loader{};
        return Loader.LoadHeightField(Desc.HeightMapPath);
    }
}
