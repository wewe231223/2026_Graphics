#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Arche/Common.h"
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
        struct AnimatorClipOption final {
        public:
            std::int32_t mClipIndex{ -1 };
            std::string mLabel{};
        };

    private:
        struct BoneEntityOption final {
        public:
            Arche::EntityID mEntityId{ Arche::NullEntityID };
            std::string mLabel{};
        };

    private:
        void RenderEntityNode(const Game::SceneWorldSnapshot& Snapshot, std::uint32_t EntityIndex);
        void RenderSelectedEntityPanel(const Game::SceneWorldSnapshot& Snapshot);
        void RenderComponentSectionTable(const char* ComponentName, const std::vector<Game::ComponentInspectionField>& Fields, const char* TableIdentifier) const;
        void RenderAnimatorEditor(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId);
        void BuildAnimatorClipOptions(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId, std::vector<AnimatorClipOption>& OutOptions) const;
        void RenderBoneSkinReferenceEditor(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId);
        void RenderCameraEditor(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId);
        void RenderRuntimeVariableTablePanel(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId);
        void BuildBoneEntityOptions(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId, std::vector<BoneEntityOption>& OutOptions) const;
        void BuildCameraFollowTargetOptions(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId, std::vector<BoneEntityOption>& OutOptions) const;
        Arche::EntityID FindHierarchyRootEntityId(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId) const;
        std::string BuildBoneEntityLabel(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId) const;
        void SyncSelectedEntityFromSelectedEntityId(const Game::SceneWorldSnapshot& Snapshot);

    private:
        std::uint32_t mSelectedEntityIndex{};
        float mHierarchyRegionRatio{ 0.6f };
        Arche::EntityID mSelectedEntityId{ Arche::NullEntityID };
        std::uint64_t mPickedEntityChangedSubscriptionId{};
    };
}
