#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <typeindex>
#include "Arche/World.h"
#include "Game/Base/RenderFrameData.h"

namespace Game {
    enum class Phase : std::uint32_t {
        PreUpdate,
        Update,
        Render,
        Count
    };

    enum class Access {
        Read,
        Write
    };

    struct ComponentAccess final {
        std::type_index Type{ typeid(void) };
        Access AccessMode{ Access::Read };
    };

    struct ResourceAccess final {
        std::type_index Type{ typeid(void) };
        Access AccessMode{ Access::Read };
    };

    struct FrameContext final {
        RFD::RenderFrameData RenderData{};
    };

    class ISystem abstract {
    public:
        virtual ~ISystem() = default;

    public:
        virtual const std::string& Name() const = 0;
        virtual Phase GetPhase() const = 0;
        virtual std::span<const ComponentAccess> ComponentAccesses() const = 0;
        virtual std::span<const ResourceAccess> ResourceAccesses() const = 0;
        virtual void Execute(Arche::World& World, FrameContext& Ctx, float Dt) = 0;
    };
}
