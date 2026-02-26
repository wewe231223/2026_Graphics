#include "SystemSceduler.h"
#include <algorithm>
#include <queue>

namespace Game {
    SystemSceduler::SystemSceduler()
        : mPhaseToBatches{} {
    }

    SystemSceduler::~SystemSceduler() {
    }

    SystemSceduler::SystemSceduler(const SystemSceduler& Other)
        : mPhaseToBatches{ Other.mPhaseToBatches } {
    }

    SystemSceduler& SystemSceduler::operator=(const SystemSceduler& Other) {
        if (this == &Other) {
            return *this;
        }

        mPhaseToBatches = Other.mPhaseToBatches;
        return *this;
    }

    SystemSceduler::SystemSceduler(SystemSceduler&& Other) noexcept
        : mPhaseToBatches{ std::move(Other.mPhaseToBatches) } {
    }

    SystemSceduler& SystemSceduler::operator=(SystemSceduler&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mPhaseToBatches = std::move(Other.mPhaseToBatches);
        return *this;
    }

    void SystemSceduler::BuildExecutionPlan(const std::vector<std::unique_ptr<ISystem>>& Systems) {
        for (PhaseBatchArray& Batches : mPhaseToBatches) {
            Batches.clear();
        }

        std::array<std::vector<ISystem*>, static_cast<std::size_t>(Phase::Count)> PhaseToSystems{};
        for (const std::unique_ptr<ISystem>& System : Systems) {
            const std::size_t PhaseIndex{ ConvertPhaseToIndex(System->GetPhase()) };
            PhaseToSystems[PhaseIndex].push_back(System.get());
        }

        for (std::size_t PhaseIndex{ 0 }; PhaseIndex < PhaseToSystems.size(); ++PhaseIndex) {
            std::vector<ISystem*>& TargetPhaseSystems{ PhaseToSystems[PhaseIndex] };

            std::vector<std::vector<std::size_t>> AdjacentIndices(TargetPhaseSystems.size());
            std::vector<std::size_t> InDegrees(TargetPhaseSystems.size(), 0);

            for (std::size_t LeftIndex{ 0 }; LeftIndex < TargetPhaseSystems.size(); ++LeftIndex) {
                for (std::size_t RightIndex{ LeftIndex + 1 }; RightIndex < TargetPhaseSystems.size(); ++RightIndex) {
                    if (HasConflict(*TargetPhaseSystems[LeftIndex], *TargetPhaseSystems[RightIndex])) {
                        AdjacentIndices[LeftIndex].push_back(RightIndex);
                        InDegrees[RightIndex] += 1;
                    }
                }
            }

            std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<std::size_t>> ReadyIndices{};
            for (std::size_t SystemIndex{ 0 }; SystemIndex < InDegrees.size(); ++SystemIndex) {
                if (InDegrees[SystemIndex] == 0) {
                    ReadyIndices.push(SystemIndex);
                }
            }

            std::vector<ISystem*> TopologicallySortedSystems{};
            TopologicallySortedSystems.reserve(TargetPhaseSystems.size());

            while (!ReadyIndices.empty()) {
                const std::size_t CurrentIndex{ ReadyIndices.top() };
                ReadyIndices.pop();
                TopologicallySortedSystems.push_back(TargetPhaseSystems[CurrentIndex]);

                for (const std::size_t NextIndex : AdjacentIndices[CurrentIndex]) {
                    InDegrees[NextIndex] -= 1;
                    if (InDegrees[NextIndex] == 0) {
                        ReadyIndices.push(NextIndex);
                    }
                }
            }

            if (TopologicallySortedSystems.size() != TargetPhaseSystems.size()) {
                TopologicallySortedSystems = TargetPhaseSystems;
            }

            PhaseBatchArray NewBatches{};
            for (ISystem* TargetSystem : TopologicallySortedSystems) {
                bool IsAssigned{ false };

                for (SystemBatch& TargetBatch : NewBatches) {
                    const bool IsConflictInBatch{ std::any_of(TargetBatch.begin(), TargetBatch.end(), [&](const ISystem* ExistingSystem) {
                        return HasConflict(*ExistingSystem, *TargetSystem);
                    }) };

                    if (!IsConflictInBatch) {
                        TargetBatch.push_back(TargetSystem);
                        IsAssigned = true;
                        break;
                    }
                }

                if (!IsAssigned) {
                    SystemBatch NewBatch{};
                    NewBatch.push_back(TargetSystem);
                    NewBatches.push_back(std::move(NewBatch));
                }
            }

            mPhaseToBatches[PhaseIndex] = std::move(NewBatches);
        }
    }

    const SystemSceduler::PhaseBatchArray* SystemSceduler::GetPhaseBatches(Phase TargetPhase) const {
        if (TargetPhase == Phase::Count) {
            return nullptr;
        }

        const std::size_t PhaseIndex{ ConvertPhaseToIndex(TargetPhase) };
        return &mPhaseToBatches[PhaseIndex];
    }

    std::size_t SystemSceduler::ConvertPhaseToIndex(Phase TargetPhase) const {
        return static_cast<std::size_t>(TargetPhase);
    }

    bool SystemSceduler::HasConflict(const ISystem& Left, const ISystem& Right) const {
        const bool HasComponentConflict{ HasTypeAccessConflict(Left.ComponentAccesses(), Right.ComponentAccesses()) };
        if (HasComponentConflict) {
            return true;
        }

        return HasTypeAccessConflict(Left.ResourceAccesses(), Right.ResourceAccesses());
    }

    bool SystemSceduler::HasTypeAccessConflict(std::span<const ComponentAccess> Left, std::span<const ComponentAccess> Right) const {
        for (const ComponentAccess& LeftAccess : Left) {
            for (const ComponentAccess& RightAccess : Right) {
                if (LeftAccess.Type != RightAccess.Type) {
                    continue;
                }

                if (LeftAccess.AccessMode == Access::Write || RightAccess.AccessMode == Access::Write) {
                    return true;
                }
            }
        }

        return false;
    }

    bool SystemSceduler::HasTypeAccessConflict(std::span<const ResourceAccess> Left, std::span<const ResourceAccess> Right) const {
        for (const ResourceAccess& LeftAccess : Left) {
            for (const ResourceAccess& RightAccess : Right) {
                if (LeftAccess.Type != RightAccess.Type) {
                    continue;
                }

                if (LeftAccess.AccessMode == Access::Write || RightAccess.AccessMode == Access::Write) {
                    return true;
                }
            }
        }

        return false;
    }
}
