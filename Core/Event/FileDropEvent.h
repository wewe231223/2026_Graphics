#pragma once
#include <filesystem>
#include "Core/Event/EventQueue.h"
#include "Utility/StdOutput.h"
#include "Utility/StringUtils.h"


namespace Core::Event {
    struct FbxBinFileDroppedEventTag {
    };

    struct FbxBinFileDroppedPayload {
        std::filesystem::path FilePath{};
    };

    struct FileDropEventSubscriber {
        void operator()(const Event<FbxBinFileDroppedEventTag>& DroppedFileEvent) const {
            const FbxBinFileDroppedPayload* Payload{ DroppedFileEvent.GetPayloadAs<FbxBinFileDroppedPayload>() };
            if (Payload == nullptr) {
                return;
            }
            const std::string FileName{ ConvertWstringToUtf8(Payload->FilePath.filename().wstring()) };
            StdOutput::Print("Dropped file: {}", FileName);
		}
    };
}
