#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <typeindex>
#include <vector>
#include "Utility/DirectXInclude.h"
#include "Arche/World.h"
#include "Game/Base/RenderFrameData.h"
#include "Utility/SimpleMathWrapper.h"

namespace Game {
    class AssetRegistry;
    struct RegisteredMaterialGroup;

    enum class Phase : std::uint32_t {
        PreUpdate,
        Update,
        PostUpdate,
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

    struct SkinnedMeshPreparedData final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        std::vector<SimpleMath::Matrix> BonePalette{};
        std::vector<RFD::BoundingBoxContext> BoneBoundingBoxContexts{};
    };

    struct FrameContext final {
        RFD::RenderFrameData RenderData{};
        const std::vector<RegisteredMaterialGroup>* MaterialGroups{ nullptr };
        AssetRegistry* AssetRegistryResource{ nullptr };
        Arche::EntityID PickedEntityId{ Arche::NullEntityID };
        std::vector<SkinnedMeshPreparedData> SkinnedMeshPreparedDataItems{};
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
