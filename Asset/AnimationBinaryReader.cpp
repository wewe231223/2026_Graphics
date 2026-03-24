#include "AnimationBinaryReader.h"

#include <array>

using namespace asset;

namespace {
    constexpr std::uint32_t FormatVersion{ 2 };
    constexpr std::array<char, 4> FormatMagic{ 'A', 'N', 'C', 'B' };
}

AnimationBinaryReader::AnimationBinaryReader() {
}

bool AnimationBinaryReader::ReadFromFile(const std::string& Path, AnimationClipResult& ClipData) {
    mStream = std::ifstream{ Path, std::ios::binary };
    if (mStream.is_open() == false) {
        return false;
    }

    ClipData = AnimationClipResult{};

    if (ReadHeader() == false) {
        return false;
    }

    ReadAnimationClipResult(ClipData);
    return static_cast<bool>(mStream);
}

bool AnimationBinaryReader::ReadHeader() {
    std::array<char, 4> Magic{};
    ReadBytes(Magic.data(), Magic.size());

    if (Magic != FormatMagic) {
        return false;
    }

    const std::uint32_t Version{ ReadUint32() };
    if (Version != FormatVersion) {
        return false;
    }

    return true;
}

void AnimationBinaryReader::ReadAnimationClipResult(AnimationClipResult& ClipData) {
    const std::uint64_t ClipCount{ ReadUint64() };
    std::vector<AnimationClip>& Clips{ ClipData.Clips() };
    Clips.clear();
    Clips.reserve(static_cast<std::size_t>(ClipCount));

    for (std::uint64_t ClipIndex{ 0 }; ClipIndex < ClipCount; ++ClipIndex) {
        Clips.push_back(ReadAnimationClip());
    }
}

AnimationClip AnimationBinaryReader::ReadAnimationClip() {
    AnimationClip ClipData{};
    ClipData.Name = ReadString();
    ClipData.Duration = ReadDouble();
    ClipData.TicksPerSecond = ReadDouble();

    const std::uint64_t ChannelCount{ ReadUint64() };
    ClipData.Channels.reserve(static_cast<std::size_t>(ChannelCount));

    for (std::uint64_t ChannelIndex{ 0 }; ChannelIndex < ChannelCount; ++ChannelIndex) {
        ClipData.Channels.push_back(ReadAnimationChannel());
    }

    return ClipData;
}

AnimationChannel AnimationBinaryReader::ReadAnimationChannel() {
    AnimationChannel ChannelData{};
    ChannelData.NodeId = ReadUint32();
    ChannelData.NodeName = ReadString();

    const std::uint64_t PositionKeyCount{ ReadUint64() };
    ChannelData.PositionKeys.reserve(static_cast<std::size_t>(PositionKeyCount));

    for (std::uint64_t PositionKeyIndex{ 0 }; PositionKeyIndex < PositionKeyCount; ++PositionKeyIndex) {
        ChannelData.PositionKeys.push_back(ReadPositionKey());
    }

    const std::uint64_t RotationKeyCount{ ReadUint64() };
    ChannelData.RotationKeys.reserve(static_cast<std::size_t>(RotationKeyCount));

    for (std::uint64_t RotationKeyIndex{ 0 }; RotationKeyIndex < RotationKeyCount; ++RotationKeyIndex) {
        ChannelData.RotationKeys.push_back(ReadRotationKey());
    }

    const std::uint64_t ScaleKeyCount{ ReadUint64() };
    ChannelData.ScaleKeys.reserve(static_cast<std::size_t>(ScaleKeyCount));

    for (std::uint64_t ScaleKeyIndex{ 0 }; ScaleKeyIndex < ScaleKeyCount; ++ScaleKeyIndex) {
        ChannelData.ScaleKeys.push_back(ReadScaleKey());
    }

    return ChannelData;
}

AnimationKeyPosition AnimationBinaryReader::ReadPositionKey() {
    AnimationKeyPosition KeyData{};
    KeyData.Time = ReadDouble();
    KeyData.Value = ReadVec3();
    return KeyData;
}

AnimationKeyRotation AnimationBinaryReader::ReadRotationKey() {
    AnimationKeyRotation KeyData{};
    KeyData.Time = ReadDouble();
    KeyData.Value = ReadVec4();
    return KeyData;
}

AnimationKeyScale AnimationBinaryReader::ReadScaleKey() {
    AnimationKeyScale KeyData{};
    KeyData.Time = ReadDouble();
    KeyData.Value = ReadVec3();
    return KeyData;
}

std::string AnimationBinaryReader::ReadString() {
    const std::uint64_t Length{ ReadUint64() };
    std::string Value{};
    Value.resize(static_cast<std::size_t>(Length));

    if (Length > 0) {
        ReadBytes(Value.data(), Value.size());
    }

    return Value;
}

std::uint32_t AnimationBinaryReader::ReadUint32() {
    std::uint32_t Value{};
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

std::uint64_t AnimationBinaryReader::ReadUint64() {
    std::uint64_t Value{};
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

float AnimationBinaryReader::ReadFloat() {
    float Value{};
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

double AnimationBinaryReader::ReadDouble() {
    double Value{};
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

Vec3 AnimationBinaryReader::ReadVec3() {
    Vec3 Value{};
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

Vec4 AnimationBinaryReader::ReadVec4() {
    Vec4 Value{};
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

void AnimationBinaryReader::ReadBytes(void* Data, std::size_t Size) {
    mStream.read(reinterpret_cast<char*>(Data), static_cast<std::streamsize>(Size));
}
