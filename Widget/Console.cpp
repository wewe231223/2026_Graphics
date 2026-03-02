#include "Console.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <format>
#include <fstream>
#include <sstream>
#include "External/Include/ImGui/imgui.h"
#include "Utility/StdOutput.h"
#include "Game/Base/Input.h"

namespace Widget {
    void LogBuffer::AddLog(const std::string& RawMessage) {
        std::scoped_lock<std::mutex> Guard{ mMutex };
        ConsoleLogEntry ParsedEntry{ ParseLogEntry(RawMessage) };

        if (!mItems.empty()) {
            ConsoleLogEntry& LastEntry{ mItems.back() };

            if (LastEntry.Level == ParsedEntry.Level && LastEntry.Message == ParsedEntry.Message) {
                LastEntry.RepeatCount += 1;
                LastEntry.Timestamp = ParsedEntry.Timestamp;
                LastEntry.RenderText = BuildRenderText(LastEntry);
                mScrollToBottom = true;
                return;
            }
        }

        ParsedEntry.RenderText = BuildRenderText(ParsedEntry);

        if (mItems.size() >= MaxLines) {
            mItems.erase(mItems.begin());
        }

        mItems.push_back(std::move(ParsedEntry));
        mScrollToBottom = true;
    }

    std::vector<ConsoleLogEntry> LogBuffer::GetItemsCopy() const {
        std::scoped_lock<std::mutex> Guard{ mMutex };
        return mItems;
    }

    bool LogBuffer::CheckAndResetScroll() {
        std::scoped_lock<std::mutex> Guard{ mMutex };
        const bool ShouldScroll{ mScrollToBottom };
        mScrollToBottom = false;
        return ShouldScroll;
    }

    ConsoleLogEntry LogBuffer::ParseLogEntry(std::string_view RawMessage) const {
        ConsoleLogEntry Entry{};
        const std::size_t RightBracketIndex{ RawMessage.find(']') };

        if (RawMessage.size() > 2 && RawMessage.front() == '[' && RightBracketIndex != std::string_view::npos) {
            Entry.Timestamp = std::string{ RawMessage.substr(1, RightBracketIndex - 1) };
            const std::size_t ArrowIndex{ RawMessage.find('>', RightBracketIndex) };

            if (ArrowIndex != std::string_view::npos && ArrowIndex > RightBracketIndex + 1) {
                std::string LevelText{ RawMessage.substr(RightBracketIndex + 1, ArrowIndex - RightBracketIndex - 1) };
                const std::size_t LevelBegin{ LevelText.find_first_not_of(' ') };
                const std::size_t LevelEnd{ LevelText.find_last_not_of(' ') };

                if (LevelBegin != std::string::npos && LevelEnd != std::string::npos) {
                    Entry.Level = LevelText.substr(LevelBegin, LevelEnd - LevelBegin + 1);
                }

                const std::size_t MessageBegin{ ArrowIndex + 1 < RawMessage.size() && RawMessage[ArrowIndex + 1] == ' ' ? ArrowIndex + 2 : ArrowIndex + 1 };
                Entry.Message = std::string{ RawMessage.substr(MessageBegin) };
            }
        }

        if (Entry.Timestamp.empty()) {
            Entry.Timestamp = "00:00:00.000";
        }

        if (Entry.Level.empty()) {
            Entry.Level = "Log";
        }

        if (Entry.Message.empty()) {
            Entry.Message = std::string{ RawMessage };
        }

        Entry.RenderText = BuildRenderText(Entry);
        return Entry;
    }

    std::string LogBuffer::BuildRenderText(const ConsoleLogEntry& Entry) const {
        if (Entry.RepeatCount > 1) {
            return std::format("[{}] {:<7} > {} ({})", Entry.Timestamp, Entry.Level, Entry.Message, Entry.RepeatCount);
        }

        return std::format("[{}] {:<7} > {}", Entry.Timestamp, Entry.Level, Entry.Message);
    }

    std::string ExportLogsCommand::GetName() const {
        return "export";
    }

    bool ExportLogsCommand::Execute(const std::vector<std::string>& Arguments, const std::vector<ConsoleLogEntry>& Entries) {
        const std::string FileName{ Arguments.empty() ? "ConsoleLog.txt" : Arguments.front() };
        std::ofstream OutputFile{ FileName, std::ios::out | std::ios::trunc };

        if (!OutputFile.is_open()) {
            StdOutput::WriteErrorLine("[Console] [Command] Failed to export log file.");
            return false;
        }

        for (const ConsoleLogEntry& Entry : Entries) {
            OutputFile << Entry.RenderText << '\n';
        }

        StdOutput::WriteSuccessLine(std::format("[Console] [Command] Log file exported: {}", FileName));
        return true;
    }

    ImGuiConsole::ImGuiConsole() {
        RegisterCommands();

        StdOutput::RegisterSink([this](const std::string& Message) {
            std::size_t StartIndex{ 0 };

            while (StartIndex < Message.size()) {
                const std::size_t NewLineIndex{ Message.find('\n', StartIndex) };

                if (NewLineIndex == std::string::npos) {
                    mBuffer.AddLog(Message.substr(StartIndex));
                    break;
                }

                if (NewLineIndex > StartIndex) {
                    mBuffer.AddLog(Message.substr(StartIndex, NewLineIndex - StartIndex));
                }

                StartIndex = NewLineIndex + 1;
            }
        });
    }

    ImGuiConsole::~ImGuiConsole() {
        StdOutput::ClearSink();
    }

    void ImGuiConsole::Render() {
        ImGui::SetNextWindowSize({ 700.0f, 500.0f }, ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Debug Console")) {
            Globals::Input::Get().SetImGuiInputBlocked(false);
            ImGui::End();
            return;
        }

        const bool IsConsoleFocused{ ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) };
        Globals::Input::Get().SetImGuiInputBlocked(IsConsoleFocused);

        ImGui::TextUnformatted("Level");
        ImGui::SameLine();

        constexpr std::array<const char*, 5> LevelOptions{ "All", "Log", "Info", "Warning", "Success" };
        ImGui::SetNextItemWidth(150.0f);
        ImGui::Combo("##LevelFilter", &mLevelFilterIndex, LevelOptions.data(), static_cast<int>(LevelOptions.size()));

        ImGui::SameLine();
        ImGui::TextUnformatted("Keyword");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputText("##KeywordFilter", mSearchKeywordBuffer.data(), static_cast<int>(mSearchKeywordBuffer.size()));

        mSearchKeyword = mSearchKeywordBuffer.data();

        if (mLevelFilterIndex <= 0 || mLevelFilterIndex >= static_cast<int>(LevelOptions.size())) {
            mSearchLevel.clear();
        } else {
            mSearchLevel = LevelOptions[static_cast<std::size_t>(mLevelFilterIndex)];
        }

        const std::vector<ConsoleLogEntry> Logs{ mBuffer.GetItemsCopy() };

        const float PrefixWidth{ ImGui::CalcTextSize("[00:00:00] WARNING > ").x + ImGui::GetStyle().ItemInnerSpacing.x };
        const float FooterHeight{ ImGui::GetFrameHeightWithSpacing() };

        ImGui::BeginChild("ConsoleScrollingRegion", ImVec2(0.0f, -FooterHeight), false, ImGuiWindowFlags_None);

        if (ImGui::BeginTable("ConsoleLogTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Prefix", ImGuiTableColumnFlags_WidthFixed, PrefixWidth);
            ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);

            for (const ConsoleLogEntry& Entry : Logs) {
                if (!IsLogVisible(Entry)) continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("[%s] %-7s >", Entry.Timestamp.c_str(), Entry.Level.c_str());

                ImGui::TableSetColumnIndex(1);

                if (Entry.RepeatCount > 1) {
                    ImGui::TextWrapped("%s (%zu)", Entry.Message.c_str(), Entry.RepeatCount);
                }
                else {
                    ImGui::TextWrapped("%s", Entry.Message.c_str());
                }
            }
            ImGui::EndTable();
        }

        if (mBuffer.CheckAndResetScroll()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ">");
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::SetNextItemWidth(-1.0f);

        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

        if (ImGui::InputText("##CommandInput", mCommandInputBuffer.data(), mCommandInputBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            ProcessCommandInput();
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::PopStyleColor();

        ImGui::End();
    }

    void ImGuiConsole::RegisterCommands() {
        mCommands.emplace("export", std::make_unique<ExportLogsCommand>());
    }

    void ImGuiConsole::ProcessCommandInput() {
        const std::string CommandInput{ mCommandInputBuffer.data() };

        if (CommandInput.empty()) {
            return;
        }

        if (CommandInput.front() != '/') {
            StdOutput::WriteWarningLine("[Console] [Command] Command must start with '/'.");
            mCommandInputBuffer.fill('\0');
            return;
        }

        std::istringstream Stream{ CommandInput.substr(1) };
        std::string CommandName{};
        Stream >> CommandName;

        if (CommandName.empty()) {
            mCommandInputBuffer.fill('\0');
            return;
        }

        std::vector<std::string> Arguments{};
        std::string Token{};

        while (Stream >> Token) {
            Arguments.push_back(Token);
        }

        auto CommandIter{ mCommands.find(CommandName) };

        if (CommandIter == mCommands.end()) {
            StdOutput::WriteErrorLine(std::format("[Console] [Command] Unknown command: {}", CommandName));
            mCommandInputBuffer.fill('\0');
            return;
        }

        CommandIter->second->Execute(Arguments, mBuffer.GetItemsCopy());
        mCommandInputBuffer.fill('\0');
    }

    bool ImGuiConsole::IsLogVisible(const ConsoleLogEntry& Entry) const {
        if (!mSearchLevel.empty() && Entry.Level != mSearchLevel) {
            return false;
        }

        if (!mSearchKeyword.empty()) {
            std::string KeywordLower{ mSearchKeyword };
            std::string MessageLower{ Entry.Message };
            std::string LevelLower{ Entry.Level };
            std::string TimestampLower{ Entry.Timestamp };
            std::string RenderLower{ Entry.RenderText };

            std::transform(KeywordLower.begin(), KeywordLower.end(), KeywordLower.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            std::transform(MessageLower.begin(), MessageLower.end(), MessageLower.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            std::transform(LevelLower.begin(), LevelLower.end(), LevelLower.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            std::transform(TimestampLower.begin(), TimestampLower.end(), TimestampLower.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            std::transform(RenderLower.begin(), RenderLower.end(), RenderLower.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

            const bool IsMatched{ MessageLower.find(KeywordLower) != std::string::npos || LevelLower.find(KeywordLower) != std::string::npos || TimestampLower.find(KeywordLower) != std::string::npos || RenderLower.find(KeywordLower) != std::string::npos };

            if (!IsMatched) {
                return false;
            }
        }

        return true;
    }
}
