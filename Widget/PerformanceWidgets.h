#pragma once

#include <cstdint>
#include <string>
#include "Common.h"

namespace Widget {
    class FrameTimeWidget : public IWidget {
    public:
        FrameTimeWidget();
        ~FrameTimeWidget() override;
        FrameTimeWidget(const FrameTimeWidget& Other) = delete;
        FrameTimeWidget& operator=(const FrameTimeWidget& Other) = delete;
        FrameTimeWidget(FrameTimeWidget&& Other) noexcept = delete;
        FrameTimeWidget& operator=(FrameTimeWidget&& Other) noexcept = delete;

    public:
        void Render(const Game::SceneWorldSnapshot* Snapshot) override;
    };

    class TimelineWidget : public IWidget {
    public:
        TimelineWidget();
        ~TimelineWidget() override;
        TimelineWidget(const TimelineWidget& Other) = delete;
        TimelineWidget& operator=(const TimelineWidget& Other) = delete;
        TimelineWidget(TimelineWidget&& Other) noexcept = delete;
        TimelineWidget& operator=(TimelineWidget&& Other) noexcept = delete;

    public:
        void Render(const Game::SceneWorldSnapshot* Snapshot) override;

    private:
        void RenderLegendItem(const char* Label, uint32_t Color) const;
        uint32_t GetColorByName(const std::string& Name) const;
    };

    class VramUsageWidget : public IWidget {
    public:
        VramUsageWidget();
        ~VramUsageWidget() override;
        VramUsageWidget(const VramUsageWidget& Other) = delete;
        VramUsageWidget& operator=(const VramUsageWidget& Other) = delete;
        VramUsageWidget(VramUsageWidget&& Other) noexcept = delete;
        VramUsageWidget& operator=(VramUsageWidget&& Other) noexcept = delete;

    public:
        void Render(const Game::SceneWorldSnapshot* Snapshot) override;
    };
}
