#pragma once

#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "Terrain/TerrainMeshTypes.h"

namespace Terrain {
    struct TerrainDataHandle final {
    public:
        std::uint32_t mValue{ (std::numeric_limits<std::uint32_t>::max)() };
        std::uint32_t mGeneration{};
    };

    struct TerrainWorldData final {
    public:
        TerrainDataHandle mHandle{};
        std::uint32_t mTerrainId{};
        DirectX::SimpleMath::Vector3 mPosition{};
        DirectX::SimpleMath::Vector3 mRotation{};
        DirectX::SimpleMath::Quaternion mOrientation{ 0.0F, 0.0F, 0.0F, 1.0F };
        DirectX::SimpleMath::Vector3 mScale{ 1.0F, 1.0F, 1.0F };
        float mHalfExtentX{};
        float mHalfExtentZ{};
        std::uint32_t mHeightFieldWidth{};
        std::uint32_t mHeightFieldHeight{};
        float mHeightFieldCellSizeX{};
        float mHeightFieldCellSizeZ{};
        float mHeightFieldMaxHeight{};
        bool mHeightFieldCenterOrigin{};
        std::shared_ptr<const std::vector<float>> mHeightFieldValues{};
        std::shared_ptr<const SplatMapData> mSplatMapData{};
    };

    using TerrainHandle = TerrainDataHandle;
    using TerrainSnapshot = TerrainWorldData;
    using TerrainVersion = std::uint32_t;

    class ITerrainQuery {
    public:
        ITerrainQuery();
        virtual ~ITerrainQuery();
        ITerrainQuery(const ITerrainQuery& Other);
        ITerrainQuery& operator=(const ITerrainQuery& Other);
        ITerrainQuery(ITerrainQuery&& Other) noexcept;
        ITerrainQuery& operator=(ITerrainQuery&& Other) noexcept;

    public:
        virtual bool TryGetTerrainWorldData(TerrainDataHandle Handle, std::shared_ptr<const TerrainWorldData>& OutTerrainData) const = 0;
        virtual bool TryGetSurfaceAtWorldPosition(TerrainDataHandle Handle, float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const = 0;
        virtual bool TryGetSurfaceHeightAtWorldPosition(TerrainDataHandle Handle, float WorldX, float WorldZ, float& OutWorldHeight) const = 0;
        virtual bool TryRaycast(TerrainDataHandle Handle, const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const = 0;
        virtual bool TryGetSurfaceAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const = 0;
        virtual bool TryGetSurfaceHeightAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight) const = 0;
        virtual bool TryRaycast(const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const = 0;
        virtual bool TryGetSplatWeightsAtWorldPosition(TerrainDataHandle Handle, float WorldX, float WorldZ, asset::Vec4& OutSplatWeights) const = 0;
        virtual bool TryGetSplatWeightsAtWorldPosition(float WorldX, float WorldZ, asset::Vec4& OutSplatWeights) const = 0;
    };

    bool IsTerrainDataHandleValid(TerrainDataHandle Handle);
}
