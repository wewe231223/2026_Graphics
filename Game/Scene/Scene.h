#pragma once
#include <memory>
#include <vector>
#include "Arche/World.h"
#include "System.h"
#include "SystemSceduler.h"

namespace Game {
    class Scene final {
    public:
        Scene();
        ~Scene();

        Scene(const Scene& Other) = delete;
        Scene& operator=(const Scene& Other) = delete;

        Scene(Scene&& Other) noexcept = delete;
        Scene& operator=(Scene&& Other) noexcept = delete;

    public:
        Arche::World& GetWorld();
        const Arche::World& GetWorld() const;

        FrameContext& GetFrameContext();
        const FrameContext& GetFrameContext() const;

        void AddSystem(std::unique_ptr<ISystem> NewSystem);
        void BuildSystemExecutionPlan();
        void ExecutePhase(Phase TargetPhase, float Dt);
        
    private:
        Arche::World mWorld{};
        FrameContext mFrameContext{};
        std::vector<std::unique_ptr<ISystem>> mSystems{};
        SystemSceduler mSystemSceduler{};
    };
}
