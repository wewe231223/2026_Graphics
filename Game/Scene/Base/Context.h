#pragma once
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>
#include "Arche/World.h"
#include "Game/Scene/Components/Frustum.h"
#include "RenderContract/Gather/RenderGatherResult.h"

namespace Terrain {
    class ITerrainQuery;
}

namespace Game {
    struct RegisteredMaterialGroup;

    namespace Pipeline {
        struct PipelineFrameInput final {
        public:
            const std::vector<RegisteredMaterialGroup>* mMaterialGroups{};
            const Terrain::ITerrainQuery* mTerrainQueryResource{};
            Arche::EntityID mPickedEntityId{ Arche::NullEntityID };
            std::uint32_t mFrameIndex{};
            std::uint32_t mRenderFlags{};
            RenderContract::ShadowMappingParameter mShadowMappingParameter{};
            Frustum mActiveCameraFrustum{};
            SimpleMath::Vector3 mActiveCameraPosition{};
            bool mHasActiveCameraFrustum{};
            bool mHasActiveCameraPosition{};
        };

        class PipelineContext final {
        public:
            PipelineContext(Arche::World& World, Arche::EntityID UnitEntityId, std::span<const Arche::EntityID> EntityIds, const PipelineFrameInput& FrameInput, RenderContract::RenderGatherResult& RenderGatherResultValue);
            ~PipelineContext();

            PipelineContext(const PipelineContext& Other);
            PipelineContext& operator=(const PipelineContext& Other);

            PipelineContext(PipelineContext&& Other) noexcept;
            PipelineContext& operator=(PipelineContext&& Other) noexcept;

        public:
            Arche::EntityID GetUnitEntityId() const;
            Arche::World& GetWorld();
            const Arche::World& GetWorld() const;
            const PipelineFrameInput& GetFrameInput() const;
            const std::vector<RegisteredMaterialGroup>* GetMaterialGroups() const;
            const Terrain::ITerrainQuery* GetTerrainQuery() const;
            Arche::EntityID GetPickedEntityId() const;
            std::uint32_t GetFrameIndex() const;
            std::uint32_t GetRenderFlags() const;
            bool HasRenderFlag(std::uint32_t Flag) const;
            const RenderContract::ShadowMappingParameter& GetShadowMappingParameter() const;
            const Frustum* GetActiveCameraFrustum() const;
            bool GetActiveCameraPosition(SimpleMath::Vector3& OutCameraPosition) const;

            template <TrivialComponent T>
            const T* ReadComponent(Arche::EntityID EntityId) const;

            template <TrivialComponent T>
            T* WriteComponent(Arche::EntityID EntityId);

            template <TrivialComponent... Ts, typename Func>
            void ForEach(Func&& FuncObject);

            void AddRenderResult(const RenderContract::RenderGatherResult& RenderResult);
            void AddRenderResult(RenderContract::RenderGatherResult&& RenderResult);
            RenderContract::RenderGatherResult& GetRenderGatherResult();
            const RenderContract::RenderGatherResult& GetRenderGatherResult() const;

        private:
            Arche::World* mWorld{};
            Arche::EntityID mUnitEntityId{ Arche::NullEntityID };
            std::span<const Arche::EntityID> mEntityIds{};
            PipelineFrameInput mFrameInput{};
            RenderContract::RenderGatherResult* mRenderGatherResult{};
        };
    }
}

#include "Context.inl"
