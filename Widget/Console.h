#pragma once
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "Common.h"

namespace Widget {
    struct ConsoleLogEntry {
        std::string Timestamp{};
        std::string Level{};
        std::string Message{};
        std::string RenderText{};
        std::size_t RepeatCount{ 1 };
    };

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
        void AddLog(const std::string& RawMessage);
        std::vector<ConsoleLogEntry> GetItemsCopy() const;
        bool CheckAndResetScroll();

    private:
        ConsoleLogEntry ParseLogEntry(std::string_view RawMessage) const;
        std::string BuildRenderText(const ConsoleLogEntry& Entry) const;

    private:
        mutable std::mutex mMutex{};
        std::vector<ConsoleLogEntry> mItems{};
        bool mScrollToBottom{ false };
    };

    struct ConsoleCommandContext final {
    public:
        const Game::SceneWorldSnapshot* SceneSnapshot{ nullptr };
        const std::vector<ConsoleLogEntry>* LogEntries{ nullptr };
    };

    class IConsoleCommand {
    public:
        IConsoleCommand() = default;
        virtual ~IConsoleCommand() = default;

        IConsoleCommand(const IConsoleCommand&) = delete;
        IConsoleCommand& operator=(const IConsoleCommand&) = delete;

        IConsoleCommand(IConsoleCommand&&) noexcept = delete;
        IConsoleCommand& operator=(IConsoleCommand&&) noexcept = delete;

    public:
        virtual std::string GetName() const = 0;
        virtual bool Execute(const std::vector<std::string>& Arguments, const ConsoleCommandContext& Context) = 0;
    };

    class ExportLogsCommand : public IConsoleCommand {
    public:
        ExportLogsCommand();
        ~ExportLogsCommand() override;

        ExportLogsCommand(const ExportLogsCommand&) = delete;
        ExportLogsCommand& operator=(const ExportLogsCommand&) = delete;

        ExportLogsCommand(ExportLogsCommand&&) noexcept = delete;
        ExportLogsCommand& operator=(ExportLogsCommand&&) noexcept = delete;

    public:
        std::string GetName() const override;
        bool Execute(const std::vector<std::string>& Arguments, const ConsoleCommandContext& Context) override;
    };


    class ExportSceneCommand final : public IConsoleCommand {
    public:
        ExportSceneCommand();
        ~ExportSceneCommand() override;

        ExportSceneCommand(const ExportSceneCommand& Other) = delete;
        ExportSceneCommand& operator=(const ExportSceneCommand& Other) = delete;

        ExportSceneCommand(ExportSceneCommand&& Other) noexcept = delete;
        ExportSceneCommand& operator=(ExportSceneCommand&& Other) noexcept = delete;

    public:
        std::string GetName() const override;
        bool Execute(const std::vector<std::string>& Arguments, const ConsoleCommandContext& Context) override;
    };

    class ImGuiConsole : public IWidget {
    public:
        using CommandMap = std::unordered_map<std::string, std::unique_ptr<IConsoleCommand>>;

    public:
        ImGuiConsole();
        ~ImGuiConsole();

        ImGuiConsole(const ImGuiConsole&) = delete;
        ImGuiConsole& operator=(const ImGuiConsole&) = delete;

        ImGuiConsole(ImGuiConsole&&) noexcept = delete;
        ImGuiConsole& operator=(ImGuiConsole&&) noexcept = delete;

    public:
        void Render(const Game::SceneWorldSnapshot* Snapshot) override;

    private:
        void RegisterCommands();
        void ProcessCommandInput();
        bool IsLogVisible(const ConsoleLogEntry& Entry) const;

    private:
        LogBuffer mBuffer{};
        CommandMap mCommands{};
        std::string mSearchKeyword{};
        std::string mSearchLevel{};
        std::array<char, 128> mSearchKeywordBuffer{};
        std::array<char, 256> mCommandInputBuffer{};
        int mLevelFilterIndex{ 0 };
        const Game::SceneWorldSnapshot* mCurrentSnapshot{ nullptr };
    };
}
