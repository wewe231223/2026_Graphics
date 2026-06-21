#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include "DirectXTK12/SimpleMath.h"

namespace Game {
    struct FootIKJointConstraint final {
        std::size_t mJointIndex{};
        DirectX::SimpleMath::Vector3 mPreferredBendDirection{};
        float mMinimumAngleRadians{};
        float mMaximumAngleRadians{ 3.1415926536f };
        bool mUsePreferredBendDirection{};
        bool mUseAngleLimit{};
    };

    struct FootIKSolveParameters final {
        std::span<const DirectX::SimpleMath::Vector3> mJointPositions{};
        std::span<const FootIKJointConstraint> mJointConstraints{};
        DirectX::SimpleMath::Vector3 mTargetPosition{};
        int mMaxIterationCount{ 8 };
        float mConvergenceDistance{ 1.0e-3f };
    };

    struct FootIKSolveResult final {
        bool mReachedTarget{};
        std::vector<DirectX::SimpleMath::Vector3> mJointPositions{};
    };

    class IFootIKSolver {
    public:
        IFootIKSolver();
        virtual ~IFootIKSolver();
        IFootIKSolver(const IFootIKSolver& Other);
        IFootIKSolver& operator=(const IFootIKSolver& Other);
        IFootIKSolver(IFootIKSolver&& Other) noexcept;
        IFootIKSolver& operator=(IFootIKSolver&& Other) noexcept;

    public:
        virtual bool Solve(const FootIKSolveParameters& SolveParameters, FootIKSolveResult& OutSolveResult) const = 0;
        virtual std::unique_ptr<IFootIKSolver> Clone() const = 0;
    };
}
