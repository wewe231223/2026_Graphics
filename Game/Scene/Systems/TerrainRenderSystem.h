#pragma once

#include <string>
#include "Arche/Common.h"
#include "Game/Scene/System.h"

namespace Game {
    struct Frustum;
    struct TerrainTileMetadata;
    class TerrainRenderResource;

    class TerrainRenderSystem final : public ISystem {
    public:
        TerrainRenderSystem();
        ~TerrainRenderSystem() override;
        TerrainRenderSystem(const TerrainRenderSystem& Other);
        TerrainRenderSystem& operator=(const TerrainRenderSystem& Other);
        TerrainRenderSystem(TerrainRenderSystem&& Other) noexcept;
        TerrainRenderSystem& operator=(TerrainRenderSystem&& Other) noexcept;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        std::uint32_t SelectLodIndex(const TerrainTileMetadata& TileMetadata, const SimpleMath::Matrix& WorldMatrix, const SimpleMath::Vector3& CameraPosition, bool HasCameraPosition, const TerrainRenderResource& Resource) const;
        bool IsTileVisibleByFrustum(const DirectX::BoundingOrientedBox& LocalBoundingBox, const SimpleMath::Matrix& WorldMatrix, const Frustum* CullingFrustumComponent, bool IsFrustumCullingEnabled, DirectX::BoundingOrientedBox& OutWorldBoundingBox) const;

    private:
        const std::string mName{ "TerrainRenderSystem" };
    };
}
