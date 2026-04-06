#include "Game/Scene/TerrainHeightResolver.h"

#include <algorithm>
#include <cmath>
#include <utility>
#ifdef max
#undef max
#endif 

#ifdef min
#undef min
#endif 

namespace Game {
    TerrainHeightResolver::TerrainHeightResolver()
        : mWidth{},
        mHeight{},
        mHeightValues{},
        mMaxHeight{ 1.0f },
        mCellSizeX{ 1.0f },
        mCellSizeZ{ 1.0f },
        mOriginOffsetX{},
        mOriginOffsetZ{},
        mCenterOrigin{},
        mInitialized{} {
    }

    TerrainHeightResolver::~TerrainHeightResolver() {
    }

    TerrainHeightResolver::TerrainHeightResolver(const TerrainHeightResolver& Other)
        : mWidth{ Other.mWidth },
        mHeight{ Other.mHeight },
        mHeightValues{ Other.mHeightValues },
        mMaxHeight{ Other.mMaxHeight },
        mCellSizeX{ Other.mCellSizeX },
        mCellSizeZ{ Other.mCellSizeZ },
        mOriginOffsetX{ Other.mOriginOffsetX },
        mOriginOffsetZ{ Other.mOriginOffsetZ },
        mCenterOrigin{ Other.mCenterOrigin },
        mInitialized{ Other.mInitialized } {
    }

    TerrainHeightResolver& TerrainHeightResolver::operator=(const TerrainHeightResolver& Other) {
        if (this == &Other) {
            return *this;
        }

        mWidth = Other.mWidth;
        mHeight = Other.mHeight;
        mHeightValues = Other.mHeightValues;
        mMaxHeight = Other.mMaxHeight;
        mCellSizeX = Other.mCellSizeX;
        mCellSizeZ = Other.mCellSizeZ;
        mOriginOffsetX = Other.mOriginOffsetX;
        mOriginOffsetZ = Other.mOriginOffsetZ;
        mCenterOrigin = Other.mCenterOrigin;
        mInitialized = Other.mInitialized;
        return *this;
    }

    TerrainHeightResolver::TerrainHeightResolver(TerrainHeightResolver&& Other) noexcept
        : mWidth{ Other.mWidth },
        mHeight{ Other.mHeight },
        mHeightValues{ std::move(Other.mHeightValues) },
        mMaxHeight{ Other.mMaxHeight },
        mCellSizeX{ Other.mCellSizeX },
        mCellSizeZ{ Other.mCellSizeZ },
        mOriginOffsetX{ Other.mOriginOffsetX },
        mOriginOffsetZ{ Other.mOriginOffsetZ },
        mCenterOrigin{ Other.mCenterOrigin },
        mInitialized{ Other.mInitialized } {
    }

    TerrainHeightResolver& TerrainHeightResolver::operator=(TerrainHeightResolver&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mWidth = Other.mWidth;
        mHeight = Other.mHeight;
        mHeightValues = std::move(Other.mHeightValues);
        mMaxHeight = Other.mMaxHeight;
        mCellSizeX = Other.mCellSizeX;
        mCellSizeZ = Other.mCellSizeZ;
        mOriginOffsetX = Other.mOriginOffsetX;
        mOriginOffsetZ = Other.mOriginOffsetZ;
        mCenterOrigin = Other.mCenterOrigin;
        mInitialized = Other.mInitialized;
        return *this;
    }

    void TerrainHeightResolver::Initialize(const HeightFieldData& HeightFieldDataValue, const TerrainBuildDesc& TerrainBuildDescValue) {
        mWidth = HeightFieldDataValue.Width;
        mHeight = HeightFieldDataValue.Height;
        mHeightValues = HeightFieldDataValue.HeightValues;
        mMaxHeight = TerrainBuildDescValue.MaxHeight;
        mCellSizeX = TerrainBuildDescValue.CellSizeX;
        mCellSizeZ = TerrainBuildDescValue.CellSizeZ;
        mCenterOrigin = TerrainBuildDescValue.CenterOrigin;
        mOriginOffsetX = (static_cast<float>(mWidth) - 1.0f) * mCellSizeX * 0.5f;
        mOriginOffsetZ = (static_cast<float>(mHeight) - 1.0f) * mCellSizeZ * 0.5f;
        mInitialized = true;
    }

    bool TerrainHeightResolver::TryResolvePositionY(SimpleMath::Vector3& InOutPosition) const {
        if (mInitialized == false || mWidth < 2 || mHeight < 2 || mCellSizeX <= 0.0f || mCellSizeZ <= 0.0f) {
            return false;
        }

        float GridPositionX{ InOutPosition.x };
        float GridPositionZ{ InOutPosition.z };
        if (mCenterOrigin == true) {
            GridPositionX += mOriginOffsetX;
            GridPositionZ += mOriginOffsetZ;
        }

        const float MaxGridPositionX{ static_cast<float>(mWidth - 1) * mCellSizeX };
        const float MaxGridPositionZ{ static_cast<float>(mHeight - 1) * mCellSizeZ };
        if (GridPositionX < 0.0f || GridPositionZ < 0.0f || GridPositionX > MaxGridPositionX || GridPositionZ > MaxGridPositionZ) {
            return false;
        }

        const float GridX{ GridPositionX / mCellSizeX };
        const float GridZ{ GridPositionZ / mCellSizeZ };
        const std::uint32_t BaseGridX{ std::min(static_cast<std::uint32_t>(std::floor(GridX)), mWidth - 2) };
        const std::uint32_t BaseGridZ{ std::min(static_cast<std::uint32_t>(std::floor(GridZ)), mHeight - 2) };
        const std::uint32_t NextGridX{ BaseGridX + 1 };
        const std::uint32_t NextGridZ{ BaseGridZ + 1 };

        const float LocalX{ GridX - static_cast<float>(BaseGridX) };
        const float LocalZ{ GridZ - static_cast<float>(BaseGridZ) };

        const float Height00{ SampleCellHeight(BaseGridX, BaseGridZ) };
        const float Height10{ SampleCellHeight(NextGridX, BaseGridZ) };
        const float Height01{ SampleCellHeight(BaseGridX, NextGridZ) };
        const float Height11{ SampleCellHeight(NextGridX, NextGridZ) };
        const float HeightTop{ Height00 + ((Height10 - Height00) * LocalX) };
        const float HeightBottom{ Height01 + ((Height11 - Height01) * LocalX) };
        const float InterpolatedHeight{ HeightTop + ((HeightBottom - HeightTop) * LocalZ) };

        InOutPosition.y = InterpolatedHeight;
        return true;
    }

    std::uint32_t TerrainHeightResolver::CalculateHeightFieldIndex(std::uint32_t GridX, std::uint32_t GridZ) const {
        return (GridZ * mWidth) + GridX;
    }

    float TerrainHeightResolver::SampleCellHeight(std::uint32_t GridX, std::uint32_t GridZ) const {
        const std::uint32_t HeightFieldIndex{ CalculateHeightFieldIndex(GridX, GridZ) };
        const float Height01Value{ std::clamp(mHeightValues[HeightFieldIndex], 0.0f, 1.0f) };
        return Height01Value * mMaxHeight;
    }
}
