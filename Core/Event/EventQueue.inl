#pragma once

namespace Core::Event {

    template <typename TEventTag>
    SubscriptionId Subscribe(EventHandler<TEventTag> Handler) {
        EventTypeId TypeId{ Event<TEventTag>::GetTypeId() };

        Internal::UntypedEventHandler WrappedHandler{ [Handler](const std::any& Payload, bool Immediate) {
            Event<TEventTag> TypedEvent{ Payload, Immediate };
            Handler(TypedEvent);
        } };

        return Internal::SubscribeUntyped(TypeId, std::move(WrappedHandler));
    }

    template <typename TEventTag>
    void Enqueue(Event<TEventTag> NewEvent) {
        EventTypeId TypeId{ Event<TEventTag>::GetTypeId() };
        Internal::EnqueueUntyped(TypeId, NewEvent.GetPayload(), NewEvent.IsImmediate());
    }

    template <typename TEventTag, typename TPayload>
    void Enqueue(TPayload Payload, bool Immediate) {
        Event<TEventTag> NewEvent{ std::any{ std::move(Payload) }, Immediate };
        Enqueue(std::move(NewEvent));
    }

    template <typename TEventTag>
    void Enqueue(bool Immediate) {
        Event<TEventTag> NewEvent{ Immediate };
        Enqueue(std::move(NewEvent));
    }

}
