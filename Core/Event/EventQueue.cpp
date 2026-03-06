#include "Core/Event/EventQueue.h"
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Core::Event {

    namespace Internal {
        using namespace Core::Event;

        std::mutex GEventQueueMutex{};
        std::unordered_map<EventTypeId, std::vector<std::pair<SubscriptionId, UntypedEventHandler>>> GSubscribers{};
        std::vector<std::tuple<EventTypeId, std::any, bool>> GPendingEvents{};
        SubscriptionId GNextSubscriptionId{ 1 };

        std::vector<UntypedEventHandler> CollectHandlers(EventTypeId TypeId) {
            std::vector<UntypedEventHandler> Handlers{};

            std::scoped_lock<std::mutex> Lock{ GEventQueueMutex };

            std::unordered_map<EventTypeId, std::vector<std::pair<SubscriptionId, UntypedEventHandler>>>::const_iterator FoundIterator{ GSubscribers.find(TypeId) };

            if (FoundIterator == GSubscribers.end()) {
                return Handlers;
            }

            const std::vector<std::pair<SubscriptionId, UntypedEventHandler>>& RegisteredHandlers{ FoundIterator->second };
            Handlers.reserve(RegisteredHandlers.size());

            for (const std::pair<SubscriptionId, UntypedEventHandler>& RegisteredHandler : RegisteredHandlers) {
                Handlers.push_back(RegisteredHandler.second);
            }

            return Handlers;
        }

        void DispatchEvent(EventTypeId TypeId, const std::any& Payload, bool Immediate) {
            std::vector<UntypedEventHandler> Handlers{ CollectHandlers(TypeId) };

            for (const UntypedEventHandler& Handler : Handlers) {
                Handler(Payload, Immediate);
            }
        }

        SubscriptionId SubscribeUntyped(EventTypeId TypeId, UntypedEventHandler Handler) {
            std::scoped_lock<std::mutex> Lock{ GEventQueueMutex };

            SubscriptionId NewSubscriptionId{ GNextSubscriptionId };
            GNextSubscriptionId++;

            std::vector<std::pair<SubscriptionId, UntypedEventHandler>>& Handlers{ GSubscribers[TypeId] };
            Handlers.emplace_back(NewSubscriptionId, std::move(Handler));

            return NewSubscriptionId;
        }

        void EnqueueUntyped(EventTypeId TypeId, std::any Payload, bool Immediate) {
            if (Immediate) {
                DispatchEvent(TypeId, Payload, Immediate);
                return;
            }

            std::scoped_lock<std::mutex> Lock{ GEventQueueMutex };
            GPendingEvents.emplace_back(TypeId, std::move(Payload), Immediate);
        }

    }

    void Flush() {
        std::vector<std::tuple<EventTypeId, std::any, bool>> PendingEvents{};

        {
            std::scoped_lock<std::mutex> Lock{ Internal::GEventQueueMutex };
            PendingEvents.swap(Internal::GPendingEvents);
        }

        for (const std::tuple<EventTypeId, std::any, bool>& PendingEvent : PendingEvents) {
            const EventTypeId& TypeId{ std::get<0>(PendingEvent) };
            const std::any& Payload{ std::get<1>(PendingEvent) };
            bool Immediate{ std::get<2>(PendingEvent) };

            Internal::DispatchEvent(TypeId, Payload, Immediate);
        }
    }

}
