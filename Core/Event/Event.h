#pragma once

#include "Core/Event/EventTag.h"
#include <any>

namespace Core::Event {
    template <typename TEventTag>
    class Event {
    public:
        Event();
        explicit Event(bool Immediate);
        Event(std::any Payload, bool Immediate);
        ~Event() = default;
        Event(const Event& Other) = default;
        Event& operator=(const Event& Other) = default;
        Event(Event&& Other) noexcept = default;
        Event& operator=(Event&& Other) noexcept = default;

    public:
        static EventTypeId GetTypeId();

        const std::any& GetPayload() const;
        bool IsImmediate() const;

        template <typename TPayload>
        const TPayload* GetPayloadAs() const;

    private:
        std::any mPayload;
        bool mImmediate;
    };
}

#include "Core/Event/Event.inl"
