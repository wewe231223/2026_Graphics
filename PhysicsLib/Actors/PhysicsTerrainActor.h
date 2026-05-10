#pragma once

/*
PhysicsLib Header Guide
Role:
- Provides a Static Actor for HeightField terrain surface queries and Dynamic Actor terrain collision.
Initialization:
- Fill ActorDesc HeightFieldWidth, HeightFieldHeight, HeightFieldCellSizeX, HeightFieldCellSizeZ, HeightFieldMaxHeight, and HeightFieldValues.
Usage:
- Register through PhysicsWorld::CreateTerrainActor and query surface height with TryGetSurfaceHeightAtWorldPosition.
Notes:
- HeightFieldValues should contain width * height normalized height samples for stable interpolation.
*/

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <DirectXTK12/SimpleMath.h>

#include "PhysicsLib/Actors/PhysicsStaticActor.h"

class PhysicsTerrainActor final : public PhysicsStaticActor {
public:
    struct ActorDesc {
        DirectX::SimpleMath::Vector3 Position{};
        DirectX::SimpleMath::Vector3 Rotation{};
        DirectX::SimpleMath::Vector3 Scale{};
        float HalfExtentX{};
        float HalfExtentZ{};
        std::uint32_t HeightFieldWidth{};
        std::uint32_t HeightFieldHeight{};
        float HeightFieldCellSizeX{};
        float HeightFieldCellSizeZ{};
        float HeightFieldMaxHeight{};
        bool HeightFieldCenterOrigin{};
        std::vector<float> HeightFieldValues{};
    };

public:
    PhysicsTerrainActor();
    ~PhysicsTerrainActor() override;
    PhysicsTerrainActor(const PhysicsTerrainActor& Other);
    PhysicsTerrainActor& operator=(const PhysicsTerrainActor& Other);
    PhysicsTerrainActor(PhysicsTerrainActor&& Other) noexcept;
    PhysicsTerrainActor& operator=(PhysicsTerrainActor&& Other) noexcept;

    explicit PhysicsTerrainActor(const ActorDesc& Desc);

public:
    void SetActorDesc(const ActorDesc& Desc);
    ActorDesc GetActorDesc() const;
    static ActorDesc BuildHeightFieldActorDesc(std::uint32_t HeightFieldWidth, std::uint32_t HeightFieldHeight, const std::vector<float>& HeightFieldValues, float HeightFieldMaxHeight, float HeightFieldCellSizeX, float HeightFieldCellSizeZ, bool HeightFieldCenterOrigin);
    bool IsTerrainActor() const override;

    bool TryGetSurfaceAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const;
    bool TryGetSurfaceHeightAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight) const;
    bool TryRaycast(const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const;
    bool ResolveDynamicCollision(PhysicsActorBase& DynamicActor, float DeltaTime) const override;
    std::unique_ptr<PhysicsActorBase> Clone() const override;

private:
    bool TryGetSurfaceHeightAtLocalPosition(float LocalX, float LocalZ, float& OutLocalHeight) const;
    bool TryGetSurfaceNormalAtLocalPosition(float LocalX, float LocalZ, DirectX::SimpleMath::Vector3& OutLocalNormal) const;
    bool TryResolveSurfaceAtLocalPosition(float LocalX, float LocalZ, float& OutLocalHeight, DirectX::SimpleMath::Vector3& OutLocalNormal) const;
    std::size_t CalculateHeightFieldIndex(std::uint32_t X, std::uint32_t Z) const;
    float SampleCellHeight(std::uint32_t X, std::uint32_t Z) const;

private:
    float mHalfExtentX;
    float mHalfExtentZ;
    std::uint32_t mHeightFieldWidth;
    std::uint32_t mHeightFieldHeight;
    float mHeightFieldCellSizeX;
    float mHeightFieldCellSizeZ;
    float mHeightFieldMaxHeight;
    bool mHeightFieldCenterOrigin;
    std::vector<float> mHeightFieldValues;
};
