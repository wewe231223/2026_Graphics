#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

#include "AnimationClipResult.h"

namespace asset {
    class AnimationBinaryReader final {
    public:
        AnimationBinaryReader();
        ~AnimationBinaryReader() = default;
        AnimationBinaryReader(const AnimationBinaryReader& Other) = delete;
        AnimationBinaryReader& operator=(const AnimationBinaryReader& Other) = delete;
        AnimationBinaryReader(AnimationBinaryReader&& Other) noexcept = delete;
        AnimationBinaryReader& operator=(AnimationBinaryReader&& Other) noexcept = delete;

    public:
        bool ReadFromFile(const std::string& Path, AnimationClipResult& ClipData);

    private:
        bool ReadHeader();
        void ReadAnimationClipResult(AnimationClipResult& ClipData);
        AnimationClip ReadAnimationClip();
        AnimationChannel ReadAnimationChannel();
        AnimationKeyPosition ReadPositionKey();
        AnimationKeyRotation ReadRotationKey();
        AnimationKeyScale ReadScaleKey();

        std::string ReadString();
        std::uint32_t ReadUint32();
        std::uint64_t ReadUint64();
        float ReadFloat();
        double ReadDouble();
        Vec3 ReadVec3();
        Vec4 ReadVec4();
        void ReadBytes(void* Data, std::size_t Size);

    private:
        std::ifstream mStream{};
    };
}
