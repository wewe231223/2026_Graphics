#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <mutex>
#include "Common.h"

namespace Widget {
    class LogBuffer : public std::streambuf {
    public:
        static constexpr size_t MAX_LINES = 1000;

        void AddLog(const std::string& str);
        const std::vector<std::string>& GetItems() const { return items; }
        bool CheckAndResetScroll();

    protected:
        int_type overflow(int_type v) override;

    private:
        std::vector<std::string> items;
        std::string currentLine;
        std::mutex mtx;
        bool scrollToBottom = false;
    };

    // ImGui 콘솔 위젯 클래스
    class ImGuiConsole : public IWidget {
    public:
        ImGuiConsole();
        ~ImGuiConsole();

        void Render() override;

    private:
        LogBuffer buffer;
        std::streambuf* oldCoutBuf;
    };
}