#pragma once

#include "Core/Event/EventQueue.h"
#include <filesystem>

namespace Core::Event {
    struct FbxBinFileDroppedEventTag {
    };

    struct FbxBinFileDroppedPayload {
        std::filesystem::path FilePath{};
    };

    SubscriptionId SubscribeFbxBinFileDroppedEvent();
}
