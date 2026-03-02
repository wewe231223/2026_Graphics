#include "StdOutput.h"
#include <mutex>

namespace StdOutput {
    namespace {
        std::mutex SinkMutex{};
        SinkFunction GlobalSink{};

        void NotifySinkLocked(std::string_view Message) {
            if (GlobalSink) {
                GlobalSink(std::string{ Message });
            }
        }

        void WriteToFile(std::FILE* Stream, std::string_view Message) {
            std::fwrite(Message.data(), sizeof(char), Message.size(), Stream);
            std::fflush(Stream);

            std::scoped_lock<std::mutex> Guard{ SinkMutex };
            NotifySinkLocked(Message);
        }
    }

    void RegisterSink(SinkFunction Sink) {
        std::scoped_lock<std::mutex> Guard{ SinkMutex };
        GlobalSink = std::move(Sink);
    }

    void ClearSink() {
        std::scoped_lock<std::mutex> Guard{ SinkMutex };
        GlobalSink = SinkFunction{};
    }

    void Write(std::string_view Message) {
        WriteToFile(stdout, Message);
    }

    void WriteLine(std::string_view Message) {
        std::string FullMessage{ Message };
        FullMessage += '\n';
        WriteToFile(stdout, FullMessage);
    }

    void WriteError(std::string_view Message) {
        WriteToFile(stderr, Message);
    }

    void WriteErrorLine(std::string_view Message) {
        std::string FullMessage{ Message };
        FullMessage += '\n';
        WriteToFile(stderr, FullMessage);
    }
}
