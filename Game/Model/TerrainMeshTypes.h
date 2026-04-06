#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Asset/Common.h"

namespace Game {
    struct TerrainBuildDesc final {
        std::string HeightMapPath{};
        float MaxHeight{ 1.0f };
        float CellSizeX{ 1.0f };
        float CellSizeZ{ 1.0f };
        bool FlipV{ false };
        bool CenterOrigin{ false };
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
