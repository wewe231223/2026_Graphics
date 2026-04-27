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
