#include "ComponentInspection.h"
#include <utility>
#include "Bone.h"
#include "BoundingBox.h"
#include "Culling.h"
#include "FootIKRig.h"
#include "FootIKRuntime.h"
#include "Tags.h"
#include "PrefabInstance.h"
#include "Name.h"
#include "Material.h"
#include "EntityHierarchy.h"
#include "Camera.h"
#include "Frustum.h"
#include "SkinnedMeshRenderer.h"
#include "StaticMeshRenderer.h"
#include "Transform.h"
#include "Animator.h"
#include "ScriptComponent.h"
#include "SkySphere.h"

namespace {
    template <typename T>
    void AppendComponentInspectionSection(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, Arche::EntityID EntityId, std::vector<Game::ComponentInspectionSection>& OutSections) {
        const T* Component{ ReadOnlyWorld.GetComponent<T>(EntityId) };

        if (Component == nullptr) {
            return;
        }

        Game::ComponentInspectionSection Section{};
        Section.ComponentName = T::GetComponentInspectionName();
        Component->BuildComponentInspectionFields(Section.Fields);
        OutSections.push_back(std::move(Section));
    }
}

namespace Game {
    void BuildComponentInspectionSections(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, Arche::EntityID EntityId, std::vector<ComponentInspectionSection>& OutSections) {
        OutSections.clear();

        AppendComponentInspectionSection<Name>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<Tag>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<Transform>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<EntityHierarchy>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<Bone>(ReadOnlyWorld, EntityId, OutSections);

        AppendComponentInspectionSection<Material>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<StaticMeshRenderer>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<SkinnedMeshRenderer>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<Culling>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<Animator>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<FootIKRig>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<FootIKRuntime>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<BehaviorInstanceComponent>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<BoundingBox>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<PrefabInstance>(ReadOnlyWorld, EntityId, OutSections);

        AppendComponentInspectionSection<Camera>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<Frustum>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<SkySphere>(ReadOnlyWorld, EntityId, OutSections);
    }
}
