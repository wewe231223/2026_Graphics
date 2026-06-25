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
            MaxFrameTimeMilliseconds = std::max(MaxFrameTimeMilliseconds, Sample.mCpuTimeMilliseconds);
            if (Sample.mHasGpuTime) {
                MaxFrameTimeMilliseconds = std::max(MaxFrameTimeMilliseconds, Sample.mGpuTimeMilliseconds);
            }
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
        DrawList->AddRect(PlotMin, PlotMax, IM_COL32(255, 255, 255, 75), 0.0f, 0, 1.0f);
        DrawList->PushClipRect(PlotMin, PlotMax, true);
        const auto RenderSeries{ [&FrameTimeSamples, DrawList, PlotMin, PlotMax, PlotMinMilliseconds, PlotMaxMilliseconds, HistorySeconds](bool IsGpuSeries, ImU32 Color) {
            ImVec2 PreviousPoint{};
            bool HasPreviousPoint{};

            for (const FrameTimeSample& Sample : FrameTimeSamples) {
                if (IsGpuSeries && Sample.mHasGpuTime == false) {
                    HasPreviousPoint = false;
                    continue;
                }

                const float TimeMilliseconds{ IsGpuSeries ? Sample.mGpuTimeMilliseconds : Sample.mCpuTimeMilliseconds };
                const float ClampedAgeSeconds{ std::clamp(Sample.mAgeSeconds, 0.0f, HistorySeconds) };
                const float XRatio{ HistorySeconds > 0.0f ? (HistorySeconds - ClampedAgeSeconds) / HistorySeconds : 1.0f };
                const float YRatio{ PlotMaxMilliseconds > PlotMinMilliseconds ? (TimeMilliseconds - PlotMinMilliseconds) / (PlotMaxMilliseconds - PlotMinMilliseconds) : 0.0f };
                const float ClampedYRatio{ std::clamp(YRatio, 0.0f, 1.0f) };
                const ImVec2 Point{ PlotMin.x + (PlotMax.x - PlotMin.x) * XRatio, PlotMax.y - (PlotMax.y - PlotMin.y) * ClampedYRatio };

                if (HasPreviousPoint) {
                    DrawList->AddLine(PreviousPoint, Point, Color, 2.0f);
                }

                PreviousPoint = Point;
                HasPreviousPoint = true;
            }

            if (HasPreviousPoint) {
                DrawList->AddCircleFilled(PreviousPoint, 2.5f, Color);
            }
        } };
        RenderSeries(false, IM_COL32(97, 170, 255, 255));
        RenderSeries(true, IM_COL32(95, 220, 135, 255));
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
        const FrameTimeSample* LatestCpuTimeSample{ FrameTimeSamples.empty() == true ? nullptr : &FrameTimeSamples.back() };
        const std::vector<FrameTimeSample>::const_reverse_iterator LatestGpuTimeSampleIterator{ std::find_if(FrameTimeSamples.rbegin(), FrameTimeSamples.rend(), [](const FrameTimeSample& FrameTimeSample) {
            return FrameTimeSample.mHasGpuTime;
        }) };
        const FrameTimeSample* LatestGpuTimeSample{ LatestGpuTimeSampleIterator == FrameTimeSamples.rend() ? nullptr : &*LatestGpuTimeSampleIterator };

        ImGui::Columns(5, "PerformanceFrameTimeColumns", false);
        if (LatestCpuTimeSample != nullptr) {
            ImGui::Text("CPU Frame\n%.2f ms", LatestCpuTimeSample->mCpuTimeMilliseconds);
        }
        else {
            ImGui::TextUnformatted("CPU Frame\nN/A");
        }
        ImGui::NextColumn();
        if (LatestGpuTimeSample != nullptr) {
            ImGui::Text("GPU Frame\n%.2f ms", LatestGpuTimeSample->mGpuTimeMilliseconds);
        }
        else {
            ImGui::TextUnformatted("GPU Frame\nN/A");
        }
        ImGui::NextColumn();
        ImGui::Text("CPU Avg FPS\n%.2f", Provider.GetAverageFps());
        ImGui::NextColumn();
        ImGui::Text("CPU 1%% Low\n%.2f", Provider.GetOnePercentLowFps());
        ImGui::NextColumn();
        ImGui::Text("CPU 0.1%% Low\n%.2f", Provider.GetZeroPointOnePercentLowFps());
        ImGui::Columns(1);

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

        if (Snapshot != nullptr) {
            const Game::PhysicsRuntimeStatus& RuntimeStatus{ Snapshot->GetPhysicsRuntimeStatus() };
            ImGui::SeparatorText("Physics Runtime");
            ImGui::Text("PhysicsRuntime %s", RuntimeStatus.mIsRunning == true ? "Running" : "Stopped");
            ImGui::Text("Mode %s", RuntimeStatus.mIsRuntimeModeEnabled == true ? "Runtime" : "Sync");
            ImGui::Text("LatestStepIndex %llu", static_cast<unsigned long long>(RuntimeStatus.mLatestStepIndex));
            ImGui::Text("SnapshotStepIndex %llu", static_cast<unsigned long long>(RuntimeStatus.mSnapshotStepIndex));
            ImGui::Text("ActorCount %zu", RuntimeStatus.mActorCount);
            ImGui::Text("SnapshotPhysicsStepMs %.3f / 16.667 ms", RuntimeStatus.mLastStepElapsedMilliseconds);
            ImGui::Text("SnapshotPhysicsUpdate %zu steps, %.3f ms", RuntimeStatus.mLastUpdateStepCount, RuntimeStatus.mLastUpdateStepElapsedMilliseconds);
            ImGui::Text("SnapshotAgeMs %.2f", RuntimeStatus.mSnapshotAgeMilliseconds);
        }

        ImGui::TextUnformatted("CPU: blue, GPU: green");
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
        std::string FixedPipelineLegendText{ "Fixed Pipeline Stage" };
        std::string PhysicsEngineLegendText{ "Physics Engine Stage" };
        std::string ParallelProcessingLegendText{ "Parallel Processing Stage" };
        std::string CommandSubmissionLegendText{ "Command Submission Stage" };
        std::string ImGuiRenderLegendText{ "ImGui Render Stage" };

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

            FixedPipelineLegendText = std::format("Fixed Pipeline Stage ({:.2f} ms)", ResolvePhaseDurationMilliseconds("FixedPipelineStage"));
            PhysicsEngineLegendText = std::format("Physics Engine Stage ({:.2f} ms)", ResolvePhaseDurationMilliseconds("PhysicsEngineStage"));
            ParallelProcessingLegendText = std::format("Parallel Processing Stage ({:.2f} ms)", ResolvePhaseDurationMilliseconds("ParallelProcessingStage"));
            CommandSubmissionLegendText = std::format("Command Submission Stage ({:.2f} ms)", ResolvePhaseDurationMilliseconds("CommandSubmissionStage"));
            ImGuiRenderLegendText = std::format("ImGui Render Stage ({:.2f} ms)", ResolvePhaseDurationMilliseconds("ImGuiRenderStage"));
        }

        ImGui::Columns(5, "PerformanceTimelineLegendColumns", false);
        RenderLegendItem(FixedPipelineLegendText.c_str(), GetColorByName("FixedPipelineStage"));
        ImGui::NextColumn();
        RenderLegendItem(PhysicsEngineLegendText.c_str(), GetColorByName("PhysicsEngineStage"));
        ImGui::NextColumn();
        RenderLegendItem(ParallelProcessingLegendText.c_str(), GetColorByName("ParallelProcessingStage"));
        ImGui::NextColumn();
        RenderLegendItem(CommandSubmissionLegendText.c_str(), GetColorByName("CommandSubmissionStage"));
        ImGui::NextColumn();
        RenderLegendItem(ImGuiRenderLegendText.c_str(), GetColorByName("ImGuiRenderStage"));
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
        if (Name == "FixedPipelineStage") {
            return IM_COL32(52, 152, 219, 255);
        }

        if (Name == "PhysicsEngineStage") {
            return IM_COL32(241, 196, 15, 255);
        }

        if (Name == "ParallelProcessingStage") {
            return IM_COL32(26, 188, 156, 255);
        }

        if (Name == "CommandSubmissionStage") {
            return IM_COL32(52, 73, 94, 255);
        }

        if (Name == "ImGuiRenderStage") {
            return IM_COL32(230, 126, 34, 255);
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
