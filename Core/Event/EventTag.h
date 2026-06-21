#pragma once

#include <typeindex>

namespace Core::Event {

    using EventTypeId = std::type_index;

    struct EmptyEventPayload {
    };

    template <typename TEventTag>
    EventTypeId GetEventTypeId();

    template <typename TEventTag>
    EventTypeId GetEventTypeId() {
        EventTypeId TypeId{ typeid(TEventTag) };
        return TypeId;
    }

}
