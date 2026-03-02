#include "Console.h"
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#include <thread>

#include "External/Include/ImGui/imgui.h"
#include "External/Include/ImGui/imgui_internal.h"

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
        _pipe(pipeFds, 4096, _O_TEXT);
        oldStdout = _dup(_fileno(stdout));
        _dup2(pipeFds[1], _fileno(stdout));

        exitThread = false;
        captureThread = std::thread([this]() {
            char readBuf[1024];
            while (!exitThread) {
                int bytesRead = _read(pipeFds[0], readBuf, sizeof(readBuf) - 1);
                if (bytesRead > 0) {
                    readBuf[bytesRead] = '\0';
                    std::string temp(readBuf);
                    size_t pos = 0, next;
                    while ((next = temp.find('\n', pos)) != std::string::npos) {
                        buffer.AddLog(temp.substr(pos, next - pos));
                        pos = next + 1;
                    }
                    if (pos < temp.size()) buffer.AddLog(temp.substr(pos));
                }
            }
            });
    }

    ImGuiConsole::~ImGuiConsole() {
        exitThread = true;
        _dup2(oldStdout, _fileno(stdout));
        _close(pipeFds[1]);
        if (captureThread.joinable()) captureThread.join();
        _close(pipeFds[0]);
        _close(oldStdout);
    }

    void ImGuiConsole::Render() {
        ImGui::SetNextWindowSize({ 500, 400 }, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Debug Console")) {
            ImGui::End();
            return;
        }

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
        ImGui::End();
    }
}