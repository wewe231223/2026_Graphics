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
            std::vector<std::vector<bool>> PhaseConflictMatrix(TargetPhaseSystems.size(), std::vector<bool>(TargetPhaseSystems.size(), false));
            for (std::size_t LeftIndex{ 0 }; LeftIndex < TargetPhaseSystems.size(); ++LeftIndex) {
                for (std::size_t RightIndex{ LeftIndex + 1 }; RightIndex < TargetPhaseSystems.size(); ++RightIndex) {
                    const bool IsConflict{ HasConflict(*TargetPhaseSystems[LeftIndex], *TargetPhaseSystems[RightIndex]) };
                    PhaseConflictMatrix[LeftIndex][RightIndex] = IsConflict;
                    PhaseConflictMatrix[RightIndex][LeftIndex] = IsConflict;
                }
            }

            std::vector<std::vector<std::size_t>> AdjacentIndices(TargetPhaseSystems.size());
            std::vector<std::size_t> InDegrees(TargetPhaseSystems.size(), 0);
            for (std::size_t LeftIndex{ 0 }; LeftIndex < TargetPhaseSystems.size(); ++LeftIndex) {
                for (std::size_t RightIndex{ LeftIndex + 1 }; RightIndex < TargetPhaseSystems.size(); ++RightIndex) {
                    if (PhaseConflictMatrix[LeftIndex][RightIndex] == false) {
                        continue;
                    }

                    AdjacentIndices[LeftIndex].push_back(RightIndex);
                    InDegrees[RightIndex] += 1;
                }
            }

            std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<std::size_t>> ReadyIndices{};
            for (std::size_t SystemIndex{ 0 }; SystemIndex < InDegrees.size(); ++SystemIndex) {
                if (InDegrees[SystemIndex] == 0) {
                    ReadyIndices.push(SystemIndex);
                }
            }

            std::vector<std::size_t> TopologicallySortedIndices{};
            TopologicallySortedIndices.reserve(TargetPhaseSystems.size());
            while (!ReadyIndices.empty()) {
                const std::size_t CurrentIndex{ ReadyIndices.top() };
                ReadyIndices.pop();
                TopologicallySortedIndices.push_back(CurrentIndex);

                for (const std::size_t NextIndex : AdjacentIndices[CurrentIndex]) {
                    InDegrees[NextIndex] -= 1;
                    if (InDegrees[NextIndex] == 0) {
                        ReadyIndices.push(NextIndex);
                    }
                }
            }

            if (TopologicallySortedIndices.size() != TargetPhaseSystems.size()) {
                TopologicallySortedIndices.clear();
                TopologicallySortedIndices.reserve(TargetPhaseSystems.size());
                for (std::size_t SystemIndex{ 0 }; SystemIndex < TargetPhaseSystems.size(); ++SystemIndex) {
                    TopologicallySortedIndices.push_back(SystemIndex);
                }
            }

            std::vector<std::vector<std::size_t>> BatchIndices{};
            BatchIndices.reserve(TopologicallySortedIndices.size());
            for (const std::size_t TargetSystemIndex : TopologicallySortedIndices) {
                bool IsAssigned{ false };
                for (std::vector<std::size_t>& TargetBatchIndices : BatchIndices) {
                    const bool IsConflictInBatch{ std::any_of(TargetBatchIndices.begin(), TargetBatchIndices.end(), [&](const std::size_t ExistingSystemIndex) {
                        return PhaseConflictMatrix[ExistingSystemIndex][TargetSystemIndex];
                    }) };

                    if (IsConflictInBatch == false) {
                        TargetBatchIndices.push_back(TargetSystemIndex);
                        IsAssigned = true;
                        break;
                    }
                }

                if (IsAssigned == false) {
                    BatchIndices.emplace_back();
                    BatchIndices.back().push_back(TargetSystemIndex);
                }
            }

            PhaseBatchArray NewBatches{};
            NewBatches.reserve(BatchIndices.size());
            for (const std::vector<std::size_t>& TargetBatchIndices : BatchIndices) {
                SystemBatch NewBatch{};
                NewBatch.reserve(TargetBatchIndices.size());
                for (const std::size_t SystemIndex : TargetBatchIndices) {
                    NewBatch.push_back(TargetPhaseSystems[SystemIndex]);
                }

                NewBatches.push_back(std::move(NewBatch));
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
