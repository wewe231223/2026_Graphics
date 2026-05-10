#pragma once

#include "Core/Event/Event.h"
#include <any>
#include <cstdint>
#include <functional>
#include <utility>

namespace Core::Event {
	using SubscriptionId = std::uint64_t;

	namespace Internal {
		using UntypedEventHandler = std::function<void(const std::any&, bool)>;

		SubscriptionId SubscribeUntyped(EventTypeId TypeId, UntypedEventHandler Handler);
		void EnqueueUntyped(EventTypeId TypeId, std::any Payload, bool Immediate);
	}

	void Flush();

	template <typename TEventTag>
	using EventHandler = std::function<void(const Event<TEventTag>&)>;

	template <typename TEventTag>
	SubscriptionId Subscribe(EventHandler<TEventTag> Handler);

	template <typename TEventTag>
	void Enqueue(Event<TEventTag> NewEvent);

	template <typename TEventTag, typename TPayload>
	void Enqueue(TPayload Payload, bool Immediate = false);

	template <typename TEventTag>
	void Enqueue(bool Immediate = false);
}

#include "Core/Event/EventQueue.inl"
