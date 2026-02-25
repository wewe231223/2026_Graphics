#pragma once 
#include <concepts>
#include "Game/Scene/System.h"

template<typename T>
concept Resettable = requires(T t) {
    { t.Reset() } -> std::same_as<void>;
};


namespace Game {
    template<Resettable... TIntents>
    class CleanUpSystem : public ISystem {
    public:
		CleanUpSystem() = default;
		virtual ~CleanUpSystem() = default;

		CleanUpSystem(const CleanUpSystem&) = default;
		CleanUpSystem& operator=(const CleanUpSystem&) = default;

		CleanUpSystem(CleanUpSystem&&) = default;
		CleanUpSystem& operator=(CleanUpSystem&&) = default;

    public:
        // 시스템 이름 정의
        virtual const std::string& Name() const override {
            return mName;
        }

        virtual Phase GetPhase() const override {
            return Phase::PostRender;
        }

        virtual std::span<const ComponentAccess> ComponentAccesses() const override {
            static std::vector<ComponentAccess> accesses = {
                { typeid(TIntents), Access::Write }...
            };
            return accesses;
        }

        // 리소스 접근 (필요 없다면 빈 값)
        virtual std::span<const ResourceAccess> ResourceAccesses() const override {
            return {};
        }

        virtual void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override {
            // Fold Expression을 사용하여 각 Intent의 Reset() 호출
            ([&] {
                auto view = World.Query<TIntents>();
                for (auto [intent] : view) {
                    intent.Reset();
                }
                }(), ...);
        }
    private:
        const std::string mName{ "CleanUpSystem" };
    };
}