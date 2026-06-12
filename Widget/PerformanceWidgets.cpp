#include "PerformanceWidgets.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>
#include "External/Include/ImGui/imgui.h"
#include "Game/Scene/Base/SceneWorldSnapshot.h"
#include "PerformanceProvider.h"

#ifdef max
#undef max
#endif

namespace Widget {
    static float ResolveFrameTimePlotMaxMilliseconds(const std::vector<FrameTimeSample>& FrameTimeSamples) {
        float MaxFrameTimeMilliseconds{ 1.0f };
        for (const FrameTimeSample& Sample : FrameTimeSamples) {
            MaxFrameTimeMilliseconds = std::max(MaxFrameTimeMilliseconds, Sample.TimeMilliseconds);
        }

        const float PaddedMaxMilliseconds{ MaxFrameTimeMilliseconds * 1.25f };
        if (PaddedMaxMilliseconds <= 2.0f) {
            return 2.0f;
        }

        if (PaddedMaxMilliseconds <= 4.0f) {
            return 4.0f;
        }

        if (PaddedMaxMilliseconds <= 8.0f) {
            return 8.0f;
        }

        if (PaddedMaxMilliseconds <= 16.6f) {
            return 16.6f;
        }

        if (PaddedMaxMilliseconds <= 33.3f) {
            return 33.3f;
        }

        if (PaddedMaxMilliseconds <= 66.6f) {
            return 66.6f;
        }

        return std::ceil(PaddedMaxMilliseconds / 50.0f) * 50.0f;
    }

    static float ResolveFrameTimeScaleWidth(float PlotMinMilliseconds, float PlotMaxMilliseconds) {
        const std::string MinLabelText{ std::format("{:.1f} ms", PlotMinMilliseconds) };
        const std::string MaxLabelText{ std::format("{:.1f} ms", PlotMaxMilliseconds) };
        const float MinLabelWidth{ ImGui::CalcTextSize(MinLabelText.c_str()).x };
        const float MaxLabelWidth{ ImGui::CalcTextSize(MaxLabelText.c_str()).x };
        return std::max(MinLabelWidth, MaxLabelWidth) + 8.0f;
    }

    static void RenderFrameTimeScale(const ImVec2& ScaleMin, const ImVec2& PlotMin, const ImVec2& PlotMax, float PlotMinMilliseconds, float PlotMaxMilliseconds) {
        constexpr int TickCount{ 4 };
        ImDrawList* DrawList{ ImGui::GetWindowDrawList() };

        DrawList->PushClipRect(PlotMin, PlotMax, true);
        for (int TickIndex{ 0 }; TickIndex <= TickCount; TickIndex += 1) {
            const float Ratio{ static_cast<float>(TickIndex) / static_cast<float>(TickCount) };
            const float Y{ PlotMax.y - (PlotMax.y - PlotMin.y) * Ratio };
            DrawList->AddLine(ImVec2(PlotMin.x, Y), ImVec2(PlotMax.x, Y), IM_COL32(255, 255, 255, 45), 1.0f);
        }
        DrawList->PopClipRect();

        for (int TickIndex{ 0 }; TickIndex <= TickCount; TickIndex += 1) {
            const float Ratio{ static_cast<float>(TickIndex) / static_cast<float>(TickCount) };
            const float Value{ PlotMinMilliseconds + (PlotMaxMilliseconds - PlotMinMilliseconds) * Ratio };
            const float Y{ PlotMax.y - (PlotMax.y - PlotMin.y) * Ratio };
            const std::string LabelText{ std::format("{:.1f} ms", Value) };
            const ImVec2 LabelSize{ ImGui::CalcTextSize(LabelText.c_str()) };
            const float LabelX{ ScaleMin.x + std::max(0.0f, PlotMin.x - ScaleMin.x - LabelSize.x - 6.0f) };
            const float LabelY{ std::clamp(Y - LabelSize.y * 0.5f, PlotMin.y, PlotMax.y - LabelSize.y) };
            DrawList->AddText(ImVec2(LabelX, LabelY), IM_COL32(210, 210, 210, 255), LabelText.c_str());
        }
    }

    static void RenderFrameTimeThreshold(const ImVec2& PlotMin, const ImVec2& PlotMax, float PlotMinMilliseconds, float PlotMaxMilliseconds, float ThresholdMilliseconds) {
        if (ThresholdMilliseconds < PlotMinMilliseconds || ThresholdMilliseconds > PlotMaxMilliseconds) {
            return;
        }

        const float ThresholdRatio{ (ThresholdMilliseconds - PlotMinMilliseconds) / (PlotMaxMilliseconds - PlotMinMilliseconds) };
        const float ThresholdY{ PlotMax.y - (PlotMax.y - PlotMin.y) * ThresholdRatio };
        ImDrawList* DrawList{ ImGui::GetWindowDrawList() };
        DrawList->PushClipRect(PlotMin, PlotMax, true);
        DrawList->AddLine(ImVec2(PlotMin.x, ThresholdY), ImVec2(PlotMax.x, ThresholdY), IM_COL32(255, 64, 64, 255), 2.0f);
        DrawList->PopClipRect();
    }

    static void RenderFrameTimeGraph(const std::vector<FrameTimeSample>& FrameTimeSamples, const ImVec2& PlotMin, const ImVec2& PlotMax, float PlotMinMilliseconds, float PlotMaxMilliseconds, float HistorySeconds) {
        if (FrameTimeSamples.empty()) {
            return;
        }

        ImDrawList* DrawList{ ImGui::GetWindowDrawList() };
        ImVec2 PreviousPoint{};
        bool HasPreviousPoint{};

        DrawList->AddRect(PlotMin, PlotMax, IM_COL32(255, 255, 255, 75), 0.0f, 0, 1.0f);
        DrawList->PushClipRect(PlotMin, PlotMax, true);
        for (const FrameTimeSample& Sample : FrameTimeSamples) {
            const float ClampedAgeSeconds{ std::clamp(Sample.AgeSeconds, 0.0f, HistorySeconds) };
            const float XRatio{ HistorySeconds > 0.0f ? (HistorySeconds - ClampedAgeSeconds) / HistorySeconds : 1.0f };
            const float YRatio{ PlotMaxMilliseconds > PlotMinMilliseconds ? (Sample.TimeMilliseconds - PlotMinMilliseconds) / (PlotMaxMilliseconds - PlotMinMilliseconds) : 0.0f };
            const float ClampedYRatio{ std::clamp(YRatio, 0.0f, 1.0f) };
            const ImVec2 Point{ PlotMin.x + (PlotMax.x - PlotMin.x) * XRatio, PlotMax.y - (PlotMax.y - PlotMin.y) * ClampedYRatio };

            if (HasPreviousPoint) {
                DrawList->AddLine(PreviousPoint, Point, IM_COL32(97, 170, 255, 255), 2.0f);
            }

            PreviousPoint = Point;
            HasPreviousPoint = true;
        }

        if (FrameTimeSamples.size() == 1) {
            DrawList->AddCircleFilled(PreviousPoint, 2.5f, IM_COL32(97, 170, 255, 255));
        }
        DrawList->PopClipRect();
    }

    static void RenderFrameTimeXAxis(const ImVec2& PlotMin, const ImVec2& PlotMax, const ImVec2& AxisMin, const ImVec2& AxisMax, float HistorySeconds) {
        constexpr int TickCount{ 4 };
        ImDrawList* DrawList{ ImGui::GetWindowDrawList() };

        DrawList->PushClipRect(PlotMin, PlotMax, true);
        for (int TickIndex{ 0 }; TickIndex <= TickCount; TickIndex += 1) {
            const float Ratio{ static_cast<float>(TickIndex) / static_cast<float>(TickCount) };
            const float X{ PlotMin.x + (PlotMax.x - PlotMin.x) * Ratio };
            DrawList->AddLine(ImVec2(X, PlotMin.y), ImVec2(X, PlotMax.y), IM_COL32(255, 255, 255, 35), 1.0f);
        }
        DrawList->PopClipRect();

        for (int TickIndex{ 0 }; TickIndex <= TickCount; TickIndex += 1) {
            const float Ratio{ static_cast<float>(TickIndex) / static_cast<float>(TickCount) };
            const float SecondsAgo{ HistorySeconds * (1.0f - Ratio) };
            const std::string LabelText{ SecondsAgo <= 0.0001f ? "0.0s" : std::format("-{:.1f}s", SecondsAgo) };
            const ImVec2 LabelSize{ ImGui::CalcTextSize(LabelText.c_str()) };
            const float X{ AxisMin.x + (AxisMax.x - AxisMin.x) * Ratio };
            const float LabelX{ std::clamp(X - LabelSize.x * 0.5f, AxisMin.x, AxisMax.x - LabelSize.x) };
            const float LabelY{ AxisMin.y + std::max(0.0f, (AxisMax.y - AxisMin.y - LabelSize.y) * 0.5f) };
            DrawList->AddText(ImVec2(LabelX, LabelY), IM_COL32(210, 210, 210, 255), LabelText.c_str());
        }
    }

    FrameTimeWidget::FrameTimeWidget() {
    }

    FrameTimeWidget::~FrameTimeWidget() {
    }

    void FrameTimeWidget::Render(const Game::SceneWorldSnapshot* Snapshot) {
        if (!ImGui::Begin("Performance Frame Time")) {
            ImGui::End();
            return;
        }

        PerformanceProvider& Provider{ PerformanceProvider::Get() };
        const std::vector<FrameTimeSample> FrameTimeSamples{ Provider.GetFrameTimeSamples() };
        const float HistorySeconds{ Provider.GetFrameTimeHistorySeconds() };

        ImGui::Columns(3, "PerformanceFrameTimeColumns", false);
        ImGui::Text("Avg FPS\n%.2f", Provider.GetAverageFps());
        ImGui::NextColumn();
        ImGui::Text("1%% Low\n%.2f", Provider.GetOnePercentLowFps());
        ImGui::NextColumn();
        ImGui::Text("0.1%% Low\n%.2f", Provider.GetZeroPointOnePercentLowFps());
        ImGui::Columns(1);

        if (Snapshot != nullptr) {
            const Game::PhysicsRuntimeStatus& RuntimeStatus{ Snapshot->GetPhysicsRuntimeStatus() };
            ImGui::SeparatorText("Physics Runtime");
            ImGui::Text("PhysicsRuntime %s", RuntimeStatus.mIsRunning == true ? "Running" : "Stopped");
            ImGui::Text("Mode %s", RuntimeStatus.mIsRuntimeModeEnabled == true ? "Runtime" : "Sync");
            ImGui::Text("LatestStepIndex %llu", static_cast<unsigned long long>(RuntimeStatus.mLatestStepIndex));
            ImGui::Text("SnapshotStepIndex %llu", static_cast<unsigned long long>(RuntimeStatus.mSnapshotStepIndex));
            ImGui::Text("ActorCount %zu", RuntimeStatus.mActorCount);
            ImGui::Text("SnapshotAgeMs %.2f", RuntimeStatus.mSnapshotAgeMilliseconds);
        }

        constexpr float ThresholdMilliseconds{ 16.6f };
        bool IsThresholdVisible{};

        if (!FrameTimeSamples.empty()) {
            constexpr float PlotMinMilliseconds{ 0.0f };
            constexpr float PlotHeight{ 160.0f };
            const float PlotMaxMilliseconds{ ResolveFrameTimePlotMaxMilliseconds(FrameTimeSamples) };
            const float ScaleWidth{ ResolveFrameTimeScaleWidth(PlotMinMilliseconds, PlotMaxMilliseconds) };
            const ImVec2 ScaleMin{ ImGui::GetCursorScreenPos() };
            IsThresholdVisible = ThresholdMilliseconds >= PlotMinMilliseconds && ThresholdMilliseconds <= PlotMaxMilliseconds;

            ImGui::Dummy(ImVec2(ScaleWidth, PlotHeight));
            ImGui::SameLine();
            const ImVec2 PlotMin{ ImGui::GetCursorScreenPos() };
            const float PlotWidth{ std::max(1.0f, ImGui::GetContentRegionAvail().x) };
            ImGui::Dummy(ImVec2(PlotWidth, PlotHeight));
            const ImVec2 PlotMax{ ImGui::GetItemRectMax() };
            RenderFrameTimeScale(ScaleMin, PlotMin, PlotMax, PlotMinMilliseconds, PlotMaxMilliseconds);

            const float XAxisHeight{ ImGui::GetTextLineHeightWithSpacing() };
            ImGui::Dummy(ImVec2(ScaleWidth, XAxisHeight));
            ImGui::SameLine();
            const ImVec2 AxisMin{ ImGui::GetCursorScreenPos() };
            const float AxisWidth{ ImGui::GetContentRegionAvail().x };
            ImGui::Dummy(ImVec2(AxisWidth, XAxisHeight));
            const ImVec2 AxisMax{ ImGui::GetItemRectMax() };
            RenderFrameTimeXAxis(PlotMin, PlotMax, AxisMin, AxisMax, HistorySeconds);
            RenderFrameTimeGraph(FrameTimeSamples, PlotMin, PlotMax, PlotMinMilliseconds, PlotMaxMilliseconds, HistorySeconds);
            RenderFrameTimeThreshold(PlotMin, PlotMax, PlotMinMilliseconds, PlotMaxMilliseconds, ThresholdMilliseconds);
        }

        ImGui::TextUnformatted("Frame Time (ms)");
        ImGui::SameLine();
        if (IsThresholdVisible) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Red Line: 16.6ms (60 FPS Threshold)");
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "16.6ms Threshold Above Current Scale");
        }

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

        const std::vector<ProfileEntry> Entries{ PerformanceProvider::Get().GetTimelineAverageProfiles() };
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
