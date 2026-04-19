#include "Game/Scene/Systems/FootIKSolver.h"

namespace Game {
    IFootIKSolver::IFootIKSolver() = default;

    IFootIKSolver::~IFootIKSolver() = default;

    IFootIKSolver::IFootIKSolver(const IFootIKSolver& Other) = default;

    IFootIKSolver& IFootIKSolver::operator=(const IFootIKSolver& Other) = default;

    IFootIKSolver::IFootIKSolver(IFootIKSolver&& Other) noexcept = default;

    IFootIKSolver& IFootIKSolver::operator=(IFootIKSolver&& Other) noexcept = default;
}
