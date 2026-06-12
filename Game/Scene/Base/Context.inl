#pragma once
#include <utility>

namespace Game {
    namespace Pipeline {
        template <TrivialComponent T>
        const T* PipelineContext::ReadComponent(Arche::EntityID EntityId) const {
            if (mWorld == nullptr || EntityId == Arche::NullEntityID) {
                return nullptr;
            }

            return std::as_const(*mWorld).GetComponent<T>(EntityId);
        }

        template <TrivialComponent T>
        T* PipelineContext::WriteComponent(Arche::EntityID EntityId) {
            if (mWorld == nullptr || EntityId == Arche::NullEntityID) {
                return nullptr;
            }

            return const_cast<T*>(std::as_const(*mWorld).GetComponent<T>(EntityId));
        }

        template <TrivialComponent... Ts, typename Func>
        void PipelineContext::ForEach(Func&& FuncObject) {
            if (mWorld == nullptr) {
                return;
            }

            auto ExecuteForEntity = [&](Arche::EntityID EntityId) {
                if (EntityId == Arche::NullEntityID) {
                    return;
                }

                const Arche::World& WorldValue{ std::as_const(*mWorld) };
                std::tuple<Ts*...> ComponentPointers{ const_cast<Ts*>(WorldValue.GetComponent<Ts>(EntityId))... };
                const bool HasAllComponents{ std::apply([](auto*... Pointers) { return ((Pointers != nullptr) && ...); }, ComponentPointers) };
                if (HasAllComponents == false) {
                    return;
                }

                std::apply([&](auto*... Pointers) {
                    if constexpr (std::is_invocable_v<Func&, Arche::EntityID, Ts&...>) {
                        FuncObject(EntityId, *Pointers...);
                    }
                    else {
                        FuncObject(*Pointers...);
                    }
                }, ComponentPointers);
            };

            ExecuteForEntity(mUnitEntityId);

            for (const Arche::EntityID EntityId : mEntityIds) {
                if (EntityId == mUnitEntityId) {
                    continue;
                }

                ExecuteForEntity(EntityId);
            }
        }
    }
}
