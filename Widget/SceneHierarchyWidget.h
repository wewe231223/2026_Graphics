#pragma once

#include <cstdint>
#include "Common.h"

namespace Game {
    class SceneWorldSnapshot;
}

namespace Widget {
    class SceneHierarchyWidget final : public IWidget {
    public:
        SceneHierarchyWidget();
        ~SceneHierarchyWidget() override;

        SceneHierarchyWidget(const SceneHierarchyWidget& Other) = delete;
        SceneHierarchyWidget& operator=(const SceneHierarchyWidget& Other) = delete;

        SceneHierarchyWidget(SceneHierarchyWidget&& Other) noexcept = delete;
        SceneHierarchyWidget& operator=(SceneHierarchyWidget&& Other) noexcept = delete;

    public:
        void Render(const Game::SceneWorldSnapshot* Snapshot) override;

    private:
        void RenderEntityNode(const Game::SceneWorldSnapshot& Snapshot, std::uint32_t EntityIndex);
    };
}
