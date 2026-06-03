#pragma once

#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

struct TerrainWorldData final {
public:
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
    std::vector<float> mHeightFieldValues{};
};

struct TerrainSnapshot final {
public:
    std::vector<TerrainWorldData> mTerrainDataItems{};
};

class ITerrainQuery {
public:
    ITerrainQuery();
    virtual ~ITerrainQuery();
    ITerrainQuery(const ITerrainQuery& Other);
    ITerrainQuery& operator=(const ITerrainQuery& Other);
    ITerrainQuery(ITerrainQuery&& Other) noexcept;
    ITerrainQuery& operator=(ITerrainQuery&& Other) noexcept;

public:
    virtual bool TryGetSurfaceAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const = 0;
    virtual bool TryGetSurfaceHeightAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight) const = 0;
    virtual bool TryRaycast(const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const = 0;
};

class TerrainDataRepository final : public ITerrainQuery {
public:
    TerrainDataRepository();
    ~TerrainDataRepository() override;
    TerrainDataRepository(const TerrainDataRepository& Other) = delete;
    TerrainDataRepository& operator=(const TerrainDataRepository& Other) = delete;
    TerrainDataRepository(TerrainDataRepository&& Other) noexcept = delete;
    TerrainDataRepository& operator=(TerrainDataRepository&& Other) noexcept = delete;

public:
    void Clear();
    void PublishSnapshot(std::vector<TerrainWorldData> TerrainDataItems);
    void UpsertTerrainData(const TerrainWorldData& TerrainData);
    bool RemoveTerrainData(std::uint32_t TerrainId);
    std::shared_ptr<const TerrainSnapshot> GetSnapshot() const;

    bool TryGetSurfaceAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const override;
    bool TryGetSurfaceHeightAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight) const override;
    bool TryRaycast(const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const override;

private:
    mutable std::mutex mSnapshotMutex;
    std::shared_ptr<const TerrainSnapshot> mSnapshot;
};

bool IsTerrainWorldDataValid(const TerrainWorldData& TerrainData);
bool AreTerrainWorldDataEquivalent(const TerrainWorldData& Left, const TerrainWorldData& Right);
bool TryGetTerrainSurfaceAtWorldPosition(const TerrainWorldData& TerrainData, float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal);
bool TryGetTerrainSurfaceHeightAtWorldPosition(const TerrainWorldData& TerrainData, float WorldX, float WorldZ, float& OutWorldHeight);
bool TryRaycastTerrainWorldData(const TerrainWorldData& TerrainData, const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance);
