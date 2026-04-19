#include "Game/Scene/Systems/FabrikFootIKSolver.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
    constexpr float DirectionLengthEpsilon{ 1.0e-6f };
    constexpr DirectX::SimpleMath::Vector3 DefaultDirection{ 0.0f, 1.0f, 0.0f };

    bool IsFiniteFloat(const float Value) {
        return ::std::isfinite(Value) != 0;
    }

    bool IsFiniteVector3(const DirectX::SimpleMath::Vector3& Value) {
        return IsFiniteFloat(Value.x) && IsFiniteFloat(Value.y) && IsFiniteFloat(Value.z);
    }

    bool TryResolveNormalizedDirection(const DirectX::SimpleMath::Vector3& SourceDirection, DirectX::SimpleMath::Vector3& OutNormalizedDirection) {
        if (IsFiniteVector3(SourceDirection) == false) {
            return false;
        }

        const float SourceLengthSquared{ SourceDirection.LengthSquared() };
        if (IsFiniteFloat(SourceLengthSquared) == false || SourceLengthSquared <= DirectionLengthEpsilon) {
            return false;
        }

        OutNormalizedDirection = SourceDirection;
        OutNormalizedDirection.Normalize();
        return IsFiniteVector3(OutNormalizedDirection);
    }
}

namespace Game {
    FabrikFootIKSolver::FabrikFootIKSolver() = default;

    FabrikFootIKSolver::~FabrikFootIKSolver() = default;

    FabrikFootIKSolver::FabrikFootIKSolver(const FabrikFootIKSolver& Other) = default;

    FabrikFootIKSolver& FabrikFootIKSolver::operator=(const FabrikFootIKSolver& Other) = default;

    FabrikFootIKSolver::FabrikFootIKSolver(FabrikFootIKSolver&& Other) noexcept = default;

    FabrikFootIKSolver& FabrikFootIKSolver::operator=(FabrikFootIKSolver&& Other) noexcept = default;

    bool FabrikFootIKSolver::Solve(const FootIKSolveParameters& SolveParameters, FootIKSolveResult& OutSolveResult) const {
        OutSolveResult.mReachedTarget = false;
        OutSolveResult.mJointPositions.clear();
        if (SolveParameters.mJointPositions.size() < 2 || IsFiniteVector3(SolveParameters.mTargetPosition) == false) {
            return false;
        }

        const int SafeMaxIterationCount{ SolveParameters.mMaxIterationCount > 0 ? SolveParameters.mMaxIterationCount : 1 };
        const float SafeConvergenceDistance{ IsFiniteFloat(SolveParameters.mConvergenceDistance) && SolveParameters.mConvergenceDistance > 0.0f ? SolveParameters.mConvergenceDistance : 1.0e-3f };
        ::std::vector<DirectX::SimpleMath::Vector3> SolvedJointPositions{ SolveParameters.mJointPositions.begin(), SolveParameters.mJointPositions.end() };
        ::std::vector<float> SegmentLengths{};
        ::std::vector<DirectX::SimpleMath::Vector3> FallbackDirections{};
        SegmentLengths.reserve(SolvedJointPositions.size() - 1);
        FallbackDirections.reserve(SolvedJointPositions.size() - 1);

        float TotalLength{};
        for (::std::size_t JointIndex{ 0 }; JointIndex + 1 < SolvedJointPositions.size(); ++JointIndex) {
            const DirectX::SimpleMath::Vector3 SegmentVector{ SolvedJointPositions[JointIndex + 1] - SolvedJointPositions[JointIndex] };
            DirectX::SimpleMath::Vector3 SegmentDirection{};
            if (TryResolveNormalizedDirection(SegmentVector, SegmentDirection) == false) {
                return false;
            }

            const float SegmentLength{ SegmentVector.Length() };
            if (IsFiniteFloat(SegmentLength) == false || SegmentLength <= DirectionLengthEpsilon) {
                return false;
            }

            SegmentLengths.push_back(SegmentLength);
            FallbackDirections.push_back(SegmentDirection);
            TotalLength += SegmentLength;
        }

        if (IsFiniteFloat(TotalLength) == false || TotalLength <= DirectionLengthEpsilon) {
            return false;
        }

        const DirectX::SimpleMath::Vector3 RootPosition{ SolvedJointPositions.front() };
        if (IsFiniteVector3(RootPosition) == false) {
            return false;
        }

        const DirectX::SimpleMath::Vector3 RootToTargetVector{ SolveParameters.mTargetPosition - RootPosition };
        const float RootToTargetDistance{ RootToTargetVector.Length() };
        if (IsFiniteFloat(RootToTargetDistance) == false) {
            return false;
        }

        if (RootToTargetDistance >= TotalLength) {
            DirectX::SimpleMath::Vector3 StretchDirection{};
            if (TryResolveNormalizedDirection(RootToTargetVector, StretchDirection) == false) {
                StretchDirection = FallbackDirections.empty() == true ? DefaultDirection : FallbackDirections.front();
            }

            SolvedJointPositions.front() = RootPosition;
            for (::std::size_t JointIndex{ 0 }; JointIndex + 1 < SolvedJointPositions.size(); ++JointIndex) {
                SolvedJointPositions[JointIndex + 1] = SolvedJointPositions[JointIndex] + (StretchDirection * SegmentLengths[JointIndex]);
            }
        }
        else {
            const ::std::size_t LastJointIndex{ SolvedJointPositions.size() - 1 };
            for (int IterationIndex{}; IterationIndex < SafeMaxIterationCount; ++IterationIndex) {
                SolvedJointPositions[LastJointIndex] = SolveParameters.mTargetPosition;
                for (::std::size_t ReverseIndex{ LastJointIndex }; ReverseIndex > 0; --ReverseIndex) {
                    const ::std::size_t ParentJointIndex{ ReverseIndex - 1 };
                    DirectX::SimpleMath::Vector3 ParentDirection{ SolvedJointPositions[ParentJointIndex] - SolvedJointPositions[ReverseIndex] };
                    if (TryResolveNormalizedDirection(ParentDirection, ParentDirection) == false) {
                        ParentDirection = FallbackDirections[ParentJointIndex];
                    }

                    SolvedJointPositions[ParentJointIndex] = SolvedJointPositions[ReverseIndex] + (ParentDirection * SegmentLengths[ParentJointIndex]);
                }

                SolvedJointPositions.front() = RootPosition;
                for (::std::size_t JointIndex{}; JointIndex + 1 < SolvedJointPositions.size(); ++JointIndex) {
                    DirectX::SimpleMath::Vector3 ChildDirection{ SolvedJointPositions[JointIndex + 1] - SolvedJointPositions[JointIndex] };
                    if (TryResolveNormalizedDirection(ChildDirection, ChildDirection) == false) {
                        ChildDirection = FallbackDirections[JointIndex];
                    }

                    SolvedJointPositions[JointIndex + 1] = SolvedJointPositions[JointIndex] + (ChildDirection * SegmentLengths[JointIndex]);
                }

                const DirectX::SimpleMath::Vector3 EndToTargetVector{ SolveParameters.mTargetPosition - SolvedJointPositions[LastJointIndex] };
                const float EndToTargetDistance{ EndToTargetVector.Length() };
                if (IsFiniteFloat(EndToTargetDistance) == true && EndToTargetDistance <= SafeConvergenceDistance) {
                    OutSolveResult.mReachedTarget = true;
                    break;
                }
            }
        }

        if (OutSolveResult.mReachedTarget == false) {
            const DirectX::SimpleMath::Vector3 EndToTargetVector{ SolveParameters.mTargetPosition - SolvedJointPositions.back() };
            const float EndToTargetDistance{ EndToTargetVector.Length() };
            OutSolveResult.mReachedTarget = IsFiniteFloat(EndToTargetDistance) == true && EndToTargetDistance <= SafeConvergenceDistance;
        }

        OutSolveResult.mJointPositions = ::std::move(SolvedJointPositions);
        return true;
    }

    std::unique_ptr<IFootIKSolver> FabrikFootIKSolver::Clone() const {
        return ::std::make_unique<FabrikFootIKSolver>(*this);
    }

    std::unique_ptr<IFootIKSolver> CreateFabrikFootIKSolver() {
        return ::std::make_unique<FabrikFootIKSolver>();
    }
}
