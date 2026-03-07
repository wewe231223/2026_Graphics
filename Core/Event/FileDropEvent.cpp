#include "Core/Event/FileDropEvent.h"
#include "Utility/StdOutput.h"
#include "Utility/StringUtils.h"

namespace Core::Event {
    SubscriptionId SubscribeFbxBinFileDroppedEvent() {
        const SubscriptionId FbxBinDropSubscriptionId{ Subscribe<FbxBinFileDroppedEventTag>([](const Event<FbxBinFileDroppedEventTag>& DroppedFileEvent) {
            const FbxBinFileDroppedPayload* Payload{ DroppedFileEvent.GetPayloadAs<FbxBinFileDroppedPayload>() };
            if (Payload == nullptr) {
                return;
            }

            const std::string FileName{ ConvertWstringToUtf8(Payload->FilePath.filename().wstring()) };
            StdOutput::PrintInfoLine("Dropped fbxbin file: {}", FileName);
        }) };

        return FbxBinDropSubscriptionId;
    }
}
