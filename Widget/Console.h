#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "Common.h"

namespace Widget {
    class LogBuffer {
    public:
        static constexpr std::size_t MaxLines{ 1000 };

    public:
        LogBuffer() = default;
        ~LogBuffer() = default;

        LogBuffer(const LogBuffer&) = delete;
        LogBuffer& operator=(const LogBuffer&) = delete;

        LogBuffer(LogBuffer&&) noexcept = delete;
        LogBuffer& operator=(LogBuffer&&) noexcept = delete;

    public:
        void AddLog(const std::string& Message);
        std::vector<std::string> GetItemsCopy() const;
        bool CheckAndResetScroll();

    private:
        mutable std::mutex mMutex{};
        std::vector<std::string> mItems{};
        std::string mCurrentLine{};
        bool mScrollToBottom{ false };
    };

    class ImGuiConsole : public IWidget {
    public:
        ImGuiConsole();
        ~ImGuiConsole();

        ImGuiConsole(const ImGuiConsole&) = delete;
        ImGuiConsole& operator=(const ImGuiConsole&) = delete;

        ImGuiConsole(ImGuiConsole&&) noexcept = delete;
        ImGuiConsole& operator=(ImGuiConsole&&) noexcept = delete;

    public:
        void Render() override;

    private:
        LogBuffer mBuffer{};
    };
}
