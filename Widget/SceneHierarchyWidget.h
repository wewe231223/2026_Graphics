#pragma once

#include <cstdint>
#include <vector>
#include "Common.h"
#include "Game/Scene/Components/ComponentInspection.h"

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
        void RenderSelectedEntityPanel(const Game::SceneWorldSnapshot& Snapshot);
        void RenderComponentSectionTable(const char* ComponentName, const std::vector<Game::ComponentInspectionField>& Fields, const char* TableIdentifier) const;

    private:
        std::uint32_t mSelectedEntityIndex{};
        float mHierarchyRegionRatio{ 0.6f };
    };
}
