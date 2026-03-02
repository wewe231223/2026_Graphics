#pragma once
#include <cstdio>
#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace StdOutput {
    using SinkFunction = std::function<void(const std::string&)>;

    void RegisterSink(SinkFunction Sink);
    void ClearSink();

    void Write(std::string_view Message);
    void WriteLine(std::string_view Message);
    void WriteError(std::string_view Message);
    void WriteErrorLine(std::string_view Message);

    template<typename... Args>
    void Print(std::format_string<Args...> Format, Args&&... Arguments);

    template<typename... Args>
    void PrintLine(std::format_string<Args...> Format, Args&&... Arguments);

    template<typename... Args>
    void PrintError(std::format_string<Args...> Format, Args&&... Arguments);

    template<typename... Args>
    void PrintErrorLine(std::format_string<Args...> Format, Args&&... Arguments);

    template<typename... Args>
    void Print(std::format_string<Args...> Format, Args&&... Arguments) {
        Write(std::format(Format, std::forward<Args>(Arguments)...));
    }

    template<typename... Args>
    void PrintLine(std::format_string<Args...> Format, Args&&... Arguments) {
        WriteLine(std::format(Format, std::forward<Args>(Arguments)...));
    }

    template<typename... Args>
    void PrintError(std::format_string<Args...> Format, Args&&... Arguments) {
        WriteError(std::format(Format, std::forward<Args>(Arguments)...));
    }

    template<typename... Args>
    void PrintErrorLine(std::format_string<Args...> Format, Args&&... Arguments) {
        WriteErrorLine(std::format(Format, std::forward<Args>(Arguments)...));
    }
}
