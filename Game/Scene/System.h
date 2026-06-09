#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <typeindex>
#include "Arche/World.h"
#include "Game/Scene/FrameContext.h"

namespace Game {
    enum class Phase : std::uint32_t {
        PreUpdate,
        Update,
        PostUpdate,
        PhysicsActorUpdate,
        TransformWorld,
        IK,
        RenderPrepare,
        Render,
        PostRender, 
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

    class ISystem abstract {
    public:
        virtual ~ISystem() = default;

    public:
        virtual const std::string& Name() const                                 PURE;
        virtual Phase GetPhase() const                                          PURE;
        virtual std::span<const ComponentAccess> ComponentAccesses() const      PURE;
        virtual std::span<const ResourceAccess> ResourceAccesses() const        PURE;
        virtual void Execute(Arche::World& World, FrameContext& Ctx, float Dt)  PURE;
    };
}
