#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "Common.h"

namespace Widget {
    class ResourceBrowserWidget final : public IWidget {
    public:
        ResourceBrowserWidget();
        ~ResourceBrowserWidget() override;

        ResourceBrowserWidget(const ResourceBrowserWidget& Other) = delete;
        ResourceBrowserWidget& operator=(const ResourceBrowserWidget& Other) = delete;

        ResourceBrowserWidget(ResourceBrowserWidget&& Other) noexcept = delete;
        ResourceBrowserWidget& operator=(ResourceBrowserWidget&& Other) noexcept = delete;

    public:
        void Render() override;
        void SetCurrentSceneName(const std::string& SceneName);

    private:
        void RefreshFiles();
        std::filesystem::path GetResourceDirectoryPath() const;
        void RenderResourceListWindow();
        void RenderScreenDropTarget();

    private:
        std::string mCurrentSceneName{};
        std::vector<std::filesystem::path> mResourceFiles{};
        bool mNeedRefresh{ true };
    };
}
