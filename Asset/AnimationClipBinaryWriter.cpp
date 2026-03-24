#include "AnimationClipBinaryWriter.h"

#include <array>

namespace asset {
    namespace {
        constexpr std::uint32_t FormatVersion{ 1 };
        constexpr std::array<char, 4> FormatMagic{ 'A', 'N', 'C', 'B' };
    }

    AnimationClipBinaryWriter::AnimationClipBinaryWriter() {
    }

    bool AnimationClipBinaryWriter::WriteToFile(const std::string& Path, const AnimationClipResult& ClipData) {
        mStream = std::ofstream{ Path, std::ios::binary };
        if (mStream.is_open() == false) {
            return false;
        }

        WriteHeader();
        WriteAnimationClipResult(ClipData);
        return static_cast<bool>(mStream);
    }

    void AnimationClipBinaryWriter::WriteHeader() {
        WriteBytes(FormatMagic.data(), FormatMagic.size());
        WriteUint32(FormatVersion);
    }

    void AnimationClipBinaryWriter::WriteAnimationClipResult(const AnimationClipResult& ClipData) {
        const std::vector<AnimationClip>& Clips{ ClipData.Clips() };
        WriteUint64(static_cast<std::uint64_t>(Clips.size()));

        for (const AnimationClip& Clip : Clips) {
            WriteAnimationClip(Clip);
        }
    }

    void AnimationClipBinaryWriter::WriteAnimationClip(const AnimationClip& ClipData) {
        WriteString(ClipData.Name);
        WriteDouble(ClipData.Duration);
        WriteDouble(ClipData.TicksPerSecond);
        WriteUint64(static_cast<std::uint64_t>(ClipData.Channels.size()));

        for (const AnimationChannel& Channel : ClipData.Channels) {
            WriteAnimationChannel(Channel);
        }
    }

    void AnimationClipBinaryWriter::WriteAnimationChannel(const AnimationChannel& ChannelData) {
        WriteString(ChannelData.NodeName);
        WriteUint64(static_cast<std::uint64_t>(ChannelData.PositionKeys.size()));

        for (const AnimationKeyPosition& PositionKey : ChannelData.PositionKeys) {
            WritePositionKey(PositionKey);
        }

        WriteUint64(static_cast<std::uint64_t>(ChannelData.RotationKeys.size()));
        for (const AnimationKeyRotation& RotationKey : ChannelData.RotationKeys) {
            WriteRotationKey(RotationKey);
        }

        WriteUint64(static_cast<std::uint64_t>(ChannelData.ScaleKeys.size()));
        for (const AnimationKeyScale& ScaleKey : ChannelData.ScaleKeys) {
            WriteScaleKey(ScaleKey);
        }
    }

    void AnimationClipBinaryWriter::WritePositionKey(const AnimationKeyPosition& KeyData) {
        WriteDouble(KeyData.Time);
        WriteVec3(KeyData.Value);
    }

    void AnimationClipBinaryWriter::WriteRotationKey(const AnimationKeyRotation& KeyData) {
        WriteDouble(KeyData.Time);
        WriteVec4(KeyData.Value);
    }

    void AnimationClipBinaryWriter::WriteScaleKey(const AnimationKeyScale& KeyData) {
        WriteDouble(KeyData.Time);
        WriteVec3(KeyData.Value);
    }

    void AnimationClipBinaryWriter::WriteString(const std::string& Value) {
        WriteUint64(static_cast<std::uint64_t>(Value.size()));
        WriteBytes(Value.data(), Value.size());
    }

    void AnimationClipBinaryWriter::WriteUint32(std::uint32_t Value) {
        WriteBytes(&Value, sizeof(Value));
    }

    void AnimationClipBinaryWriter::WriteUint64(std::uint64_t Value) {
        WriteBytes(&Value, sizeof(Value));
    }

    void AnimationClipBinaryWriter::WriteFloat(float Value) {
        WriteBytes(&Value, sizeof(Value));
    }

    void AnimationClipBinaryWriter::WriteDouble(double Value) {
        WriteBytes(&Value, sizeof(Value));
    }

    void AnimationClipBinaryWriter::WriteVec3(const Vec3& Value) {
        WriteBytes(&Value, sizeof(Value));
    }

    void AnimationClipBinaryWriter::WriteVec4(const Vec4& Value) {
        WriteBytes(&Value, sizeof(Value));
    }

    void AnimationClipBinaryWriter::WriteBytes(const void* Data, std::size_t Size) {
        mStream.write(reinterpret_cast<const char*>(Data), static_cast<std::streamsize>(Size));
    }
}
