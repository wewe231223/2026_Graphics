#pragma once
#include <utility>

namespace Game {
    namespace Pipeline {
        template <TrivialComponent T>
        const T* PipelineContext::ReadComponent(Arche::EntityID EntityId) const {
            if (mWorld == nullptr || ContainsEntity(EntityId) == false) {
                return nullptr;
            }

            return std::as_const(*mWorld).GetComponent<T>(EntityId);
        }

        template <TrivialComponent T>
        T* PipelineContext::WriteComponent(Arche::EntityID EntityId) {
            if (mWorld == nullptr || ContainsEntity(EntityId) == false) {
                return nullptr;
            }

            return mWorld->GetComponent<T>(EntityId);
        }

        template <TrivialComponent... Ts, typename Func>
        void PipelineContext::ForEach(Func&& FuncObject) {
            if (mWorld == nullptr) {
                return;
            }

            for (const Arche::EntityID EntityId : mEntityIds) {
                std::tuple<Ts*...> ComponentPointers{ mWorld->GetComponent<Ts>(EntityId)... };
                const bool HasAllComponents{ std::apply([](auto*... Pointers) { return ((Pointers != nullptr) && ...); }, ComponentPointers) };
                if (HasAllComponents == false) {
                    continue;
                }

                std::apply([&](auto*... Pointers) {
                    if constexpr (std::is_invocable_v<Func&, Arche::EntityID, Ts&...>) {
                        FuncObject(EntityId, *Pointers...);
                    }
                    else {
                        FuncObject(*Pointers...);
                    }
                }, ComponentPointers);
            }
        }
    }
}
