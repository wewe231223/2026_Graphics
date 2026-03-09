#pragma once

#include "Arche/Common.h"

namespace Game {
    struct PickedEntityChangedEventTag {
    };

    struct PickedEntityChangedPayload {
        Arche::EntityID PickedEntityId{ Arche::NullEntityID };
    };

    struct HierarchyEntitySelectedEventTag {
    };

    struct HierarchyEntitySelectedPayload {
        Arche::EntityID SelectedEntityId{ Arche::NullEntityID };
    };
}
