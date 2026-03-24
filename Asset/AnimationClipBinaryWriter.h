#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

#include "AnimationClipResult.h"

namespace asset {
    class AnimationClipBinaryWriter final {
    public:
        AnimationClipBinaryWriter();
        ~AnimationClipBinaryWriter() = default;

        AnimationClipBinaryWriter(const AnimationClipBinaryWriter& Other) = delete;
        AnimationClipBinaryWriter& operator=(const AnimationClipBinaryWriter& Other) = delete;
        AnimationClipBinaryWriter(AnimationClipBinaryWriter&& Other) noexcept = delete;
        AnimationClipBinaryWriter& operator=(AnimationClipBinaryWriter&& Other) noexcept = delete;

    public:
        bool WriteToFile(const std::string& Path, const AnimationClipResult& ClipData);

    private:
        void WriteHeader();
        void WriteAnimationClipResult(const AnimationClipResult& ClipData);
        void WriteAnimationClip(const AnimationClip& ClipData);
        void WriteAnimationChannel(const AnimationChannel& ChannelData);
        void WritePositionKey(const AnimationKeyPosition& KeyData);
        void WriteRotationKey(const AnimationKeyRotation& KeyData);
        void WriteScaleKey(const AnimationKeyScale& KeyData);

        void WriteString(const std::string& Value);
        void WriteUint32(std::uint32_t Value);
        void WriteUint64(std::uint64_t Value);
        void WriteFloat(float Value);
        void WriteDouble(double Value);
        void WriteVec3(const Vec3& Value);
        void WriteVec4(const Vec4& Value);
        void WriteBytes(const void* Data, std::size_t Size);

    private:
        std::ofstream mStream{};
    };
}
