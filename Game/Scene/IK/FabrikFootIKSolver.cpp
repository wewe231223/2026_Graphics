#include "Game/Scene/IK/FabrikFootIKSolver.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
    constexpr float DirectionLengthEpsilon{ 1.0e-6f };
    constexpr float PiRadians{ 3.1415926536f };
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

    bool TryResolveDirectionAndLength(const DirectX::SimpleMath::Vector3& SourceDirection, DirectX::SimpleMath::Vector3& OutNormalizedDirection, float& OutDirectionLength) {
        if (IsFiniteVector3(SourceDirection) == false) {
            return false;
        }

        const float SourceLength{ SourceDirection.Length() };
        if (IsFiniteFloat(SourceLength) == false || SourceLength <= DirectionLengthEpsilon) {
            return false;
        }

        OutDirectionLength = SourceLength;
        OutNormalizedDirection = SourceDirection;
        OutNormalizedDirection.Normalize();
        return IsFiniteVector3(OutNormalizedDirection);
    }

    float ResolveClampedAngleRadians(const float AngleRadians) {
        if (IsFiniteFloat(AngleRadians) == false) {
            return 0.0f;
        }

        return ::std::clamp(AngleRadians, 0.0f, PiRadians);
    }

    float ResolveSegmentDistanceFromJointAngle(const float ParentSegmentLength, const float ChildSegmentLength, const float JointAngleRadians) {
        if (IsFiniteFloat(ParentSegmentLength) == false || IsFiniteFloat(ChildSegmentLength) == false) {
            return 0.0f;
        }

        const float SafeParentSegmentLength{ (::std::max)(ParentSegmentLength, 0.0f) };
        const float SafeChildSegmentLength{ (::std::max)(ChildSegmentLength, 0.0f) };
        const float SafeJointAngleRadians{ ResolveClampedAngleRadians(JointAngleRadians) };
        const float DistanceSquared{ (SafeParentSegmentLength * SafeParentSegmentLength) + (SafeChildSegmentLength * SafeChildSegmentLength) - (2.0f * SafeParentSegmentLength * SafeChildSegmentLength * ::std::cos(SafeJointAngleRadians)) };
        if (IsFiniteFloat(DistanceSquared) == false) {
            return 0.0f;
        }

        const float SafeDistanceSquared{ (::std::max)(DistanceSquared, 0.0f) };
        return ::std::sqrt(SafeDistanceSquared);
    }

    bool TryResolveDirectionOnPlane(const DirectX::SimpleMath::Vector3& SourceDirection, const DirectX::SimpleMath::Vector3& PlaneNormalDirection, DirectX::SimpleMath::Vector3& OutProjectedDirection) {
        DirectX::SimpleMath::Vector3 SafePlaneNormalDirection{};
        if (TryResolveNormalizedDirection(PlaneNormalDirection, SafePlaneNormalDirection) == false || IsFiniteVector3(SourceDirection) == false) {
            return false;
        }

        const DirectX::SimpleMath::Vector3 ProjectedDirection{ SourceDirection - (SafePlaneNormalDirection * SourceDirection.Dot(SafePlaneNormalDirection)) };
        return TryResolveNormalizedDirection(ProjectedDirection, OutProjectedDirection);
    }

    bool TryApplyJointConstraint(const Game::FootIKJointConstraint& JointConstraint, const ::std::vector<float>& SegmentLengths, ::std::vector<DirectX::SimpleMath::Vector3>& InOutJointPositions) {
        if (JointConstraint.mJointIndex == 0 || JointConstraint.mJointIndex + 1 >= InOutJointPositions.size()) {
            return false;
        }

        const ::std::size_t ParentJointIndex{ JointConstraint.mJointIndex - 1 };
        const ::std::size_t JointIndex{ JointConstraint.mJointIndex };
        const ::std::size_t ChildJointIndex{ JointConstraint.mJointIndex + 1 };
        if (ParentJointIndex >= SegmentLengths.size() || JointIndex >= SegmentLengths.size()) {
            return false;
        }

        const float ParentSegmentLength{ SegmentLengths[ParentJointIndex] };
        const float ChildSegmentLength{ SegmentLengths[JointIndex] };
        if (IsFiniteFloat(ParentSegmentLength) == false || IsFiniteFloat(ChildSegmentLength) == false || ParentSegmentLength <= DirectionLengthEpsilon || ChildSegmentLength <= DirectionLengthEpsilon) {
            return false;
        }

        const DirectX::SimpleMath::Vector3 ParentPosition{ InOutJointPositions[ParentJointIndex] };
        if (IsFiniteVector3(ParentPosition) == false) {
            return false;
        }

        DirectX::SimpleMath::Vector3 ChildPosition{ InOutJointPositions[ChildJointIndex] };
        if (IsFiniteVector3(ChildPosition) == false) {
            return false;
        }

        DirectX::SimpleMath::Vector3 ParentToChildDirection{};
        float ParentToChildDistance{};
        if (TryResolveDirectionAndLength(ChildPosition - ParentPosition, ParentToChildDirection, ParentToChildDistance) == false) {
            return false;
        }

        const float ReachMinimumDistance{ ::std::abs(ParentSegmentLength - ChildSegmentLength) };
        const float ReachMaximumDistance{ ParentSegmentLength + ChildSegmentLength };
        float DesiredParentToChildDistance{ ::std::clamp(ParentToChildDistance, ReachMinimumDistance, ReachMaximumDistance) };
        if (JointConstraint.mUseAngleLimit == true) {
            float SafeMinimumAngleRadians{ ResolveClampedAngleRadians(JointConstraint.mMinimumAngleRadians) };
            float SafeMaximumAngleRadians{ ResolveClampedAngleRadians(JointConstraint.mMaximumAngleRadians) };
            if (SafeMaximumAngleRadians < SafeMinimumAngleRadians) {
                ::std::swap(SafeMinimumAngleRadians, SafeMaximumAngleRadians);
            }

            const float MinimumParentToChildDistance{ ResolveSegmentDistanceFromJointAngle(ParentSegmentLength, ChildSegmentLength, SafeMinimumAngleRadians) };
            const float MaximumParentToChildDistance{ ResolveSegmentDistanceFromJointAngle(ParentSegmentLength, ChildSegmentLength, SafeMaximumAngleRadians) };
            DesiredParentToChildDistance = ::std::clamp(DesiredParentToChildDistance, MinimumParentToChildDistance, MaximumParentToChildDistance);
        }

        if (IsFiniteFloat(DesiredParentToChildDistance) == false || DesiredParentToChildDistance <= DirectionLengthEpsilon) {
            return false;
        }

        if (::std::abs(DesiredParentToChildDistance - ParentToChildDistance) > DirectionLengthEpsilon) {
            ChildPosition = ParentPosition + (ParentToChildDirection * DesiredParentToChildDistance);
            ParentToChildDistance = DesiredParentToChildDistance;
        }

        if (TryResolveNormalizedDirection(ChildPosition - ParentPosition, ParentToChildDirection) == false) {
            return false;
        }

        const float AlongDistanceRaw{ ((ParentSegmentLength * ParentSegmentLength) - (ChildSegmentLength * ChildSegmentLength) + (ParentToChildDistance * ParentToChildDistance)) / (2.0f * ParentToChildDistance) };
        if (IsFiniteFloat(AlongDistanceRaw) == false) {
            return false;
        }

        const float AlongDistance{ ::std::clamp(AlongDistanceRaw, 0.0f, ParentSegmentLength) };
        const float HeightSquaredRaw{ (ParentSegmentLength * ParentSegmentLength) - (AlongDistance * AlongDistance) };
        if (IsFiniteFloat(HeightSquaredRaw) == false) {
            return false;
        }

        const float Height{ ::std::sqrt((::std::max)(HeightSquaredRaw, 0.0f)) };
        if (IsFiniteFloat(Height) == false) {
            return false;
        }

        const DirectX::SimpleMath::Vector3 JointBasePosition{ ParentPosition + (ParentToChildDirection * AlongDistance) };
        if (IsFiniteVector3(JointBasePosition) == false) {
            return false;
        }

        DirectX::SimpleMath::Vector3 ConstrainedJointPosition{ JointBasePosition };
        if (Height > DirectionLengthEpsilon) {
            DirectX::SimpleMath::Vector3 PoleDirection{};
            bool IsPoleDirectionResolved{};
            if (JointConstraint.mUsePreferredBendDirection == true) {
                IsPoleDirectionResolved = TryResolveDirectionOnPlane(JointConstraint.mPreferredBendDirection, ParentToChildDirection, PoleDirection);
            }

            if (IsPoleDirectionResolved == false) {
                const DirectX::SimpleMath::Vector3 ParentToJointVector{ InOutJointPositions[JointIndex] - ParentPosition };
                const DirectX::SimpleMath::Vector3 JointProjectionOnParentToChildDirection{ ParentToChildDirection * ParentToJointVector.Dot(ParentToChildDirection) };
                const DirectX::SimpleMath::Vector3 CurrentPoleDirection{ ParentToJointVector - JointProjectionOnParentToChildDirection };
                IsPoleDirectionResolved = TryResolveNormalizedDirection(CurrentPoleDirection, PoleDirection);
            }

            if (IsPoleDirectionResolved == false) {
                return false;
            }

            ConstrainedJointPosition = JointBasePosition + (PoleDirection * Height);
        }

        if (IsFiniteVector3(ConstrainedJointPosition) == false) {
            return false;
        }

        InOutJointPositions[JointIndex] = ConstrainedJointPosition;
        InOutJointPositions[ChildJointIndex] = ChildPosition;
        return true;
    }

    void ApplyJointConstraints(const ::std::span<const Game::FootIKJointConstraint> JointConstraints, const ::std::vector<float>& SegmentLengths, ::std::vector<DirectX::SimpleMath::Vector3>& InOutJointPositions) {
        for (const Game::FootIKJointConstraint& JointConstraint : JointConstraints) {
            TryApplyJointConstraint(JointConstraint, SegmentLengths, InOutJointPositions);
        }
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

                ApplyJointConstraints(SolveParameters.mJointConstraints, SegmentLengths, SolvedJointPositions);

                const DirectX::SimpleMath::Vector3 EndToTargetVector{ SolveParameters.mTargetPosition - SolvedJointPositions[LastJointIndex] };
                const float EndToTargetDistance{ EndToTargetVector.Length() };
                if (IsFiniteFloat(EndToTargetDistance) == true && EndToTargetDistance <= SafeConvergenceDistance) {
                    OutSolveResult.mReachedTarget = true;
                    break;
                }
            }
        }

        ApplyJointConstraints(SolveParameters.mJointConstraints, SegmentLengths, SolvedJointPositions);

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
