#pragma once

#include <string>

#include "Game/Model/TerrainMeshTypes.h"

namespace Game {
    class HeightMapLoader final {
    public:
        HeightMapLoader();
        ~HeightMapLoader();
        HeightMapLoader(const HeightMapLoader& Other);
        HeightMapLoader& operator=(const HeightMapLoader& Other);
        HeightMapLoader(HeightMapLoader&& Other) noexcept;
        HeightMapLoader& operator=(HeightMapLoader&& Other) noexcept;

    public:
        HeightFieldData LoadHeightField(const std::string& Path) const;
    };
}
