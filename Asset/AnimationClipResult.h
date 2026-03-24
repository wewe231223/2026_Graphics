#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Common.h"

namespace asset {
    struct AnimationKeyPosition final {
    public:
        double Time{ 0.0 };
        Vec3 Value{};
    };

    struct AnimationKeyRotation final {
    public:
        double Time{ 0.0 };
        Vec4 Value{ 0.0f, 0.0f, 0.0f, 1.0f };
    };

    struct AnimationKeyScale final {
    public:
        double Time{ 0.0 };
        Vec3 Value{ 1.0f, 1.0f, 1.0f };
    };

    struct AnimationChannel final {
    public:
        std::string NodeName{};
        std::vector<AnimationKeyPosition> PositionKeys{};
        std::vector<AnimationKeyRotation> RotationKeys{};
        std::vector<AnimationKeyScale> ScaleKeys{};
    };

    struct AnimationClip final {
    public:
        std::string Name{};
        double Duration{ 0.0 };
        double TicksPerSecond{ 0.0 };
        std::vector<AnimationChannel> Channels{};
    };

    class AnimationClipResult final {
    public:
        AnimationClipResult();
        ~AnimationClipResult() = default;
        AnimationClipResult(const AnimationClipResult& Other) = default;
        AnimationClipResult& operator=(const AnimationClipResult& Other) = default;
        AnimationClipResult(AnimationClipResult&& Other) noexcept = default;
        AnimationClipResult& operator=(AnimationClipResult&& Other) noexcept = default;

    public:
        std::vector<AnimationClip>& Clips();
        const std::vector<AnimationClip>& Clips() const;
        std::size_t ClipCount() const;

    private:
        std::vector<AnimationClip> mClips{};
    };

    using Animation = AnimationClipResult;
}
