#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <typeindex>
#include <vector>
#include <unordered_map>
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
        Skinning,
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

    struct SkinnedPoseCacheEntry final {
        std::uint32_t SkinArrayIndex{};
        std::vector<SimpleMath::Matrix> BoneMatrices{};
        bool IsValid{};
    };

    struct FrameContext final {
        RFD::RenderFrameData RenderData{};
        const std::vector<RegisteredMaterialGroup>* MaterialGroups{ nullptr };
        AssetRegistry* AssetRegistryResource{ nullptr };
        Arche::EntityID PickedEntityId{ Arche::NullEntityID };
        std::unordered_map<Arche::EntityID, SimpleMath::Matrix> WorldMatrices{};
        std::unordered_map<Arche::EntityID, SkinnedPoseCacheEntry> SkinnedPoseCache{};
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
