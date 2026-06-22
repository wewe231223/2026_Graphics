#pragma once

#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>

#include <cstdint>
#include <future>
#include <memory>
#include <shared_mutex>
#include <vector>

#include "Terrain/TerrainMeshTypes.h"
#include "Terrain/TerrainQuery.h"

namespace Terrain {
    struct TerrainTileMetadata final {
    public:
        std::uint32_t mTileIndexX{};
        std::uint32_t mTileIndexZ{};
        std::uint32_t mStartX{};
        std::uint32_t mStartZ{};
        std::uint32_t mQuadCountX{};
        std::uint32_t mQuadCountZ{};
        DirectX::BoundingOrientedBox mLocalBoundingBox{};
        DirectX::SimpleMath::Vector3 mCenter{};
        std::uint32_t mSubMeshIndex{};
        std::vector<std::uint32_t> mSubMeshIndexByLod{};
    };

    struct TerrainStreamingBuildResult final {
    public:
        TerrainBuildDesc mBuildDesc{};
        std::shared_ptr<const HeightFieldData> mHeightField{};
        std::shared_ptr<const SplatMapData> mSplatMap{};
        std::vector<TerrainTileMetadata> mTileMetadata{};
        std::uint32_t mTileQuadCount{};
        std::uint32_t mTileCountX{};
        std::uint32_t mTileCountZ{};
        std::uint32_t mLodCount{ 1 };
        std::vector<float> mLodDistances{};
        DirectX::BoundingOrientedBox mLocalBoundingBox{};
        std::int32_t mTargetOriginGridX{};
        std::int32_t mTargetOriginGridZ{};
        float mStreamWorldOriginX{};
        float mStreamWorldOriginZ{};
        bool mSucceeded{};
    };

    class ITerrainManager : public ITerrainQuery {
    public:
        ITerrainManager();
        ~ITerrainManager() override;
        ITerrainManager(const ITerrainManager& Other);
        ITerrainManager& operator=(const ITerrainManager& Other);
        ITerrainManager(ITerrainManager&& Other) noexcept;
        ITerrainManager& operator=(ITerrainManager&& Other) noexcept;

    public:
        virtual void Clear() = 0;
        virtual TerrainDataHandle UpsertTerrainData(std::uint32_t TerrainId, const DirectX::SimpleMath::Vector3& Position, const DirectX::SimpleMath::Vector3& Rotation, const DirectX::SimpleMath::Vector3& Scale, const TerrainBuildDesc& BuildDesc, const std::shared_ptr<const HeightFieldData>& HeightField) = 0;
        virtual TerrainDataHandle UpsertTerrainData(const TerrainWorldData& TerrainData) = 0;
        virtual bool RemoveTerrainData(TerrainDataHandle Handle) = 0;
        virtual TerrainStreamingBuildResult BuildStreamingData(TerrainBuildDesc StreamingDesc, std::int32_t TargetOriginGridX, std::int32_t TargetOriginGridZ) const = 0;
    };

    class TerrainManager final : public ITerrainManager {
    public:
        TerrainManager();
        ~TerrainManager() override;
        TerrainManager(const TerrainManager& Other) = delete;
        TerrainManager& operator=(const TerrainManager& Other) = delete;
        TerrainManager(TerrainManager&& Other) noexcept = delete;
        TerrainManager& operator=(TerrainManager&& Other) noexcept = delete;

    public:
        void Clear() override;
        TerrainDataHandle UpsertTerrainData(std::uint32_t TerrainId, const DirectX::SimpleMath::Vector3& Position, const DirectX::SimpleMath::Vector3& Rotation, const DirectX::SimpleMath::Vector3& Scale, const TerrainBuildDesc& BuildDesc, const std::shared_ptr<const HeightFieldData>& HeightField) override;
        TerrainDataHandle UpsertTerrainData(const TerrainWorldData& TerrainData) override;
        bool RemoveTerrainData(TerrainDataHandle Handle) override;
        TerrainStreamingBuildResult BuildStreamingData(TerrainBuildDesc StreamingDesc, std::int32_t TargetOriginGridX, std::int32_t TargetOriginGridZ) const override;

        bool TryGetTerrainWorldData(TerrainDataHandle Handle, std::shared_ptr<const TerrainWorldData>& OutTerrainData) const override;
        bool TryGetSurfaceAtWorldPosition(TerrainDataHandle Handle, float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const override;
        bool TryGetSurfaceHeightAtWorldPosition(TerrainDataHandle Handle, float WorldX, float WorldZ, float& OutWorldHeight) const override;
        bool TryRaycast(TerrainDataHandle Handle, const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const override;
        bool TryGetSurfaceAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const override;
        bool TryGetSurfaceHeightAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight) const override;
        bool TryRaycast(const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const override;
        bool TryGetSplatWeightsAtWorldPosition(TerrainDataHandle Handle, float WorldX, float WorldZ, asset::Vec4& OutSplatWeights) const override;
        bool TryGetSplatWeightsAtWorldPosition(float WorldX, float WorldZ, asset::Vec4& OutSplatWeights) const override;

    private:
        std::shared_ptr<const TerrainWorldData> BuildTerrainWorldData(std::uint32_t TerrainId, const DirectX::SimpleMath::Vector3& Position, const DirectX::SimpleMath::Vector3& Rotation, const DirectX::SimpleMath::Vector3& Scale, const TerrainBuildDesc& BuildDesc, const std::shared_ptr<const HeightFieldData>& HeightField, TerrainDataHandle Handle) const;
        std::vector<std::shared_ptr<const TerrainWorldData>> CollectTerrainDataSnapshot() const;

    private:
        mutable std::shared_mutex mTerrainDataMutex;
        std::vector<std::shared_ptr<const TerrainWorldData>> mTerrainDataItems;
        std::vector<std::uint32_t> mTerrainDataGenerations;
    };
}
