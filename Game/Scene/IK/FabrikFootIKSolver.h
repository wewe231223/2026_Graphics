#pragma once

#include <memory>
#include "Game/Scene/IK/FootIKSolver.h"

namespace Game {
    class FabrikFootIKSolver final : public IFootIKSolver {
    public:
        FabrikFootIKSolver();
        ~FabrikFootIKSolver() override;
        FabrikFootIKSolver(const FabrikFootIKSolver& Other);
        FabrikFootIKSolver& operator=(const FabrikFootIKSolver& Other);
        FabrikFootIKSolver(FabrikFootIKSolver&& Other) noexcept;
        FabrikFootIKSolver& operator=(FabrikFootIKSolver&& Other) noexcept;

    public:
        bool Solve(const FootIKSolveParameters& SolveParameters, FootIKSolveResult& OutSolveResult) const override;
        std::unique_ptr<IFootIKSolver> Clone() const override;
    };

    std::unique_ptr<IFootIKSolver> CreateFabrikFootIKSolver();
}
