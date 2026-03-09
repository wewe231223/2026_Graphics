#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <typeindex>
#include <vector>
#include "Arche/World.h"
#include "Game/Base/RenderFrameData.h"

namespace Game {
    struct RegisteredMaterialGroup;

    enum class Phase : std::uint32_t {
        PreUpdate,
        Update,
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

    struct FrameContext final {
        RFD::RenderFrameData RenderData{};
        const std::vector<RegisteredMaterialGroup>* MaterialGroups{ nullptr };
        Arche::EntityID PickedEntityId{ Arche::NullEntityID };
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
