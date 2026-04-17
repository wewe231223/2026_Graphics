#include "SceneHierarchyWidget.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "External/Include/ImGui/imgui.h"
#include "Core/Event/EventQueue.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/RuntimeVariableTable.h"
#include "Game/Scene/Components/ComponentInspection.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Events/SelectionEvent.h"
#include "Game/Scene/SceneWorldSnapshot.h"

#ifdef max 
#undef max
#undef min
#endif 

namespace Widget {
    SceneHierarchyWidget::SceneHierarchyWidget()
        : mSelectedEntityIndex{}
        , mHierarchyRegionRatio{ 0.6f }
        , mSelectedEntityId{ Arche::NullEntityID }
        , mPickedEntityChangedSubscriptionId{} {
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
            if (Sections[SectionIndex].ComponentName == "FootIKRig") {
                continue;
            }

            const std::string TableIdentifier{ std::format("ComponentTable##{}:{}:{}", Entity.mEntityId.index, Entity.mEntityId.generation, SectionIndex) };
            RenderComponentSectionTable(Sections[SectionIndex].ComponentName.c_str(), Sections[SectionIndex].Fields, TableIdentifier.c_str());
        }

        RenderAnimatorEditor(Snapshot, Entity.mEntityId);
        RenderBoneSkinReferenceEditor(Snapshot, Entity.mEntityId);
        RenderCameraEditor(Snapshot, Entity.mEntityId);
        RenderRuntimeVariableTablePanel(Snapshot, Entity.mEntityId);
        RenderFootIKRigEditor(Snapshot, Entity.mEntityId);
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

    void SceneHierarchyWidget::RenderAnimatorEditor(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId) {
        Arche::World* World{ Snapshot.GetWorld() };
        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ Snapshot.GetReadOnlyWorld() };
        if (World == nullptr || ReadOnlyWorld == nullptr) {
            return;
        }

        const Game::Animator* AnimatorComponent{ ReadOnlyWorld->GetComponent<Game::Animator>(EntityId) };
        if (AnimatorComponent == nullptr) {
            return;
        }

        std::vector<AnimatorClipOption> Options{};
        BuildAnimatorClipOptions(Snapshot, EntityId, Options);

        std::string PreviewLabel{ "<None>" };
        for (const AnimatorClipOption& Option : Options) {
            if (Option.mClipIndex != AnimatorComponent->clipIndex) {
                continue;
            }

            PreviewLabel = Option.mLabel;
            break;
        }

        ImGui::SeparatorText("Animator");

        const std::string ComboIdentifier{ std::format("##AnimatorClipSelector##{}:{}", EntityId.index, EntityId.generation) };
        if (!ImGui::BeginCombo(ComboIdentifier.c_str(), PreviewLabel.c_str())) {
            return;
        }

        for (const AnimatorClipOption& Option : Options) {
            const bool IsSelected{ Option.mClipIndex == AnimatorComponent->clipIndex };
            if (ImGui::Selectable(Option.mLabel.c_str(), IsSelected)) {
                const std::int32_t SelectedClipIndex{ Option.mClipIndex };
                World->WriteComponent<Game::Animator>(EntityId, [SelectedClipIndex](Game::Animator& TargetComponent) {
                    TargetComponent.clipIndex = SelectedClipIndex;
                });
            }
        }

        ImGui::EndCombo();
    }

    void SceneHierarchyWidget::BuildAnimatorClipOptions(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId, std::vector<AnimatorClipOption>& OutOptions) const {
        OutOptions.clear();

        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ Snapshot.GetReadOnlyWorld() };
        if (ReadOnlyWorld == nullptr) {
            return;
        }

        const Game::Animator* AnimatorComponent{ ReadOnlyWorld->GetComponent<Game::Animator>(EntityId) };
        if (AnimatorComponent == nullptr) {
            return;
        }

        AnimatorClipOption NoneOption{};
        NoneOption.mClipIndex = -1;
        NoneOption.mLabel = "<None>";
        OutOptions.push_back(std::move(NoneOption));

        if (AnimatorComponent->animation == nullptr) {
            return;
        }

        const std::vector<asset::AnimationClip>& Clips{ AnimatorComponent->animation->Clips() };
        for (std::size_t ClipIndex{ 0 }; ClipIndex < Clips.size(); ++ClipIndex) {
            AnimatorClipOption Option{};
            Option.mClipIndex = static_cast<std::int32_t>(ClipIndex);
            if (Clips[ClipIndex].Name.empty()) {
                Option.mLabel = std::format("Clip_{}", ClipIndex);
            }
            else {
                Option.mLabel = Clips[ClipIndex].Name;
            }

            OutOptions.push_back(std::move(Option));
        }
    }

    void SceneHierarchyWidget::RenderBoneSkinReferenceEditor(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId) {
        Arche::World* World{ Snapshot.GetWorld() };
        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ Snapshot.GetReadOnlyWorld() };
        if (World == nullptr || ReadOnlyWorld == nullptr) {
            return;
        }

        const Game::BoneSkinReference* BoneSkinReferenceComponent{ ReadOnlyWorld->GetComponent<Game::BoneSkinReference>(EntityId) };
        if (BoneSkinReferenceComponent == nullptr) {
            return;
        }

        std::vector<BoneEntityOption> Options{};
        BuildBoneEntityOptions(Snapshot, EntityId, Options);

        std::string PreviewLabel{ BuildBoneEntityLabel(Snapshot, BoneSkinReferenceComponent->boneRootEntityId) };
        if (PreviewLabel.empty()) {
            PreviewLabel = "<None>";
        }

        ImGui::SeparatorText("BoneSkinReference");

        const std::string ComboIdentifier{ std::format("##BoneSkinRootSelector##{}:{}", EntityId.index, EntityId.generation) };
        if (!ImGui::BeginCombo(ComboIdentifier.c_str(), PreviewLabel.c_str())) {
            return;
        }

        const bool IsSelectedNone{ BoneSkinReferenceComponent->boneRootEntityId == Arche::NullEntityID };
        if (ImGui::Selectable("<None>", IsSelectedNone)) {
            World->WriteComponent<Game::BoneSkinReference>(EntityId, [](Game::BoneSkinReference& TargetComponent) {
                TargetComponent.boneRootEntityId = Arche::NullEntityID;
            });
        }

        for (const BoneEntityOption& Option : Options) {
            const bool IsSelected{ BoneSkinReferenceComponent->boneRootEntityId == Option.mEntityId };
            if (ImGui::Selectable(Option.mLabel.c_str(), IsSelected)) {
                const Arche::EntityID SelectedEntityId{ Option.mEntityId };
                World->WriteComponent<Game::BoneSkinReference>(EntityId, [SelectedEntityId](Game::BoneSkinReference& TargetComponent) {
                    TargetComponent.boneRootEntityId = SelectedEntityId;
                });
            }
        }

        ImGui::EndCombo();
    }

    void SceneHierarchyWidget::RenderCameraEditor(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId) {
        Arche::World* World{ Snapshot.GetWorld() };
        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ Snapshot.GetReadOnlyWorld() };
        if (World == nullptr || ReadOnlyWorld == nullptr) {
            return;
        }

        const Game::Camera* CameraComponent{ ReadOnlyWorld->GetComponent<Game::Camera>(EntityId) };
        if (CameraComponent == nullptr) {
            return;
        }

        std::vector<BoneEntityOption> Options{};
        BuildCameraFollowTargetOptions(Snapshot, EntityId, Options);

        std::string PreviewLabel{ BuildBoneEntityLabel(Snapshot, CameraComponent->thirdPersonFollowTarget) };
        if (PreviewLabel.empty()) {
            PreviewLabel = "<None>";
        }

        ImGui::SeparatorText("Camera");
        const std::string ComboIdentifier{ std::format("##CameraFollowTargetSelector##{}:{}", EntityId.index, EntityId.generation) };
        if (!ImGui::BeginCombo(ComboIdentifier.c_str(), PreviewLabel.c_str())) {
            return;
        }

        const bool IsSelectedNone{ CameraComponent->thirdPersonFollowTarget == Arche::NullEntityID };
        if (ImGui::Selectable("<None>", IsSelectedNone)) {
            World->WriteComponent<Game::Camera>(EntityId, [](Game::Camera& TargetComponent) {
                TargetComponent.thirdPersonFollowTarget = Arche::NullEntityID;
                TargetComponent.thirdPersonFollowTargetSerializedId = -1;
            });
        }

        for (const BoneEntityOption& Option : Options) {
            const bool IsSelected{ CameraComponent->thirdPersonFollowTarget == Option.mEntityId };
            if (ImGui::Selectable(Option.mLabel.c_str(), IsSelected)) {
                const Arche::EntityID SelectedEntityId{ Option.mEntityId };
                World->WriteComponent<Game::Camera>(EntityId, [SelectedEntityId](Game::Camera& TargetComponent) {
                    TargetComponent.thirdPersonFollowTarget = SelectedEntityId;
                    TargetComponent.thirdPersonFollowTargetSerializedId = -1;
                });
            }
        }

        ImGui::EndCombo();
    }

    void SceneHierarchyWidget::RenderRuntimeVariableTablePanel(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId) {
        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ Snapshot.GetReadOnlyWorld() };
        if (ReadOnlyWorld == nullptr) {
            return;
        }

        const Game::RuntimeVariableTable* RuntimeVariableTableComponent{ ReadOnlyWorld->GetComponent<Game::RuntimeVariableTable>(EntityId) };
        if (RuntimeVariableTableComponent == nullptr) {
            return;
        }

        const Game::Animator* AnimatorComponent{ ReadOnlyWorld->GetComponent<Game::Animator>(EntityId) };
        if (AnimatorComponent == nullptr || AnimatorComponent->GraphAsset == nullptr) {
            return;
        }

        const ::std::vector<Game::RuntimeParameterDefinition>& ParameterDefinitions{ AnimatorComponent->GraphAsset->GetParameterDefinitions() };
        if (ParameterDefinitions.empty()) {
            return;
        }

        ImGui::SeparatorText("RuntimeVariableTable");

        const ::std::size_t MaxCount{ ::std::min(ParameterDefinitions.size(), static_cast<::std::size_t>(Game::RuntimeVariableTableMaxParameterCount)) };

        if (ImGui::TreeNode("Bool")) {
            for (::std::size_t ParameterIndex{ 0 }; ParameterIndex < MaxCount; ++ParameterIndex) {
                const Game::RuntimeParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex] };
                if (Definition.ParameterTypeValue != Game::RuntimeParameterDefinition::ParameterType::Bool) {
                    continue;
                }

                const bool Value{ RuntimeVariableTableComponent->BoolValues[ParameterIndex] };
                ImGui::BulletText("%s: %s", Definition.ParameterName.c_str(), Value ? "true" : "false");
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Trigger")) {
            for (::std::size_t ParameterIndex{ 0 }; ParameterIndex < MaxCount; ++ParameterIndex) {
                const Game::RuntimeParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex] };
                if (Definition.ParameterTypeValue != Game::RuntimeParameterDefinition::ParameterType::Trigger) {
                    continue;
                }

                const bool Value{ RuntimeVariableTableComponent->BoolValues[ParameterIndex] };
                ImGui::BulletText("%s: %s", Definition.ParameterName.c_str(), Value ? "set" : "unset");
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Int")) {
            for (::std::size_t ParameterIndex{ 0 }; ParameterIndex < MaxCount; ++ParameterIndex) {
                const Game::RuntimeParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex] };
                if (Definition.ParameterTypeValue != Game::RuntimeParameterDefinition::ParameterType::Int) {
                    continue;
                }

                ImGui::BulletText("%s: %d", Definition.ParameterName.c_str(), RuntimeVariableTableComponent->IntValues[ParameterIndex]);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Float")) {
            for (::std::size_t ParameterIndex{ 0 }; ParameterIndex < MaxCount; ++ParameterIndex) {
                const Game::RuntimeParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex] };
                if (Definition.ParameterTypeValue != Game::RuntimeParameterDefinition::ParameterType::Float) {
                    continue;
                }

                ImGui::BulletText("%s: %.3f", Definition.ParameterName.c_str(), RuntimeVariableTableComponent->FloatValues[ParameterIndex]);
            }
            ImGui::TreePop();
        }
    }

    void SceneHierarchyWidget::RenderFootIKRigEditor(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId) {
        Arche::World* World{ Snapshot.GetWorld() };
        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ Snapshot.GetReadOnlyWorld() };
        if (World == nullptr || ReadOnlyWorld == nullptr) {
            return;
        }

        const Game::FootIKRig* FootIKRigComponent{ ReadOnlyWorld->GetComponent<Game::FootIKRig>(EntityId) };
        if (FootIKRigComponent == nullptr) {
            return;
        }

        ImGui::SeparatorText("FootIKRig");

        const bool IsEnabled{ FootIKRigComponent->mEnabled };
        const std::string ToggleButtonLabel{ std::format("{}##FootIKToggleButton##{}:{}", IsEnabled ? "IK Off" : "IK On", EntityId.index, EntityId.generation) };
        if (ImGui::Button(ToggleButtonLabel.c_str())) {
            World->WriteComponent<Game::FootIKRig>(EntityId, [](Game::FootIKRig& TargetComponent) {
                TargetComponent.mEnabled = (TargetComponent.mEnabled == false);
            });
        }

        ImGui::SameLine();
        ImGui::TextUnformatted(IsEnabled ? "Enabled" : "Disabled");

        float FootSoleOffset{ FootIKRigComponent->mFootSoleOffset };
        const std::string FootSoleOffsetSliderIdentifier{ std::format("Foot Sole Offset##{}:{}", EntityId.index, EntityId.generation) };
        if (ImGui::SliderFloat(FootSoleOffsetSliderIdentifier.c_str(), &FootSoleOffset, -0.30f, 0.50f, "%.3f")) {
            const float UpdatedFootSoleOffset{ FootSoleOffset };
            World->WriteComponent<Game::FootIKRig>(EntityId, [UpdatedFootSoleOffset](Game::FootIKRig& TargetComponent) {
                TargetComponent.mFootSoleOffset = UpdatedFootSoleOffset;
            });
        }

        float BlendSpeed{ FootIKRigComponent->mBlendSpeed };
        const std::string BlendSpeedSliderIdentifier{ std::format("Blend Speed##{}:{}", EntityId.index, EntityId.generation) };
        if (ImGui::SliderFloat(BlendSpeedSliderIdentifier.c_str(), &BlendSpeed, 0.0f, 30.0f, "%.2f")) {
            const float UpdatedBlendSpeed{ BlendSpeed };
            World->WriteComponent<Game::FootIKRig>(EntityId, [UpdatedBlendSpeed](Game::FootIKRig& TargetComponent) {
                TargetComponent.mBlendSpeed = UpdatedBlendSpeed;
            });
        }

        float MaxLift{ FootIKRigComponent->mMaxLift };
        const std::string MaxLiftSliderIdentifier{ std::format("Max Lift##{}:{}", EntityId.index, EntityId.generation) };
        if (ImGui::SliderFloat(MaxLiftSliderIdentifier.c_str(), &MaxLift, 0.0f, 1.0f, "%.3f")) {
            const float UpdatedMaxLift{ MaxLift };
            World->WriteComponent<Game::FootIKRig>(EntityId, [UpdatedMaxLift](Game::FootIKRig& TargetComponent) {
                TargetComponent.mMaxLift = UpdatedMaxLift;
            });
        }
    }

    void SceneHierarchyWidget::BuildBoneEntityOptions(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId, std::vector<BoneEntityOption>& OutOptions) const {
        OutOptions.clear();

        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ Snapshot.GetReadOnlyWorld() };
        if (ReadOnlyWorld == nullptr) {
            return;
        }

        const Arche::EntityID HierarchyRootEntityId{ FindHierarchyRootEntityId(Snapshot, EntityId) };

        for (const Game::SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot : Snapshot.GetEntities()) {
            const Game::Bone* BoneComponent{ ReadOnlyWorld->GetComponent<Game::Bone>(EntitySnapshot.mEntityId) };
            if (BoneComponent == nullptr) {
                continue;
            }

            if (FindHierarchyRootEntityId(Snapshot, EntitySnapshot.mEntityId) != HierarchyRootEntityId) {
                continue;
            }

            BoneEntityOption Option{};
            Option.mEntityId = EntitySnapshot.mEntityId;
            Option.mLabel = BuildBoneEntityLabel(Snapshot, EntitySnapshot.mEntityId);
            OutOptions.push_back(std::move(Option));
        }
    }

    void SceneHierarchyWidget::BuildCameraFollowTargetOptions(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId, std::vector<BoneEntityOption>& OutOptions) const {
        OutOptions.clear();
        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ Snapshot.GetReadOnlyWorld() };
        if (ReadOnlyWorld == nullptr) {
            return;
        }

        for (const Game::SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot : Snapshot.GetEntities()) {
            if (EntitySnapshot.mEntityId == EntityId) {
                continue;
            }

            const Game::Transform* TransformComponent{ ReadOnlyWorld->GetComponent<Game::Transform>(EntitySnapshot.mEntityId) };
            if (TransformComponent == nullptr) {
                continue;
            }

            BoneEntityOption Option{};
            Option.mEntityId = EntitySnapshot.mEntityId;
            Option.mLabel = BuildBoneEntityLabel(Snapshot, EntitySnapshot.mEntityId);
            OutOptions.push_back(std::move(Option));
        }
    }


    Arche::EntityID SceneHierarchyWidget::FindHierarchyRootEntityId(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId) const {
        Arche::EntityID CurrentEntityId{ EntityId };

        for (;;) {
            bool IsFound{ false };

            for (const Game::SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot : Snapshot.GetEntities()) {
                if (EntitySnapshot.mEntityId != CurrentEntityId) {
                    continue;
                }

                if (EntitySnapshot.mParentId == Arche::NullEntityID) {
                    return CurrentEntityId;
                }

                CurrentEntityId = EntitySnapshot.mParentId;
                IsFound = true;
                break;
            }

            if (IsFound == false) {
                return CurrentEntityId;
            }
        }
    }

    std::string SceneHierarchyWidget::BuildBoneEntityLabel(const Game::SceneWorldSnapshot& Snapshot, Arche::EntityID EntityId) const {
        if (EntityId == Arche::NullEntityID) {
            return {};
        }

        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ Snapshot.GetReadOnlyWorld() };
        if (ReadOnlyWorld == nullptr) {
            return {};
        }

        const Game::Name* NameComponent{ ReadOnlyWorld->GetComponent<Game::Name>(EntityId) };
        const char* NameText{ NameComponent == nullptr ? "<Unnamed>" : Game::GetNameText(*NameComponent) };
        return std::format("{} ({}:{})", NameText, EntityId.index, EntityId.generation);
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
