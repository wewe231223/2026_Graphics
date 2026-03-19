#include "SceneHierarchyWidget.h"
#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include "External/Include/ImGui/imgui.h"
#include "Game/Scene/Components/ComponentInspection.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/SceneWorldSnapshot.h"
#include "Game/Scene/Events/SelectionEvent.h"
#include "Core/Event/EventQueue.h"

namespace Widget {
    SceneHierarchyWidget::SceneHierarchyWidget()
        : mSelectedEntityIndex{},
        mHierarchyRegionRatio{ 0.6f },
        mSelectedEntityId{ Arche::NullEntityID },
        mPickedEntityChangedSubscriptionId{} {
        mPickedEntityChangedSubscriptionId = Core::Event::Subscribe<Game::PickedEntityChangedEventTag>([this](const Core::Event::Event<Game::PickedEntityChangedEventTag>& PickedEntityChangedEvent) {
            const Game::PickedEntityChangedPayload* Payload{ PickedEntityChangedEvent.GetPayloadAs<Game::PickedEntityChangedPayload>() };

            if (Payload == nullptr) {
                return;
            }

            mSelectedEntityId = Payload->PickedEntityId;
        });
    }

    SceneHierarchyWidget::~SceneHierarchyWidget() {
    }

    void SceneHierarchyWidget::Render(const Game::SceneWorldSnapshot* Snapshot) {
        ImGui::SetNextWindowSize(ImVec2(520.0f, 760.0f), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Scene Hierarchy")) {
            ImGui::End();
            return;
        }

        if (Snapshot == nullptr) {
            ImGui::TextUnformatted("Hierarchy is empty.");
            ImGui::End();
            return;
        }

        const std::vector<std::uint32_t>& RootIndices{ Snapshot->GetRootIndices() };
        const float AvailableHeight{ ImGui::GetContentRegionAvail().y };
        const float SeparatorHeight{ ImGui::GetStyle().ItemSpacing.y + 4.0f };
        const float MinPanelHeight{ 120.0f };

        if (RootIndices.empty()) {
            ImGui::TextUnformatted("Hierarchy is empty.");
            ImGui::End();
            return;
        }

        SyncSelectedEntityFromSelectedEntityId(*Snapshot);

        mHierarchyRegionRatio = std::clamp(mHierarchyRegionRatio, 0.3f, 0.85f);
        const float MaxHierarchyPanelHeight{ std::max(MinPanelHeight, AvailableHeight - MinPanelHeight - SeparatorHeight) };
        float HierarchyPanelHeight{ AvailableHeight * mHierarchyRegionRatio };
        HierarchyPanelHeight = std::clamp(HierarchyPanelHeight, MinPanelHeight, MaxHierarchyPanelHeight);

        ImGui::BeginChild("HierarchyTreePanel", ImVec2(0.0f, HierarchyPanelHeight), true, ImGuiWindowFlags_HorizontalScrollbar);

        for (const std::uint32_t RootEntityIndex : RootIndices) {
            RenderEntityNode(*Snapshot, RootEntityIndex);
        }

        ImGui::EndChild();

        ImGui::InvisibleButton("HierarchyPanelSplitter", ImVec2(-1.0f, 4.0f));
        if (ImGui::IsItemActive()) {
            const float Delta{ ImGui::GetIO().MouseDelta.y };
            if (AvailableHeight > 1.0f) {
                mHierarchyRegionRatio = std::clamp((HierarchyPanelHeight + Delta) / AvailableHeight, 0.3f, 0.85f);
            }
        }

        ImGui::BeginChild("HierarchySelectedEntityPanel", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_None);
        RenderSelectedEntityPanel(*Snapshot);
        ImGui::EndChild();

        ImGui::End();
    }

    void SceneHierarchyWidget::RenderEntityNode(const Game::SceneWorldSnapshot& Snapshot, std::uint32_t EntityIndex) {
        const std::vector<Game::SceneWorldSnapshot::SceneEntitySnapshot>& Entities{ Snapshot.GetEntities() };

        if (EntityIndex >= Entities.size()) {
            return;
        }

        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ Snapshot.GetReadOnlyWorld() };
        if (ReadOnlyWorld == nullptr) {
            return;
        }

        const Game::SceneWorldSnapshot::SceneEntitySnapshot& Entity{ Entities[EntityIndex] };
        const Game::Name* NameComponent{ ReadOnlyWorld->GetComponent<Game::Name>(Entity.mEntityId) };
        if (NameComponent == nullptr) {
            return;
        }

        const char* NameText{ Game::GetNameText(*NameComponent) };
        const std::vector<std::uint32_t>& ChildIndices{ Snapshot.GetChildIndices(EntityIndex) };

        const bool IsLeaf{ ChildIndices.empty() };
        const bool IsSelected{ mSelectedEntityIndex == EntityIndex };
        const std::string Label{ std::format("{}##{}:{}", NameText, Entity.mEntityId.index, Entity.mEntityId.generation) };

        ImGuiTreeNodeFlags Flags{ ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding };

        if (IsLeaf) {
            Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        if (IsSelected) {
            Flags |= ImGuiTreeNodeFlags_Selected;
        }

        const bool IsOpen{ ImGui::TreeNodeEx(Label.c_str(), Flags) };

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            mSelectedEntityIndex = EntityIndex;
            mSelectedEntityId = Entity.mEntityId;

            Game::HierarchyEntitySelectedPayload Payload{};
            Payload.SelectedEntityId = Entity.mEntityId;
            Core::Event::Enqueue<Game::HierarchyEntitySelectedEventTag, Game::HierarchyEntitySelectedPayload>(std::move(Payload), true);
        }

        if (!IsOpen || IsLeaf) {
            return;
        }

        for (const std::uint32_t ChildIndex : ChildIndices) {
            RenderEntityNode(Snapshot, ChildIndex);
        }

        ImGui::TreePop();
    }

    void SceneHierarchyWidget::RenderSelectedEntityPanel(const Game::SceneWorldSnapshot& Snapshot) {
        const std::vector<Game::SceneWorldSnapshot::SceneEntitySnapshot>& Entities{ Snapshot.GetEntities() };

        if (Entities.empty()) {
            ImGui::TextUnformatted("No entity data.");
            return;
        }

        if (mSelectedEntityIndex >= Entities.size()) {
            mSelectedEntityIndex = 0;
        }

        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ Snapshot.GetReadOnlyWorld() };
        if (ReadOnlyWorld == nullptr) {
            ImGui::TextUnformatted("World data unavailable.");
            return;
        }

        const Game::SceneWorldSnapshot::SceneEntitySnapshot& Entity{ Entities[mSelectedEntityIndex] };
        const Game::Name* NameComponent{ ReadOnlyWorld->GetComponent<Game::Name>(Entity.mEntityId) };
        const char* NameText{ NameComponent == nullptr ? "<Unnamed>" : Game::GetNameText(*NameComponent) };

        ImGui::TextUnformatted("Selected Entity");
        ImGui::Separator();
        ImGui::Text("Name: %s", NameText);
        ImGui::Text("ID: %u:%u", Entity.mEntityId.index, Entity.mEntityId.generation);

        std::vector<Game::ComponentInspectionSection> Sections{};
        Game::BuildComponentInspectionSections(*ReadOnlyWorld, Entity.mEntityId, Sections);

        for (std::size_t SectionIndex{ 0 }; SectionIndex < Sections.size(); ++SectionIndex) {
            const std::string TableIdentifier{ std::format("ComponentTable##{}:{}:{}", Entity.mEntityId.index, Entity.mEntityId.generation, SectionIndex) };
            RenderComponentSectionTable(Sections[SectionIndex].ComponentName.c_str(), Sections[SectionIndex].Fields, TableIdentifier.c_str());
        }
    }

    void SceneHierarchyWidget::RenderComponentSectionTable(const char* ComponentName, const std::vector<Game::ComponentInspectionField>& Fields, const char* TableIdentifier) const {
        ImGui::SeparatorText(ComponentName);

        if (!ImGui::BeginTable(TableIdentifier, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
            return;
        }

        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        for (const Game::ComponentInspectionField& Field : Fields) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(Field.Label.c_str());
            ImGui::TableSetColumnIndex(1);

            if (Field.Value.find('\n') == std::string::npos) {
                ImGui::TextWrapped("%s", Field.Value.c_str());
                continue;
            }

            ImGui::PushTextWrapPos();
            ImGui::TextUnformatted(Field.Value.c_str());
            ImGui::PopTextWrapPos();
        }

        ImGui::EndTable();
    }

    void SceneHierarchyWidget::SyncSelectedEntityFromSelectedEntityId(const Game::SceneWorldSnapshot& Snapshot) {
        if (mSelectedEntityId == Arche::NullEntityID) {
            return;
        }

        const std::vector<Game::SceneWorldSnapshot::SceneEntitySnapshot>& Entities{ Snapshot.GetEntities() };

        for (std::uint32_t EntityIndex{ 0 }; EntityIndex < Entities.size(); ++EntityIndex) {
            if (Entities[EntityIndex].mEntityId != mSelectedEntityId) {
                continue;
            }

            mSelectedEntityIndex = EntityIndex;
            return;
        }
    }
}
