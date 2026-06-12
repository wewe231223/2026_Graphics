#pragma once
#include <array>
#include <memory>
#include <vector>
#include "Game/Scene/Base/SynchronousSystem.h"

namespace Game {
    class SystemSceduler final {
    public:
        using SystemBatch = std::vector<ISystem*>;
        using PhaseBatchArray = std::vector<SystemBatch>;

    public:
        SystemSceduler();
        ~SystemSceduler();
        SystemSceduler(const SystemSceduler& Other);
        SystemSceduler& operator=(const SystemSceduler& Other);
        SystemSceduler(SystemSceduler&& Other) noexcept;
        SystemSceduler& operator=(SystemSceduler&& Other) noexcept;

    public:
        void BuildExecutionPlan(const std::vector<std::unique_ptr<ISystem>>& Systems);
        const PhaseBatchArray* GetPhaseBatches(Phase TargetPhase) const;

    private:
        std::size_t ConvertPhaseToIndex(Phase TargetPhase) const;
        bool HasConflict(const ISystem& Left, const ISystem& Right) const;
        bool HasTypeAccessConflict(std::span<const ComponentAccess> Left, std::span<const ComponentAccess> Right) const;
        bool HasTypeAccessConflict(std::span<const ResourceAccess> Left, std::span<const ResourceAccess> Right) const;

    private:
        std::array<PhaseBatchArray, static_cast<std::size_t>(Phase::Count)> mPhaseToBatches{};
    };
}
