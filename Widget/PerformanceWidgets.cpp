#include "PerformanceWidgets.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>
#include "External/Include/ImGui/imgui.h"
#include "PerformanceProvider.h"

#ifdef max
#undef max
#endif

namespace Widget {
    FrameTimeWidget::FrameTimeWidget() {
    }

    FrameTimeWidget::~FrameTimeWidget() {
    }

    void FrameTimeWidget::Render(const Game::SceneWorldSnapshot* Snapshot) {
        (void)Snapshot;
        if (!ImGui::Begin("Performance Frame Time")) {
            ImGui::End();
            return;
        }

        const std::vector<float> FrameTimes{ PerformanceProvider::Get().GetFrameTimeMilliseconds() };

        if (!FrameTimes.empty()) {
            ImGui::PushItemWidth(-1.0f);
            ImGui::PlotLines("##FrameTimePlot", FrameTimes.data(), static_cast<int>(FrameTimes.size()), 0, nullptr, 0.0f, 33.3f, ImVec2(0.0f, 160.0f));
            ImGui::PopItemWidth(); 

            const ImVec2 PlotMin{ ImGui::GetItemRectMin() };
            const ImVec2 PlotMax{ ImGui::GetItemRectMax() };
            const float ThresholdMs{ 16.6f };
            const float ClampedThresholdMs{ std::clamp(ThresholdMs, 0.0f, 33.3f) };
            const float ThresholdY{ PlotMax.y - (PlotMax.y - PlotMin.y) * (ClampedThresholdMs / 33.3f) };
            ImDrawList* DrawList{ ImGui::GetWindowDrawList() };
            DrawList->PushClipRect(PlotMin, PlotMax, true);
            DrawList->AddLine(ImVec2(PlotMin.x, ThresholdY), ImVec2(PlotMax.x, ThresholdY), IM_COL32(255, 64, 64, 255), 2.0f);
            DrawList->PopClipRect();
        }

        ImGui::TextUnformatted("Frame Time (ms)");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Red Line: 16.6ms (60 FPS Threshold)");

        ImGui::End();
    }

    DistributionWidget::DistributionWidget() {
    }

    DistributionWidget::~DistributionWidget() {
    }

    void DistributionWidget::Render(const Game::SceneWorldSnapshot* Snapshot) {
        (void)Snapshot;
        if (!ImGui::Begin("Performance Distribution")) {
            ImGui::End();
            return;
        }

        const std::vector<float> FrameTimes{ PerformanceProvider::Get().GetFrameTimeMilliseconds() };

        if (!FrameTimes.empty()) {
            ImGui::PushItemWidth(-1.0f);
            ImGui::PlotHistogram("##Frame Time Histogram", FrameTimes.data(), static_cast<int>(FrameTimes.size()), 0, nullptr, 0.0f, 33.3f, ImVec2(0.0f, 140.0f));
            ImGui::PopItemWidth();
        }

        ImGui::Columns(3, "PerformanceDistributionColumns", false);
        ImGui::Text("Avg FPS\n%.2f", PerformanceProvider::Get().GetAverageFps());
        ImGui::NextColumn();
        ImGui::Text("1%% Low\n%.2f", PerformanceProvider::Get().GetOnePercentLowFps());
        ImGui::NextColumn();
        ImGui::Text("0.1%% Low\n%.2f", PerformanceProvider::Get().GetZeroPointOnePercentLowFps());
        ImGui::Columns(1);

        ImGui::End();
    }

    TimelineWidget::TimelineWidget() {
    }

    TimelineWidget::~TimelineWidget() {
    }

    void TimelineWidget::Render(const Game::SceneWorldSnapshot* Snapshot) {
        (void)Snapshot;
        if (!ImGui::Begin("Performance Timeline")) {
            ImGui::End();
            return;
        }

        const std::vector<ProfileEntry> Entries{ PerformanceProvider::Get().GetCurrentFrameProfiles() };
        std::string PreUpdateLegendText{ "PreUpdate" };
        std::string UpdateLegendText{ "Update" };
        std::string PostUpdateLegendText{ "PostUpdate" };
        std::string PhysicsActorUpdateLegendText{ "PhysicsActorUpdate" };
        std::string TransformWorldLegendText{ "TransformWorld" };
        std::string IkLegendText{ "IK" };
        std::string RenderPrepareLegendText{ "RenderPrepare" };
        std::string RenderLegendText{ "Render" };
        std::string PostRenderLegendText{ "PostRender" };

        if (!Entries.empty()) {
            const double MinStart{ Entries.front().StartMicroseconds };
            double MaxEnd{ Entries.front().EndMicroseconds };

            for (const ProfileEntry& Entry : Entries) {
                MaxEnd = std::max(MaxEnd, Entry.EndMicroseconds);
            }

            const float Width{ ImGui::GetContentRegionAvail().x };
            const float LayerHeight{ 28.0f };
            const ImVec2 Origin{ ImGui::GetCursorScreenPos() };
            ImDrawList* DrawList{ ImGui::GetWindowDrawList() };
            const double Duration{ std::max(1.0, MaxEnd - MinStart) };

            std::size_t MaxDepth{};
            for (const ProfileEntry& Entry : Entries) {
                MaxDepth = std::max(MaxDepth, Entry.Depth);
            }

            const float TotalHeight{ static_cast<float>(MaxDepth + 1) * LayerHeight };
            ImGui::Dummy(ImVec2(Width, TotalHeight));

            for (const ProfileEntry& Entry : Entries) {
                const float X0{ Origin.x + static_cast<float>((Entry.StartMicroseconds - MinStart) / Duration) * Width };
                const float X1{ Origin.x + static_cast<float>((Entry.EndMicroseconds - MinStart) / Duration) * Width };
                const float Y0{ Origin.y + static_cast<float>(Entry.Depth) * LayerHeight };
                const float Y1{ Y0 + LayerHeight - 4.0f };
                const uint32_t Color{ GetColorByName(Entry.Name) };
                DrawList->AddRectFilled(ImVec2(X0, Y0), ImVec2(X1, Y1), Color, 4.0f);

                const double EntryDurationMilliseconds{ std::max(0.0, Entry.EndMicroseconds - Entry.StartMicroseconds) / 1000.0 };
                const std::string EntryLabelText{ std::format("{:.2f} ms", EntryDurationMilliseconds) };
                const ImVec2 EntryLabelSize{ ImGui::CalcTextSize(EntryLabelText.c_str()) };
                const float AvailableWidth{ X1 - X0 };
                if (AvailableWidth >= EntryLabelSize.x + 8.0f) {
                    const float TextX{ X0 + (AvailableWidth - EntryLabelSize.x) * 0.5f };
                    const float TextY{ Y0 + ((Y1 - Y0) - EntryLabelSize.y) * 0.5f };
                    DrawList->AddText(ImVec2(TextX, TextY), IM_COL32(255, 255, 255, 255), EntryLabelText.c_str());
                }
            }

            const auto ResolvePhaseDurationMilliseconds = [&Entries](const std::string& PhaseName) -> double {
                double TotalDurationMicroseconds{};
                for (const ProfileEntry& Entry : Entries) {
                    if (Entry.Name == PhaseName) {
                        TotalDurationMicroseconds += std::max(0.0, Entry.EndMicroseconds - Entry.StartMicroseconds);
                    }
                }
                return TotalDurationMicroseconds / 1000.0;
            };

            PreUpdateLegendText = std::format("PreUpdate ({:.2f} ms)", ResolvePhaseDurationMilliseconds("PreUpdate"));
            UpdateLegendText = std::format("Update ({:.2f} ms)", ResolvePhaseDurationMilliseconds("Update"));
            PostUpdateLegendText = std::format("PostUpdate ({:.2f} ms)", ResolvePhaseDurationMilliseconds("PostUpdate"));
            PhysicsActorUpdateLegendText = std::format("PhysicsActorUpdate ({:.2f} ms)", ResolvePhaseDurationMilliseconds("PhysicsActorUpdate"));
            TransformWorldLegendText = std::format("TransformWorld ({:.2f} ms)", ResolvePhaseDurationMilliseconds("TransformWorld"));
            IkLegendText = std::format("IK ({:.2f} ms)", ResolvePhaseDurationMilliseconds("IK"));
            RenderPrepareLegendText = std::format("RenderPrepare ({:.2f} ms)", ResolvePhaseDurationMilliseconds("RenderPrepare"));
            RenderLegendText = std::format("Render ({:.2f} ms)", ResolvePhaseDurationMilliseconds("Render"));
            PostRenderLegendText = std::format("PostRender ({:.2f} ms)", ResolvePhaseDurationMilliseconds("PostRender"));
        }

        ImGui::Columns(2, "PerformanceTimelineLegendColumns", false);
        RenderLegendItem(PreUpdateLegendText.c_str(), GetColorByName("PreUpdate"));
        ImGui::NextColumn();
        RenderLegendItem(UpdateLegendText.c_str(), GetColorByName("Update"));
        ImGui::NextColumn();
        RenderLegendItem(PostUpdateLegendText.c_str(), GetColorByName("PostUpdate"));
        ImGui::NextColumn();
        RenderLegendItem(PhysicsActorUpdateLegendText.c_str(), GetColorByName("PhysicsActorUpdate"));
        ImGui::NextColumn();
        RenderLegendItem(TransformWorldLegendText.c_str(), GetColorByName("TransformWorld"));
        ImGui::NextColumn();
        RenderLegendItem(IkLegendText.c_str(), GetColorByName("IK"));
        ImGui::NextColumn();
        RenderLegendItem(RenderPrepareLegendText.c_str(), GetColorByName("RenderPrepare"));
        ImGui::NextColumn();
        RenderLegendItem(RenderLegendText.c_str(), GetColorByName("Render"));
        ImGui::NextColumn();
        RenderLegendItem(PostRenderLegendText.c_str(), GetColorByName("PostRender"));
        ImGui::Columns(1);

        ImGui::End();
    }

    void TimelineWidget::RenderLegendItem(const char* Label, uint32_t Color) const {
        ImDrawList* DrawList{ ImGui::GetWindowDrawList() };
        const ImVec2 Cursor{ ImGui::GetCursorScreenPos() };
        const float BoxSize{ 12.0f };
        DrawList->AddRectFilled(Cursor, ImVec2(Cursor.x + BoxSize, Cursor.y + BoxSize), Color, 2.0f);
        ImGui::Dummy(ImVec2(BoxSize + 4.0f, BoxSize));
        ImGui::SameLine();
        ImGui::TextUnformatted(Label);
    }

    uint32_t TimelineWidget::GetColorByName(const std::string& Name) const {
        if (Name.find("PreUpdate") != std::string::npos) {
            return IM_COL32(52, 152, 219, 255);
        }

        if (Name.find("PostUpdate") != std::string::npos) {
            return IM_COL32(55, 89, 182, 255);
        }

        if (Name.find("PhysicsActorUpdate") != std::string::npos) {
            return IM_COL32(26, 188, 156, 255);
        }

        if (Name.find("Update") != std::string::npos) {
            return IM_COL32(46, 204, 113, 255);
        }

        if (Name.find("TransformWorld") != std::string::npos) {
            return IM_COL32(230, 126, 34, 255);
        }

        if (Name.find("IK") != std::string::npos) {
            return IM_COL32(241, 196, 15, 255);
        }

        if (Name.find("RenderPrepare") != std::string::npos) {
            return IM_COL32(155, 89, 182, 255);
		}

        if (Name.find("Render") != std::string::npos && Name.find("PostRender") == std::string::npos) {
            return IM_COL32(52, 73, 94, 255);
        }

        if (Name.find("PostRender") != std::string::npos) {
            return IM_COL32(231, 76, 60, 255);
        }

        return IM_COL32(149, 165, 166, 255);
    }

    VramUsageWidget::VramUsageWidget() {
    }

    VramUsageWidget::~VramUsageWidget() {
    }

    void VramUsageWidget::Render(const Game::SceneWorldSnapshot* Snapshot) {
        (void)Snapshot;
        if (!ImGui::Begin("Performance VRAM")) {
            ImGui::End();
            return;
        }

        const uint64_t BudgetBytes{ PerformanceProvider::Get().GetVramBudgetBytes() };
        const uint64_t UsageBytes{ PerformanceProvider::Get().GetVramUsageBytes() };
        const float Ratio{ std::clamp(PerformanceProvider::Get().GetVramUsageRatio(), 0.0f, 1.0f) };

        ImVec4 Color{ 0.9f, 0.85f, 0.2f, 1.0f };
        if (Ratio > 0.85f) {
            Color = ImVec4(0.95f, 0.2f, 0.2f, 1.0f);
        }

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Color);
        ImGui::ProgressBar(Ratio, ImVec2(-1.0f, 28.0f), std::format("{:.1f}%", Ratio * 100.0f).c_str());
        ImGui::PopStyleColor();

        const double UsageMb{ static_cast<double>(UsageBytes) / (1024.0 * 1024.0) };
        const double BudgetMb{ static_cast<double>(BudgetBytes) / (1024.0 * 1024.0) };
        ImGui::Text("Usage %.1f MB / Budget %.1f MB", UsageMb, BudgetMb);

        ImGui::End();
    }
}
