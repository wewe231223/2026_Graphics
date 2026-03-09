#include "ComponentInspection.h"
#include <utility>
#include "BoundingBox.h"
#include "Camera.h"
#include "StaticMeshRenderer.h"
#include "Transform.h"

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

        AppendComponentInspectionSection<Transform>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<StaticMeshRenderer>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<BoundingBox>(ReadOnlyWorld, EntityId, OutSections);
        AppendComponentInspectionSection<Camera>(ReadOnlyWorld, EntityId, OutSections);
    }
}
