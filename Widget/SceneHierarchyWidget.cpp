#include "SceneHierarchyWidget.h"
#include <format>
#include "External/Include/ImGui/imgui.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/SceneWorldSnapshot.h"

namespace Widget {
    SceneHierarchyWidget::SceneHierarchyWidget() {
    }

    SceneHierarchyWidget::~SceneHierarchyWidget() {
    }

    void SceneHierarchyWidget::Render(const Game::SceneWorldSnapshot* Snapshot) {
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

        if (RootIndices.empty()) {
            ImGui::TextUnformatted("Hierarchy is empty.");
            ImGui::End();
            return;
        }

        for (const std::uint32_t RootEntityIndex : RootIndices) {
            RenderEntityNode(*Snapshot, RootEntityIndex);
        }

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

        const std::string Label{ std::format("{}##{}:{}", NameText, Entity.mEntityId.index, Entity.mEntityId.generation) };
        const ImGuiTreeNodeFlags Flags{ ChildIndices.empty() ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : ImGuiTreeNodeFlags_None };

        const bool IsOpen{ ImGui::TreeNodeEx(Label.c_str(), Flags) };

        if (ChildIndices.empty()) {
            return;
        }

        if (!IsOpen) {
            return;
        }

        for (const std::uint32_t ChildIndex : ChildIndices) {
            RenderEntityNode(Snapshot, ChildIndex);
        }

        ImGui::TreePop();
    }
}
