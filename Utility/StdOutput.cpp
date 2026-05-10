#include "StdOutput.h"
#include <chrono>
#include <mutex>

namespace StdOutput {
    namespace {
        std::mutex SinkMutex{};
        SinkFunction GlobalSink{};

        std::string ToLevelText(LogLevel Level) {
            if (Level == LogLevel::Info) {
                return "Info";
            }

            if (Level == LogLevel::Warning) {
                return "Warning";
            }

            if (Level == LogLevel::Success) {
                return "Success";
            }

            return "Log";
        }

        std::string BuildPrefix(LogLevel Level) {
            const auto Now{ std::chrono::system_clock::now() };
            const auto LocalNow{ std::chrono::zoned_time{ std::chrono::current_zone(), Now } };
            const auto Milliseconds{ std::chrono::duration_cast<std::chrono::milliseconds>(Now.time_since_epoch()) % std::chrono::seconds{ 1 } };
            return std::format("[{:%H:%M:%S}.{:03}] {:<7} > ", LocalNow, Milliseconds.count(), ToLevelText(Level));
        }

        void NotifySinkLocked(std::string_view Message) {
            if (GlobalSink) {
                GlobalSink(std::string{ Message });
            }
        }

        std::string BuildOutputMessage(std::string_view Message, LogLevel Level) {
            std::string Output{};
            std::size_t StartIndex{ 0 };

            while (StartIndex <= Message.size()) {
                const std::size_t NewLineIndex{ Message.find('\n', StartIndex) };
                const bool HasNewLine{ NewLineIndex != std::string_view::npos };
                const std::size_t EndIndex{ HasNewLine ? NewLineIndex : Message.size() };
                const std::string_view OneLine{ Message.substr(StartIndex, EndIndex - StartIndex) };
                Output += BuildPrefix(Level);
                Output += OneLine;

                if (HasNewLine) {
                    Output += '\n';
                    StartIndex = NewLineIndex + 1;
                    continue;
                }

                break;
            }

            return Output;
        }

        void WriteToFile(std::FILE* Stream, std::string_view Message, LogLevel Level, bool AppendNewLine) {
            std::string Output{ BuildOutputMessage(Message, Level) };

            if (AppendNewLine) {
                Output += '\n';
            }

            std::fwrite(Output.data(), sizeof(char), Output.size(), Stream);
            std::fflush(Stream);

            std::scoped_lock<std::mutex> Guard{ SinkMutex };
            NotifySinkLocked(Output);
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
        WriteToFile(stdout, Message, LogLevel::Log, false);
    }

    void WriteLine(std::string_view Message) {
        WriteToFile(stdout, Message, LogLevel::Log, true);
    }

    void WriteError(std::string_view Message) {
        WriteToFile(stderr, Message, LogLevel::Warning, false);
    }

    void WriteErrorLine(std::string_view Message) {
        WriteToFile(stderr, Message, LogLevel::Warning, true);
    }

    void WriteInfo(std::string_view Message) {
        WriteToFile(stdout, Message, LogLevel::Info, false);
    }

    void WriteInfoLine(std::string_view Message) {
        WriteToFile(stdout, Message, LogLevel::Info, true);
    }

    void WriteSuccess(std::string_view Message) {
        WriteToFile(stdout, Message, LogLevel::Success, false);
    }

    void WriteSuccessLine(std::string_view Message) {
        WriteToFile(stdout, Message, LogLevel::Success, true);
    }

    void WriteWarning(std::string_view Message) {
        WriteToFile(stderr, Message, LogLevel::Warning, false);
    }

    void WriteWarningLine(std::string_view Message) {
        WriteToFile(stderr, Message, LogLevel::Warning, true);
    }
}
