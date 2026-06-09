#pragma once
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>
#include "Arche/World.h"
#include "Game/Scene/Pipeline/RenderGatherResult.h"

namespace Game {
    namespace Pipeline {
        class PipelineContext final {
        public:
            PipelineContext(Arche::World& World, Arche::EntityID UnitEntityId, std::span<const Arche::EntityID> EntityIds, RenderGatherResult& RenderGatherResult);
            ~PipelineContext();

            PipelineContext(const PipelineContext& Other);
            PipelineContext& operator=(const PipelineContext& Other);

            PipelineContext(PipelineContext&& Other) noexcept;
            PipelineContext& operator=(PipelineContext&& Other) noexcept;

        public:
            Arche::EntityID GetUnitEntityId() const;
            bool ContainsEntity(Arche::EntityID EntityId) const;

            template <TrivialComponent T>
            const T* ReadComponent(Arche::EntityID EntityId) const;

            template <TrivialComponent T>
            T* WriteComponent(Arche::EntityID EntityId);

            template <TrivialComponent... Ts, typename Func>
            void ForEach(Func&& FuncObject);

            void AddRenderResult(const RenderGatherResult& RenderResult);
            void AddRenderResult(RenderGatherResult&& RenderResult);
            RenderGatherResult& GetRenderGatherResult();
            const RenderGatherResult& GetRenderGatherResult() const;

        private:
            Arche::World* mWorld{};
            Arche::EntityID mUnitEntityId{ Arche::NullEntityID };
            std::span<const Arche::EntityID> mEntityIds{};
            RenderGatherResult* mRenderGatherResult{};
        };
    }
}

#include "PipelineContext.inl"
