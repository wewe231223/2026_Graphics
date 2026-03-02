#include "Console.h"
#include "External/Include/ImGui/imgui.h"
#include "Utility/StdOutput.h"

namespace Widget {
    void LogBuffer::AddLog(const std::string& Message) {
        std::scoped_lock<std::mutex> Guard{ mMutex };

        if (mItems.size() >= MaxLines) {
            mItems.erase(mItems.begin());
        }

        mItems.push_back(Message);
        mScrollToBottom = true;
    }

    std::vector<std::string> LogBuffer::GetItemsCopy() const {
        std::scoped_lock<std::mutex> Guard{ mMutex };
        return mItems;
    }

    bool LogBuffer::CheckAndResetScroll() {
        std::scoped_lock<std::mutex> Guard{ mMutex };
        const bool ShouldScroll{ mScrollToBottom };
        mScrollToBottom = false;
        return ShouldScroll;
    }

    ImGuiConsole::ImGuiConsole() {
        StdOutput::RegisterSink([this](const std::string& Message) {
            std::size_t StartIndex{ 0 };
            std::size_t NewLineIndex{ Message.find('\n') };

            while (NewLineIndex != std::string::npos) {
                mBuffer.AddLog(Message.substr(StartIndex, NewLineIndex - StartIndex));
                StartIndex = NewLineIndex + 1;
                NewLineIndex = Message.find('\n', StartIndex);
            }

            if (StartIndex < Message.size()) {
                mBuffer.AddLog(Message.substr(StartIndex));
            }
        });
    }

    ImGuiConsole::~ImGuiConsole() {
        StdOutput::ClearSink();
    }

    void ImGuiConsole::Render() {
        ImGui::SetNextWindowSize({ 500, 400 }, ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Debug Console")) {
            ImGui::End();
            return;
        }

        const std::vector<std::string> Logs{ mBuffer.GetItemsCopy() };

        ImGuiListClipper Clipper{};
        Clipper.Begin(static_cast<int>(Logs.size()));

        ImGui::PushTextWrapPos(0.0f);

        while (Clipper.Step()) {
            for (int Index{ Clipper.DisplayStart }; Index < Clipper.DisplayEnd; ++Index) {
                ImGui::TextUnformatted(Logs[static_cast<std::size_t>(Index)].c_str());
            }
        }

        ImGui::PopTextWrapPos();

        if (mBuffer.CheckAndResetScroll()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::End();
    }
}
