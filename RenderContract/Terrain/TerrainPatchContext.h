#pragma once

#include <cstdint>

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    struct alignas(16) TerrainPatchContext final {
    public:
        DirectX::SimpleMath::Vector4 mOuterTessFactors{};
        DirectX::SimpleMath::Vector4 mInsideTessFactors{};
        DirectX::SimpleMath::Vector4 mTileGrid{};
        DirectX::SimpleMath::Vector4 mHeightFieldParameters{};
        DirectX::SimpleMath::Vector4 mTerrainParameters{};
        DirectX::SimpleMath::Vector4 mTerrainUvParameters{};
        std::uint32_t mHeightFieldSrvDescriptorIndex{ 0xffffffffu };
        std::uint32_t mSplatMapSrvDescriptorIndex{ 0xffffffffu };
        std::uint32_t mSplatMapWidth{};
        std::uint32_t mSplatMapHeight{};
    };
}
