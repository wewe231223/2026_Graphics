#include "ResourceBrowserWidget.h"
#include <algorithm>
#include "External/Include/ImGui/imgui.h"
#include "Utility/StdOutput.h"

namespace Widget {
    ResourceBrowserWidget::ResourceBrowserWidget()
        : mCurrentSceneName{},
        mResourceFiles{},
        mNeedRefresh{ true } {
    }

    ResourceBrowserWidget::~ResourceBrowserWidget() {
    }

    void ResourceBrowserWidget::Render() {
        if (mNeedRefresh) {
            ResourceBrowserWidget::RefreshFiles();
        }

        ResourceBrowserWidget::RenderResourceListWindow();
        ResourceBrowserWidget::RenderScreenDropTarget();
    }

    void ResourceBrowserWidget::SetCurrentSceneName(const std::string& SceneName) {
        if (mCurrentSceneName == SceneName) {
            return;
        }

        mCurrentSceneName = SceneName;
        mNeedRefresh = true;
    }

    void ResourceBrowserWidget::RefreshFiles() {
        mResourceFiles.clear();

        const std::filesystem::path ResourceDirectoryPath{ ResourceBrowserWidget::GetResourceDirectoryPath() };
        if (std::filesystem::exists(ResourceDirectoryPath) == false || std::filesystem::is_directory(ResourceDirectoryPath) == false) {
            mNeedRefresh = false;
            return;
        }

        for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator{ ResourceDirectoryPath }) {
            if (Entry.is_regular_file()) {
                mResourceFiles.push_back(Entry.path());
            }
        }

        std::sort(mResourceFiles.begin(), mResourceFiles.end(), [](const std::filesystem::path& Left, const std::filesystem::path& Right) {
            return Left.filename().string() < Right.filename().string();
        });

        mNeedRefresh = false;
    }

    std::filesystem::path ResourceBrowserWidget::GetResourceDirectoryPath() const {
        if (mCurrentSceneName.empty()) {
            return std::filesystem::path{ "Resources" };
        }

        return std::filesystem::path{ "Resources" } / mCurrentSceneName;
    }

    void ResourceBrowserWidget::RenderResourceListWindow() {
        ImGui::SetNextWindowSize({ 420.0f, 320.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scene Resource Browser") == false) {
            ImGui::End();
            return;
        }

        const std::filesystem::path ResourceDirectoryPath{ ResourceBrowserWidget::GetResourceDirectoryPath() };
        const std::string DirectoryLabel{ ResourceDirectoryPath.string() };

        ImGui::Text("Directory: %s", DirectoryLabel.c_str());
        if (ImGui::Button("Refresh")) {
            mNeedRefresh = true;
        }

        ImGui::Separator();

        if (mResourceFiles.empty()) {
            ImGui::TextUnformatted("No files found.");
        }
        else {
            for (const std::filesystem::path& FilePath : mResourceFiles) {
                const std::string FileName{ FilePath.filename().string() };
                ImGui::Selectable(FileName.c_str(), false);

                if (ImGui::BeginDragDropSource()) {
                    const std::string PayloadPath{ FilePath.string() };
                    ImGui::SetDragDropPayload("RESOURCE_FILE_PATH", PayloadPath.c_str(), PayloadPath.size() + 1);
                    ImGui::Text("%s", FileName.c_str());
                    ImGui::EndDragDropSource();
                }
            }
        }

        ImGui::End();
    }

    void ResourceBrowserWidget::RenderScreenDropTarget() {
        const ImGuiViewport* MainViewport{ ImGui::GetMainViewport() };

        ImGui::SetNextWindowPos(MainViewport->Pos);
        ImGui::SetNextWindowSize(MainViewport->Size);
        ImGui::SetNextWindowViewport(MainViewport->ID);

        const ImGuiWindowFlags WindowFlags{ ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus };

        if (ImGui::Begin("##ScreenDropTarget", nullptr, WindowFlags) == false) {
            ImGui::End();
            return;
        }

        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* Payload{ ImGui::AcceptDragDropPayload("RESOURCE_FILE_PATH") };
            if (Payload != nullptr && Payload->Data != nullptr) {
                const char* FilePathCStr{ static_cast<const char*>(Payload->Data) };
                const std::filesystem::path FilePath{ FilePathCStr };
                const std::string FileName{ FilePath.filename().string() };
                Utility::StdOutput::PrintLine("[Widget] Drop File: {}", FileName);
            }

            ImGui::EndDragDropTarget();
        }

        ImGui::End();
    }
}
