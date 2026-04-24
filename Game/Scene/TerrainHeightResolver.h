#pragma once

#include <cstdint>
#include <vector>
#include "Game/Model/TerrainMeshTypes.h"
#include "Utility/SimpleMathWrapper.h"

namespace Game {
    class TerrainHeightResolver final {
    public:
        TerrainHeightResolver();
        ~TerrainHeightResolver();
        TerrainHeightResolver(const TerrainHeightResolver& Other);
        TerrainHeightResolver& operator=(const TerrainHeightResolver& Other);
        TerrainHeightResolver(TerrainHeightResolver&& Other) noexcept;
        TerrainHeightResolver& operator=(TerrainHeightResolver&& Other) noexcept;

    public:
        void Initialize(const HeightFieldData& HeightFieldDataValue, const TerrainBuildDesc& TerrainBuildDescValue);
        bool TryResolvePositionY(SimpleMath::Vector3& InOutPosition) const;
        bool TryResolvePositionYAndNormal(SimpleMath::Vector3& InOutPosition, SimpleMath::Vector3& OutNormal) const;
        bool TryRaycast(const SimpleMath::Ray& Ray, float MaxDistance, SimpleMath::Vector3& OutHitPosition, SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const;

        std::uint32_t GetWidth() const;
        std::uint32_t GetHeight() const;
        const std::vector<float>& GetHeightValues() const;
        float GetMaxHeight() const;
        float GetCellSizeX() const;
        float GetCellSizeZ() const;
        bool GetCenterOrigin() const;
        bool GetInitialized() const;

    private:
        std::uint32_t CalculateHeightFieldIndex(std::uint32_t GridX, std::uint32_t GridZ) const;
        float SampleCellHeight(std::uint32_t GridX, std::uint32_t GridZ) const;

    private:
        std::uint32_t mWidth{};
        std::uint32_t mHeight{};
        std::vector<float> mHeightValues{};
        float mMaxHeight{ 1.0f };
        float mCellSizeX{ 1.0f };
        float mCellSizeZ{ 1.0f };
        float mOriginOffsetX{};
        float mOriginOffsetZ{};
        bool mCenterOrigin{};
        bool mInitialized{};
    };
}
