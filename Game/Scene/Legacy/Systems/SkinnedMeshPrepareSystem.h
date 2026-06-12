#pragma once

#include <string>
#include "Game/Scene/Legacy/ParallelSystemBase.h"

namespace Game {
    class SkinnedMeshPrepareSystem final : public ParallelSystemBase {
    public:
        SkinnedMeshPrepareSystem();
        ~SkinnedMeshPrepareSystem() override;
        SkinnedMeshPrepareSystem(const SkinnedMeshPrepareSystem& Other) = delete;
        SkinnedMeshPrepareSystem& operator=(const SkinnedMeshPrepareSystem& Other) = delete;
        SkinnedMeshPrepareSystem(SkinnedMeshPrepareSystem&& Other) noexcept = delete;
        SkinnedMeshPrepareSystem& operator=(SkinnedMeshPrepareSystem&& Other) noexcept = delete;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "SkinnedMeshPrepareSystem" };
    };
}
