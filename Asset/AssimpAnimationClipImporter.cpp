#include "AssimpAnimationClipImporter.h"

#include <string>
#include <unordered_map>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace asset {
    namespace {
        constexpr double DefaultTicksPerSecond{ 30.0 };

        Vec3 ToVec3(const aiVector3D& Value) {
            return Vec3{ Value.x, Value.y, Value.z };
        }

        Vec4 ToVec4(const aiQuaternion& Value) {
            return Vec4{ Value.x, Value.y, Value.z, Value.w };
        }

        void BuildNodeIdLookupRecursive(const aiNode& NodeData, std::uint32_t& InOutNextNodeId, std::unordered_map<std::string, std::uint32_t>& OutNodeIdLookup) {
            const std::uint32_t CurrentNodeId{ InOutNextNodeId };
            InOutNextNodeId += 1;
            OutNodeIdLookup.insert_or_assign(std::string{ NodeData.mName.C_Str() }, CurrentNodeId);

            for (unsigned int ChildIndex{ 0 }; ChildIndex < NodeData.mNumChildren; ++ChildIndex) {
                BuildNodeIdLookupRecursive(*NodeData.mChildren[ChildIndex], InOutNextNodeId, OutNodeIdLookup);
            }
        }

        AnimationChannel ToAnimationChannel(const aiNodeAnim& ChannelData, const std::unordered_map<std::string, std::uint32_t>& NodeIdLookup) {
            AnimationChannel Channel{};
            Channel.NodeName = std::string{ ChannelData.mNodeName.C_Str() };
            const std::unordered_map<std::string, std::uint32_t>::const_iterator FoundNodeId{ NodeIdLookup.find(Channel.NodeName) };
            if (FoundNodeId != NodeIdLookup.end()) {
                Channel.NodeId = FoundNodeId->second;
            }

            Channel.PositionKeys.reserve(ChannelData.mNumPositionKeys);
            for (unsigned int PositionIndex{ 0 }; PositionIndex < ChannelData.mNumPositionKeys; ++PositionIndex) {
                const aiVectorKey& PositionKey{ ChannelData.mPositionKeys[PositionIndex] };
                AnimationKeyPosition Key{};
                Key.Time = PositionKey.mTime;
                Key.Value = ToVec3(PositionKey.mValue);
                Channel.PositionKeys.push_back(Key);
            }

            Channel.RotationKeys.reserve(ChannelData.mNumRotationKeys);
            for (unsigned int RotationIndex{ 0 }; RotationIndex < ChannelData.mNumRotationKeys; ++RotationIndex) {
                const aiQuatKey& RotationKey{ ChannelData.mRotationKeys[RotationIndex] };
                AnimationKeyRotation Key{};
                Key.Time = RotationKey.mTime;
                Key.Value = ToVec4(RotationKey.mValue);
                Channel.RotationKeys.push_back(Key);
            }

            Channel.ScaleKeys.reserve(ChannelData.mNumScalingKeys);
            for (unsigned int ScaleIndex{ 0 }; ScaleIndex < ChannelData.mNumScalingKeys; ++ScaleIndex) {
                const aiVectorKey& ScaleKey{ ChannelData.mScalingKeys[ScaleIndex] };
                AnimationKeyScale Key{};
                Key.Time = ScaleKey.mTime;
                Key.Value = ToVec3(ScaleKey.mValue);
                Channel.ScaleKeys.push_back(Key);
            }

            return Channel;
        }


        AnimationClip ToAnimationClip(const aiAnimation& AnimationData, const std::unordered_map<std::string, std::uint32_t>& NodeIdLookup) {
            AnimationClip Clip{};
            Clip.Name = std::string{ AnimationData.mName.C_Str() };
            Clip.Duration = AnimationData.mDuration;
            Clip.TicksPerSecond = AnimationData.mTicksPerSecond > 0.0 ? AnimationData.mTicksPerSecond : DefaultTicksPerSecond;

            Clip.Channels.reserve(AnimationData.mNumChannels);
            for (unsigned int ChannelIndex{ 0 }; ChannelIndex < AnimationData.mNumChannels; ++ChannelIndex) {
                Clip.Channels.push_back(ToAnimationChannel(*AnimationData.mChannels[ChannelIndex], NodeIdLookup));
            }


            return Clip;
        }
    }

    AssimpAnimationClipImporter::AssimpAnimationClipImporter(GraphicsAPI Api)
        : mApi{ Api } {
    }

    void AssimpAnimationClipImporter::LoadFromFile(std::string_view FilePath, AnimationClipResult& OutClipData) {
        Assimp::Importer Importer{};
        unsigned int Flags{ aiProcess_ValidateDataStructure };
        if (mApi == GraphicsAPI::DirectX) {
            Flags |= aiProcess_ConvertToLeftHanded;
        }

        const aiScene* Scene{ Importer.ReadFile(std::string{ FilePath }, Flags) };
        if (Scene == nullptr || Scene->mRootNode == nullptr) {
            throw AssetError{ Importer.GetErrorString() };
        }

        OutClipData = AnimationClipResult{};
        std::vector<AnimationClip>& Clips{ OutClipData.Clips() };
        Clips.reserve(Scene->mNumAnimations);
        std::unordered_map<std::string, std::uint32_t> NodeIdLookup{};
        std::uint32_t NextNodeId{ 1 };
        BuildNodeIdLookupRecursive(*Scene->mRootNode, NextNodeId, NodeIdLookup);

        for (unsigned int AnimationIndex{ 0 }; AnimationIndex < Scene->mNumAnimations; ++AnimationIndex) {
            Clips.push_back(ToAnimationClip(*Scene->mAnimations[AnimationIndex], NodeIdLookup));
        }
    }
}
