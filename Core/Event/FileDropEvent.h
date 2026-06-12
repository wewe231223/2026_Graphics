#pragma once
#include <filesystem>

namespace Core::Event {
    struct FbxBinFileDroppedEventTag {
    };

    struct FbxBinFileDroppedPayload {
        std::filesystem::path FilePath{};
    };
}
