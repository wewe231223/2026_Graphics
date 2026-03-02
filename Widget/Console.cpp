#include "Console.h"
#include "../External/Include/ImGui/imgui.h"
#include "../External/Include/ImGui/imgui_impl_dx12.h"
#include "../External/Include/ImGui/imgui_impl_win32.h"
#include "../External/Include/ImGui/imgui_internal.h"

namespace Widget {
    int LogBuffer::overflow(int_type v) {
        if (v != EOF) {
            if (v == '\n') {
                AddLog(currentLine);
                currentLine.clear();
            }
            else {
                currentLine += static_cast<char>(v);
            }
        }
        return v;
    }

    void LogBuffer::AddLog(const std::string& str) {
        std::lock_guard lock(mtx);
        if (items.size() >= MAX_LINES) {
            items.erase(items.begin());
        }
        items.push_back(str);
        scrollToBottom = true;
    }

    bool LogBuffer::CheckAndResetScroll() {
        bool s = scrollToBottom;
        scrollToBottom = false;
        return s;
    }


    ImGuiConsole::ImGuiConsole() {
        oldCoutBuf = std::cout.rdbuf(&buffer);
    }

    ImGuiConsole::~ImGuiConsole() {
        std::cout.rdbuf(oldCoutBuf);
    }

    void ImGuiConsole::Render() {
        ImGui::SetNextWindowSize({ 500, 400 }, ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Debug Console")) {
            ImGui::End();
            return;
        }

        //const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        //ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);

        const auto& logs = buffer.GetItems();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(logs.size()));

        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                ImGui::TextUnformatted(logs[i].c_str());
            }
        }

        if (buffer.CheckAndResetScroll()) {
            ImGui::SetScrollHereY(1.0f);
        }

        //ImGui::EndChild();
        //ImGui::Separator();


        ImGui::End();
    }
}