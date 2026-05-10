#pragma once

#include <utility>

namespace Core::Event {

    template <typename TEventTag>
    Event<TEventTag>::Event() :
        mPayload{},
        mImmediate{ false } {
    }

    template <typename TEventTag>
    Event<TEventTag>::Event(bool Immediate) :
        mPayload{},
        mImmediate{ Immediate } {
    }

    template <typename TEventTag>
    Event<TEventTag>::Event(std::any Payload, bool Immediate) :
        mPayload{ std::move(Payload) },
        mImmediate{ Immediate } {
    }

    template <typename TEventTag>
    EventTypeId Event<TEventTag>::GetTypeId() {
        EventTypeId TypeId{ GetEventTypeId<TEventTag>() };
        return TypeId;
    }

    template <typename TEventTag>
    const std::any& Event<TEventTag>::GetPayload() const {
        return mPayload;
    }

    template <typename TEventTag>
    bool Event<TEventTag>::IsImmediate() const {
        return mImmediate;
    }

    template <typename TEventTag>
    template <typename TPayload>
    const TPayload* Event<TEventTag>::GetPayloadAs() const {
        if (!mPayload.has_value()) {
            return nullptr;
        }

        return std::any_cast<TPayload>(&mPayload);
    }

}
